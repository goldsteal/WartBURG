/* wartburg_region.h - BURG menu_region (draw backend) + menu_data, ported.
 *
 * Adapts bean's BURG include/grub/{menu_region,menu_data}.h to GRUB 2.15:
 *   - the removed handler framework is replaced by a single backend pointer
 *     (WartBURG is gfx-only, so there is exactly one region backend);
 *   - scaling/compositing use modern exported video APIs (see wartburg_region.c).
 * Original copyright 2009 Bean Lee; GPLv3+.
 */

#ifndef GRUB_WARTBURG_REGION_HEADER
#define GRUB_WARTBURG_REGION_HEADER 1

#include <grub/err.h>
#include <grub/types.h>
#include <grub/video.h>
#include <grub/bitmap.h>
#include <grub/font.h>
#include <grub/term.h>

#define GRUB_MENU_REGION_TYPE_TEXT	0
#define GRUB_MENU_REGION_TYPE_RECT	1
#define GRUB_MENU_REGION_TYPE_BITMAP	2

/* BURG scale types (its bitmap_scale.h is gone in 2.15; mapped to modern
   selection methods in wartburg_region.c). */
#define WB_SCALE_NORMAL	0
#define WB_SCALE_CENTER	1
#define WB_SCALE_TILING	2
#define WB_SCALE_MINFIT	3
#define WB_SCALE_MAXFIT	4

struct grub_menu_region_common
{
  int type;
  int ofs_x;
  int ofs_y;
  int width;
  int height;
};
typedef struct grub_menu_region_common *grub_menu_region_common_t;

struct grub_menu_region_rect
{
  struct grub_menu_region_common common;
  grub_video_color_t color;
  grub_uint32_t fill;
};
typedef struct grub_menu_region_rect *grub_menu_region_rect_t;

struct grub_menu_region_text
{
  struct grub_menu_region_common common;
  grub_font_t font;
  grub_video_color_t color;
  char text[0];
};
typedef struct grub_menu_region_text *grub_menu_region_text_t;

struct grub_bitmap_cache
{
  struct grub_bitmap_cache *next;
  const char *name;
  struct grub_video_bitmap *bitmap;
  struct grub_video_bitmap *scaled_bitmap;
  int count;
};
typedef struct grub_bitmap_cache *grub_bitmap_cache_t;

struct grub_menu_region_bitmap
{
  struct grub_menu_region_common common;
  struct grub_video_bitmap *bitmap;
  struct grub_bitmap_cache *cache;
  int scale;
  grub_video_color_t color;
};
typedef struct grub_menu_region_bitmap *grub_menu_region_bitmap_t;

struct grub_menu_region
{
  struct grub_menu_region *next;
  const char *name;
  grub_err_t (*init) (void);
  grub_err_t (*fini) (void);
  int char_width;
  int char_height;
  grub_font_t (*get_font) (const char *name);
  struct grub_video_bitmap * (*get_bitmap) (const char *name);
  void (*free_bitmap) (struct grub_video_bitmap *bitmap);
  void (*scale_bitmap) (struct grub_menu_region_bitmap *bitmap);
  struct grub_video_bitmap * (*new_bitmap) (int width, int height);
  void (*blit_bitmap) (struct grub_video_bitmap *dst,
		       struct grub_video_bitmap *src,
		       enum grub_video_blit_operators oper,
		       int dst_x, int dst_y, int width, int height,
		       int src_x, int src_y);

  grub_video_color_t (*map_color) (int fg_color, int bg_color);
  grub_video_color_t (*map_rgb) (grub_uint8_t red, grub_uint8_t green,
				 grub_uint8_t blue);
  void (*get_screen_size) (int *width, int *height);
  int (*get_text_width) (grub_font_t font, const char *str,
			 int count, int *chars);
  int (*get_text_height) (grub_font_t font);
  void (*update_rect) (struct grub_menu_region_rect *rect,
		       int x, int y, int width, int height,
		       int scn_x, int scn_y);
  void (*update_text) (struct grub_menu_region_text *text,
		       int x, int y, int width, int height,
		       int scn_x, int scn_y);
  void (*update_bitmap) (struct grub_menu_region_bitmap *bitmap,
			 int x, int y, int width, int height,
			 int scn_x, int scn_y);
  void (*hide_cursor) (void);
  void (*draw_cursor) (struct grub_menu_region_text *text,
		       int width, int height, int scn_x, int scn_y);
};
typedef struct grub_menu_region *grub_menu_region_t;

struct grub_menu_region_update_list
{
  struct grub_menu_region_update_list *next;
  grub_menu_region_common_t region;
  int org_x;
  int org_y;
  int x;
  int y;
  int width;
  int height;
};
typedef struct grub_menu_region_update_list *grub_menu_region_update_list_t;

