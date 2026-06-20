/* wartburg_widget.c - BURG widget layout + draw, ported (layer 2).
 *
 * Port of the layout/draw half of bean's BURG menu/ext/widget.c: build widgets
 * from the parsed uitree, compute geometry (size/attach/align/direction/extend),
 * and draw the tree through the menu_region backend. The input/selection/timeout
 * half (grub_widget_input, check_timeout, onkey, scrolling, select_node) is
 * deferred to M3 (it needs the parser/loader/auth path). GRUB_EXPORT dropped.
 * Original copyright 2009 Bean Lee; GPLv3+.
 */

#include <grub/mm.h>
#include <grub/env.h>
#include <grub/err.h>
#include <grub/misc.h>
#include <grub/video.h>
#include <grub/wartburg_widget.h>

grub_widget_class_t grub_widget_class_list;
grub_uitree_t grub_widget_current_node;
int grub_widget_refresh;
grub_uitree_t grub_widget_screen;

/* Next node in pre-order traversal that is NOT a descendant of `n` (i.e. skip
   n's whole subtree). Mirrors grub_tree_next_node's non-descend branch. */
static grub_uitree_t
next_skip_subtree (grub_uitree_t root, grub_uitree_t n)
{
  while (n != root)
    {
      if (n->next)
	return n->next;
      n = n->parent;
    }
  return 0;
}

grub_err_t
grub_widget_create (grub_uitree_t node)
{
  grub_uitree_t child;

  child = node;
  while (child)
    {
      grub_widget_class_t class;
      grub_widget_t widget;
      int size;

      class = grub_widget_class_list;

      while (class)
	{
	  if (! grub_strcmp (child->name, class->name))
	    break;

	  class = class->next;
	}

      if (! class)
	{
	  grub_uitree_t next;

	  /* A theme node with no matching widget class. Don't abort the whole
	     render (that drops the menu to the text fallback) -- prune just
	     this node + its subtree and keep building the rest of the theme.
	     The root is always a registered class (`screen`), so it is never
	     pruned here. */
	  if (child == node)
	    return grub_error (GRUB_ERR_BAD_ARGUMENT, "class not found");

	  next = next_skip_subtree (node, child);
	  grub_tree_remove_node (GRUB_AS_TREE (child));
	  grub_uitree_free (child);
	  grub_errno = GRUB_ERR_NONE;
	  child = next;
	  continue;
	}

      size = (class->get_data_size) ? class->get_data_size () : 0;
      widget = grub_zalloc (sizeof (struct grub_widget) + size);
      if (! widget)
	break;

      widget->class = class;
      widget->data = (char *) widget + sizeof (struct grub_widget);
      widget->node = child;
      child->data = widget;

      child = grub_tree_next_node (GRUB_AS_TREE (node), GRUB_AS_TREE (child));
    }

  return grub_errno;
}

