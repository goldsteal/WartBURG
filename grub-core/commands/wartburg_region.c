/* wartburg_region.c - BURG menu_region draw backend (gfx-only), ported.
 *
 * Combines bean's BURG menu/ext/{region,gfx_region,data_type}.c, adapted to
 * GRUB 2.15: the removed handler framework becomes a single backend pointer;
 * gfxmenu-internal font string helpers are reimplemented via the exported glyph
 * API; bitmap fit/scale uses the exported grub_video_bitmap_scale_proportional;
 * text_region (text mode) is dropped. Original copyright 2009 Bean Lee; GPLv3+.
 */

#include <grub/mm.h>
#include <grub/env.h>
#include <grub/misc.h>
#include <grub/list.h>
#include <grub/charset.h>
#include <grub/video.h>
#include <grub/bitmap.h>
#include <grub/bitmap_scale.h>
#include <grub/font.h>
#include <grub/file.h>
#include <grub/term.h>
#include <grub/wartburg_region.h>

/* BURG used GRUB_TERM_CTRL_A/_Z (gone in 2.15); raw ctrl chars are 1..26. */
#define WB_CTRL_A 1
#define WB_CTRL_Z 26

/* ===== single backend pointer (replaces BURG handler framework) ===== */

static grub_menu_region_t cur_region;

grub_menu_region_t
grub_menu_region_get_current (void)
{
  return cur_region;
}

void
grub_menu_region_set_current (grub_menu_region_t region)
{
  cur_region = region;
}

/* ===== gfx backend (BURG menu/ext/gfx_region.c) ===== */

#define DEFAULT_VIDEO_MODE "auto"
#define REFERENCE_STRING   "m"

static int screen_width;
static int screen_height;
static grub_font_t default_font;
static struct grub_menu_region grub_gfx_region;

/* Set when the active video mode is software double-buffered (then each frame
   must be drawn into BOTH buffers; see grub_menu_region_double_repaint).  */
int grub_wb_double_repaint;

/* Exported glyph API replacements for gfxmenu-internal font string helpers. */
static int
wb_str_width (grub_font_t font, const char *str, int count, int *chars)
{
  grub_uint32_t u[512];
  const grub_uint8_t *end;
  grub_size_t n, i;
  int width = 0;

  if (! str)
    str = "";
  n = grub_utf8_to_ucs4 (u, ARRAY_SIZE (u), (const grub_uint8_t *) str,
			 grub_strlen (str), &end);
  if ((count <= 0) || ((grub_size_t) count > n))
    count = n;
  for (i = 0; (int) i < count; i++)
    {
      struct grub_font_glyph *g = grub_font_get_glyph (font, u[i]);
      if (g)
	width += g->device_width;
    }
  if (chars)
    *chars = count;
  return width;
}

static void
wb_draw_text_glyphs (const char *str, grub_font_t font,
		     grub_video_color_t color, int x, int baseline)
{
  grub_uint32_t u[512];
  const grub_uint8_t *end;
  grub_size_t n, i;

  if (! str)
    return;
  n = grub_utf8_to_ucs4 (u, ARRAY_SIZE (u), (const grub_uint8_t *) str,
			 grub_strlen (str), &end);
  for (i = 0; i < n; i++)
    {
      struct grub_font_glyph *g = grub_font_get_glyph (font, u[i]);
      if (! g)
	continue;
      grub_font_draw_glyph (g, color, x, baseline);
      x += g->device_width;
    }
}

