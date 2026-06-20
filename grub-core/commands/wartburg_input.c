/* wartburg_input.c - WartBURG interactive menu: BURG's input dispatch.
 *
 * Port of bean's BURG grub_widget_input (menu/ext/widget.c) and its helpers
 * (onkey, get_dir_cmd, run_dir_cmd, selection/nav, timeout) adapted to GRUB
 * 2.15. The loop routes keys -> theme `onkey` keybinds -> `ui_*` nav
 * pseudo-commands -> the current widget's onkey (so password/edit/term consume
 * keystrokes), recurses for nested dialogs/submenus, and authenticates
 * restricted entries before booting. Boot is via grub_script_execute_sourcecode
 * (no Secure-Boot bypass). Navigation also accepts vim hjkl.
 *
 * WartBURG adaptations vs the 2009 original:
 *  - input is polled via grub_getkey_noblock (wb_getkey), never grub_getkey:
 *    grub_getkey calls grub_refresh, and gfxterm's refresh recomposites the
 *    whole framebuffer from its own model, wiping the frame we drew.
 *  - GRUB_TERM_ASCII_CHAR dropped (keys are already unicode ints);
 *    GRUB_TERM_{UP,DOWN,LEFT,RIGHT} -> GRUB_TERM_KEY_*; grub_parser_execute /
 *    grub_command_execute -> grub_script_execute_sourcecode; grub_checkkey ->
 *    grub_getkey_noblock; GRUB_ERR_MENU_ESCAPE -> WB_MENU_ESCAPE sentinel.
 * Original copyright 2009 Bean Lee; GPLv3+.
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
#include <grub/normal.h>
#include <grub/wartburg_widget.h>
#include <grub/wartburg_bedrock.h>

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

/* onkey: a theme `onkey` section can bind a key-name to a command string. */
static char *
onkey (int key)
{
  grub_uitree_t map;
  const char *name;

  map = grub_uitree_find (&grub_uitree_root, "onkey");
  if (! map)
    return 0;

  name = grub_menu_key2name (key);
  if (! name)
    return 0;

  return grub_uitree_get_prop (map, (char *) name);
}

/* ----- directional navigation ----- */

#define DIR_NEXT	1
#define DIR_ANCHOR	2

/* Map an arrow/vim key to a ui_* nav command, honoring the container's
   direction (horizontal/vertical, reverse).  */
static char *
get_dir_cmd (grub_uitree_t node, int k)
{
  int horizontal, reverse;
  int dir;

  if (node->parent)
    grub_widget_get_direction (node->parent, &horizontal, &reverse);
  else
    horizontal = reverse = 0;

  if (k == GRUB_TERM_KEY_LEFT || k == 'h')
    dir = DIR_ANCHOR;
  else if (k == GRUB_TERM_KEY_RIGHT || k == 'l')
    dir = DIR_ANCHOR | DIR_NEXT;
  else if (k == GRUB_TERM_KEY_UP || k == 'k')
    dir = 0;
  else if (k == GRUB_TERM_KEY_DOWN || k == 'j')
    dir = DIR_NEXT;
  else
    return 0;

  if (horizontal)
    dir ^= DIR_ANCHOR;

  if ((reverse) && (! (dir & DIR_ANCHOR)))
    dir ^= DIR_NEXT;

  if (dir == 0)
    return (char *) "ui_prev_node";
  else if (dir == DIR_NEXT)
    return (char *) "ui_next_node";
  else if (dir == DIR_ANCHOR)
    return (char *) "ui_prev_anchor";
  else
    return (char *) "ui_next_anchor";
}