static void
init_size (grub_widget_t widget, int size_only)
{
  grub_widget_t parent;
  grub_uitree_t node;
  int pw, ph;
  char *p, *m1, *m2;

  node = widget->node;
  if ((! node->parent) || (! node->parent->data))
    return;

  parent = widget->node->parent->data;
  pw = parent->inner_width;
  ph = parent->inner_height;

  p = grub_widget_get_prop (node, "width");
  if (p)
    {
      node->flags |= GRUB_WIDGET_FLAG_FIXED_WIDTH;
      widget->width = grub_menu_parse_size (p, pw, 1);
    }

  p = grub_widget_get_prop (node, "height");
  if (p)
    {
      node->flags |= GRUB_WIDGET_FLAG_FIXED_HEIGHT;
      widget->height = grub_menu_parse_size (p, ph, 0);
    }

  m1 = grub_widget_get_prop (node, "attach_left");
  m2 = grub_widget_get_prop (node, "attach_right");
  if ((m1) && (m2))
    {
      node->flags |= GRUB_WIDGET_FLAG_FIXED_WIDTH;
      widget->width = pw - grub_menu_parse_size (m1, pw, 1)
	- grub_menu_parse_size (m2, pw, 1);
    }

  m1 = grub_widget_get_prop (node, "attach_top");
  m2 = grub_widget_get_prop (node, "attach_bottom");
  if ((m1) && (m2))
    {
      node->flags |= GRUB_WIDGET_FLAG_FIXED_HEIGHT;
      widget->height = ph - grub_menu_parse_size (m1, ph, 0)
	- grub_menu_parse_size (m2, ph, 0);
    }

  if (size_only)
    return;

  p = grub_widget_get_prop (node, "attach_left");
  if (p)
    {
      node->flags |= GRUB_WIDGET_FLAG_FIXED_X;
      widget->x = grub_menu_parse_size (p, pw, 1);
    }

  p = grub_widget_get_prop (node, "attach_right");
  if (p)
    {
      node->flags |= GRUB_WIDGET_FLAG_FIXED_X;
      widget->x = pw - grub_menu_parse_size (p, pw, 1) - widget->width;
    }

  p = grub_widget_get_prop (node, "attach_hcenter");
  if (p)
    {
      node->flags |= GRUB_WIDGET_FLAG_FIXED_X;
      widget->x = (pw - widget->width) / 2 + grub_menu_parse_size (p, pw, 1);
    }

  p = grub_widget_get_prop (node, "attach_top");
  if (p)
    {
      node->flags |= GRUB_WIDGET_FLAG_FIXED_Y;
      widget->y = grub_menu_parse_size (p, ph, 0);
    }

  p = grub_widget_get_prop (node, "attach_bottom");
  if (p)
    {
      node->flags |= GRUB_WIDGET_FLAG_FIXED_Y;
      widget->y = ph - grub_menu_parse_size (p, ph, 0) - widget->height;
    }

  p = grub_widget_get_prop (node, "attach_vcenter");
  if (p)
    {
      node->flags |= GRUB_WIDGET_FLAG_FIXED_Y;
      widget->y = (ph - widget->height) / 2 + grub_menu_parse_size (p, ph, 0);
    }
}

#define HALIGN_LEFT 0
#define HALIGN_CENTER 1
#define HALIGN_RIGHT 2
#define HALIGN_EXTEND 3

#define VALIGN_TOP 0
#define VALIGN_CENTER 1
#define VALIGN_BOTTOM 2
#define VALIGN_EXTEND 3

static void
align_x (grub_widget_t child, int halign, int width)
{
  int delta;

  delta = width - child->width;
  if (delta <= 0)
    return;

  if (halign == HALIGN_EXTEND)
    child->width += delta;
  else if (halign == HALIGN_CENTER)
    child->x += delta >> 1;
  else if (halign == HALIGN_RIGHT)
    child->x += delta;
}

static void
align_y (grub_widget_t child, int valign, int height)
{
  int delta;

  delta = height - child->height;
  if (delta <= 0)
    return;

  if (valign == VALIGN_EXTEND)
    child->height += delta;
  else if (valign == VALIGN_CENTER)
    child->y += delta >> 1;
  else if (valign == VALIGN_BOTTOM)
    child->y += delta;
}

void
grub_widget_get_direction (grub_uitree_t node, int *horizontal, int *reverse)
{
  char *p;

  *horizontal = 0;
  *reverse = 0;
  p = grub_widget_get_prop (node, "direction");
  if (p)
    {
      if (! grub_strcmp (p, "left_to_right"))
	*horizontal = 1;
      else if (! grub_strcmp (p, "right_to_left"))
	*reverse = *horizontal = 1;
      else if (! grub_strcmp (p, "bottom_to_top"))
	*reverse = 1;
    }
}

static int screen_width, screen_height;