static grub_err_t
grub_gfx_region_init (void)
{
  const char *modevar;
  struct grub_video_mode_info mode_info;
  grub_err_t err;
  const char *fn;
  int force_mode;

  force_mode = grub_env_get ("wartburg_gfxmode_apply") != 0;
  if (force_mode)
    {
      /* Live gfxmode change requested (F3): tear the active mode down so the
	 block below re-sets it from `gfxmode'. NB: some EFI GOP backends
	 (qemu -vga std / bochs-display under OVMF) cannot re-establish a mode
	 once finalized -- they collapse to the firmware console mode. virtio
	 GOP and real-hardware GOP switch cleanly; test with -device virtio-vga. */
      grub_env_unset ("wartburg_gfxmode_apply");
      grub_video_restore ();
      grub_errno = GRUB_ERR_NONE;
    }

  /* If a graphics mode is already active (the menu hook runs with gfxterm up),
     reuse it -- re-setting the mode here is what fought gfxterm and left the
     first frame on the wrong buffer. Only set a mode if none is active (the
     standalone command/serial path, where gfxterm never ran).  */
  if (force_mode || grub_video_get_info (&mode_info) != GRUB_ERR_NONE)
    {
      grub_errno = GRUB_ERR_NONE;	/* expected probe miss: no mode active yet */
      modevar = grub_env_get ("gfxmode");
      if (! modevar || *modevar == 0)
	err = grub_video_set_mode (DEFAULT_VIDEO_MODE,
				   GRUB_VIDEO_MODE_TYPE_PURE_TEXT, 0);
      else
	{
	  char *tmp;
	  tmp = grub_xasprintf ("%s;" DEFAULT_VIDEO_MODE, modevar);
	  if (!tmp)
	    return grub_errno;
	  err = grub_video_set_mode (tmp, GRUB_VIDEO_MODE_TYPE_PURE_TEXT, 0);
	  grub_free (tmp);
	}

      if (err)
	return err;

      err = grub_video_get_info (&mode_info);
      if (err)
	return err;
    }

  screen_width = mode_info.width;
  screen_height = mode_info.height;

  /* Software double-buffered modes (gfxterm's default) need every frame drawn
     into both buffers; record it for grub_menu_region_double_repaint.  */
  grub_wb_double_repaint =
    (mode_info.mode_type & GRUB_VIDEO_MODE_TYPE_DOUBLE_BUFFERED)
    && ! (mode_info.mode_type & GRUB_VIDEO_MODE_TYPE_UPDATING_SWAP);

  /* Draw to the DISPLAY target (== the back buffer under double buffering;
     swap_buffers presents it) and disable any clipping area gfxterm left on.  */
  grub_video_set_active_render_target (GRUB_VIDEO_RENDER_TARGET_DISPLAY);
  grub_video_set_area_status (GRUB_VIDEO_AREA_DISABLED);

  fn = grub_env_get ("gfxfont");
  if (! fn)
    fn = "";

  default_font = grub_font_get (fn);
  grub_gfx_region.char_width = wb_str_width (default_font, REFERENCE_STRING,
					     0, 0);
  grub_gfx_region.char_height = grub_font_get_ascent (default_font) +
    grub_font_get_descent (default_font);

  if ((! grub_gfx_region.char_width) || (! grub_gfx_region.char_height))
    return grub_error (GRUB_ERR_UNKNOWN_DEVICE, "invalid font");

  return grub_errno;
}

static grub_err_t
grub_gfx_region_fini (void)
{
  grub_video_restore ();
  return (grub_errno = GRUB_ERR_NONE);
}

static void
grub_gfx_region_get_screen_size (int *width, int *height)
{
  *width = screen_width;
  *height = screen_height;
}

/* ----- BURG font auto-load (themes name fonts; font.lst maps name->file) -----
   Upstream grub_font_get does NOT auto-load (BURG patched it to). So when a
   theme asks for a font we don't have loaded, look the name up in
   <font_dir>/font.lst ("<name>: <file.pf2>") and grub_font_load the file. */

static char *wb_font_dir;	/* dir with font.lst + the .pf2 files */
static char *wb_font_lst;	/* cached font.lst contents (lazy) */
static int wb_font_lst_tried;

/* names we've already attempted to load, so a genuine miss isn't retried. */
struct wb_font_seen { struct wb_font_seen *next; char *name; };
static struct wb_font_seen *wb_font_seen;

void
grub_menu_region_set_font_dir (const char *dir)
{
  struct wb_font_seen *s;

  grub_free (wb_font_dir);
  grub_free (wb_font_lst);
  wb_font_lst = 0;
  wb_font_lst_tried = 0;
  while ((s = wb_font_seen))
    {
      wb_font_seen = s->next;
      grub_free (s->name);
      grub_free (s);
    }
  wb_font_dir = (dir) ? grub_strdup (dir) : 0;
}

static void
wb_load_font_lst (void)
{
  grub_file_t file;
  char *path;
  grub_off_t sz;

  if (wb_font_lst_tried || ! wb_font_dir)
    return;
  wb_font_lst_tried = 1;

  path = grub_xasprintf ("%s/font.lst", wb_font_dir);
  if (! path)
    return;
  file = grub_file_open (path, GRUB_FILE_TYPE_THEME);
  grub_free (path);
  if (! file)
    {
      grub_errno = GRUB_ERR_NONE;
      return;
    }
  sz = grub_file_size (file);
  wb_font_lst = grub_malloc (sz + 1);
  if (wb_font_lst)
    {
      if (grub_file_read (file, wb_font_lst, sz) != (grub_ssize_t) sz)
	{
	  grub_free (wb_font_lst);
	  wb_font_lst = 0;
	}
      else
	wb_font_lst[sz] = 0;
    }
  grub_file_close (file);
  grub_errno = GRUB_ERR_NONE;
}

static int
wb_font_already_tried (const char *name)
{
  struct wb_font_seen *s;

  for (s = wb_font_seen; s; s = s->next)
    if (! grub_strcmp (s->name, name))
      return 1;

  s = grub_malloc (sizeof (*s));
  if (s)
    {
      s->name = grub_strdup (name);
      s->next = wb_font_seen;
      wb_font_seen = s;
    }
  return 0;
}