static grub_uitree_t
run_dir_cmd (char *name, grub_uitree_t current_node)
{
  grub_uitree_t root, next, node, anchor, save;

  root = current_node;
  anchor = 0;
  while (root)
    {
      if ((root->flags & GRUB_WIDGET_FLAG_ANCHOR) && (! anchor))
	anchor = root;

      if (root->flags & GRUB_WIDGET_FLAG_ROOT)
	break;

      root = root->parent;
    }

  if (! anchor)
    anchor = root;

  if ((name[8] == 'a') && (root != anchor))
    {
      save = anchor->child;
      anchor->child = 0;
      node = anchor;
      anchor = root;
    }
  else
    {
      save = 0;
      node = current_node;
    }

  next = (name[3] == 'n') ? find_next_node (anchor, node) :
    find_prev_node (anchor, node);

  if (save)
    node->child = save;

  return next;
}

/* ----- menu population from the real grub_menu ----- */

/* Map GRUB's grub-mkconfig/os-prober `--class` vocabulary onto the icon-class
   names BURG themes ship (see burg-ref icons/hover): os-prober tags macOS
   `--class osx --class darwin`, and every linux entry carries `--class
   gnu-linux`, but the BURG icon set keys on `macosx` and `linux`. Return an
   extra alias to APPEND so OS-prober-detected systems resolve to the right
   logo without the user touching grub-mkconfig.  */
static const char *
wb_class_alias (const char *name)
{
  if (! grub_strcmp (name, "osx") || ! grub_strcmp (name, "darwin"))
    return "macosx";
  if (! grub_strcmp (name, "gnu-linux"))
    return "linux";
  if (! grub_strcmp (name, "opensuse"))
    return "suse";
  return 0;
}

/* Comma-join all of ENTRY's `--class` tags (plus any icon-class aliases) into
   one string. Passing the FULL list -- not just the first class -- is what lets
   the theme's class-fallback chain (grub_widget_get_prop) try a specific distro
   icon, then a generic one (e.g. fedora -> linux -> unknown). Caller frees.  */
static char *
wb_class_list (grub_menu_entry_t entry)
{
  struct grub_menu_entry_class *c;
  grub_size_t len = 0;
  char *out, *p;

  for (c = entry->classes; c; c = c->next)
    {
      const char *alias;
      if (! c->name)
	continue;
      len += grub_strlen (c->name) + 1;
      alias = wb_class_alias (c->name);
      if (alias)
	len += grub_strlen (alias) + 1;
    }
  if (! len)
    return 0;

  out = grub_malloc (len + 1);
  if (! out)
    return 0;

  p = out;
  for (c = entry->classes; c; c = c->next)
    {
      const char *alias;
      if (! c->name)
	continue;
      if (p != out)
	*p++ = ',';
      p = grub_stpcpy (p, c->name);
      alias = wb_class_alias (c->name);
      if (alias)
	{
	  *p++ = ',';
	  p = grub_stpcpy (p, alias);
	}
    }
  *p = '\0';
  return out;
}

/* Clone a menu-item template and map a menu entry's title/icon-class onto its
   `parameters`, plus direct props (command/users/index, and a `submenu` mark
   for entries that open a nested menu).  */
static grub_uitree_t
build_item (const char *tmpl, grub_menu_entry_t entry, int index)
{
  grub_uitree_t item;
  char *parm;
  char *classes;
  char k_title[] = "title";
  char k_class[] = "class";
  char buf[12];

  item = grub_dialog_create (tmpl, 1, 0, 0, 0);
  if (! item)
    return 0;

  parm = grub_uitree_get_prop (item, "parameters");
  if (entry->title)
    grub_dialog_set_parm (item, parm, k_title, entry->title);
  /* In GRUB 2.15 entry->classes points straight at the first class node (no
     dummy head, despite menu.h's stale comment) -- matching icon_manager.c. */
  classes = wb_class_list (entry);
  if (classes)
    {
      grub_dialog_set_parm (item, parm, k_class, classes);
      grub_free (classes);
    }
  if (entry->users)
    grub_uitree_set_prop (item, "users", entry->users);
  if (entry->sourcecode)
    grub_uitree_set_prop (item, "command", entry->sourcecode);
  if (entry->submenu)
    grub_uitree_set_prop (item, "submenu", "1");

  grub_snprintf (buf, sizeof (buf), "%d", index);
  grub_uitree_set_prop (item, "index", buf);

  return item;
}

