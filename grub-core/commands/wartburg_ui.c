/* wartburg_ui.c - BURG component widget classes, ported (layer 3).
 *
 * Port of the draw component classes from bean's BURG ui/coreui.c:
 * screen, panel, image, text, progressbar. The interactive classes
 * (password, edit, term -> need onkey/key handling + the gfxmenu term) and
 * circular_progress (trig + animation) are deferred to M3.
 * grub_wartburg_ui_init/_fini register/unregister the ported classes.
 * Original copyright 2009 Bean Lee; GPLv3+.
 */

#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/wartburg_widget.h>

#define MARGIN_FINI	0
#define MARGIN_WIDTH	1
#define MARGIN_HEIGHT	2

static void
adjust_margin (grub_widget_t widget, const char *name, int mode)
{
  char buf[20], *p;
  int len, size;

  grub_strcpy (buf, name);
  len = grub_strlen (buf);
  buf[len++] = '_';

  grub_strcpy (&buf[len], "size");
  p = grub_widget_get_prop (widget->node, buf);
  size = (p) ? grub_menu_parse_size (p, 0, 1) : 0;

  if (mode != MARGIN_HEIGHT)
    {
      int v;

      grub_strcpy (&buf[len], "left");
      p = grub_widget_get_prop (widget->node, buf);
      v = (p) ? grub_menu_parse_size (p, 0, 1) : size;
      if (mode == MARGIN_FINI)
	{
	  widget->inner_x += v;
	  widget->inner_width -= v;
	}
      else
	widget->width += v;

      grub_strcpy (&buf[len], "right");
      p = grub_widget_get_prop (widget->node, buf);
      v = (p) ? grub_menu_parse_size (p, 0, 1) : size;
      if (mode == MARGIN_FINI)
	widget->inner_width -= v;
      else
	widget->width += v;
    }

  if (mode != MARGIN_WIDTH)
    {
      int v;

      grub_strcpy (&buf[len], "top");
      p = grub_widget_get_prop (widget->node, buf);
      v = (p) ? grub_menu_parse_size (p, 0, 0) : size;
      if (mode == MARGIN_FINI)
	{
	  widget->inner_y += v;
	  widget->inner_height -= v;
	}
      else
	widget->height += v;

      grub_strcpy (&buf[len], "bottom");
      p = grub_widget_get_prop (widget->node, buf);
      v = (p) ? grub_menu_parse_size (p, 0, 0) : size;
      if (mode == MARGIN_FINI)
	widget->inner_height -= v;
      else
	widget->height += v;
    }
}

static grub_menu_region_common_t
get_bitmap (grub_widget_t widget, const char *name,
	    int fallback, grub_menu_region_common_t *bitmap_selected)
{
  char *p;

  p = grub_widget_get_prop (widget->node, name);
  return grub_menu_parse_bitmap (p, (fallback) ? 0 : -1, bitmap_selected);
}

/* ===== screen ===== */

struct screen_data
{
  grub_menu_region_common_t background;
};

static int
screen_get_data_size (void)
{
  return sizeof (struct screen_data);
}

static void
screen_init_size (grub_widget_t widget)
{
  struct screen_data *data = widget->data;

  data->background = get_bitmap (widget, "background", 1, 0);
  grub_menu_region_get_screen_size (&widget->width, &widget->height);
  grub_menu_region_scale (data->background, widget->width, widget->height);
}

static void
screen_fini_size (grub_widget_t widget)
{
  adjust_margin (widget, "margin", MARGIN_FINI);
}

static void
screen_free (grub_widget_t widget)
{
  struct screen_data *data = widget->data;

  grub_menu_region_free (data->background);
}

static void
screen_draw (grub_widget_t widget, grub_menu_region_update_list_t *head,
	     int x, int y, int width, int height)
{
  struct screen_data *data = widget->data;

  grub_menu_region_add_update (head, data->background,
			       widget->org_x, widget->org_y,
			       x, y, width, height);
}

static struct grub_widget_class screen_widget_class =
  {
    .name = "screen",
    .get_data_size = screen_get_data_size,
    .init_size = screen_init_size,
    .fini_size = screen_fini_size,
    .free = screen_free,
    .draw = screen_draw
  };

/* ===== panel ===== */