static void
wb_autoload_font (const char *name)
{
  char *p;
  grub_size_t namelen;

  if (! wb_font_dir || wb_font_already_tried (name))
    return;
  wb_load_font_lst ();
  if (! wb_font_lst)
    return;

  namelen = grub_strlen (name);
  p = wb_font_lst;
  while (*p)
    {
      char *nl, *colon;

      nl = grub_strchr (p, '\n');
      colon = grub_strchr (p, ':');
      if (colon && (! nl || colon < nl)
	  && (grub_size_t) (colon - p) == namelen
	  && ! grub_strncmp (p, name, namelen))
	{
	  char *file, *end, *fname, *path;
	  grub_size_t flen;

	  file = colon + 1;
	  while (*file == ' ' || *file == '\t')
	    file++;
	  end = file;
	  while (*end && *end != '\n' && *end != '\r')
	    end++;
	  /* GRUB printf has no "%.*s"; copy the filename out by hand. */
	  flen = end - file;
	  fname = grub_malloc (flen + 1);
	  if (fname)
	    {
	      grub_memcpy (fname, file, flen);
	      fname[flen] = '\0';
	      path = grub_xasprintf ("%s/%s", wb_font_dir, fname);
	      grub_free (fname);
	      if (path)
		{
		  grub_font_load (path);
		  grub_free (path);
		  grub_errno = GRUB_ERR_NONE;
		}
	    }
	  return;
	}
      if (! nl)
	break;
      p = nl + 1;
    }
}

static grub_font_t
grub_gfx_region_get_font (const char *name)
{
  grub_font_t f;

  if (! name || ! *name)
    return default_font;

  f = grub_font_get (name);
  if (grub_strcmp (grub_font_get_name (f), name) != 0)
    {
      /* miss -> try to auto-load it from font.lst, then re-resolve. */
      wb_autoload_font (name);
      f = grub_font_get (name);
    }
  return f;
}

static int
grub_gfx_region_get_text_width (grub_font_t font, const char *str,
				int count, int *chars)
{
  return wb_str_width (font, str, count, chars);
}

static int
grub_gfx_region_get_text_height (grub_font_t font)
{
  return grub_font_get_height (font);
}

static struct grub_video_bitmap *
grub_gfx_region_get_bitmap (const char *name)
{
  struct grub_video_bitmap *bitmap;

  if (grub_video_bitmap_load (&bitmap, name))
    {
      grub_errno = 0;
      return 0;
    }

  return bitmap;
}

static void
grub_gfx_region_free_bitmap (struct grub_video_bitmap *bitmap)
{
  grub_video_bitmap_destroy (bitmap);
}

/* Map BURG scale types to modern exported scaling. */
static void
grub_gfx_region_scale_bitmap (struct grub_menu_region_bitmap *bitmap)
{
  int w = bitmap->common.width;
  int h = bitmap->common.height;
  struct grub_video_bitmap *src = bitmap->cache->bitmap;

  if ((bitmap->bitmap != bitmap->cache->bitmap) &&
      (bitmap->bitmap != bitmap->cache->scaled_bitmap))
    grub_video_bitmap_destroy (bitmap->bitmap);

  bitmap->bitmap = 0;

  switch (bitmap->scale)
    {
    case WB_SCALE_MINFIT:	/* cover */
      grub_video_bitmap_scale_proportional
	(&bitmap->bitmap, w, h, src, GRUB_VIDEO_BITMAP_SCALE_METHOD_BEST,
	 GRUB_VIDEO_BITMAP_SELECTION_METHOD_CROP,
	 GRUB_VIDEO_BITMAP_V_ALIGN_CENTER, GRUB_VIDEO_BITMAP_H_ALIGN_CENTER);
      break;
    case WB_SCALE_MAXFIT:	/* contain */
    case WB_SCALE_CENTER:	/* TODO: true no-scale centering */
      grub_video_bitmap_scale_proportional
	(&bitmap->bitmap, w, h, src, GRUB_VIDEO_BITMAP_SCALE_METHOD_BEST,
	 GRUB_VIDEO_BITMAP_SELECTION_METHOD_PADDING,
	 GRUB_VIDEO_BITMAP_V_ALIGN_CENTER, GRUB_VIDEO_BITMAP_H_ALIGN_CENTER);
      break;
    case WB_SCALE_TILING:	/* TODO: real tiling; stretch for now */
    case WB_SCALE_NORMAL:
    default:
      grub_video_bitmap_create_scaled (&bitmap->bitmap, w, h, src,
				       GRUB_VIDEO_BITMAP_SCALE_METHOD_BEST);
      break;
    }
}

static struct grub_video_bitmap *
grub_gfx_region_new_bitmap (int width, int height)
{
  struct grub_video_bitmap *dst = 0;

  grub_video_bitmap_create (&dst, width, height,
			    GRUB_VIDEO_BLIT_FORMAT_RGBA_8888);
  return dst;
}

