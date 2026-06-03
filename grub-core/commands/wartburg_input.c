/* wartburg_input.c - WartBURG interactive menu (M3): selection, navigation,
 * timeout, and boot.
 *
 * Reuses bean's BURG selection/navigation helpers (grub_widget_select_node +
 * find_selected/next/prev_node + mapkey) from menu/ext/widget.c, but drives them
 * with a fresh input loop written against GRUB 2.15 APIs (the original
 * grub_widget_input is entangled with removed APIs + submenu/dialog/mode-toggle).
 * Navigation: arrows + vim hjkl. Boot: grub_script_execute_sourcecode with
 * per-entry auth (no Secure-Boot bypass). Deferred: submenus/popups/edit/
 * interactive password dialog/mode-toggle. Original copyright 2009 Bean Lee; GPLv3+.
 */

#include <grub/mm.h>
#include <grub/env.h>
#include <grub/misc.h>
#include <grub/time.h>
#include <grub/term.h>
#include <grub/auth.h>
#include <grub/menu.h>
#include <grub/script_sh.h>
#include <grub/loader.h>
#include <grub/wartburg_widget.h>

/* ----- selection (ported from widget.c) ----- */

void
grub_widget_select_node (grub_uitree_t node, int selected)
{
  grub_uitree_t child;

  child = node->child;
  while (child)
    {
      if (selected)
	child->flags |= GRUB_WIDGET_FLAG_SELECTED;
      else
	child->flags &= ~GRUB_WIDGET_FLAG_SELECTED;
      child = grub_tree_next_node (GRUB_AS_TREE (node), GRUB_AS_TREE (child));
    }

  while (node)
    {
      if (selected)
	node->flags |= GRUB_WIDGET_FLAG_SELECTED;
      else
	node->flags &= ~GRUB_WIDGET_FLAG_SELECTED;
      node = node->parent;
    }
}

static grub_uitree_t
find_selected_node (grub_uitree_t root)
{
  grub_uitree_t node;

  node = root->child;
  while (node)
    {
      if (node->flags & GRUB_WIDGET_FLAG_SELECTED)
	{
	  if (node->flags & GRUB_WIDGET_FLAG_NODE)
	    return node;
	  else
	    node = node->child;
	}
      else
	node = node->next;
    }

  return 0;
}

static grub_uitree_t
find_next_node (grub_uitree_t anchor, grub_uitree_t node)
{
  grub_uitree_t n;

  n = node;
  do
    {
      n = grub_tree_next_node (GRUB_AS_TREE (anchor), GRUB_AS_TREE (n));
      if (! n)
	n = anchor;
      if (n == node)
	return 0;
    }
  while (! (n->flags & GRUB_WIDGET_FLAG_NODE));

  return n;
}

static grub_uitree_t
find_prev_node (grub_uitree_t anchor, grub_uitree_t node)
{
  grub_uitree_t save, n;

  save = 0;
  n = anchor;
  while (n)
    {
      if (n == node)
	{
	  if (save)
	    break;
	}
      else
	{
	  if (n->flags & GRUB_WIDGET_FLAG_NODE)
	    save = n;
	}

      n = grub_tree_next_node (GRUB_AS_TREE (anchor), GRUB_AS_TREE (n));
    }

  return save;
}

/* mapkey: theme can remap keys via a `mapkey` section (key-name -> key-name). */
static int
map_key (int key)
{
  grub_uitree_t map;
  const char *name;

  map = grub_uitree_find (&grub_uitree_root, "mapkey");
  if (! map)
    return key;

  name = grub_menu_key2name (key);
  if (! name)
    return key;

  name = grub_uitree_get_prop (map, (char *) name);
  return (name) ? grub_menu_name2key (name) : key;
}

/* ----- menu population from the real grub_menu ----- */

void
grub_wartburg_add_entry (grub_uitree_t menu_node, grub_menu_entry_t entry,
			 int index)
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
  if (entry->title)
    grub_dialog_set_parm (item, parm, k_title, entry->title);
  /* In GRUB 2.15 entry->classes points straight at the first class node (no
     dummy head, despite menu.h's stale comment) -- matching icon_manager.c. */
  if (entry->classes && entry->classes->name)
    grub_dialog_set_parm (item, parm, k_class, entry->classes->name);
  if (entry->users)
    grub_uitree_set_prop (item, "users", entry->users);
  if (entry->sourcecode)
    grub_uitree_set_prop (item, "command", entry->sourcecode);

  grub_snprintf (buf, sizeof (buf), "%d", index);
  grub_uitree_set_prop (item, "index", buf);

  grub_tree_add_child (GRUB_AS_TREE (menu_node), GRUB_AS_TREE (item), -1);
}

/* ----- boot ----- */

/* Execute a chosen entry's command, honoring per-entry auth. Returns only if
   booting did not transfer control (e.g., command failed). */