#define INDEX_TOP_LEFT		0
#define INDEX_TOP		1
#define INDEX_TOP_RIGHT		2
#define INDEX_LEFT		3
#define INDEX_RIGHT		4
#define INDEX_BOTTOM_LEFT	5
#define INDEX_BOTTOM		6
#define INDEX_BOTTOM_RIGHT	7
#define INDEX_BACKGROUND	8
#define INDEX_SELECTED		9
#define INDEX_BORDER_TOP	18
#define INDEX_BORDER_LEFT	19
#define INDEX_BORDER_RIGHT	20
#define INDEX_BORDER_BOTTOM	21
#define INDEX_COUNT		22

struct panel_data
{
  grub_menu_region_common_t bitmaps[INDEX_COUNT];
  grub_video_color_t color;
  grub_video_color_t color_selected;
  grub_uint32_t fill;
  grub_uint32_t fill_selected;
};

static const char *border_names[8] =
  {
    "top_left", "top", "top_right", "left", "right",
    "bottom_left", "bottom", "bottom_right"
  };

static int
panel_get_data_size (void)
{
  return sizeof (struct panel_data);
}

static void
get_border_size (struct panel_data *data, int *dx1, int *dy1,
		 int *dx2, int *dy2)
{
  int i;

  *dx1 = *dy1 = *dx2 = *dy2 = 0;
  for (i = INDEX_SELECTED; i >= 0; i -= INDEX_SELECTED)
    {
      if (data->bitmaps[INDEX_LEFT + i])
	*dx1 = data->bitmaps[INDEX_LEFT + i]->width;

      if (data->bitmaps[INDEX_TOP + i])
	*dy1 = data->bitmaps[INDEX_TOP + i]->height;

      if (data->bitmaps[INDEX_RIGHT + i])
	*dx2 = (data->bitmaps[INDEX_RIGHT + i])->width;

      if (data->bitmaps[INDEX_BOTTOM + i])
	*dy2 = data->bitmaps[INDEX_BOTTOM + i]->height;
    }
}

static void
panel_init_size (grub_widget_t widget)
{
  struct panel_data *data = widget->data;
  int border_size, size;
  char *p;
  int i;
  int dx1, dy1, dx2, dy2;

  for (i = INDEX_TOP_LEFT; i <= INDEX_BOTTOM_RIGHT; i++)
    data->bitmaps[i] = get_bitmap (widget, border_names[i - INDEX_TOP_LEFT],
				   0, &data->bitmaps[i + INDEX_SELECTED]);

  data->bitmaps[INDEX_BACKGROUND] =
    get_bitmap (widget, "background", 0,
		&data->bitmaps[INDEX_BACKGROUND + INDEX_SELECTED]);

  p = grub_widget_get_prop (widget->node, "border_color");
  data->fill = data->fill_selected = 0;
  if (p)
    data->color = grub_menu_parse_color (p, &data->fill,
					 &data->color_selected,
					 &data->fill_selected);

  widget->node->flags |= GRUB_WIDGET_FLAG_DYNAMIC;
  if ((data->fill == data->fill_selected) &&
      (data->color == data->color_selected))
    {
      for (i = INDEX_TOP_LEFT + INDEX_SELECTED;
	   i <= INDEX_BACKGROUND + INDEX_SELECTED; i++)
	if (data->bitmaps[i])
	  break;

      if (i > INDEX_BACKGROUND + INDEX_SELECTED)
	widget->node->flags &= ~GRUB_WIDGET_FLAG_DYNAMIC;
    }

  p = grub_widget_get_prop (widget->node, "border_size");
  border_size = (p) ? grub_menu_parse_size (p, 0, 1) : 0;

  p = grub_widget_get_prop (widget->node, "border_left");
  size = (p) ? grub_menu_parse_size (p, 0, 1) : border_size;
  if (size > 0)
    data->bitmaps[INDEX_BORDER_LEFT] = (grub_menu_region_common_t)
      grub_menu_region_create_rect (size, 0, 0, 0);

  p = grub_widget_get_prop (widget->node, "border_right");
  size = (p) ? grub_menu_parse_size (p, 0, 1) : border_size;
  if (size > 0)
    data->bitmaps[INDEX_BORDER_RIGHT] = (grub_menu_region_common_t)
      grub_menu_region_create_rect (size, 0, 0, 0);

  p = grub_widget_get_prop (widget->node, "border_top");
  size = (p) ? grub_menu_parse_size (p, 0, 0) : border_size;
  if (size > 0)
    data->bitmaps[INDEX_BORDER_TOP] = (grub_menu_region_common_t)
      grub_menu_region_create_rect (0, size, 0, 0);

  p = grub_widget_get_prop (widget->node, "border_bottom");
  size = (p) ? grub_menu_parse_size (p, 0, 0) : border_size;
  if (size > 0)
    data->bitmaps[INDEX_BORDER_BOTTOM] = (grub_menu_region_common_t)
      grub_menu_region_create_rect (0, size, 0, 0);

  widget->node->flags |= GRUB_WIDGET_FLAG_TRANSPARENT;

  get_border_size (data, &dx1, &dy1, &dx2, &dy2);

  if (! (widget->node->flags & GRUB_WIDGET_FLAG_FIXED_WIDTH))
    {
      widget->width += dx1 + dx2;

      if (data->bitmaps[INDEX_BORDER_LEFT])
	widget->width += data->bitmaps[INDEX_BORDER_LEFT]->width;

      if (data->bitmaps[INDEX_BORDER_RIGHT])
	widget->width += data->bitmaps[INDEX_BORDER_RIGHT]->width;

      adjust_margin (widget, "padding", MARGIN_WIDTH);
      adjust_margin (widget, "margin", MARGIN_WIDTH);
    }

  if (! (widget->node->flags & GRUB_WIDGET_FLAG_FIXED_HEIGHT))
    {
      widget->height += dy1 + dy2;

      if (data->bitmaps[INDEX_BORDER_TOP])
	widget->height += data->bitmaps[INDEX_BORDER_TOP]->height;

      if (data->bitmaps[INDEX_BORDER_BOTTOM])
	widget->height += data->bitmaps[INDEX_BORDER_BOTTOM]->height;

      adjust_margin (widget, "padding", MARGIN_HEIGHT);
      adjust_margin (widget, "margin", MARGIN_HEIGHT);
    }
}

