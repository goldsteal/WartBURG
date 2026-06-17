/* wartburg_ui.c - BURG component widget classes, ported (layer 3).
 *
 * Port of the draw component classes from bean's BURG ui/coreui.c:
 * screen, panel, image, text, progressbar, circular_progress. The interactive
 * classes (password, edit, term -> need onkey/key handling + the gfxmenu term)
 * remain deferred.
 * grub_wartburg_ui_init/_fini register/unregister the ported classes.
 * Original copyright 2009 Bean Lee; GPLv3+.
 */

#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/trig.h>
#include <grub/wartburg_widget.h>
#include <grub/wartburg_bedrock.h>

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

/* ===== bedrock (Bedrock-aware composite stratum icon) =====
 *
 * Draws a large "base" icon (the last-booted / hijacked stratum, or the Bedrock
 * logo) filling the menu cell, with the other strata as small, individually
 * selectable badges along the bottom. Arrows move the sub-selection (and update
 * the node's boot command); `b` cycles the base. The stratum model lives in
 * wartburg_bedrock.c.  */

struct bedrock_data
{
  grub_menu_region_common_t big;			/* large_<base> (lit)   */
  grub_menu_region_common_t big_grey;			/* grey_<base>  (idle)  */
  grub_menu_region_common_t sm_color[WB_BEDROCK_MAX];	/* small_<class> */
  grub_menu_region_common_t sm_grey[WB_BEDROCK_MAX];	/* grey_<class>  */
  int order[WB_BEDROCK_MAX];	/* stratum indices, display order        */
  int norder;			/* count in order[]                      */
  int start;			/* first order[] index drawn as a small  */
  int sel;			/* selected position within order[]      */
};

static int
bedrock_get_data_size (void)
{
  return sizeof (struct bedrock_data);
}

/* Load "<dir>/<prefix>_<class>.png", falling back to "<prefix>_unknown.png". */
static grub_menu_region_common_t
bedrock_icon (const char *dir, const char *prefix, const char *class)
{
  grub_menu_region_common_t r = 0;
  char *path;

  path = grub_xasprintf ("%s/%s_%s.png", dir, prefix, class);
  if (path)
    {
      r = (grub_menu_region_common_t)
	grub_menu_region_create_bitmap (path, WB_SCALE_MINFIT, 0);
      grub_free (path);
    }
  if (! r)
    {
      path = grub_xasprintf ("%s/%s_unknown.png", dir, prefix);
      if (path)
	{
	  r = (grub_menu_region_common_t)
	    grub_menu_region_create_bitmap (path, WB_SCALE_MINFIT, 0);
	  grub_free (path);
	}
    }
  return r;
}

/* (Re)load the large base icon for the current mode + the per-stratum badges. */
static void
bedrock_load (struct bedrock_data *data)
{
  const char *dir = grub_wartburg_bedrock_icon_dir ();
  int i, n = grub_wartburg_bedrock_count ();

  if (! dir)
    return;

  grub_menu_region_free (data->big);
  grub_menu_region_free (data->big_grey);
  data->big = bedrock_icon (dir, "large", grub_wartburg_bedrock_base_class ());
  data->big_grey = bedrock_icon (dir, "grey", grub_wartburg_bedrock_base_class ());

  for (i = 0; i < n && i < WB_BEDROCK_MAX; i++)
    {
      const struct wb_stratum *st = grub_wartburg_bedrock_get (i);
      if (! st)
	continue;
      if (! data->sm_color[i])
	data->sm_color[i] = bedrock_icon (dir, "small", st->iconclass);
      if (! data->sm_grey[i])
	data->sm_grey[i] = bedrock_icon (dir, "grey", st->iconclass);
    }
}

/* Order strata for display: in stratum mode order[0] is the base (drawn large)
   and the rest are badges; with the Bedrock logo as base, all are badges. */
static void
bedrock_compute_order (struct bedrock_data *data)
{
  int n = grub_wartburg_bedrock_count ();
  int base = grub_wartburg_bedrock_base_index ();
  int i, k = 0;

  if (base < 0)			/* Bedrock-logo mode: every stratum is a badge */
    {
      data->start = 0;
      for (i = 0; i < n && k < WB_BEDROCK_MAX; i++)
	data->order[k++] = i;
    }
  else
    {
      data->start = 1;
      data->order[k++] = base;
      for (i = 0; i < n && k < WB_BEDROCK_MAX; i++)
	if (i != base)
	  data->order[k++] = i;
    }
  data->norder = k;
  if (data->sel >= data->norder)
    data->sel = data->norder - 1;
  if (data->sel < 0)
    data->sel = 0;
}

static void
bedrock_init_size (grub_widget_t widget)
{
  struct bedrock_data *data = widget->data;

  data->sel = 0;		/* order[0]: the base in stratum mode, else 1st */
  bedrock_load (data);
  bedrock_compute_order (data);

  if (! data->big)
    return;			/* no asset -> zero size -> graceful fallback */

  if (! (widget->node->flags & GRUB_WIDGET_FLAG_FIXED_WIDTH))
    widget->width = data->big->width;
  if (! (widget->node->flags & GRUB_WIDGET_FLAG_FIXED_HEIGHT))
    widget->height = data->big->height;
}