/* Single-backend model (replaces BURG's handler framework). */
grub_menu_region_t grub_menu_region_get_current (void);
void grub_menu_region_set_current (grub_menu_region_t region);
/* Bring up / tear down the gfx backend (sets the current backend). */
grub_err_t grub_menu_region_gfx_init (void);
void grub_menu_region_gfx_fini (void);

/* Nonzero when the active mode is software double-buffered: each frame must be
   drawn into both buffers (set by gfx_init).  */
extern int grub_wb_double_repaint;

/* Directory holding the theme fonts + their BURG font.lst (name->file map).
   When a theme names a font we don't have loaded, get_font auto-loads it from
   here. Pass NULL to disable auto-loading. */
void grub_menu_region_set_font_dir (const char *dir);

static inline void
grub_menu_region_get_screen_size (int *width, int *height)
{
  grub_menu_region_get_current ()->get_screen_size (width, height);
}

static inline grub_font_t
grub_menu_region_get_font (const char *name)
{
  return ((grub_menu_region_get_current ()->get_font) ?
	  grub_menu_region_get_current ()->get_font (name) : 0);
}

static inline int
grub_menu_region_get_text_width (grub_font_t font, const char *str,
				 int count, int *chars)
{
  return grub_menu_region_get_current ()->get_text_width (font, str, count,
							  chars);
}

static inline int
grub_menu_region_get_text_height (grub_font_t font)
{
  return grub_menu_region_get_current ()->get_text_height (font);
}

static inline int
grub_menu_region_get_char_width (void)
{
  return grub_menu_region_get_current ()->char_width;
}

static inline int
grub_menu_region_get_char_height (void)
{
  return grub_menu_region_get_current ()->char_height;
}

static inline grub_video_color_t
grub_menu_region_map_color (int fg_color, int bg_color)
{
  return grub_menu_region_get_current ()->map_color (fg_color, bg_color);
}

static inline int
grub_menu_region_gfx_mode (void)
{
  return (grub_menu_region_get_current ()->get_font != 0);
}

static inline void
grub_menu_region_draw_cursor (grub_menu_region_text_t text,
			      int width, int height, int scn_x, int scn_y)
{
  grub_menu_region_get_current ()->draw_cursor (text, width, height,
						scn_x, scn_y);
}

static inline void
grub_menu_region_hide_cursor (void)
{
  if (grub_menu_region_get_current ()->hide_cursor)
    grub_menu_region_get_current ()->hide_cursor ();
}

static inline void
grub_menu_region_blit_bitmap (grub_menu_region_common_t dst,
			      grub_menu_region_common_t src,
			      enum grub_video_blit_operators oper,
			      int dst_x, int dst_y, int width, int height,
			      int src_x, int src_y)
{
  if ((grub_menu_region_get_current ()->blit_bitmap) &&
      (dst->type == GRUB_MENU_REGION_TYPE_BITMAP) &&
      (src->type == GRUB_MENU_REGION_TYPE_BITMAP))
    grub_menu_region_get_current ()->blit_bitmap
      (((grub_menu_region_bitmap_t) dst)->bitmap,
       ((grub_menu_region_bitmap_t) src)->bitmap,
       oper, dst_x, dst_y, width, height, src_x, src_y);
}

grub_menu_region_text_t
grub_menu_region_create_text (grub_font_t font, grub_video_color_t color,
			      const char *str);
grub_menu_region_rect_t
grub_menu_region_create_rect (int width, int height, grub_video_color_t color,
			      grub_uint32_t fill);
grub_menu_region_bitmap_t
grub_menu_region_create_bitmap (const char *name, int scale,
				grub_video_color_t color);
grub_menu_region_bitmap_t
grub_menu_region_new_bitmap (int width, int height);
void grub_menu_region_scale (grub_menu_region_common_t region, int width,
			     int height);
void grub_menu_region_free (grub_menu_region_common_t region);

int grub_menu_region_check_rect (int *x1, int *y1, int *w1, int *h1,
				 int x2, int y2, int w2, int h2);
void grub_menu_region_add_update (grub_menu_region_update_list_t *head,
				  grub_menu_region_common_t region,
				  int org_x, int org_y, int x, int y,
				  int width, int height);
void grub_menu_region_apply_update (grub_menu_region_update_list_t head);

/* menu_data: theme value parsers (BURG menu/ext/data_type.c). */
char *grub_menu_next_field (char *name, char c);
void grub_menu_restore_field (char *name, char c);
grub_video_color_t grub_menu_parse_color (const char *str, grub_uint32_t *fill,
					  grub_video_color_t *color_selected,
					  grub_uint32_t *fill_selected);
grub_menu_region_common_t
grub_menu_parse_bitmap (const char *str, grub_uint32_t def_fill,
			grub_menu_region_common_t *bitmap_selected);
long grub_menu_parse_size (const char *str, int parent_size, int horizontal);
const char *grub_menu_key2name (int key);
int grub_menu_name2key (const char *name);

#endif /* GRUB_WARTBURG_REGION_HEADER */