static void
grub_gfx_region_blit_bitmap (struct grub_video_bitmap *dst __attribute__ ((unused)),
			     struct grub_video_bitmap *src __attribute__ ((unused)),
			     enum grub_video_blit_operators oper __attribute__ ((unused)),
			     int dst_x __attribute__ ((unused)),
			     int dst_y __attribute__ ((unused)),
			     int width __attribute__ ((unused)),
			     int height __attribute__ ((unused)),
			     int src_x __attribute__ ((unused)),
			     int src_y __attribute__ ((unused)))
{
  /* TODO (layer 3): bitmap->bitmap compositing for nine-slice boxes, via
     render targets (grub_video_create_render_target/set_active_render_target).
     Not exercised by layer 1. */
}

static grub_video_color_t
grub_gfx_region_map_color (int fg_color,
			   int bg_color __attribute__ ((unused)))
{
  return grub_video_map_color (fg_color);
}

static grub_video_color_t
grub_gfx_region_map_rgb (grub_uint8_t red, grub_uint8_t green,
			 grub_uint8_t blue)
{
  return grub_video_map_rgb (red, green, blue);
}

static void
grub_gfx_region_update_rect (struct grub_menu_region_rect *rect,
			     int x, int y, int width, int height,
			     int scn_x, int scn_y)
{
  if (rect->fill)
    {
      struct grub_font_glyph *glyph;
      int w, h, font_ascent, font_width;

      grub_video_set_viewport (scn_x, scn_y, width, height);
      glyph = grub_font_get_glyph_with_fallback (default_font, rect->fill);
      font_ascent = grub_font_get_ascent (default_font);
      font_width = glyph->device_width;
      for (h = -y; h < height; h += grub_gfx_region.char_height)
	{
	  for (w = -x; w < width; w += font_width)
	    grub_font_draw_glyph (glyph, rect->color, w, h + font_ascent);
	}
      grub_video_set_viewport (0, 0, screen_width, screen_height);
    }
  else
    grub_video_fill_rect (rect->color, scn_x, scn_y, width, height);}

static void
grub_gfx_region_update_text (struct grub_menu_region_text *text,
			     int x, int y, int width, int height,
			     int scn_x, int scn_y)
{
  grub_video_set_viewport (scn_x, scn_y, width, height);
  wb_draw_text_glyphs (text->text, text->font, text->color, - x,
		       - y + grub_font_get_ascent (text->font));
  grub_video_set_viewport (0, 0, screen_width, screen_height);}

static void
grub_gfx_region_update_bitmap (struct grub_menu_region_bitmap *bitmap,
			       int x, int y, int width, int height,
			       int scn_x, int scn_y)
{
  grub_video_blit_bitmap (bitmap->bitmap, GRUB_VIDEO_BLIT_BLEND,
			  scn_x, scn_y, x, y, width, height);}

#define CURSOR_HEIGHT	2

static void
grub_gfx_region_draw_cursor (struct grub_menu_region_text *text,
			     int width, int height, int scn_x, int scn_y)
{
  int y;

  y = grub_font_get_ascent (text->font);
  if (y >= height)
    return;

  height -= y;
  if (height > CURSOR_HEIGHT)
    height = CURSOR_HEIGHT;

  grub_video_fill_rect (text->color, scn_x, scn_y + y, width, height);
}

static struct grub_menu_region grub_gfx_region =
  {
    .name = "gfx",
    .init = grub_gfx_region_init,
    .fini = grub_gfx_region_fini,
    .get_screen_size = grub_gfx_region_get_screen_size,
    .get_font = grub_gfx_region_get_font,
    .get_text_width = grub_gfx_region_get_text_width,
    .get_text_height = grub_gfx_region_get_text_height,
    .get_bitmap = grub_gfx_region_get_bitmap,
    .free_bitmap = grub_gfx_region_free_bitmap,
    .scale_bitmap = grub_gfx_region_scale_bitmap,
    .map_color = grub_gfx_region_map_color,
    .map_rgb = grub_gfx_region_map_rgb,
    .update_rect = grub_gfx_region_update_rect,
    .update_text = grub_gfx_region_update_text,
    .update_bitmap = grub_gfx_region_update_bitmap,
    .draw_cursor = grub_gfx_region_draw_cursor,
    .new_bitmap = grub_gfx_region_new_bitmap,
    .blit_bitmap = grub_gfx_region_blit_bitmap
  };

grub_err_t
grub_menu_region_gfx_init (void)
{
  cur_region = &grub_gfx_region;
  return grub_gfx_region_init ();
}

void
grub_menu_region_gfx_fini (void)
{
  grub_gfx_region_fini ();
  cur_region = 0;
}

/* ===== region object helpers (BURG menu/ext/region.c) ===== */

static grub_bitmap_cache_t cache_head;