static void
adjust_layout (grub_widget_t widget, int calc_mode)
{
  grub_uitree_t node;
  int space, extra, count, width, height, max_items, index, max_width, max_height;
  int horizontal, reverse;
  char *p;

  node = widget->node;

  p = grub_widget_get_prop (node, "space");
  space = (p) ? grub_menu_parse_size (p, 0, 1) : 0;

  p = grub_widget_get_prop (node, "max_items");
  max_items = (p) ? grub_strtoul (p, 0, 0) : 0;

  grub_widget_get_direction (node, &horizontal, &reverse);

  width = 0;
  height = 0;
  count = 0;
  index = 0;
  max_width = screen_width;
  max_height = screen_height;
  for (node = node->child; node; node = node->next)
    {
      grub_widget_t child;
      int skip;

      child = node->data;
      if (! child)
	continue;

      if (calc_mode)
	skip = (horizontal) ? (child->width <= 0) : (child->height <= 0);
      else
	{
	  init_size (child, 0);
	  skip = ((child->width <= 0) || (child->height <= 0) ||
		  (node->flags & GRUB_WIDGET_FLAG_FIXED_XY));
	}

      if (skip)
	continue;

      if (horizontal)
	{
	  width += child->width + space;
	  if (child->height > height)
	    height = child->height;
	}
      else
	{
	  height += child->height + space;
	  if (child->width > width)
	    width = child->width;
	}

      if (grub_widget_get_prop (node, "extend"))
	{
	  node->flags |= GRUB_WIDGET_FLAG_EXTEND;
	  count++;
	}

      index++;
      if (index == max_items)
	{
	  if (horizontal)
	    {
	      if (width - space < max_width)
		max_width = width - space;
	    }
	  else
	    {
	      if (height - space < max_height)
		max_height = height - space;
	    }
	}
    }

  if (horizontal)
    {
      if (width > 0)
	width -= space;
    }
  else
    {
      if (height > 0)
	height -= space;
    }

  node = widget->node;
  if (calc_mode)
    {
      init_size (widget, 1);

      if (! (node->flags & GRUB_WIDGET_FLAG_FIXED_WIDTH))
	{
	  p = grub_widget_get_prop (node, "max_width");
	  if (p)
	    {
	      int w;

	      w = grub_menu_parse_size (p, 0, 1);
	      if (width > w)
		width = w;
	    }

	  if (width > max_width)
	    width = max_width;

	  p = grub_widget_get_prop (node, "min_width");
	  if (p)
	    {
	      int w;

	      w = grub_menu_parse_size (p, 0, 1);
	      if (width < w)
		width = w;
	    }

	  widget->width = width;
	}

      if (! (node->flags & GRUB_WIDGET_FLAG_FIXED_HEIGHT))
	{
	  p = grub_widget_get_prop (node, "max_height");
	  if (p)
	    {
	      int h;

	      h = grub_menu_parse_size (p, 0, 0);
	      if (height > h)
		height = h;
	    }

	  if (height > max_height)
	    height = max_height;

	  p = grub_widget_get_prop (node, "min_height");
	  if (p)
	    {
	      int h;

	      h = grub_menu_parse_size (p, 0, 0);
	      if (height < h)
		height = h;
	    }

	  widget->height = height;
	}

      return;
    }

  if (horizontal)
    {
      extra = widget->inner_width - width;
      if (reverse)
	{
	  if ((extra > 0) && (count > 0))
	    width = widget->inner_width;
	}
      else
	width = 0;
      height = 0;
    }
  else
    {
      extra = widget->inner_height - height;
      if (reverse)
	{
	  if ((extra > 0) && (count > 0))
	    height = widget->inner_height;
	}
      else
	height = 0;
      width = 0;
    }

  if (extra < 0)
    extra = 0;

  if (count > 1)
    extra = extra / count;

  for (node = node->child; node; node = node->next)
    {
      grub_widget_t child;
      int flags, valign, halign;

      child = node->data;
      flags = (node->flags & GRUB_WIDGET_FLAG_FIXED_XY);
      if ((! child) || (child->width <= 0) || (child->height <= 0) ||
	  (flags == GRUB_WIDGET_FLAG_FIXED_XY))
	continue;

      halign = HALIGN_EXTEND;
      p = grub_widget_get_prop (node, "halign");
      if (p)
	{
	  if (! grub_strcmp (p, "left"))
	    halign = HALIGN_LEFT;
	  else if (! grub_strcmp (p, "center"))
	    halign = HALIGN_CENTER;
	  else if (! grub_strcmp (p, "right"))
	    halign = HALIGN_RIGHT;
	}

      valign = VALIGN_EXTEND;
      p = grub_widget_get_prop (node, "valign");
      if (p)
	{
	  if (! grub_strcmp (p, "top"))
	    valign = VALIGN_TOP;
	  else if (! grub_strcmp (p, "center"))
	    valign = VALIGN_CENTER;
	  else if (! grub_strcmp (p, "bottom"))
	    valign = VALIGN_BOTTOM;
	}

      if (flags == GRUB_WIDGET_FLAG_FIXED_X)
	align_y (child, valign, widget->inner_height);
      else if (flags == GRUB_WIDGET_FLAG_FIXED_Y)
	align_x (child, halign, widget->inner_width);
      else
	{
	  if (horizontal)
	    {
	      int w;

	      w = child->width;
	      if (node->flags & GRUB_WIDGET_FLAG_EXTEND)
		w += extra;

	      if (reverse)
		{
		  child->x = width - w;
		  width -= w + space;
		}
	      else
		{
		  child->x = width;
		  width += w + space;
		}

	      if (node->flags & GRUB_WIDGET_FLAG_EXTEND)
		align_x (child, halign, w);
	      align_y (child, valign, widget->inner_height);
	    }
	  else
	    {
	      int h;

	      h = child->height;
	      if (node->flags & GRUB_WIDGET_FLAG_EXTEND)
		h += extra;

	      if (reverse)
		{
		  child->y = height - h;
		  height -= h + space;
		}
	      else
		{
		  child->y = height;
		  height += h + space;
		}

	      if (node->flags & GRUB_WIDGET_FLAG_EXTEND)
		align_y (child, valign, h);
	      align_x (child, halign, widget->inner_width);
	    }
	}
    }
}