static void
resize_border (grub_widget_t widget, grub_menu_region_common_t *bitmaps,
	       int index, int x, int y, int width, int height)
{
  if (bitmaps[index])
    {
      grub_menu_region_scale (bitmaps[index], width, height);
      bitmaps[index]->ofs_x = x + widget->inner_x;
      bitmaps[index]->ofs_y = y + widget->inner_y;
    }

  if (bitmaps[index + INDEX_SELECTED])
    {
      grub_menu_region_scale (bitmaps[index + INDEX_SELECTED], width, height);
      bitmaps[index + INDEX_SELECTED]->ofs_x = x + widget->inner_x;
      bitmaps[index + INDEX_SELECTED]->ofs_y = y + widget->inner_y;
    }
}

static void
panel_fini_size (grub_widget_t widget)
{
  struct panel_data *data = widget->data;
  int dx1, dy1, dx2, dy2, bw1, bh1, bw2, bh2, width, height;

  adjust_margin (widget, "padding", MARGIN_FINI);

  get_border_size (data, &dx1, &dy1, &dx2, &dy2);

  bw1 = (data->bitmaps[INDEX_BORDER_LEFT]) ?
    data->bitmaps[INDEX_BORDER_LEFT]->width : 0;
  bw2 = (data->bitmaps[INDEX_BORDER_RIGHT]) ?
    data->bitmaps[INDEX_BORDER_RIGHT]->width : 0;
  bh1 = (data->bitmaps[INDEX_BORDER_TOP]) ?
    data->bitmaps[INDEX_BORDER_TOP]->height : 0;
  bh2 = (data->bitmaps[INDEX_BORDER_BOTTOM]) ?
    data->bitmaps[INDEX_BORDER_BOTTOM]->height : 0;

  width = widget->inner_width - dx1 - dx2 - bw1 - bw2;
  height = widget->inner_height - dy1 - dy2 - bh1 - bh2;

  resize_border (widget, data->bitmaps, INDEX_TOP_LEFT, bw1, bh1, dx1, dy1);
  resize_border (widget, data->bitmaps, INDEX_TOP, bw1 + dx1, bh1, width, dy1);
  resize_border (widget, data->bitmaps, INDEX_TOP_RIGHT,
		 bw1 + dx1 + width, bh1, dx2, dy1);
  resize_border (widget, data->bitmaps, INDEX_LEFT, bw1, bh1 + dy1, dx1, height);
  resize_border (widget, data->bitmaps, INDEX_RIGHT,
		 bw1 + dx1 + width, bh1 + dy1, dx2, height);
  resize_border (widget, data->bitmaps, INDEX_BOTTOM_LEFT,
		 bw1, bh1 + dy1 + height, dx1, dy2);
  resize_border (widget, data->bitmaps, INDEX_BOTTOM,
		 bw1 + dx1, bh1 + dy1 + height, width, dy2);
  resize_border (widget, data->bitmaps, INDEX_BOTTOM_RIGHT,
		 bw1 + dx1 + width, bh1 + dy1 + height, dx2, dy2);
  resize_border (widget, data->bitmaps, INDEX_BACKGROUND,
		 bw1 + dx1, bh1 + dy1, width, height);

  if (data->bitmaps[INDEX_BORDER_TOP])
    {
      data->bitmaps[INDEX_BORDER_TOP]->ofs_x = widget->inner_x;
      data->bitmaps[INDEX_BORDER_TOP]->ofs_y = widget->inner_y;
      data->bitmaps[INDEX_BORDER_TOP]->width = widget->inner_width;
    }

  if (data->bitmaps[INDEX_BORDER_BOTTOM])
    {
      data->bitmaps[INDEX_BORDER_BOTTOM]->ofs_x = widget->inner_x;
      data->bitmaps[INDEX_BORDER_BOTTOM]->ofs_y = widget->inner_y +
	widget->inner_height - bh2;
      data->bitmaps[INDEX_BORDER_BOTTOM]->width = widget->inner_width;
    }

  if (data->bitmaps[INDEX_BORDER_LEFT])
    {
      data->bitmaps[INDEX_BORDER_LEFT]->ofs_x = widget->inner_x;
      data->bitmaps[INDEX_BORDER_LEFT]->ofs_y = widget->inner_y + bh1;
      data->bitmaps[INDEX_BORDER_LEFT]->height =
	widget->inner_height - bh1 - bh2;
    }

  if (data->bitmaps[INDEX_BORDER_RIGHT])
    {
      data->bitmaps[INDEX_BORDER_RIGHT]->ofs_x = widget->inner_x +
	widget->inner_width - bw2;
      data->bitmaps[INDEX_BORDER_RIGHT]->ofs_y = widget->inner_y + bh1;
      data->bitmaps[INDEX_BORDER_RIGHT]->height =
	widget->inner_height - bh1 - bh2;
    }

  widget->inner_width = width;
  widget->inner_height = height;
  widget->inner_x += bw1 + dx1;
  widget->inner_y += bh1 + dy1;

  adjust_margin (widget, "margin", MARGIN_FINI);
}