static void
bedrock_fini_size (grub_widget_t widget)
{
  struct bedrock_data *data = widget->data;
  int ss, i, n;

  grub_menu_region_scale (data->big, widget->width, widget->height);
  grub_menu_region_scale (data->big_grey, widget->width, widget->height);

  ss = (widget->height * 2) / 5;	/* badge edge ~ 40% of the cell height */
  if (ss < 1)
    ss = 1;
  n = grub_wartburg_bedrock_count ();
  for (i = 0; i < n && i < WB_BEDROCK_MAX; i++)
    {
      grub_menu_region_scale (data->sm_color[i], ss, ss);
      grub_menu_region_scale (data->sm_grey[i], ss, ss);
    }
}

static void
bedrock_free (grub_widget_t widget)
{
  struct bedrock_data *data = widget->data;
  int i;

  grub_menu_region_free (data->big);
  grub_menu_region_free (data->big_grey);
  for (i = 0; i < WB_BEDROCK_MAX; i++)
    {
      grub_menu_region_free (data->sm_color[i]);
      grub_menu_region_free (data->sm_grey[i]);
    }
}

static void
bedrock_draw (grub_widget_t widget, grub_menu_region_update_list_t *head,
	      int x, int y, int width, int height)
{
  struct bedrock_data *data = widget->data;
  int selected = (widget->node->flags & GRUB_WIDGET_FLAG_SELECTED) != 0;
  grub_menu_region_common_t bigreg;
  int ns, sw, gap, total, sx, sy, k;

  /* The base icon is lit (colour) only while this composite owns the menu
     selection; otherwise it sits greyed like an unselected entry. */
  bigreg = selected ? data->big : data->big_grey;
  if (! bigreg)
    bigreg = data->big ? data->big : data->big_grey;
  if (! bigreg)
    return;

  bigreg->ofs_x = 0;
  bigreg->ofs_y = 0;
  grub_menu_region_add_update (head, bigreg, widget->org_x, widget->org_y,
			       x, y, width, height);

  ns = data->norder - data->start;
  if (ns <= 0)
    return;

  sw = 0;			/* badge edge (first loaded badge sets it) */
  for (k = data->start; k < data->norder; k++)
    {
      grub_menu_region_common_t r = data->sm_grey[data->order[k]];
      if (r)
	{
	  sw = r->width;
	  break;
	}
    }
  if (sw <= 0)
    return;

  gap = sw / 4;
  total = ns * sw + (ns - 1) * gap;
  sx = (widget->width - total) / 2;
  if (sx < 0)
    sx = 0;
  sy = widget->height - sw;
  if (sy < 0)
    sy = 0;

  for (k = data->start; k < data->norder; k++)
    {
      int i = data->order[k];
      grub_menu_region_common_t r;

      /* Selected badge in colour, the rest greyed (only while this composite
	 owns the menu selection). */
      r = (selected && k == data->sel) ? data->sm_color[i] : data->sm_grey[i];
      if (! r)
	r = data->sm_grey[i] ? data->sm_grey[i] : data->sm_color[i];
      if (r)
	{
	  r->ofs_x = sx;
	  r->ofs_y = sy;
	  grub_menu_region_add_update (head, r, widget->org_x, widget->org_y,
				       x, y, width, height);
	}
      sx += sw + gap;
    }
}

/* Full screen repaint after a sub-selection / base change (avoids leftover
   pixels from the previous frame's badges). */
static void
bedrock_repaint (grub_widget_t widget)
{
  grub_widget_draw (grub_widget_screen ? grub_widget_screen : widget->node);
}

static int
bedrock_onkey (grub_widget_t widget, int key)
{
  struct bedrock_data *data = widget->data;

  switch (key)
    {
    case GRUB_TERM_KEY_LEFT:
    case 'h':
      if (data->sel > 0)
	{
	  data->sel--;
	  grub_wartburg_bedrock_stamp_node (widget->node,
					    data->order[data->sel]);
	  bedrock_repaint (widget);
	  return GRUB_WIDGET_RESULT_DONE;
	}
      return GRUB_WIDGET_RESULT_SKIP;	/* at the start -> leave to prev item */

    case GRUB_TERM_KEY_RIGHT:
    case 'l':
      if (data->sel < data->norder - 1)
	{
	  data->sel++;
	  grub_wartburg_bedrock_stamp_node (widget->node,
					    data->order[data->sel]);
	  bedrock_repaint (widget);
	  return GRUB_WIDGET_RESULT_DONE;
	}
      return GRUB_WIDGET_RESULT_SKIP;	/* at the end -> leave to next item */

    case 'b':				/* cycle the large base icon */
      grub_wartburg_bedrock_cycle_base ();
      data->sel = 0;
      bedrock_load (data);
      bedrock_compute_order (data);
      bedrock_fini_size (widget);
      grub_wartburg_bedrock_stamp_node (widget->node, data->order[data->sel]);
      bedrock_repaint (widget);
      return GRUB_WIDGET_RESULT_DONE;

    default:				/* Enter / up / down / esc / c / e */
      return GRUB_WIDGET_RESULT_SKIP;
    }
}