#define grub_cur_menu_region	grub_menu_region_get_current ()

grub_menu_region_text_t
grub_menu_region_create_text (grub_font_t font, grub_video_color_t color,
			      const char *str)
{
  grub_menu_region_text_t region;

  if (! str)
    str = "";

  region = grub_zalloc (sizeof (*region) + grub_strlen (str) + 1);
  if (! region)
    return 0;

  region->common.type = GRUB_MENU_REGION_TYPE_TEXT;
  region->common.width = grub_menu_region_get_text_width (font, str, 0, 0);
  region->common.height = grub_menu_region_get_text_height (font);

  region->font = font;
  region->color = color;
  grub_strcpy (region->text, str);

  return region;
}

grub_menu_region_rect_t
grub_menu_region_create_rect (int width, int height, grub_video_color_t color,
			      grub_uint32_t fill)
{
  grub_menu_region_rect_t region;

  region = grub_zalloc (sizeof (*region));
  if (! region)
    return 0;

  region->common.type = GRUB_MENU_REGION_TYPE_RECT;
  region->common.width = width;
  region->common.height = height;
  region->color = color;
  region->fill = fill;

  return region;
}

grub_menu_region_bitmap_t
grub_menu_region_create_bitmap (const char *name, int scale,
				grub_video_color_t color)
{
  grub_menu_region_bitmap_t region = 0;
  struct grub_video_bitmap *bitmap;
  grub_bitmap_cache_t cache = 0;

  if (! grub_cur_menu_region->get_bitmap)
    return 0;

  region = grub_zalloc (sizeof (*region));
  if (! region)
    return 0;

  for (cache = cache_head; cache; cache = cache->next)
    if (! grub_strcmp (cache->name, name))
      break;
  if (cache)
    bitmap = cache->bitmap;
  else
    {
      cache = grub_zalloc (sizeof (*cache));
      if (! cache)
	goto quit;

      bitmap = grub_cur_menu_region->get_bitmap (name);

      if (! bitmap)
	goto quit;

      cache->bitmap = bitmap;
      cache->name = grub_strdup (name);
      cache->next = cache_head;
      cache_head = cache;
    }

  cache->count++;
  region->common.type = GRUB_MENU_REGION_TYPE_BITMAP;
  region->common.width = bitmap->mode_info.width;
  region->common.height = bitmap->mode_info.height;
  region->bitmap = bitmap;
  region->cache = cache;
  region->scale = scale;
  region->color = color;
  return region;

 quit:
  grub_free (region);
  grub_free (cache);
  return 0;
}

void
grub_menu_region_scale (grub_menu_region_common_t region, int width,
			int height)
{
  if ((! region) || ((width == region->width) && (height == region->height)))
    return;

  region->width = width;
  region->height = height;

  if ((region->type == GRUB_MENU_REGION_TYPE_BITMAP) &&
      (grub_cur_menu_region->scale_bitmap))
    {
      grub_menu_region_bitmap_t b;

      b = (grub_menu_region_bitmap_t) region;
      if ((b->cache->scaled_bitmap) &&
	  (width == (int) b->cache->scaled_bitmap->mode_info.width) &&
	  (height == (int) b->cache->scaled_bitmap->mode_info.height))
	{
	  b->bitmap = b->cache->scaled_bitmap;
	}
      else
	{
	  grub_cur_menu_region->scale_bitmap (b);
	  if (! b->cache->scaled_bitmap)
	    b->cache->scaled_bitmap = b->bitmap;
	}
    }
}

static void
grub_bitmap_cache_free (grub_bitmap_cache_t cache)
{
  cache->count--;
  if (cache->count == 0)
    {
      {
	grub_bitmap_cache_t *pp = &cache_head;
	while (*pp && *pp != cache)
	  pp = &(*pp)->next;
	if (*pp)
	  *pp = cache->next;
      }
      grub_cur_menu_region->free_bitmap (cache->bitmap);
      grub_cur_menu_region->free_bitmap (cache->scaled_bitmap);
      grub_free ((char *) cache->name);
      grub_free (cache);
    }
}

void
grub_menu_region_free (grub_menu_region_common_t region)
{
  if (! region)
    return;

  if ((region->type == GRUB_MENU_REGION_TYPE_BITMAP) &&
      (grub_cur_menu_region->free_bitmap))
    {
      grub_menu_region_bitmap_t r = (grub_menu_region_bitmap_t) region;

      if (r->cache)
	{
	  if ((r->bitmap != r->cache->bitmap) &&
	      (r->bitmap != r->cache->scaled_bitmap))
	    grub_cur_menu_region->free_bitmap (r->bitmap);

	  grub_bitmap_cache_free (r->cache);
	}
      else
	grub_cur_menu_region->free_bitmap (r->bitmap);
    }

  grub_free (region);
}