static void
panel_free (grub_widget_t widget)
{
  struct panel_data *data = widget->data;
  int i;

  for (i = 0; i < INDEX_COUNT; i++)
    grub_menu_region_free (data->bitmaps[i]);
}

static void
panel_draw (grub_widget_t widget, grub_menu_region_update_list_t *head,
	    int x, int y, int width, int height)
{
  struct panel_data *data = widget->data;
  int i, ofs;

  for (i = INDEX_BORDER_TOP; i <= INDEX_BORDER_BOTTOM; i++)
    if (data->bitmaps[i])
      {
	if (widget->node->flags & GRUB_WIDGET_FLAG_SELECTED)
	  {
	    ((grub_menu_region_rect_t) data->bitmaps[i])->color =
	      data->color_selected;
	    ((grub_menu_region_rect_t) data->bitmaps[i])->fill =
	      data->fill_selected;
	  }
	else
	  {
	    ((grub_menu_region_rect_t) data->bitmaps[i])->color = data->color;
	    ((grub_menu_region_rect_t) data->bitmaps[i])->fill = data->fill;
	  }

	grub_menu_region_add_update (head, data->bitmaps[i],
				     widget->org_x, widget->org_y,
				     x, y, width, height);
      }

  ofs = (widget->node->flags & GRUB_WIDGET_FLAG_SELECTED) ? INDEX_SELECTED : 0;
  for (i = INDEX_TOP_LEFT; i <= INDEX_BACKGROUND; i++)
    if (data->bitmaps[i + ofs])
      grub_menu_region_add_update (head, data->bitmaps[i + ofs],
				   widget->org_x, widget->org_y,
				   x, y, width, height);
    else
      grub_menu_region_add_update (head, data->bitmaps[i],
				   widget->org_x, widget->org_y,
				   x, y, width, height);
}

static struct grub_widget_class panel_widget_class =
  {
    .name = "panel",
    .get_data_size = panel_get_data_size,
    .init_size = panel_init_size,
    .fini_size = panel_fini_size,
    .free = panel_free,
    .draw = panel_draw
  };

/* ===== image ===== */

