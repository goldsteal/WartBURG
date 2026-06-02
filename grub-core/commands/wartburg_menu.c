/* wartburg_menu.c - BURG dialog template/parameter machinery + menu population.
 *
 * Ports the template-clone + "parameters" mapping helpers from bean's BURG
 * menu/ext/dialog.c (grub_dialog_create / set_parm / get_parm + find_prop /
 * find_parm), and a build_menuitem-style helper to add one menu item to a
 * container node. The interactive dialog bits (popup/free/message/password)
 * are deferred to M3. Original copyright 2009 Bean Lee; GPLv3+.
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