grub_menu_region_bitmap_t
grub_menu_region_new_bitmap (int width, int height)
{
  grub_menu_region_bitmap_t region = 0;
  struct grub_video_bitmap *bm;

  if (! grub_menu_region_get_current ()->new_bitmap)
    return 0;

  region = grub_zalloc (sizeof (*region));
  if (! region)
    return 0;

  bm = grub_menu_region_get_current ()->new_bitmap (width, height);
  if (! bm)
    {
      grub_free (region);
      return 0;
    }

  region->common.type = GRUB_MENU_REGION_TYPE_BITMAP;
  region->common.width = width;
  region->common.height = height;
  region->bitmap = bm;
  return region;
}

int
grub_menu_region_check_rect (int *x1, int *y1, int *w1, int *h1,
			     int x2, int y2, int w2, int h2)
{
  *w1 += *x1;
  *h1 += *y1;
  w2 += x2;
  h2 += y2;

  if (*x1 < x2)
    *x1 = x2;

  if (*y1 < y2)
    *y1 = y2;

  if (*w1 > w2)
    *w1 = w2;

  if (*h1 > h2)
    *h1 = h2;

  if ((*w1 <= *x1) || (*h1 <= *y1))
    return 0;

  *w1 -= *x1;
  *h1 -= *y1;

  return 1;
}

void
grub_menu_region_add_update (grub_menu_region_update_list_t *head,
			     grub_menu_region_common_t region,
			     int org_x, int org_y, int x, int y,
			     int width, int height)
{
  grub_menu_region_update_list_t u;

  if (! region)
    return;

  org_x += region->ofs_x;
  org_y += region->ofs_y;
  x -= region->ofs_x;
  y -= region->ofs_y;

  if (! grub_menu_region_check_rect (&x, &y, &width, &height,
				     0, 0, region->width, region->height))
    return;

  u = grub_malloc (sizeof (*u));
  if (! u)
    return;

  u->region = region;
  u->org_x = org_x;
  u->org_y = org_y;
  u->x = x;
  u->y = y;
  u->width = width;
  u->height = height;

  if ((! grub_menu_region_gfx_mode ()) ||
      ((region->type == GRUB_MENU_REGION_TYPE_RECT) &&
       (! ((grub_menu_region_rect_t) region)->fill)) ||
      ((region->type == GRUB_MENU_REGION_TYPE_BITMAP) &&
       (0 /* 2.15 grub_video_bitmap has no 'transparent'; don't cull under bitmaps */)))
    {
      grub_menu_region_update_list_t *p, q;

      for (p = head, q = *p; q;)
	{
	  int x1, y1, w1, h1;

	  x1 = org_x + x - q->org_x;
	  y1 = org_y + y - q->org_y;
	  w1 = width;
	  h1 = height;

	  if (! grub_menu_region_check_rect (&x1, &y1, &w1, &h1,
					     q->x, q->y, q->width, q->height))
	    {
	      p = &(q->next);
	      q = q->next;
	      continue;
	    }

	  if (y1 > q->y)
	    {
	      grub_menu_region_update_list_t n;

	      n = grub_malloc (sizeof (*u));
	      if (n)
		{
		  n->region = q->region;
		  n->org_x = q->org_x;
		  n->org_y = q->org_y;
		  n->x = q->x;
		  n->y = q->y;
		  n->width = q->width;
		  n->height = y1 - q->y;
		  *p = n;
		  p = &(n->next);
		}
	    }

	  if (x1 > q->x)
	    {
	      grub_menu_region_update_list_t n;

	      n = grub_malloc (sizeof (*u));
	      if (n)
		{
		  n->region = q->region;
		  n->org_x = q->org_x;
		  n->org_y = q->org_y;
		  n->x = q->x;
		  n->y = y1;
		  n->width = x1 - q->x;
		  n->height = h1;
		  *p = n;
		  p = &(n->next);
		}
	    }

	  if (x1 + w1 < q->x + q->width)
	    {
	      grub_menu_region_update_list_t n;

	      n = grub_malloc (sizeof (*u));
	      if (n)
		{
		  n->region = q->region;
		  n->org_x = q->org_x;
		  n->org_y = q->org_y;
		  n->x = x1 + w1;
		  n->y = y1;
		  n->width = q->x + q->width - n->x;
		  n->height = h1;
		  *p = n;
		  p = &(n->next);
		}
	    }

	  if (y1 + h1 < q->y + q->height)
	    {
	      grub_menu_region_update_list_t n;

	      n = grub_malloc (sizeof (*u));
	      if (n)
		{
		  n->region = q->region;
		  n->org_x = q->org_x;
		  n->org_y = q->org_y;
		  n->x = q->x;
		  n->y = y1 + h1;
		  n->width = q->width;
		  n->height = q->y + q->height - n->y;
		  *p = n;
		  p = &(n->next);
		}
	    }

	  *p = q->next;
	  grub_free (q);
	  q = *p;
	}
    }

  u->next = *head;
  *head = u;
}