struct image_data
{
  grub_menu_region_common_t image;
  grub_menu_region_common_t image_selected;
};

static int
image_get_data_size (void)
{
  return sizeof (struct image_data);
}

static void
image_init_size (grub_widget_t widget)
{
  struct image_data *data = widget->data;

  data->image = get_bitmap (widget, "image", 0, &data->image_selected);
  if (! data->image)
    return;

  if (data->image_selected)
    widget->node->flags |= GRUB_WIDGET_FLAG_DYNAMIC;
  else
    widget->node->flags &= ~GRUB_WIDGET_FLAG_DYNAMIC;

  if (! (widget->node->flags & GRUB_WIDGET_FLAG_FIXED_WIDTH))
    widget->width = data->image->width;

  if (! (widget->node->flags & GRUB_WIDGET_FLAG_FIXED_HEIGHT))
    widget->height = data->image->height;
}

static void
image_fini_size (grub_widget_t widget)
{
  struct image_data *data = widget->data;

  grub_menu_region_scale (data->image, widget->width, widget->height);
  grub_menu_region_scale (data->image_selected, widget->width, widget->height);
}

static void
image_free (grub_widget_t widget)
{
  struct image_data *data = widget->data;

  grub_menu_region_free (data->image);
  grub_menu_region_free (data->image_selected);
}

static void
image_draw (grub_widget_t widget, grub_menu_region_update_list_t *head,
	    int x, int y, int width, int height)
{
  struct image_data *data = widget->data;
  grub_menu_region_common_t image;

  image = ((widget->node->flags & GRUB_WIDGET_FLAG_SELECTED)
	   && data->image_selected) ? data->image_selected : data->image;

  grub_menu_region_add_update (head, image, widget->org_x, widget->org_y,
			       x, y, width, height);
}

static struct grub_widget_class image_widget_class =
  {
    .name = "image",
    .get_data_size = image_get_data_size,
    .init_size = image_init_size,
    .fini_size = image_fini_size,
    .free = image_free,
    .draw = image_draw
  };

/* ===== text ===== */

struct text_data
{
  grub_menu_region_text_t text;
  grub_video_color_t color;
  grub_video_color_t color_selected;
};

static int
text_get_data_size (void)
{
  return sizeof (struct text_data);
}

static void
text_init_size (grub_widget_t widget)
{
  struct text_data *data = widget->data;
  grub_font_t font;
  char *p;

  widget->node->flags |= GRUB_WIDGET_FLAG_TRANSPARENT;

  p = grub_widget_get_prop (widget->node, "font");
  font = grub_menu_region_get_font (p);

  p = grub_widget_get_prop (widget->node, "color");
  if (p)
    data->color = grub_menu_parse_color (p, 0, &data->color_selected, 0);

  if (data->color != data->color_selected)
    widget->node->flags |= GRUB_WIDGET_FLAG_DYNAMIC;
  else
    widget->node->flags &= ~GRUB_WIDGET_FLAG_DYNAMIC;

  p = (grub_menu_region_gfx_mode ()) ?
    grub_widget_get_prop (widget->node, "gfx_text") : 0;

  if (! p)
    p = grub_widget_get_prop (widget->node, "text");

  if (p)
    data->text = grub_menu_region_create_text (font, 0, p);
  if (! data->text)
    return;

  if (! (widget->node->flags & GRUB_WIDGET_FLAG_FIXED_WIDTH))
    widget->width = data->text->common.width;

  if (! (widget->node->flags & GRUB_WIDGET_FLAG_FIXED_HEIGHT))
    widget->height = data->text->common.height;
}

static void
text_free (grub_widget_t widget)
{
  struct text_data *data = widget->data;

  grub_menu_region_free ((grub_menu_region_common_t) data->text);
}

static void
text_draw (grub_widget_t widget, grub_menu_region_update_list_t *head,
	   int x, int y, int width, int height)
{
  struct text_data *data = widget->data;

  if (! data->text)
    return;

  data->text->color = ((widget->node->flags & GRUB_WIDGET_FLAG_SELECTED) ?
		       data->color_selected : data->color);

  if (! (widget->node->flags & GRUB_WIDGET_FLAG_SELECTED))
    grub_menu_region_hide_cursor ();

  grub_menu_region_add_update (head, (grub_menu_region_common_t) data->text,
			       widget->org_x, widget->org_y,
			       x, y, width, height);
}

static struct grub_widget_class text_widget_class =
  {
    .name = "text",
    .get_data_size = text_get_data_size,
    .init_size = text_init_size,
    .free = text_free,
    .draw = text_draw
  };