void
grub_wartburg_add_entry (grub_uitree_t menu_node, grub_menu_entry_t entry,
			 int index)
{
  grub_uitree_t item;

  if (! menu_node)
    return;

  item = build_item ("template_menuitem", entry, index);
  if (item)
    grub_tree_add_child (GRUB_AS_TREE (menu_node), GRUB_AS_TREE (item), -1);
}

/* Drill into a submenu entry: execute its sourcecode in a fresh menu context
   (GRUB builds the nested entries there), build a `template_submenu` popup from
   them, and run it as a nested dialog. Mirrors grub_menu_execute_entry's
   submenu branch, but renders the nested menu with our theme.  */
static void
enter_submenu (grub_uitree_t node)
{
  char *cmd;
  grub_menu_t menu;
  grub_uitree_t sub;

  cmd = grub_widget_get_prop (node, "command");
  if (! cmd || ! grub_widget_screen)
    return;

  grub_env_context_open ();
  menu = grub_zalloc (sizeof (*menu));
  if (! menu)
    {
      grub_env_context_close ();
      return;
    }
  grub_env_set_menu (menu);

  grub_script_execute_sourcecode (cmd);
  grub_errno = GRUB_ERR_NONE;

  if (menu->size)
    {
      sub = grub_dialog_create ("template_submenu", 1, 0, 0, 0);
      if (sub)
	{
	  grub_menu_entry_t e;
	  int i;

	  grub_tree_add_child (GRUB_AS_TREE (grub_widget_screen),
			       GRUB_AS_TREE (sub), -1);
	  for (i = 0, e = menu->entry_list; e; e = e->next, i++)
	    {
	      grub_uitree_t it = build_item ("template_subitem", e, i);
	      if (it)
		grub_tree_add_child (GRUB_AS_TREE (sub), GRUB_AS_TREE (it), -1);
	    }
	  grub_dialog_popup (sub);
	  grub_dialog_free (sub, 0, 0);
	}
    }

  grub_normal_free_menu (menu);
  grub_env_context_close ();
}

/* ----- auth + command execution ----- */

/* Authenticate against a userlist before running a restricted entry. For now
   this is GRUB's standard (text-prompt) auth; the themed password dialog
   replaces it in the dialog-machinery step. Returns 1 if allowed.  */
static int
wb_check_users (const char *users)
{
  if (! users || ! *users)
    return 1;
  if (grub_auth_check_authentication (users) == GRUB_ERR_NONE)
    return 1;
  grub_errno = GRUB_ERR_NONE;	/* denied; no bypass */
  return 0;
}

/* ----- timeout (auto-boot countdown) ----- */

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

/* Adapted from BURG check_timeout: returns nonzero ("init", menu already
   drawn) when a countdown ran; sets *key to the key that ended it ('\r' to
   boot the default on expiry).  */
static int
check_timeout (grub_uitree_t root, int *key)
{
  const char *p;
  int total, left, k;
  grub_uint64_t last;
  grub_uitree_t tnode;

  p = grub_env_get ("timeout");
  if (! p)
    return 0;

  total = grub_strtoul (p, 0, 0);
  k = grub_getkey_noblock ();
  if ((k != GRUB_TERM_NO_KEY) || (! total))
    {
      *key = (k != GRUB_TERM_NO_KEY) ? k : '\r';
      return 0;
    }

  grub_widget_draw (root);
  tnode = grub_uitree_find_id (root, "__timeout__");
  total *= 1000;
  left = total;
  *key = '\r';
  last = grub_get_time_ms ();
  while (left > 0)
    {
      grub_uint64_t now;

      k = grub_getkey_noblock ();
      if (k != GRUB_TERM_NO_KEY)
	{
	  *key = k;
	  break;
	}

      if (tnode)
	set_timeout_widgets (tnode, total, left);

      now = grub_get_time_ms ();
      left -= (now - last);
      last = now;
    }

  if (tnode)
    tnode->flags |= GRUB_WIDGET_FLAG_HIDDEN;

  return 1;
}

