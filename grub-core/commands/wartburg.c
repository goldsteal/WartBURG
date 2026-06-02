/* wartburg.c - WartBURG: a reversible BURG-like horizontal menu renderer. */
/*
 *  WartBURG is a self-contained, removable GRUB addon. It claims GRUB's single
 *  graphical-menu hook (grub_gfxmenu_try_hook) with save/restore so that
 *  `rmmod wartburg` cleanly reverts to whatever menu was active before (text or
 *  gfxmenu). It draws using only GRUB's *exported* low-level primitives
 *  (grub_video_*, grub_font_* glyph API) and reuses normal/menu.c's input,
 *  timeout and boot loop. It patches no GRUB core file.
 */

#include <grub/types.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/command.h>
#include <grub/dl.h>
#include <grub/err.h>
#include <grub/video.h>
#include <grub/font.h>
#include <grub/charset.h>
#include <grub/menu.h>
#include <grub/menu_viewer.h>
#include <grub/wartburg_theme.h>
#include <grub/wartburg_widget.h>

GRUB_MOD_LICENSE ("GPLv3+");

#define WB_MAX_GLYPHS 256
#define WB_PAD_X 16
#define WB_PAD_Y 8
#define WB_GAP   12

static grub_command_t cmd;
static grub_command_t cmd_parse;
static grub_command_t cmd_render;
static int wb_ui_registered;

/* Saved predecessor so the hook is fully reversible (restored in MOD_FINI).  */
static grub_err_t (*wb_prev_try_hook) (int entry, grub_menu_t menu, int nested);

struct wb_view
{
  grub_menu_t menu;
  int selected;
  unsigned int width;
  unsigned int height;
  grub_font_t font;
  int double_repaint;
};

/* One cached view, mirroring gfxmenu's pattern: freed/cleared in MOD_FINI,
   so the viewer's fini callback can be a no-op (no use-after-free).  */
static struct wb_view cached;

/* Decode a UTF-8 title into UCS-4. Returns number of codepoints (<= max).  */
static grub_size_t
wb_decode (const char *s, grub_uint32_t *buf, grub_size_t max)
{
  const grub_uint8_t *end;
  if (!s)
    return 0;
  return grub_utf8_to_ucs4 (buf, max, (const grub_uint8_t *) s,
			    grub_strlen (s), &end);
}

/* Width in pixels of a decoded string, using the exported glyph API.  */
static int
wb_text_width (grub_font_t font, const grub_uint32_t *u, grub_size_t n)
{
  int w = 0;
  grub_size_t i;
  for (i = 0; i < n; i++)
    {
      struct grub_font_glyph *g = grub_font_get_glyph (font, u[i]);
      if (g)
	w += g->device_width;
    }
  return w;
}

/* Draw a decoded string at (x, baseline) glyph by glyph.  */
static void
wb_draw_text (grub_font_t font, grub_video_color_t color, int x, int baseline,
	      const grub_uint32_t *u, grub_size_t n)
{
  grub_size_t i;
  for (i = 0; i < n; i++)
    {
      struct grub_font_glyph *g = grub_font_get_glyph (font, u[i]);
      if (!g)
	continue;
      grub_font_draw_glyph (g, color, x, baseline);
      x += g->device_width;
    }
}