static void
boot_node (grub_uitree_t node)
{
  char *cmd, *users, *index;

  users = grub_widget_get_prop (node, "users");
  if (users && grub_auth_check_authentication (users) != GRUB_ERR_NONE)
    {
      grub_errno = GRUB_ERR_NONE;	/* denied; no bypass */
      return;
    }

  cmd = grub_widget_get_prop (node, "command");
  if (! cmd)
    return;

  index = grub_uitree_get_prop (node, "index");
  if (index)
    grub_env_set ("chosen", index);

  grub_script_execute_sourcecode (cmd);
  if (grub_errno == GRUB_ERR_NONE && grub_loader_is_loaded ())
    grub_script_execute_sourcecode ("boot");

  grub_errno = GRUB_ERR_NONE;
}

/* ----- timeout (auto-boot countdown with progressbar) ----- */

static void
set_timeout_widgets (grub_uitree_t tnode, int total, int left)
{
  grub_uitree_t child;
  grub_menu_region_update_list_t head = 0;

  for (child = tnode; child;
       child = grub_tree_next_node (GRUB_AS_TREE (tnode), GRUB_AS_TREE (child)))
    {
      grub_widget_t w = child->data;
      if (w && w->class->set_timeout)
	{
	  w->class->set_timeout (w, total, left);
	  if (w->class->draw)
	    w->class->draw (w, &head, 0, 0, w->width, w->height);
	}
    }
  grub_menu_region_apply_update (head);
}

/* Returns the key that ended the countdown ('\r' on expiry to boot default). */
static int
run_timeout (grub_uitree_t root)
{
  const char *p;
  int total, left, key;
  grub_uint64_t last;
  grub_uitree_t tnode;

  p = grub_env_get ("timeout");
  if (! p)
    return 0;

  total = grub_strtoul (p, 0, 0);
  if ((key = grub_getkey_noblock ()) != GRUB_TERM_NO_KEY)
    return key;
  if (! total)
    return '\r';

  tnode = grub_uitree_find_id (root, "__timeout__");
  total *= 1000;
  left = total;
  last = grub_get_time_ms ();
  while (left > 0)
    {
      grub_uint64_t now;

      key = grub_getkey_noblock ();
      if (key != GRUB_TERM_NO_KEY)
	break;

      if (tnode)
	set_timeout_widgets (tnode, total, left);

      now = grub_get_time_ms ();
      left -= (now - last);
      last = now;
    }

  if (tnode)
    tnode->flags |= GRUB_WIDGET_FLAG_HIDDEN;

  return (left <= 0) ? '\r' : key;
}

/* ----- the menu loop ----- */

#define WB_KEY_PREV(k) \
  ((k) == GRUB_TERM_KEY_UP || (k) == GRUB_TERM_KEY_LEFT \
   || (k) == 'h' || (k) == 'k')
#define WB_KEY_NEXT(k) \
  ((k) == GRUB_TERM_KEY_DOWN || (k) == GRUB_TERM_KEY_RIGHT \
   || (k) == 'l' || (k) == 'j')

/* Block for a key WITHOUT grub_refresh(). grub_getkey() refreshes every active
   terminal output, and gfxterm's refresh recomposites the whole framebuffer
   from its own model -- wiping the menu we drew. Polling getkey_noblock avoids
   that, so our presented frame survives.  */
static int
wb_getkey (void)
{
  int k;

  while ((k = grub_getkey_noblock ()) == GRUB_TERM_NO_KEY)
    grub_cpu_idle ();
  return k;
}

void
grub_wartburg_run (grub_uitree_t root, int default_num)
{
  grub_uitree_t cur;
  int c, timed;

  root->flags |= (GRUB_WIDGET_FLAG_ROOT | GRUB_WIDGET_FLAG_ANCHOR);

  cur = find_selected_node (root);
  if (! cur)
    {
      int i;

      cur = find_next_node (root, root);	/* first item */
      for (i = 0; cur && i < default_num; i++)
	cur = find_next_node (root, cur);	/* advance to default */
      if (cur)
	grub_widget_select_node (cur, 1);
      else
	cur = root;
    }

  /* Scroll the default selection into view before the first paint (it may sit
     past the visible edge of a horizontal/vertical menu). */
  if (cur != root)
    grub_widget_scroll (cur);

  grub_widget_draw (root);

  /* Timeout pass: auto-boot the selected (default) entry unless a key interrupts. */
  timed = run_timeout (root);
  if (timed == '\r')
    {
      boot_node (cur);
      grub_widget_draw (root);	/* boot returned (failed) -> back to menu */
      c = 0;
    }
  else
    c = map_key (timed);

  while (1)
    {
      if (c == 0)
	c = map_key (wb_getkey ());

      if (WB_KEY_PREV (c) || WB_KEY_NEXT (c))
	{
	  grub_uitree_t nv;

	  nv = WB_KEY_PREV (c) ? find_prev_node (root, cur)
	    : find_next_node (root, cur);
	  if (nv && nv != cur)
	    {
	      grub_widget_select_node (cur, 0);
	      grub_widget_select_node (nv, 1);
	      cur = nv;
	      grub_widget_scroll (cur);	/* keep selection on-screen */
	      grub_widget_draw (root);
	    }
	}
      else if (c == '\r' || c == '\n')
	{
	  boot_node (cur);
	  grub_widget_draw (root);	/* boot returned (failed) */
	}

      c = 0;
    }
}