static struct grub_widget_class bedrock_widget_class =
  {
    .name = "bedrock",
    .get_data_size = bedrock_get_data_size,
    .init_size = bedrock_init_size,
    .fini_size = bedrock_fini_size,
    .free = bedrock_free,
    .draw = bedrock_draw,
    .onkey = bedrock_onkey
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

/* ===== circular_progress ===== */

struct circular_progress_data
{
  grub_menu_region_common_t back;
  grub_menu_region_common_t tick;
  grub_menu_region_common_t merge;
  int num_ticks;
  int start_angle;
  int ticks;
  int update;
  int dir;
};

static int
circular_progress_get_data_size (void)
{
  return sizeof (struct circular_progress_data);
}

#define DEF_TICKS	24
#define MAX_TICKS	100

static void
circular_progress_init_size (grub_widget_t widget)
{
  struct circular_progress_data *data = widget->data;
  char *p;

  data->tick = get_bitmap (widget, "tick", 0, 0);
  if (! data->tick)
    return;

  data->back = get_bitmap (widget, "background", 0, 0);
  p = grub_widget_get_prop (widget->node, "num_ticks");
  data->num_ticks = (p) ? grub_strtoul (p, 0, 0) : 0;
  if ((data->num_ticks <= 0) || (data->num_ticks > MAX_TICKS))
    data->num_ticks = DEF_TICKS;

  p = grub_widget_get_prop (widget->node, "start_angle");
  data->start_angle = ((p) ? grub_strtol (p, 0, 0) : 0) *
    GRUB_TRIG_ANGLE_MAX / 360;

  p = grub_widget_get_prop (widget->node, "clockwise");
  data->dir = (p) ? -1 : 1;

  if (data->back)
    {
      if (! (widget->node->flags & GRUB_WIDGET_FLAG_FIXED_WIDTH))
	widget->width = data->back->width;

      if (! (widget->node->flags & GRUB_WIDGET_FLAG_FIXED_HEIGHT))
	widget->height = data->back->height;
    }

  data->ticks = -1;
}

static void
circular_progress_fini_size (grub_widget_t widget)
{
  struct circular_progress_data *data = widget->data;

  grub_menu_region_scale (data->back, widget->width, widget->height);
}

static void
circular_progress_free (grub_widget_t widget)
{
  struct circular_progress_data *data = widget->data;

  grub_menu_region_free (data->back);
  grub_menu_region_free (data->tick);
}

static void
circular_progress_draw (grub_widget_t widget,
			grub_menu_region_update_list_t *head,
			int x, int y, int width, int height)
{
  struct circular_progress_data *data = widget->data;

  if (data->update)
    {
      if (data->update < 0)
	grub_menu_region_add_update (head, data->back, widget->org_x,
				     widget->org_y, x, y, width, height);
      grub_menu_region_add_update (head, data->tick, widget->org_x,
				   widget->org_y, x, y, width, height);
      data->update = 0;
    }
}

static void
circular_progress_set_timeout (grub_widget_t widget, int total, int left)
{
  struct circular_progress_data *data = widget->data;

  if (! data->tick)
    return;

  if (left > 0)
    {
      int ticks = ((total - left) * data->num_ticks) / total;

      if (ticks > data->ticks)
	{
	  int x, y, r, a;

	  x = (widget->width - data->tick->width) / 2;
	  y = (widget->height - data->tick->height) / 2;
	  r = (x > y) ? y : x;

	  a = data->start_angle +
	    (data->dir * ticks * GRUB_TRIG_ANGLE_MAX) / data->num_ticks;
	  x += (grub_cos (a) * r / GRUB_TRIG_FRACTION_SCALE);
	  y -= (grub_sin (a) * r / GRUB_TRIG_FRACTION_SCALE);

	  data->tick->ofs_x = x;
	  data->tick->ofs_y = y;

	  if (data->ticks >= 0)
	    data->update = 1;
	  else
	    data->update = -1;
	  data->ticks = ticks;
	}
    }
}

static struct grub_widget_class circular_progress_widget_class =
  {
    .name = "circular_progress",
    .get_data_size = circular_progress_get_data_size,
    .init_size = circular_progress_init_size,
    .fini_size = circular_progress_fini_size,
    .free = circular_progress_free,
    .draw = circular_progress_draw,
    .set_timeout = circular_progress_set_timeout
  };

/* ===== shared text-buffer helper (password / edit / term) ===== */

#define STR_INC_STEP		8
#define DEFAULT_COLUMNS		20

static grub_menu_region_text_t
resize_text (grub_menu_region_text_t text, int len)
{
  int size;

  size = (sizeof (struct grub_menu_region_text) + len + 1 + 15) & ~15;
  return grub_realloc (text, size);
}

/* ===== password ===== */

struct password_data
{
  grub_menu_region_text_t text;
  grub_video_color_t color;
  grub_video_color_t color_selected;
  char *password;
  int x;
  int pos;
  int char_width;
  int char_height;
  int modified;
};

static int
password_get_data_size (void)
{
  return sizeof (struct password_data);
}

static void
password_init_size (grub_widget_t widget)
{
  struct password_data *data = widget->data;
  grub_font_t font;
  char *p;

  widget->node->flags |= GRUB_WIDGET_FLAG_TRANSPARENT | GRUB_WIDGET_FLAG_NODE;

  p = grub_widget_get_prop (widget->node, "font");
  font = grub_menu_region_get_font (p);

  p = grub_widget_get_prop (widget->node, "color");
  if (p)
    data->color = grub_menu_parse_color (p, 0, &data->color_selected, 0);

  if (data->color != data->color_selected)
    widget->node->flags |= GRUB_WIDGET_FLAG_DYNAMIC;
  else
    widget->node->flags &= ~GRUB_WIDGET_FLAG_DYNAMIC;

  data->char_width = grub_menu_region_get_text_width (font, "*", 0, 0);
  data->char_height = grub_menu_region_get_text_height (font);

  data->text = grub_menu_region_create_text (font, 0, 0);
  if (! data->text)
    return;

  if (! (widget->node->flags & GRUB_WIDGET_FLAG_FIXED_WIDTH))
    {
      int columns;

      p = grub_widget_get_prop (widget->node, "columns");
      columns = (p) ? grub_strtoul (p, 0, 0) : DEFAULT_COLUMNS;
      widget->width = columns * data->char_width;
    }

  if (! (widget->node->flags & GRUB_WIDGET_FLAG_FIXED_HEIGHT))
    widget->height = data->char_height;
}

static void
password_free (grub_widget_t widget)
{
  struct password_data *data = widget->data;

  grub_menu_region_free ((grub_menu_region_common_t) data->text);
  grub_free (data->password);
}

static void
password_draw (grub_widget_t widget, grub_menu_region_update_list_t *head,
	       int x, int y, int width, int height)
{
  struct password_data *data = widget->data;

  data->text->color = ((widget->node->flags & GRUB_WIDGET_FLAG_SELECTED) ?
		       data->color_selected : data->color);

  grub_menu_region_add_update (head, (grub_menu_region_common_t) data->text,
			       widget->org_x, widget->org_y,
			       x, y, width, height);
}

static int
password_scroll_x (struct password_data *data, int width)
{
  int text_width;

  text_width = data->text->common.width;
  data->x = data->text->common.ofs_x + text_width;

  if ((data->x >= 0) && (data->x + data->char_width <= width))
    return 0;

  width = (width + 1) >> 1;
  if (width > text_width)
    width = text_width;

  data->x = width;
  data->text->common.ofs_x = width - text_width;
  return 1;
}

static void
password_draw_cursor (grub_widget_t widget)
{
  struct password_data *data = widget->data;

  grub_menu_region_draw_cursor (data->text, data->char_width, data->char_height,
				widget->org_x + data->x, widget->org_y);
}

static int
password_onkey (grub_widget_t widget, int key)
{
  struct password_data *data = widget->data;

  if ((key == GRUB_TERM_KEY_UP) || (key == GRUB_TERM_KEY_DOWN) ||
      (key == GRUB_TERM_KEY_LEFT) || (key == GRUB_TERM_KEY_RIGHT))
    return GRUB_WIDGET_RESULT_DONE;
  else if (key == GRUB_TERM_TAB)
    {
      if (data->modified)
	{
	  char *buf;

	  buf = grub_malloc (data->pos + 1);
	  if (! buf)
	    return grub_errno;

	  grub_memcpy (buf, data->password, data->pos);
	  buf[data->pos] = 0;
	  if (grub_uitree_set_prop (widget->node, "text", buf))
	    {
	      grub_free (buf);
	      return grub_errno;
	    }

	  grub_free (buf);
	}
      return GRUB_WIDGET_RESULT_SKIP;
    }
  else if ((key >= 32) && (key < 127))
    {
      if ((data->pos & (STR_INC_STEP - 1)) == 0)
	{
	  data->password = grub_realloc (data->password,
					 data->pos + STR_INC_STEP);
	  if (! data->password)
	    return grub_errno;
	}
      data->password[data->pos] = key;

      data->text = resize_text (data->text, data->pos + 1);
      if (! data->text)
	return grub_errno;

      data->text->text[data->pos++] = '*';
      data->text->text[data->pos] = 0;
      data->text->common.width += data->char_width;
      password_scroll_x (data, widget->width);
      grub_widget_draw (widget->node);
      data->modified = 1;

      return GRUB_WIDGET_RESULT_DONE;
    }
  else if (key == GRUB_TERM_BACKSPACE)
    {
      if (data->pos)
	{
	  data->pos--;
	  data->text->text[data->pos] = 0;
	  data->text->common.width -= data->char_width;
	  password_scroll_x (data, widget->width);
	  grub_widget_draw (widget->node);
	  data->modified = 1;
	}
      return GRUB_WIDGET_RESULT_DONE;
    }
  else
    return GRUB_WIDGET_RESULT_SKIP;
}

static struct grub_widget_class password_widget_class =
  {
    .name = "password",
    .get_data_size = password_get_data_size,
    .init_size = password_init_size,
    .free = password_free,
    .draw = password_draw,
    .draw_cursor = password_draw_cursor,
    .onkey = password_onkey,
  };

/* ===== edit (multi-line editor; the `e` key) ===== */

#define REFERENCE_STRING	"m"
#define DEFAULT_MAX_LINES	100
#define DEFAULT_LINES		1
#define LINE_INC_STEP		8

/* Emacs-style control keys (raw control codes; not in 2.15 term.h). */
#ifndef GRUB_TERM_CTRL_P
#define GRUB_TERM_CTRL_P	16
#endif
#ifndef GRUB_TERM_CTRL_N
#define GRUB_TERM_CTRL_N	14
#endif
#ifndef GRUB_TERM_CTRL_X
#define GRUB_TERM_CTRL_X	24
#endif

struct edit_data
{
  grub_menu_region_text_t *lines;
  int num_lines;
  int max_lines;
  int line;
  int pos;
  int x;
  int y;
  grub_video_color_t color;
  grub_video_color_t color_selected;
  int font_height;
  int font_width;
  int modified;
};

static int cursor_off;

static int
edit_get_data_size (void)
{
  return sizeof (struct edit_data);
}

static void
edit_init_size (grub_widget_t widget)
{
  struct edit_data *data = widget->data;
  grub_font_t font;
  int num, max;
  char *p;

  widget->node->flags |= GRUB_WIDGET_FLAG_TRANSPARENT | GRUB_WIDGET_FLAG_NODE;

  p = grub_widget_get_prop (widget->node, "font");
  font = grub_menu_region_get_font (p);

  p = grub_widget_get_prop (widget->node, "color");
  if (p)
    data->color = grub_menu_parse_color (p, 0, &data->color_selected, 0);

  if (data->color != data->color_selected)
    widget->node->flags |= GRUB_WIDGET_FLAG_DYNAMIC;
  else
    widget->node->flags &= ~GRUB_WIDGET_FLAG_DYNAMIC;

  p = grub_widget_get_prop (widget->node, "max_lines");
  data->max_lines = (p) ? grub_strtoul (p, 0, 0) : DEFAULT_MAX_LINES;

  data->font_width = grub_menu_region_get_text_width (font, REFERENCE_STRING,
						      0, 0);
  data->font_height = grub_menu_region_get_text_height (font);

  p = grub_widget_get_prop (widget->node, "text");
  if (! p)
    p = (char *) "";

  num = max = 0;
  do
    {
      char *n;

      n = grub_menu_next_field (p, '\n');
      if (num >= max)
	{
	  data->lines = grub_realloc (data->lines, (max + LINE_INC_STEP)
				      * sizeof (data->lines[0]));
	  if (! data->lines)
	    return;
	  grub_memset (data->lines + max, 0,
		       LINE_INC_STEP * sizeof (data->lines[0]));
	  max += LINE_INC_STEP;
	}
      data->lines[num] = grub_menu_region_create_text (font, 0, p);
      if (! data->lines[num])
	return;
      data->lines[num]->common.ofs_y = num * data->font_height;
      grub_menu_restore_field (n, '\n');
      num++;
      p = n;
    } while ((p) && (num != data->max_lines));
  data->num_lines = num;

  if (! (widget->node->flags & GRUB_WIDGET_FLAG_FIXED_WIDTH))
    {
      int columns;

      p = grub_widget_get_prop (widget->node, "columns");
      columns = (p) ? grub_strtoul (p, 0, 0) : DEFAULT_COLUMNS;
      widget->width = columns * data->font_width;
    }

  if (! (widget->node->flags & GRUB_WIDGET_FLAG_FIXED_HEIGHT))
    {
      int lines;

      p = grub_widget_get_prop (widget->node, "lines");
      lines = (p) ? grub_strtoul (p, 0, 0) : DEFAULT_LINES;
      widget->height = lines * data->font_height;
    }
}

static void
edit_free (grub_widget_t widget)
{
  struct edit_data *data = widget->data;
  int i;

  for (i = 0; i < data->num_lines; i++)
    grub_menu_region_free ((grub_menu_region_common_t) data->lines[i]);
  grub_free (data->lines);
}

static void
edit_draw (grub_widget_t widget, grub_menu_region_update_list_t *head,
	   int x, int y, int width, int height)
{
  struct edit_data *data = widget->data;
  grub_video_color_t color;
  grub_menu_region_text_t *p;
  int i;

  color = ((widget->node->flags & GRUB_WIDGET_FLAG_SELECTED) ?
	   data->color_selected : data->color);

  for (i = 0, p = data->lines; i < data->num_lines; i++, p++)
    if (*p)
      {
	(*p)->color = color;
	grub_menu_region_add_update (head,
				     (grub_menu_region_common_t) *p,
				     widget->org_x, widget->org_y,
				     x, y, width, height);
      }
}

static void
edit_draw_cursor (grub_widget_t widget)
{
  struct edit_data *data = widget->data;
  grub_font_t font;
  char *line;
  int cursor_width;

  if (cursor_off)
    return;

  line = data->lines[data->line]->text;
  font = data->lines[data->line]->font;

  cursor_width = (line[data->pos]) ?
    grub_menu_region_get_text_width (font, line + data->pos, 1, 0) :
    data->font_width;

  grub_menu_region_draw_cursor (data->lines[data->line], cursor_width,
				data->font_height, widget->org_x + data->x,
				widget->org_y + data->y);
}

static int
edit_scroll_x (struct edit_data *data, int text_width, int width)
{
  data->x = data->lines[data->line]->common.ofs_x + text_width;

  if ((data->x >= 0) && (data->x + data->font_width <= width))
    return 0;

  width = (width + 1) >> 1;
  if (width > text_width)
    width = text_width;

  data->x = width;
  data->lines[data->line]->common.ofs_x = width - text_width;
  return 1;
}

static void
edit_move_y (struct edit_data *data, int delta)
{
  int i;
  grub_menu_region_text_t *p;

  for (i = 0, p = data->lines; i < data->num_lines; i++, p++)
    if (*p)
      (*p)->common.ofs_y += delta;
}

static int
edit_scroll_y (struct edit_data *data, int height)
{
  int text_height, delta;

  text_height = data->font_height * data->line;
  data->y = data->lines[0]->common.ofs_y + text_height;

  if ((data->lines[0]->common.ofs_y <= 0) &&
      (data->y >= 0) && (data->y + data->font_height <= height))
    return 0;

  height = (height + 1) >> 1;
  if (height > text_height)
    height = text_height;

  delta = height - data->y;
  data->y = height;
  edit_move_y (data, delta);
  return 1;
}

static int
edit_prev_char_width (grub_font_t font, char *line, int pos, int *len)
{
  int width;
  char *p;

  width = 0;
  p = line;
  while (1)
    {
      int w, n;

      w = grub_menu_region_get_text_width (font, p, 1, &n);
      pos -= n;
      if (pos <= 0)
	break;

      p += n;
      width += w;
    }

  *len = p - line;

  return width;
}

static void
edit_draw_region (grub_uitree_t node, int x, int y, int width, int height)
{
  grub_menu_region_update_list_t head;

  head = 0;
  grub_widget_draw_region (&head, node, x, y, width, height);
  grub_menu_region_apply_update (head);
}

static int
edit_handle_key (grub_widget_t widget, int key)
{
  struct edit_data *data = widget->data;
  grub_font_t font;
  char *line;
  int update_x, update_y, update_width, update_height, text_width, scroll_y;

  line = data->lines[data->line]->text;
  font = data->lines[data->line]->font;
  update_x = data->x;
  update_y = data->y;
  update_width = (line[data->pos]) ?
    grub_menu_region_get_text_width (font, line + data->pos, 1, 0) :
    data->font_width;
  update_height = data->font_height;
  text_width = -1;
  scroll_y = 0;

  if (key == GRUB_TERM_KEY_LEFT)
    {
      if (! data->pos)
	return GRUB_WIDGET_RESULT_DONE;

      text_width = edit_prev_char_width (font, line, data->pos, &data->pos);
    }
  else if (key == GRUB_TERM_KEY_RIGHT)
    {
      int n;

      if (! line[data->pos])
	return GRUB_WIDGET_RESULT_DONE;

      text_width = data->x - data->lines[data->line]->common.ofs_x +
	grub_menu_region_get_text_width (font, line + data->pos, 1, &n);
      data->pos += n;
    }
  else if (key == GRUB_TERM_KEY_HOME)
    {
      if (! data->pos)
	return GRUB_WIDGET_RESULT_DONE;

      text_width = data->pos = 0;
    }
  else if (key == GRUB_TERM_KEY_END)
    {
      text_width = data->lines[data->line]->common.width;
      data->pos = grub_strlen (line);
    }
  else if ((key == GRUB_TERM_KEY_UP) || (key == GRUB_TERM_CTRL_P) ||
	   (key == GRUB_TERM_KEY_PPAGE))
    {
      int n;

      if (! data->line)
	return GRUB_WIDGET_RESULT_DONE;

      n = (key == GRUB_TERM_KEY_PPAGE) ? widget->height / data->font_height : 1;
      if (n > data->line)
	n = data->line;

      if (! n)
	return GRUB_WIDGET_RESULT_DONE;

      if ((data->line + 1 == data->num_lines) && (! *line))
	{
	  grub_menu_region_free ((grub_menu_region_common_t)
				 data->lines[data->line]);
	  data->lines[data->line] = 0;
	  data->num_lines--;
	}

      data->line -= n;
      scroll_y = 1;
    }
  else if ((key == GRUB_TERM_KEY_DOWN) || (key == GRUB_TERM_CTRL_N))
    {
      if (data->max_lines == 1)
	return GRUB_WIDGET_RESULT_DONE;

      data->line++;
      data->y += data->font_height;

      if (data->line >= data->num_lines)
	{
	  data->num_lines++;
	  if ((data->num_lines & (LINE_INC_STEP - 1)) == 0)
	    {
	      data->lines =
		grub_realloc (data->lines,
			      (data->num_lines + LINE_INC_STEP) *
			      sizeof (void *));
	      if (! data->lines)
		return grub_errno;

	      grub_memset (data->lines + data->num_lines, 0,
			   LINE_INC_STEP * sizeof (void *));
	    }
	}

      if (! data->lines[data->line])
	{
	  data->lines[data->line] = grub_menu_region_create_text (font, 0, 0);
	  if (! data->lines[data->line])
	    return grub_errno;

	  data->lines[data->line]->common.ofs_y = data->y;
	}

      scroll_y = 1;
    }
  else if (key == '\r' || key == '\n')
    {
      int i;

      if (data->max_lines == 1)
	return GRUB_WIDGET_RESULT_DONE;

      data->line++;
      data->num_lines++;
      if ((data->num_lines & (LINE_INC_STEP - 1)) == 0)
	{
	  data->lines = grub_realloc (data->lines,
				      (data->num_lines + LINE_INC_STEP) *
				      sizeof (void *));
	  if (! data->lines)
	    return grub_errno;

	  grub_memset (data->lines + data->num_lines, 0,
		       LINE_INC_STEP * sizeof (void *));
	}

      for (i = data->num_lines - 1; i > data->line; i--)
	{
	  data->lines[i] = data->lines[i - 1];
	  data->lines[i]->common.ofs_y += data->font_height;
	}

      data->y += data->font_height;
      data->lines[data->line] =
	grub_menu_region_create_text (font, 0, line + data->pos);
      data->lines[data->line]->common.ofs_y = data->y;

      line[data->pos] = 0;
      data->lines[data->line - 1]->common.width =
	grub_menu_region_get_text_width (font, line, 0, 0);
      data->lines[data->line - 1]->common.ofs_x = 0;
      data->x = 0;
      data->pos = 0;

      update_x = 0;
      update_width = widget->width;
      update_height = (data->num_lines - data->line + 1) * data->font_height;

      scroll_y = 1;
      data->modified = 1;
    }
  else if (key == GRUB_TERM_KEY_NPAGE)
    {
      int n;

      n = widget->height / data->font_height;
      if (data->line + n >= data->num_lines)
	n = data->num_lines - 1 - data->line;

      if (! n)
	return GRUB_WIDGET_RESULT_DONE;

      data->line += n;
      scroll_y = 1;
    }
  else if ((key >= 32) && (key < 127))
    {
      int len, i;

      len = grub_strlen (line);
      data->lines[data->line] = resize_text (data->lines[data->line], len + 1);
      if (! data->lines[data->line])
	return grub_errno;

      line = data->lines[data->line]->text;

      for (i = len - 1; i >= data->pos; i--)
	line[i + 1] = line[i];
      line[len + 1] = 0;
      line[data->pos] = key;
      data->lines[data->line]->common.width =
	grub_menu_region_get_text_width (font, line, 0, 0);
      text_width = data->x - data->lines[data->line]->common.ofs_x;
      update_width = data->lines[data->line]->common.width - text_width;
      text_width +=
	grub_menu_region_get_text_width (font, line + data->pos, 1, 0);
      data->pos++;
      data->modified = 1;
    }
  else if (key == GRUB_TERM_BACKSPACE)
    {
      int n, delta;
      char *p;

      if (! data->pos)
	{
	  int i, len;

	  if (! data->line)
	    return GRUB_WIDGET_RESULT_DONE;

	  data->line--;
	  data->pos = grub_strlen (data->lines[data->line]->text);
	  len = grub_strlen (line);
	  if (len)
	    {
	      data->lines[data->line] = resize_text (data->lines[data->line],
						     data->pos + len);
	      if (! data->lines[data->line])
		return grub_errno;

	      grub_strcpy (data->lines[data->line]->text + data->pos,
			   line);
	    }
	  grub_menu_region_free ((grub_menu_region_common_t)
				 data->lines[data->line + 1]);
	  for (i = data->line + 1; i < data->num_lines - 1; i++)
	    {
	      data->lines[i] = data->lines[i + 1];
	      data->lines[i]->common.ofs_y -= data->font_height;
	    }
	  data->num_lines--;
	  data->lines[data->num_lines] = 0;

	  data->x = data->lines[data->line]->common.width;
	  data->lines[data->line]->common.width =
	    grub_menu_region_get_text_width (font,
					     data->lines[data->line]->text,
					     0, 0);
	  data->y -= data->font_height;

	  edit_scroll_x (data, data->x, widget->width);
	  update_x = 0;
	  update_width = widget->width;
	  update_y = data->y;
	  update_height = (data->num_lines - data->line + 1) *
	    data->font_height;
	}
      else
	{
	  text_width = edit_prev_char_width (font, line, data->pos, &n);
	  update_width = data->lines[data->line]->common.width -
	    text_width;
	  if (! line[data->pos])
	    update_width += data->font_width;

	  delta = data->pos - n;
	  for (p = line + data->pos; ; p++)
	    {
	      *(p - delta) = *p;
	      if (! *p)
		break;
	    }
	  data->pos = n;
	  data->lines[data->line]->common.width =
	    grub_menu_region_get_text_width (font, line, 0, 0);
	  update_x = data->lines[data->line]->common.ofs_x + text_width;
	}
      data->modified = 1;
    }
  else if ((key == GRUB_TERM_CTRL_X) || (key == GRUB_TERM_TAB))
    {
      if (data->modified)
	{
	  int i, len;
	  char *buf, *p;

	  len = 0;
	  for (i = 0; i < data->num_lines; i++)
	    len += grub_strlen (data->lines[i]->text) + 1;

	  buf = grub_malloc (len);
	  if (! buf)
	    return grub_errno;

	  p = buf;
	  for (i = 0; i < data->num_lines; i++)
	    {
	      grub_strcpy (p, data->lines[i]->text);
	      p += grub_strlen (p);
	      *(p++) = '\n';
	    }
	  *(p - 1) = 0;
	  if (grub_uitree_set_prop (widget->node, "text", buf))
	    {
	      grub_free (buf);
	      return grub_errno;
	    }

	  grub_free (buf);
	  data->modified = 0;
	}

      return (key == GRUB_TERM_TAB) ? GRUB_WIDGET_RESULT_SKIP : 0;
    }
  else
    return GRUB_WIDGET_RESULT_SKIP;

  if ((text_width >= 0) && (edit_scroll_x (data, text_width, widget->width)))
    {
      update_x = 0;
      update_width = widget->width;
    }

  if (scroll_y)
    {
      if ((data->max_lines) && (data->line >= data->max_lines))
	{
	  int i, n;

	  n = widget->height / (2 * data->font_height);
	  if (n < 1)
	    n = 1;
	  else if (n > data->line)
	    n = data->line;

	  for (i = 0; i < n; i++)
	    grub_menu_region_free ((grub_menu_region_common_t) data->lines[i]);

	  for (i = 0; i <= data->line - n; i++)
	    data->lines[i] = data->lines[i + n];

	  for (; i <= data->line; i++)
	    data->lines[i] = 0;

	  data->line -= n;
	  data->num_lines -= n;
	}

      data->x = 0;
      data->pos = 0;
      if (edit_scroll_y (data, widget->height))
	{
	  data->x = data->lines[data->line]->common.ofs_x = 0;
	  update_x = 0;
	  update_y = 0;
	  update_width = widget->width;
	  update_height = widget->height;
	}
      else if (edit_scroll_x (data, 0, widget->width))
	{
	  edit_draw_region (widget->node, update_x, update_y,
			    update_width, update_height);
	  update_x = 0;
	  update_y = data->y;
	  update_width = widget->width;
	  update_height = data->font_height;
	}
    }

  edit_draw_region (widget->node, update_x, update_y, update_width,
		    update_height);

  return GRUB_WIDGET_RESULT_DONE;
}

static int
edit_onkey (grub_widget_t widget, int key)
{
  return edit_handle_key (widget, key);
}

static struct grub_widget_class edit_widget_class =
  {
    .name = "edit",
    .get_data_size = edit_get_data_size,
    .init_size = edit_init_size,
    .free = edit_free,
    .draw = edit_draw,
    .draw_cursor = edit_draw_cursor,
    .onkey = edit_onkey,
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
  grub_widget_class_register (&circular_progress_widget_class);
  grub_widget_class_register (&password_widget_class);
  grub_widget_class_register (&edit_widget_class);
  grub_widget_class_register (&bedrock_widget_class);
}

void
grub_wartburg_ui_fini (void)
{
  grub_widget_class_unregister (&bedrock_widget_class);
  grub_widget_class_unregister (&edit_widget_class);
  grub_widget_class_unregister (&password_widget_class);
  grub_widget_class_unregister (&circular_progress_widget_class);
  grub_widget_class_unregister (&progressbar_widget_class);
  grub_widget_class_unregister (&text_widget_class);
  grub_widget_class_unregister (&image_widget_class);
  grub_widget_class_unregister (&panel_widget_class);
  grub_widget_class_unregister (&screen_widget_class);
}