/* Render the whole horizontal menu once into the current video buffer.  */
static void
wb_render (struct wb_view *v)
{
  grub_video_color_t bg      = grub_video_map_rgb (20, 20, 30);
  grub_video_color_t fg      = grub_video_map_rgb (200, 205, 215);
  grub_video_color_t fg_sel  = grub_video_map_rgb (255, 255, 255);
  grub_video_color_t hl      = grub_video_map_rgb (60, 90, 160);

  int ascent = grub_font_get_ascent (v->font);
  int fh     = grub_font_get_height (v->font);
  int item_h = fh + 2 * WB_PAD_Y;
  int y0     = ((int) v->height - item_h) / 2;
  int baseline = y0 + WB_PAD_Y + ascent;
  int n = v->menu ? v->menu->size : 0;
  int i, total = 0, x;

  /* Draw straight to the screen, not gfxterm's offscreen surface, and lift
     gfxterm's clip area so our full-screen fill isn't cropped. */
  grub_video_set_active_render_target (GRUB_VIDEO_RENDER_TARGET_DISPLAY);
  grub_video_set_area_status (GRUB_VIDEO_AREA_DISABLED);

  grub_video_fill_rect (bg, 0, 0, v->width, v->height);
  if (n <= 0)
    return;

  /* Pass 1: total width to centre the row.  */
  for (i = 0; i < n; i++)
    {
      grub_uint32_t u[WB_MAX_GLYPHS];
      grub_menu_entry_t e = grub_menu_get_entry (v->menu, i);
      grub_size_t len = e ? wb_decode (e->title, u, WB_MAX_GLYPHS) : 0;
      total += wb_text_width (v->font, u, len) + 2 * WB_PAD_X;
    }
  total += WB_GAP * (n - 1);
  x = ((int) v->width - total) / 2;
  if (x < 0)
    x = 0;

  /* Pass 2: draw each item, highlighting the selected one.  */
  for (i = 0; i < n; i++)
    {
      grub_uint32_t u[WB_MAX_GLYPHS];
      grub_menu_entry_t e = grub_menu_get_entry (v->menu, i);
      grub_size_t len = e ? wb_decode (e->title, u, WB_MAX_GLYPHS) : 0;
      int tw = wb_text_width (v->font, u, len);
      int box_w = tw + 2 * WB_PAD_X;

      if (i == v->selected)
	grub_video_fill_rect (hl, x, y0, box_w, item_h);
      wb_draw_text (v->font, i == v->selected ? fg_sel : fg,
		    x + WB_PAD_X, baseline, u, len);
      x += box_w + WB_GAP;
    }
}

/* Render, handling double buffering so both buffers stay consistent.  */
static void
wb_paint (struct wb_view *v)
{
  /* Always present after drawing (mirrors gfxmenu). On double-buffered modes
     draw again so the flipped-to buffer also holds our frame. */
  wb_render (v);
  grub_video_swap_buffers ();
  if (v->double_repaint)
    wb_render (v);
}

static void
wb_set_chosen_entry (int entry, void *data)
{
  struct wb_view *v = data;
  v->selected = entry;
  wb_paint (v);
}

static void
wb_print_timeout (int timeout __attribute__ ((unused)),
		  void *data __attribute__ ((unused)))
{
  /* M1: no on-screen countdown yet (timeout still works internally). */
}

static void
wb_clear_timeout (void *data __attribute__ ((unused)))
{
}

static void
wb_fini (void *data __attribute__ ((unused)))
{
  /* View lives in `cached`; nothing to free per-instance. */
}

/* The graphical-menu hook: build the view, draw it, register the viewer.
   Returning an error makes normal/menu.c fall back to the text menu.  */