static void
init_widget (grub_uitree_t node)
{
  grub_uitree_t child;
  grub_widget_t widget;

  child = node->child;
  while (child)
    {
      init_widget (child);
      child = child->next;
    }

  widget = node->data;
  adjust_layout (widget, 1);
  if (widget->class->init_size)
    widget->class->init_size (widget);
}

void
grub_widget_init (grub_uitree_t node)
{
  grub_uitree_t child;

  grub_menu_region_get_screen_size (&screen_width, &screen_height);

  init_widget (node);

  init_size (node->data, 0);
  child = node;
  while (child)
    {
      grub_widget_t widget;

      widget = child->data;
      widget->inner_width = widget->width;
      widget->inner_height = widget->height;

      if (child->parent)
	{
	  grub_widget_t parent = child->parent->data;
	  widget->org_x = parent->org_x + parent->inner_x + widget->x;
	  widget->org_y = parent->org_y + parent->inner_y + widget->y;
	}

      if (widget->class->fini_size)
	widget->class->fini_size (widget);
      adjust_layout (widget, 0);

      if ((widget->width > 0) && (widget->height > 0))
	{
	  if (grub_widget_get_prop (child, "anchor"))
	    child->flags |= GRUB_WIDGET_FLAG_ANCHOR;

	  if (grub_widget_get_prop (child, "command"))
	    child->flags |= GRUB_WIDGET_FLAG_NODE;
	}

      child = grub_tree_next_node (GRUB_AS_TREE (node), GRUB_AS_TREE (child));
    }
}

void
grub_widget_free (grub_uitree_t node)
{
  grub_uitree_t child;

  child = node;
  while (child)
    {
      grub_widget_t widget;

      widget = child->data;
      if (widget)
	{
	  if (widget->class->free)
	    widget->class->free (widget);

	  grub_free (widget);
	  child->data = 0;
	}

      child = grub_tree_next_node (GRUB_AS_TREE (node), GRUB_AS_TREE (child));
    }
}