/* ===== progressbar ===== */

struct progressbar_data
{
  grub_menu_region_common_t bar;
  grub_menu_region_common_t bg_bar;
  int last_ofs;
  int cur_ofs;
};

static int
progressbar_get_data_size (void)
{
  return sizeof (struct progressbar_data);
}

static void
progressbar_init_size (grub_widget_t widget)
{
  struct progressbar_data *data = widget->data;
  grub_menu_region_common_t bar;

  data->bar = get_bitmap (widget, "image", 0, &data->bg_bar);
  bar = (data->bar) ? data->bar : data->bg_bar;
  if (bar)
    {
      if (! (widget->node->flags & GRUB_WIDGET_FLAG_FIXED_WIDTH))
	widget->width = bar->width;

      if (! (widget->node->flags & GRUB_WIDGET_FLAG_FIXED_HEIGHT))
	widget->height = bar->height;
    }
  else
    {
      grub_video_color_t color;
      grub_uint32_t fill;
      grub_video_color_t bg_color;
      grub_uint32_t bg_fill;
      char *p;

      p = grub_widget_get_prop (widget->node, "color");
      if (! p)
	p = (char *) "";

      fill = bg_fill = 0;
      color = grub_menu_parse_color (p, &fill, &bg_color, &bg_fill);

      data->bar = (grub_menu_region_common_t)
	grub_menu_region_create_rect (0, 0, color, fill);
      if (color != bg_color)
	data->bg_bar = (grub_menu_region_common_t)
	  grub_menu_region_create_rect (0, 0, bg_color, bg_fill);
    }
}

static void
progressbar_fini_size (grub_widget_t widget)
{
  struct progressbar_data *data = widget->data;

  grub_menu_region_scale (data->bar, widget->width, widget->height);
  grub_menu_region_scale (data->bg_bar, widget->width, widget->height);
}

static void
progressbar_free (grub_widget_t widget)
{
  struct progressbar_data *data = widget->data;

  grub_menu_region_free (data->bar);
  grub_menu_region_free (data->bg_bar);
}

static void
progressbar_draw (grub_widget_t widget, grub_menu_region_update_list_t *head,
		  int x, int y, int width, int height)
{
  struct progressbar_data *data = widget->data;
  int x1, y1, w1, h1;

  x1 = data->last_ofs;
  y1 = 0;
  w1 = data->cur_ofs - data->last_ofs;
  h1 = widget->height;
  if (grub_menu_region_check_rect (&x1, &y1, &w1, &h1, x, y, width, height))
    grub_menu_region_add_update (head, data->bar, widget->org_x, widget->org_y,
				 x1, y1, w1, h1);

  if ((! data->last_ofs) && (data->bg_bar))
    {
      x1 = data->cur_ofs;
      y1 = 0;
      w1 = widget->width - x1;
      h1 = widget->height;

      if (grub_menu_region_check_rect (&x1, &y1, &w1, &h1, x, y, width, height))
	grub_menu_region_add_update (head, data->bg_bar,
				     widget->org_x, widget->org_y,
				     x1, y1, w1, h1);
    }

  data->last_ofs = data->cur_ofs;
}

static void
progressbar_set_timeout (grub_widget_t widget, int total, int left)
{
  struct progressbar_data *data = widget->data;

  if (left > 0)
    data->cur_ofs = (total - left) * widget->width / total;
}

static struct grub_widget_class progressbar_widget_class =
  {
    .name = "progressbar",
    .get_data_size = progressbar_get_data_size,
    .init_size = progressbar_init_size,
    .fini_size = progressbar_fini_size,
    .free = progressbar_free,
    .draw = progressbar_draw,
    .set_timeout = progressbar_set_timeout
  };

/* ===== registration ===== */

void
grub_wartburg_ui_init (void)
{
  grub_widget_class_register (&screen_widget_class);
  grub_widget_class_register (&panel_widget_class);
  grub_widget_class_register (&image_widget_class);
  grub_widget_class_register (&text_widget_class);
  grub_widget_class_register (&progressbar_widget_class);
}

void
grub_wartburg_ui_fini (void)
{
  grub_widget_class_unregister (&progressbar_widget_class);
  grub_widget_class_unregister (&text_widget_class);
  grub_widget_class_unregister (&image_widget_class);
  grub_widget_class_unregister (&panel_widget_class);
  grub_widget_class_unregister (&screen_widget_class);
}