/* ----- the input dispatch ----- */

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

static const char *wb_default_themes =
  "radiance,coffee,burg,refit,minimum,proto,sora_clean,winter,ubuntu,ubuntu2,chiva,black_and_white";

static const char *wb_default_gfxmodes =
  "800x600,640x480,1024x768,1024x600,auto";

static int
theme_name_matches (const char *theme, const char *name, grub_size_t len)
{
  const char *p, *end;

  if (! theme || ! *theme)
    return 0;
  p = grub_strrchr (theme, '/');
  if (p && ! grub_strcmp (p, "/theme"))
    {
      end = p;
      while (p > theme && p[-1] != '/')
        p--;
      return ((grub_size_t) (end - p) == len && ! grub_strncmp (p, name, len));
    }
  return (grub_strlen (theme) == len && ! grub_strncmp (theme, name, len));
}

static void
remember_selected_index (void)
{
  if (grub_widget_current_node)
    {
      char *index = grub_uitree_get_prop (grub_widget_current_node, "index");
      if (index)
        grub_env_set ("wartburg_selected", index);
    }
}

static char *
copy_theme_name (const char *name, grub_size_t len)
{
  char *theme;

  theme = grub_malloc (len + 1);
  if (! theme)
    return 0;
  grub_memcpy (theme, name, len);
  theme[len] = '\0';
  return theme;
}

static int
set_theme_from_token (const char *name, grub_size_t len)
{
  char *theme;

  if (! len)
    return 0;
  theme = copy_theme_name (name, len);
  if (! theme)
    return 0;
  grub_env_set ("theme", theme);
  grub_env_set ("wartburg_theme_persist", "1");
  grub_free (theme);
  remember_selected_index ();
  grub_widget_refresh = GRUB_WIDGET_RELOAD_MODE;
  return 1;
}

static int
cycle_theme (void)
{
  const char *list, *cur;
  const char *p, *first, *next;
  grub_size_t first_len, next_len;
  int found;

  list = grub_env_get ("wartburg_themes");
  if (! list || ! *list)
    list = wb_default_themes;
  cur = grub_env_get ("theme");

  p = list;
  first = p;
  while (*p && *p != ',')
    p++;
  first_len = p - first;
  next = first;
  next_len = first_len;
  found = 0;

  p = list;
  while (*p)
    {
      const char *start = p;
      grub_size_t len;

      while (*p && *p != ',')
        p++;
      len = p - start;
      if (len && theme_name_matches (cur, start, len))
        {
          if (*p == ',')
            {
              const char *q = p + 1;
              next = q;
              while (*q && *q != ',')
                q++;
              next_len = q - next;
            }
          found = 1;
          break;
        }
      if (*p == ',')
        p++;
    }

  if (! found)
    {
      next = first;
      next_len = first_len;
    }
  if (! next_len)
    return 0;

  return set_theme_from_token (next, next_len);
}

static grub_uitree_t
choice_node (const char *template_name, const char *name, grub_size_t len,
	     const char *iconclass, int gfxmode, int index)
{
  grub_uitree_t item;
  char *label, *cmd, *parm;
  char k_title[] = "title";
  char k_class[] = "class";
  char buf[12];

  label = copy_theme_name (name, len);
  if (! label)
    return 0;

  item = grub_dialog_create (template_name, 1, 0, 0, 0);
  if (! item)
    item = grub_dialog_create ("template_menuitem", 1, 0, 0, 0);
  if (! item)
    {
      grub_free (label);
      return 0;
    }

  parm = grub_uitree_get_prop (item, "parameters");
  grub_dialog_set_parm (item, parm, k_title, label);
  grub_dialog_set_parm (item, parm, k_class, iconclass);

  if (gfxmode)
    cmd = grub_xasprintf ("set wartburg_gfxmode_next=%s; set wartburg_gfxmode_reload=1",
				  label);
  else
    cmd = grub_xasprintf ("set wartburg_theme_next=%s; set wartburg_theme_reload=1",
				  label);
  if (cmd)
    {
      grub_uitree_set_prop (item, "command", cmd);
      grub_free (cmd);
    }
  grub_snprintf (buf, sizeof (buf), "%d", index);
  grub_uitree_set_prop (item, "index", buf);
  grub_free (label);
  return item;
}