void
grub_menu_region_apply_update (grub_menu_region_update_list_t head)
{
  grub_menu_region_update_list_t prev = 0;

  grub_menu_region_hide_cursor ();
  while (head)
    {
      grub_menu_region_update_list_t temp;

      temp = head->next;
      head->next = prev;
      prev = head;
      head = temp;
    }

  head = prev;
  while (head)
    {
      grub_menu_region_update_list_t c;
      grub_menu_region_common_t r;
      int scn_x, scn_y;

      c = head;
      head = head->next;
      r = c->region;
      scn_x = c->org_x + c->x;
      scn_y = c->org_y + c->y;

      switch (r->type)
	{
	case GRUB_MENU_REGION_TYPE_RECT:
	  grub_cur_menu_region->update_rect ((grub_menu_region_rect_t) r,
					     c->x, c->y, c->width, c->height,
					     scn_x, scn_y);
	  break;

	case GRUB_MENU_REGION_TYPE_TEXT:
	  grub_cur_menu_region->update_text ((grub_menu_region_text_t) r,
					     c->x, c->y, c->width, c->height,
					     scn_x, scn_y);
	  break;

	case GRUB_MENU_REGION_TYPE_BITMAP:
	  grub_cur_menu_region->update_bitmap ((grub_menu_region_bitmap_t) r,
					       c->x, c->y, c->width, c->height,
					       scn_x, scn_y);
	  break;
	}
      grub_free (c);
    }
}

/* ===== theme value parsers (BURG menu/ext/data_type.c) ===== */

char *
grub_menu_next_field (char *name, char c)
{
  char *p;

  p = grub_strchr (name, c);
  if (p)
    *(p++) = '\0';

  return p;
}

void
grub_menu_restore_field (char *name, char c)
{
  if (name)
    *(name - 1) = c;
}

static const char *color_list[16] =
{
  "black", "blue", "green", "cyan", "red", "magenta", "brown", "light-gray",
  "dark-gray", "light-blue", "light-green", "light-cyan", "light-red",
  "light-magenta", "yellow", "white"
};

static int
parse_color_name (int *ret, char *name)
{
  grub_uint8_t i;
  for (i = 0; i < sizeof (color_list) / sizeof (*color_list); i++)
    if (! grub_strcmp (name, color_list[i]))
      {
	*ret = i;
	return 0;
      }
  return -1;
}

static int
parse_key (char *name)
{
  int k;

  if (name[1] == 0)
    return name[0];

  if (*name != '#')
    return -1;

  k = grub_strtoul (name + 1, (const char **) &name, 0);
  return (*name != 0) ? 0 : k;
}

static grub_video_color_t
parse_color (char *name, grub_uint32_t *fill)
{
  int fg, bg;
  char *p, *n;

  n = grub_menu_next_field (name, ',');
  if (n)
    *fill = parse_key (n);

  if (*name == '#')
    {
      grub_uint32_t rgb;

      rgb = grub_strtoul (name + 1, (const char **) &name, 16);
      if (grub_menu_region_get_current ()->map_rgb)
	{
	  grub_menu_restore_field (n, ',');
	  return grub_menu_region_get_current ()->map_rgb (rgb >> 16, rgb >> 8,
							   rgb);
	}

      if (*name == '/')
	name++;
    }

  p = grub_menu_next_field (name, '/');
  if (parse_color_name (&fg, name) == -1)
    fg = 7;

  grub_menu_restore_field (p, '/');
  if (p)
    {
      if (parse_color_name (&bg, p) == -1)
	bg = 0;
    }
  else
    {
      if ((fill) && (! grub_menu_region_gfx_mode ()))
	{
	  bg = fg;
	  fg = 7;
	}
      else
	bg = 0;
    }

  grub_menu_restore_field (n, ',');
  return grub_menu_region_map_color (fg, bg);
}

grub_video_color_t
grub_menu_parse_color (const char *str, grub_uint32_t *fill,
		       grub_video_color_t *color_selected,
		       grub_uint32_t *fill_selected)
{
  char *name, *n;
  grub_video_color_t color, color1;

  name = (char *) str;
  n = grub_menu_next_field (name, ':');
  color = parse_color (name, fill);
  grub_menu_restore_field (n, ':');
  color1 = (n) ? parse_color (n, fill_selected) : color;

  if (color_selected)
    *color_selected = color1;

  return color;
}

