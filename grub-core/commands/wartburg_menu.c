/* wartburg_menu.c - BURG dialog template/parameter machinery + menu population.
 *
 * Ports the template-clone + "parameters" mapping helpers from bean's BURG
 * menu/ext/dialog.c (grub_dialog_create / set_parm / get_parm + find_prop /
 * find_parm), a build_menuitem-style helper, and the dialog runner
 * (grub_dialog_popup/free/message) used for submenus and message popups.
 * grub_dialog_popup runs a nested grub_widget_input over a dialog subtree.
 * Original copyright 2009 Bean Lee; GPLv3+.
 */

#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/wartburg_widget.h>

grub_uitree_t
grub_dialog_create (const char *name, int copy, int index,
		    grub_uitree_t *menu, grub_uitree_t *save)
{
  grub_uitree_t node;
  grub_uitree_t parent;

  parent = grub_uitree_find (&grub_uitree_root, name);
  if (! parent)
    return 0;

  node = parent->child;
  while ((index) && (node))
    {
      index--;
      node = node->next;
    }

  if (! node)
    return 0;

  if (copy)
    node = grub_uitree_clone (node);
  else
    {
      *menu = parent;
      *save = parent->child;
      parent->child = node->next;
    }

  return node;
}

static grub_uitree_t
find_prop (grub_uitree_t node, char *path, char **out)
{
  while (1)
    {
      char *n;
      grub_uitree_t child;

      n = grub_menu_next_field (path, '.');
      if (! n)
	{
	  *out = path;
	  return node;
	}

      child = grub_uitree_find (node, path);
      node = (child) ? child : grub_uitree_find_id (node, path);
      grub_menu_restore_field (n, '.');
      if (! node)
	return 0;
      path = n;
    }
}

static grub_uitree_t
find_parm (grub_uitree_t node, char *parm, char *name,
	   char **out, char **next)
{
  *next = 0;
  *out = name;

  if (grub_strchr (name, '.'))
    node = find_prop (node, name, out);
  else if (parm)
    do
      {
	char *n, *v;

	n = grub_menu_next_field (parm, ':');
	v = grub_menu_next_field (parm, '=');
	if ((v) && (! grub_strcmp (parm, name)))
	  {
	    grub_menu_restore_field (v, '=');
	    *next = n;
	    return find_prop (node, v, out);
	  }
	grub_menu_restore_field (v, '=');
	grub_menu_restore_field (n, ':');
	parm = n;
      } while (parm);

  return node;
}

int
grub_dialog_set_parm (grub_uitree_t node, char *parm,
		      char *name, const char *value)
{
  char *out, *next;
  int result;

  node = find_parm (node, parm, name, &out, &next);
  result = (node) ? (grub_uitree_set_prop (node, out, value) == 0) : 0;
  grub_menu_restore_field (next, ':');

  return result;
}

char *
grub_dialog_get_parm (grub_uitree_t node, char *parm, char *name)
{
  char *out, *next, *result;

  node = find_parm (node, parm, name, &out, &next);
  result = (node) ? grub_uitree_get_prop (node, out) : 0;
  grub_menu_restore_field (next, ':');

  return result;
}

/* ----- dialog runner (popup / free / message) ----- */

/* Run a nested input loop over a dialog/submenu subtree; repaint the screen
   under it on close. Returns grub_widget_input's result (WB_MENU_ESCAPE on
   escape, 0 on a command that completed).  */
grub_err_t
grub_dialog_popup (grub_uitree_t node)
{
  grub_err_t r;
  grub_uitree_t save;

  grub_widget_create (node);
  grub_widget_init (node);
  node->flags |= GRUB_WIDGET_FLAG_FIXED_XY;
  save = grub_widget_current_node;
  r = grub_widget_input (node, 1);
  grub_widget_current_node = save;
  /* Repaint the whole screen under the just-closed dialog (we always do full
     redraws, so skip BURG's partial update_screen).  */
  if (! grub_widget_refresh && grub_widget_screen)
    grub_widget_draw (grub_widget_screen);
  grub_widget_free (node);

  return r;
}

void
grub_dialog_free (grub_uitree_t node, grub_uitree_t menu, grub_uitree_t save)
{
  grub_tree_remove_node (GRUB_AS_TREE (node));
  if (save)
    {
      node->next = menu->child;
      node->parent = menu;
      menu->child = save;
    }
  else
    grub_uitree_free (node);
}

/* Clone a dialog template by name and attach it under the screen. */
static grub_uitree_t
create_dialog (const char *name)
{
  grub_uitree_t node;

  if (! grub_widget_screen)
    return 0;

  node = grub_dialog_create (name, 1, 0, 0, 0);
  if (! node)
    return 0;

  grub_tree_add_child (GRUB_AS_TREE (grub_widget_screen),
		       GRUB_AS_TREE (node), -1);
  return node;
}

/* Pop up the theme's `dialog_message` template with TEXT (e.g. "Access denied").
   No-op if the theme provides no such template.  */
void
grub_dialog_message (const char *text)
{
  grub_uitree_t node;
  char k_text[] = "text";

  node = create_dialog ("dialog_message");
  if (! node)
    return;

  if (text)
    {
      char *parm = grub_uitree_get_prop (node, "parameters");
      grub_dialog_set_parm (node, parm, k_text, text);
    }
  grub_dialog_popup (node);
  grub_dialog_free (node, 0, 0);
}

/* Add one menu item (cloned template_menuitem) to a container node, mapping
   title/icon-class/command through the template's "parameters". Mirrors BURG
   build_menuitem + the per-entry bookkeeping in add_user_menu. */
void
grub_wartburg_add_item (grub_uitree_t menu_node, const char *title,
			const char *iconclass, const char *command, int index)
{
  grub_uitree_t item;
  char *parm;
  char k_title[] = "title";
  char k_class[] = "class";
  char buf[12];

  if (! menu_node)
    return;

  item = grub_dialog_create ("template_menuitem", 1, 0, 0, 0);
  if (! item)
    return;

  parm = grub_uitree_get_prop (item, "parameters");
  if (title)
    grub_dialog_set_parm (item, parm, k_title, title);
  if (iconclass)
    grub_dialog_set_parm (item, parm, k_class, iconclass);
  if (command)
    grub_uitree_set_prop (item, "command", command);

  grub_snprintf (buf, sizeof (buf), "%d", index);
  grub_uitree_set_prop (item, "index", buf);

  grub_tree_add_child (GRUB_AS_TREE (menu_node), GRUB_AS_TREE (item), -1);
}