static void
draw_child (grub_menu_region_update_list_t *head, grub_uitree_t node,
	    int x, int y, int width, int height)
{
  grub_widget_t widget;
  grub_uitree_t child;

  widget = node->data;
  for (child = node->child; child; child = child->next)
    {
      grub_widget_t c;
      int cx, cy, cw, ch;

      if (child->flags & GRUB_WIDGET_FLAG_HIDDEN)
	continue;

      c = child->data;
      if (! c)			/* node with no widget (e.g. pruned class) */
	continue;
      cx = widget->org_x + x - c->org_x;
      cy = widget->org_y + y - c->org_y;
      cw = width;
      ch = height;

      if (grub_menu_region_check_rect (&cx, &cy, &cw, &ch, 0, 0,
				       c->width, c->height))
	{
	  if (c->class->draw)
	    c->class->draw (c, head, cx, cy, cw, ch);

	  if (grub_menu_region_check_rect (&cx, &cy, &cw, &ch,
					   c->inner_x, c->inner_y,
					   c->inner_width, c->inner_height))
	    draw_child (head, child, cx, cy, cw, ch);
	}
    }
}

static void
draw_parent (grub_menu_region_update_list_t *head, grub_uitree_t node,
	     int x, int y, int width, int height)
{
  grub_widget_t widget;

  widget = node->data;
  if (node->flags & GRUB_WIDGET_FLAG_TRANSPARENT)
    {
      grub_widget_t p;

      p = node->parent->data;
      draw_parent (head, node->parent, x + widget->x + p->inner_x,
		   y + widget->y + p->inner_y, width, height);
    }

  if (widget->class->draw)
    widget->class->draw (widget, head, x, y, width, height);
}

void
grub_widget_draw_region (grub_menu_region_update_list_t *head,
			 grub_uitree_t node, int x, int y,
			 int width, int height)
{
  grub_widget_t w;
  grub_uitree_t c;

  if (node->flags & GRUB_WIDGET_FLAG_HIDDEN)
    return;

  w = node->data;
  if (! grub_menu_region_check_rect (&x, &y, &width, &height,
				     0, 0, w->width, w->height))
    return;

  c = node;
  while (1)
    {
      grub_widget_t p;

      if ((! c->parent) || (! c->parent->data))
	break;

      p = c->parent->data;
      x += ((grub_widget_t) c->data)->x;
      y += ((grub_widget_t) c->data)->y;
      if (! grub_menu_region_check_rect (&x, &y, &width, &height,
					 0, 0,
					 p->inner_width, p->inner_height))
	return;

      x += p->inner_x;
      y += p->inner_y;
      c = c->parent;
    }

  x += ((grub_widget_t) c->data)->org_x - w->org_x;
  y += ((grub_widget_t) c->data)->org_y - w->org_y;

  draw_parent (head, node, x, y, width, height);
  if (! grub_menu_region_check_rect (&x, &y, &width, &height,
				     w->inner_x, w->inner_y,
				     w->inner_width, w->inner_height))
    return;
  draw_child (head, node, x, y, width, height);
}

void
grub_widget_draw (grub_uitree_t node)
{
  grub_widget_t widget;

  widget = node->data;
  if (widget)
    {
      grub_menu_region_update_list_t head;
      int pass, passes;

      /* grub_widget_draw repaints the whole tree. Draw it, swap to present,
	 and -- when the mode is software double-buffered -- repaint into the
	 now-active second buffer so both buffers match (mirrors gfxmenu's
	 double_repaint; without it the first frame stays on the hidden
	 buffer and the screen reads blank until the next redraw).  */
      passes = grub_wb_double_repaint ? 2 : 1;
      for (pass = 0; pass < passes; pass++)
	{
	  head = 0;
	  grub_widget_draw_region (&head, node, 0, 0, widget->width,
				   widget->height);
	  grub_menu_region_apply_update (head);
	  if (pass == 0)
	    grub_video_swap_buffers ();
	}
    }
}