static grub_menu_region_common_t
parse_bitmap (char *name, grub_uint32_t def_fill)
{
  int scale = WB_SCALE_NORMAL;
  grub_video_color_t color;
  char *ps, *pc;
  grub_uint32_t fill;

  if (name[0] == '(')
    {
      ps = grub_strchr (name, ')');
      if (! ps)
	return 0;
    }
  else
    ps = name;

  ps = grub_menu_next_field (ps, ',');
  if (ps)
    {
      pc = grub_menu_next_field (ps, ',');

      if (! grub_strcmp (ps, "center"))
	scale = WB_SCALE_CENTER;
      else if (! grub_strcmp (ps, "tiling"))
	scale = WB_SCALE_TILING;
      else if (! grub_strcmp (ps, "minfit"))
	scale = WB_SCALE_MINFIT;
      else if (! grub_strcmp (ps, "maxfit"))
	scale = WB_SCALE_MAXFIT;

      grub_menu_restore_field (pc, ',');
    }
  else
    pc = 0;

  fill = def_fill;
  color = (pc) ? parse_color (pc, &fill) : 0;

  if ((*name) && (grub_menu_region_gfx_mode ()))
    {
      grub_menu_region_bitmap_t bitmap;

      if (! grub_strcmp (name, "none"))
	{
	  grub_menu_restore_field (ps, ',');
	  return 0;
	}

      bitmap = grub_menu_region_create_bitmap (name, scale, color);
      if (bitmap)
	{
	  grub_menu_restore_field (ps, ',');
	  return (grub_menu_region_common_t) bitmap;
	}
    }

  grub_menu_restore_field (ps, ',');

  if ((int) fill == -1)
    return 0;

  return (grub_menu_region_common_t)
    grub_menu_region_create_rect (grub_menu_region_get_char_width (),
				  grub_menu_region_get_char_height (),
				  color, fill);
}

grub_menu_region_common_t
grub_menu_parse_bitmap (const char *str, grub_uint32_t def_fill,
			grub_menu_region_common_t *bitmap_selected)
{
  char *name, *n;
  grub_menu_region_common_t bitmap;

  if (bitmap_selected)
    *bitmap_selected = 0;

  name = (str) ? (char *) str : (char *) "";
  n = grub_menu_next_field (name, ':');
  bitmap = parse_bitmap (name, def_fill);
  grub_menu_restore_field (n, ':');
  if ((n) && (bitmap_selected))
    *bitmap_selected = parse_bitmap (n, def_fill);

  return bitmap;
}

long
grub_menu_parse_size (const char *str, int parent_size, int horizontal)
{
  char *end;
  long ret;

  ret = grub_strtol (str, (const char **) &end, 0);
  if (*end == 0)
    ret *= (horizontal) ?
      grub_menu_region_get_char_width () :
      grub_menu_region_get_char_height ();
  else
    {
      if (*end == '%')
	{
	  ret = (ret * parent_size) / 100;
	  end++;
	}

      if ((*end == '/') && (! grub_menu_region_gfx_mode ()))
	{
	  int old;

	  old = ret;
	  ret = grub_strtol (end + 1, (const char **) &end, 0);
	  if (old < 0)
	    ret = -ret;

	  if (*end == '%')
	    ret = (ret * parent_size) / 100;
	}
    }

  return ret;
}

static const char *key_list[] =
  {
    "\002left", "\006right", "\020up", "\016down", "\001home", "\005end",
    "\004delete", "\007page_up", "\003page_down", "\033esc", "\011tab",
    "\010backspace", "\renter", " space", 0
  };

const char *
grub_menu_key2name (int key)
{
  static char keyname[sizeof ("ctrl-a")];
  const char **p;

  for (p = key_list; *p; p++)
    {
      if (key == p[0][0])
	return p[0] + 1;
    }

  keyname[0] = 0;
  if ((key > 32) && (key < 127))
    {
      keyname[0] = key;
      keyname[1] = 0;
    }
  else if ((key >= WB_CTRL_A) && (key <= WB_CTRL_Z))
    grub_snprintf (keyname, sizeof (keyname), "ctrl-%c",
		   key - WB_CTRL_A + 'a');
  else if ((key >= GRUB_TERM_KEY_F1) && (key <= GRUB_TERM_KEY_F10))
    grub_snprintf (keyname, sizeof (keyname), "f%d",
		   key - GRUB_TERM_KEY_F1 + 1);

  return (keyname[0]) ? keyname : 0;
}

int
grub_menu_name2key (const char *name)
{
  const char **p;

  for (p = key_list; *p; p++)
    {
      if (! grub_strcmp (name, p[0] + 1))
	return p[0][0];
    }

  if ((name[0] > 32) && (name[0] < 127) && (name[1] == 0))
    return name[0];
  else if ((! grub_memcmp (name, "ctrl-", 5)) &&
	   (name[5] >= 'a') && (name[5] <= 'z'))
    return name[5] - 'a' + WB_CTRL_A;
  else if (name[0] == 'f')
    {
      int num;

      num = grub_strtol (name + 1, 0, 0);
      if ((num >= 1) && (num <= 10))
	return GRUB_TERM_KEY_F1 + num - 1;
    }

  return 0;
}