static grub_uitree_t
theme_choice_node (const char *name, grub_size_t len, int index)
{
  return choice_node ("template_subitem", name, len, "theme",
		      0, index);
}

static grub_uitree_t
gfxmode_choice_node (const char *name, grub_size_t len, int index)
{
  return choice_node ("template_subitem", name, len, "display",
		      1, index);
}

static void
theme_menu (grub_uitree_t root)
{
  const char *list, *reload, *next;
  const char *p;
  grub_uitree_t sub;
  int index;

  if (! grub_widget_screen)
    return;

  sub = grub_dialog_create ("template_submenu", 1, 0, 0, 0);
  if (! sub)
    {
      cycle_theme ();
      return;
    }

  list = grub_env_get ("wartburg_themes");
  if (! list || ! *list)
    list = wb_default_themes;

  for (p = list, index = 0; *p; index++)
    {
      const char *start = p;
      grub_size_t len;
      grub_uitree_t item;

      while (*p && *p != ',')
        p++;
      len = p - start;
      item = theme_choice_node (start, len, index);
      if (item)
        grub_tree_add_child (GRUB_AS_TREE (sub), GRUB_AS_TREE (item), -1);
      if (*p == ',')
        p++;
    }

  grub_tree_add_child (GRUB_AS_TREE (grub_widget_screen), GRUB_AS_TREE (sub), -1);
  grub_dialog_popup (sub);
  grub_dialog_free (sub, 0, 0);

  reload = grub_env_get ("wartburg_theme_reload");
  next = grub_env_get ("wartburg_theme_next");
  if (reload && *reload && next && *next)
    {
      grub_env_set ("theme", next);
      grub_env_set ("wartburg_theme_persist", "1");
      grub_env_unset ("wartburg_theme_reload");
      grub_env_unset ("wartburg_theme_next");
      remember_selected_index ();
      grub_widget_refresh = GRUB_WIDGET_RELOAD_MODE;
    }
  else
    grub_widget_draw (root);
}

static void
gfxmode_menu (grub_uitree_t root)
{
  const char *list, *reload, *next;
  const char *p;
  grub_uitree_t sub;
  int index;

  if (! grub_widget_screen)
    return;

  sub = grub_dialog_create ("template_submenu", 1, 0, 0, 0);
  if (! sub)
    return;

  list = grub_env_get ("wartburg_gfxmodes");
  if (! list || ! *list)
    list = wb_default_gfxmodes;

  for (p = list, index = 0; *p; index++)
    {
      const char *start = p;
      grub_size_t len;
      grub_uitree_t item;

      while (*p && *p != ',')
        p++;
      len = p - start;
      item = gfxmode_choice_node (start, len, index);
      if (item)
        grub_tree_add_child (GRUB_AS_TREE (sub), GRUB_AS_TREE (item), -1);
      if (*p == ',')
        p++;
    }

  grub_tree_add_child (GRUB_AS_TREE (grub_widget_screen), GRUB_AS_TREE (sub), -1);
  grub_dialog_popup (sub);
  grub_dialog_free (sub, 0, 0);

  reload = grub_env_get ("wartburg_gfxmode_reload");
  next = grub_env_get ("wartburg_gfxmode_next");
  if (reload && *reload && next && *next)
    {
      grub_env_set ("gfxmode", next);
      grub_env_set ("wartburg_gfxmode_persist", "1");
      grub_env_unset ("wartburg_gfxmode_reload");
      grub_env_unset ("wartburg_gfxmode_next");
      remember_selected_index ();
      grub_widget_refresh = GRUB_WIDGET_RELOAD_MODE;
    }
  else
    grub_widget_draw (root);
}