/* ----- scrolling (ported from BURG widget.c) -----
   Bring a (newly selected) node into view inside its scrollable ancestors:
   walk up the parent chain and, at each level where the node overflows the
   parent's inner area, shift the parent's non-fixed children to reveal it.  */

static void
update_position (grub_uitree_t parent, grub_uitree_t node)
{
  grub_widget_t p, w;

  p = parent->data;
  w = node->data;
  if ((p) && (w))
    {
      grub_uitree_t child;

      w->org_x = p->org_x + p->inner_x + w->x;
      w->org_y = p->org_y + p->inner_y + w->y;
      for (child = node->child; child; child = child->next)
	update_position (node, child);
    }
}

static void
scroll_node (grub_uitree_t node, int dx, int dy)
{
  grub_uitree_t child;

  for (child = node->child; child; child = child->next)
    {
      grub_widget_t widget;

      widget = child->data;
      if ((! widget) || (child->flags & GRUB_WIDGET_FLAG_FIXED_XY))
	continue;

      widget->x += dx;
      widget->y += dy;
      update_position (node, child);
    }
}

grub_uitree_t
grub_widget_scroll (grub_uitree_t node)
{
  grub_uitree_t save;
  grub_widget_t widget;
  int x, y, width, height;

  save = node;
  widget = node->data;
  if (! widget)
    return node;
  x = 0;
  y = 0;
  width = widget->width;
  height = widget->height;
  while (1)
    {
      grub_widget_t parent;
      int dx;
      int dy;

      if (! node->parent)
	break;

      parent = node->parent->data;
      if (! parent)
	break;

      if (widget->width <= parent->inner_width)
	{
	  x = 0;
	  width = widget->width;
	}

      if (widget->height <= parent->inner_height)
	{
	  y = 0;
	  height = widget->height;
	}

      x += widget->x;
      y += widget->y;

      dx = 0;
      dy = 0;
      if (x + width > parent->inner_width)
	{
	  dx = parent->inner_width - width - x;
	  x += dx;
	}

      if (y + height > parent->inner_height)
	{
	  dy = parent->inner_height - height - y;
	  y += dy;
	}

      if (x < 0)
	{
	  dx += -x;
	  x = 0;
	}

      if (y < 0)
	{
	  dy += -y;
	  y = 0;
	}

      if ((dx) || (dy))
	{
	  save = node->parent;
	  if (node->flags & GRUB_WIDGET_FLAG_FIXED_XY)
	    {
	      widget->x += dx;
	      widget->y += dy;
	      update_position (save, node);
	    }
	  else
	    scroll_node (save, dx, dy);
	}

      if (! grub_menu_region_check_rect (&x, &y, &width, &height,
					 0, 0,
					 parent->inner_width,
					 parent->inner_height))
	return node;

      x += parent->inner_x;
      y += parent->inner_y;

      node = node->parent;
      widget = node->data;
    }

  return save;
}

static grub_uitree_t
find_child (grub_uitree_t node, const char *name)
{
  grub_uitree_t child;

  if (! *name)
    return 0;

  child = node->child;
  while (child)
    {
      if (! grub_strcmp (child->name, name))
	break;

      child = child->next;
    }

  return child;
}

char *
grub_widget_get_prop (grub_uitree_t node, const char *name)
{
  grub_uitree_t class_node;
  grub_uitree_t child;
  char *prop, *class;

  prop = grub_uitree_get_prop (node, name);
  if (prop)
    return prop;

  class_node = grub_uitree_find (&grub_uitree_root, "class");
  if (! class_node)
    return 0;

  class = grub_uitree_get_prop (node, "class");
  while (class)
    {
      char *p;

      p = grub_menu_next_field (class, ',');
      child = find_child (class_node, class);
      grub_menu_restore_field (p, ',');
      if (child)
	{
	  prop = grub_uitree_get_prop (child, name);
	  if (prop)
	    return prop;
	}
      class = p;
    }

  prop = 0;
  child = find_child (class_node, node->name);
  if (child)
    prop = grub_uitree_get_prop (child, name);

  return prop;
}