static grub_err_t
wartburg_try (int entry, grub_menu_t menu, int nested __attribute__ ((unused)))
{
  struct grub_video_mode_info mode_info;
  struct grub_menu_viewer *instance;
  grub_font_t font;

  if (grub_video_get_info (&mode_info) != GRUB_ERR_NONE)
    return grub_errno;

  font = grub_font_get ("Unknown Regular 16");
  if (!font)
    return grub_error (GRUB_ERR_BAD_FONT, "WartBURG: no font available");

  instance = grub_zalloc (sizeof (*instance));
  if (!instance)
    return grub_errno;

  cached.menu = menu;
  cached.selected = entry;
  cached.width = mode_info.width;
  cached.height = mode_info.height;
  cached.font = font;
  cached.double_repaint =
    (mode_info.mode_type & GRUB_VIDEO_MODE_TYPE_DOUBLE_BUFFERED)
    && !(mode_info.mode_type & GRUB_VIDEO_MODE_TYPE_UPDATING_SWAP);

  grub_video_set_viewport (0, 0, mode_info.width, mode_info.height);
  if (cached.double_repaint)
    {
      grub_video_swap_buffers ();
      grub_video_set_viewport (0, 0, mode_info.width, mode_info.height);
    }
  wb_paint (&cached);

  instance->data = &cached;
  instance->set_chosen_entry = wb_set_chosen_entry;
  instance->print_timeout = wb_print_timeout;
  instance->clear_timeout = wb_clear_timeout;
  instance->fini = wb_fini;
  grub_menu_register_viewer (instance);

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_wartburg (grub_command_t command __attribute__ ((unused)),
		   int argc __attribute__ ((unused)),
		   char **argv __attribute__ ((unused)))
{
  grub_printf ("WartBURG active.\n");
  return GRUB_ERR_NONE;
}

/* wbparse <theme-file>: parse a BURG theme via the ported parser and dump the
   resulting node tree (M2 parser validation). */
static grub_err_t
grub_cmd_wbparse (grub_command_t command __attribute__ ((unused)),
		  int argc, char **argv)
{
  grub_uitree_t root;

  if (argc < 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "usage: wbparse <theme-file>");

  root = grub_uitree_create_node ("root");
  if (!root)
    return grub_errno;

  grub_uitree_load_file (root, argv[0], 0);
  if (grub_errno)
    {
      grub_uitree_free (root);
      return grub_errno;
    }

  grub_printf ("=== WartBURG parsed theme: %s ===\n", argv[0]);
  grub_uitree_dump (root);
  grub_printf ("=== end theme dump ===\n");
  grub_uitree_free (root);
  return GRUB_ERR_NONE;
}

/* wbrender <theme-file>: parse a BURG theme and render its `screen` statically
   via the ported engine (M2 wire-up). Menu-item population comes later. */
static grub_err_t
grub_cmd_wbrender (grub_command_t command __attribute__ ((unused)),
		   int argc, char **argv)
{
  grub_uitree_t screen;
  grub_err_t err;

  if (argc < 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "usage: wbrender <theme-file>");

  err = grub_menu_region_gfx_init ();
  if (err)
    return err;

  if (! wb_ui_registered)
    {
      grub_wartburg_ui_init ();
      wb_ui_registered = 1;
    }

  grub_uitree_load_file (&grub_uitree_root, argv[0], GRUB_UITREE_LOAD_FLAG_ROOT);
  if (grub_errno)
    return grub_errno;

  screen = grub_uitree_find (&grub_uitree_root, "screen");
  if (! screen)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "theme has no `screen' section");

  err = grub_widget_create (screen);
  if (err)
    return err;

  grub_widget_init (screen);
  grub_widget_draw (screen);
  grub_video_swap_buffers ();

  return GRUB_ERR_NONE;
}

GRUB_MOD_INIT (wartburg)
{
  grub_printf ("\n=== WartBURG Initialized ===\n");

  cmd = grub_register_command ("wartburg", grub_cmd_wartburg, 0,
			       "Activate WartBURG.");
  cmd_parse = grub_register_command ("wbparse", grub_cmd_wbparse,
				     "FILE", "Parse a BURG theme and dump it.");
  cmd_render = grub_register_command ("wbrender", grub_cmd_wbrender,
				      "FILE", "Render a BURG theme statically.");

  /* Reversibly claim the graphical-menu hook. */
  wb_prev_try_hook = grub_gfxmenu_try_hook;
  grub_gfxmenu_try_hook = wartburg_try;
}

GRUB_MOD_FINI (wartburg)
{
  /* Restore whatever menu was active before us. */
  grub_gfxmenu_try_hook = wb_prev_try_hook;
  if (wb_ui_registered)
    grub_wartburg_ui_fini ();
  grub_unregister_command (cmd_render);
  grub_unregister_command (cmd_parse);
  grub_unregister_command (cmd);
}