int
grub_widget_input (grub_uitree_t root, int nested)
{
  int init, c;

  root->flags |= (GRUB_WIDGET_FLAG_ROOT | GRUB_WIDGET_FLAG_ANCHOR);

  grub_widget_current_node = find_selected_node (root);
  if (! grub_widget_current_node)
    {
      grub_widget_current_node = find_next_node (root, root);
      if (! grub_widget_current_node)
	grub_widget_current_node = root;
      else
	grub_widget_select_node (grub_widget_current_node, 1);
    }
  if (grub_widget_current_node != root)
    grub_widget_scroll (grub_widget_current_node);

  init = c = 0;
  if (! nested)
    init = check_timeout (root, &c);

  if (! init)
    {
      grub_uitree_t node = grub_uitree_find_id (root, "__timeout__");
      if (node)
	node->flags |= GRUB_WIDGET_FLAG_HIDDEN;
    }

  while (1)
    {
      char *cmd, *users;

      users = 0;
      cmd = onkey (c);
      if (! cmd)
	{
	  if (c == '\r' || c == '\n')
	    {
	      cmd = grub_widget_get_prop (grub_widget_current_node, "command");
	      users = grub_widget_get_prop (grub_widget_current_node, "users");
	      c = 0;
	    }
	  else if (c == GRUB_TERM_ESC)
	    cmd = (char *) "ui_escape";
	  else if (c == GRUB_TERM_TAB)
	    cmd = (char *) "ui_next_anchor";
	  else if (c == 'c')
	    {
	      /* Drop to GRUB's own command-line console (renders full-screen via
		 gfxterm; history/completion/auth all handled by `normal'). */
	      grub_cmdline_run (1, 0);
	      grub_errno = GRUB_ERR_NONE;
	      grub_widget_draw (grub_widget_screen ? grub_widget_screen : root);
	    }
	  else if (c == 'e')
	    {
	      /* Edit the selected entry with GRUB's own entry editor. */
	      char *idx = grub_uitree_get_prop (grub_widget_current_node, "index");
	      grub_menu_t m = grub_env_get_menu ();
	      if (idx && m)
		{
		  grub_menu_entry_t en =
		    grub_menu_get_entry (m, grub_strtoul (idx, 0, 0));
		  if (en)
		    grub_menu_entry_run (en);
		}
	      grub_errno = GRUB_ERR_NONE;
	      grub_widget_draw (grub_widget_screen ? grub_widget_screen : root);
	    }
	  else if (c == GRUB_TERM_KEY_F2)
	    {
	      theme_menu (root);
	      if (grub_widget_refresh == GRUB_WIDGET_RELOAD_MODE)
		return GRUB_WIDGET_RESULT_DONE;
	    }
	  else if (c == GRUB_TERM_KEY_F3)
	    {
	      gfxmode_menu (root);
	      if (grub_widget_refresh == GRUB_WIDGET_RELOAD_MODE)
		return GRUB_WIDGET_RESULT_DONE;
	    }
	  else
	    cmd = get_dir_cmd (grub_widget_current_node, c);
	}
      else if (*cmd == '*')
	{
	  cmd++;
	  users = (char *) "";
	}

      if ((cmd) && (*cmd))
	{
	  if ((users) && (! wb_check_users (users)))
	    {
	      grub_errno = 0;
	    }
	  else if (! grub_strcmp (cmd, "ui_escape"))
	    {
	      if (nested)
		return WB_MENU_ESCAPE;
	    }
	  else if (! grub_strcmp (cmd, "ui_quit"))
	    {
	      return WB_MENU_ESCAPE;
	    }
	  else if ((! grub_strcmp (cmd, "ui_next_node")) ||
		   (! grub_strcmp (cmd, "ui_prev_node")) ||
		   (! grub_strcmp (cmd, "ui_next_anchor")) ||
		   (! grub_strcmp (cmd, "ui_prev_anchor")))
	    {
	      grub_uitree_t next;

	      next = run_dir_cmd (cmd, grub_widget_current_node);
	      if ((next) && (next != grub_widget_current_node))
		{
		  grub_widget_select_node (grub_widget_current_node, 0);
		  grub_widget_select_node (next, 1);
		  grub_widget_current_node = next;
		  if (init)
		    {
		      grub_widget_scroll (next);
		      grub_widget_draw (root);
		    }
		}
	    }
	  else if (! grub_memcmp (cmd, "ui_next_class", 13))
	    {
	      char *class = cmd + 13;

	      while (*class == ' ')
		class++;

	      if (! *class)
		{
		  char *parm;
		  parm = grub_uitree_get_prop (grub_widget_current_node,
					       "parameters");
		  class = grub_dialog_get_parm (grub_widget_current_node,
						parm, (char *) "class");
		}
	      if (class)
		{
		  grub_uitree_t cur, next;

		  cur = grub_widget_current_node;
		  while (1)
		    {
		      char *parm, *ncls;

		      next = run_dir_cmd ((char *) "ui_next_node", cur);
		      if ((! next) || (next == grub_widget_current_node))
			break;

		      parm = grub_uitree_get_prop (next, "parameters");
		      ncls = grub_dialog_get_parm (next, parm, (char *) "class");
		      if ((ncls) && (! grub_strcmp (class, ncls)))
			{
			  grub_widget_select_node (grub_widget_current_node, 0);
			  grub_widget_select_node (next, 1);
			  grub_widget_current_node = next;
			  if (init)
			    {
			      grub_widget_scroll (next);
			      grub_widget_draw (root);
			    }
			  break;
			}
		      cur = next;
		    }
		}
	    }
	  else if (grub_widget_get_prop (grub_widget_current_node, "submenu"))
	    {
	      enter_submenu (grub_widget_current_node);
	      grub_widget_draw (root);
	    }
	  else
	    {
	      if ((! c) && (! nested))
		{
		  char *index, *stratum;

		  index = grub_uitree_get_prop (grub_widget_current_node,
						"index");
		  if (index)
		    grub_env_set ("chosen", index);

		  /* Bedrock composite: remember which stratum we boot so its
		     icon defaults to "large" next time (WB_BASE_LAST). */
		  stratum = grub_uitree_get_prop (grub_widget_current_node,
						  "stratum");
		  if (stratum && *stratum)
		    grub_wartburg_bedrock_set_last (stratum);
		}

	      grub_script_execute_sourcecode (cmd);

	      if (grub_widget_refresh)
		return grub_errno;

	      if (grub_errno == GRUB_ERR_NONE && grub_loader_is_loaded ())
		grub_script_execute_sourcecode ("boot");

	      if (nested)
		return grub_errno;

	      grub_errno = 0;
	    }
	}

      if (! init)
	{
	  grub_widget_draw (root);
	  init++;
	}

      while (1)
	{
	  grub_widget_t widget;

	  widget = grub_widget_current_node->data;
	  if (widget && widget->class->draw_cursor)
	    widget->class->draw_cursor (widget);

	  c = map_key (wb_getkey ());
	  if (widget && widget->class->onkey)
	    {
	      int r;

	      r = widget->class->onkey (widget, c);
	      if (grub_widget_refresh)
		return r;

	      if ((r >= 0) && (nested))
		return r;
	      else if (r == GRUB_WIDGET_RESULT_SKIP)
		break;
	    }
	  else
	    break;
	}
    }
}

/* Entry point: pre-select the default entry, then run the dispatch (top level).  */
int
grub_wartburg_run (grub_uitree_t root, int default_num)
{
  grub_uitree_t cur;

  root->flags |= (GRUB_WIDGET_FLAG_ROOT | GRUB_WIDGET_FLAG_ANCHOR);

  if (! find_selected_node (root))
    {
      int i;

      cur = find_next_node (root, root);	/* first item */
      for (i = 0; cur && i < default_num; i++)
	cur = find_next_node (root, cur);	/* advance to default */
      if (cur)
	grub_widget_select_node (cur, 1);
    }

  return grub_widget_input (root, 0);
}
