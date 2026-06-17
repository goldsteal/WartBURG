/* wartburg_bedrock.c - Bedrock Linux awareness: stratum model + composite item.
 *
 * Builds a model of the host's Bedrock strata from the GRUB menu (entries
 * tagged `--class bedrock --class stratum-<name>`), tracks which one is drawn
 * "large" (last-booted / hijacked / Bedrock logo), persists the last-booted
 * stratum to grubenv, and injects a single composite `bedrock` widget into the
 * menu in place of the individual stratum entries. Additive; no core patch.
 */

#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/env.h>
#include <grub/command.h>
#include <grub/menu.h>
#include <grub/wartburg_widget.h>
#include <grub/wartburg_bedrock.h>

static struct wb_stratum strata[WB_BEDROCK_MAX];
static int n_strata;
static char *icon_dir;
static int base_mode = WB_BASE_LAST;
static char *hijacked_name;	/* env wartburg_bedrock_hijacked */
static char *last_name;		/* env wartburg_bedrock_last     */

int
grub_wartburg_bedrock_is_stratum (grub_menu_entry_t e)
{
  struct grub_menu_entry_class *c;

  for (c = e->classes; c; c = c->next)
    if (c->name && ! grub_strcmp (c->name, "bedrock"))
      return 1;
  return 0;
}

/* The stratum name from an entry's `--class stratum-<name>` tag (or 0). */
static const char *
entry_stratum_name (grub_menu_entry_t e)
{
  struct grub_menu_entry_class *c;

  for (c = e->classes; c; c = c->next)
    if (c->name && ! grub_memcmp (c->name, "stratum-", 8) && c->name[8])
      return c->name + 8;
  return 0;
}

void
grub_wartburg_bedrock_free (void)
{
  int i;

  for (i = 0; i < n_strata; i++)
    {
      grub_free (strata[i].name);
      grub_free (strata[i].iconclass);
      grub_free (strata[i].command);
      grub_free (strata[i].users);
    }
  grub_memset (strata, 0, sizeof (strata));
  n_strata = 0;

  grub_free (icon_dir);
  icon_dir = 0;
  grub_free (hijacked_name);
  hijacked_name = 0;
  grub_free (last_name);
  last_name = 0;
}

/* .../themes/<name>/theme  ->  .../themes/icons */
static char *
icon_dir_from_theme (const char *theme_path)
{
  char *d, *s;
  char *out = 0;

  d = grub_strdup (theme_path);
  if (! d)
    return 0;

  s = grub_strrchr (d, '/');	/* strip "/theme" -> theme dir          */
  if (s)
    {
      *s = '\0';
      s = grub_strrchr (d, '/');	/* strip "/<name>" -> themes dir */
      if (s)
	{
	  *s = '\0';
	  out = grub_xasprintf ("%s/icons", d);
	}
    }
  grub_free (d);
  return out;
}

void
grub_wartburg_bedrock_build (grub_menu_t menu, const char *theme_path)
{
  grub_menu_entry_t e;
  const char *p;
  int i;

  grub_wartburg_bedrock_free ();

  if (theme_path)
    icon_dir = icon_dir_from_theme (theme_path);

  for (i = 0, e = menu->entry_list; e; e = e->next, i++)
    {
      const char *sn;
      struct wb_stratum *st;

      if (n_strata >= WB_BEDROCK_MAX)
	break;
      if (! grub_wartburg_bedrock_is_stratum (e))
	continue;
      sn = entry_stratum_name (e);
      if (! sn)
	continue;

      st = &strata[n_strata];
      st->name = grub_strdup (sn);
      st->iconclass = grub_strdup (sn);
      st->command = e->sourcecode ? grub_strdup (e->sourcecode) : 0;
      st->users = e->users ? grub_strdup (e->users) : 0;
      st->index = i;
      n_strata++;
    }

  p = grub_env_get ("wartburg_bedrock_base");
  if (p && ! grub_strcmp (p, "hijacked"))
    base_mode = WB_BASE_HIJACKED;
  else if (p && ! grub_strcmp (p, "bedrock"))
    base_mode = WB_BASE_BEDROCK;
  else
    base_mode = WB_BASE_LAST;

  p = grub_env_get ("wartburg_bedrock_hijacked");
  hijacked_name = p ? grub_strdup (p) : 0;
  p = grub_env_get ("wartburg_bedrock_last");
  last_name = p ? grub_strdup (p) : 0;
}

int
grub_wartburg_bedrock_active (void)
{
  const char *p = grub_env_get ("wartburg_bedrock");

  return (p && p[0] == '1' && n_strata >= 2 && icon_dir != 0);
}

int
grub_wartburg_bedrock_count (void)
{
  return n_strata;
}

const struct wb_stratum *
grub_wartburg_bedrock_get (int i)
{
  if (i < 0 || i >= n_strata)
    return 0;
  return &strata[i];
}

const char *
grub_wartburg_bedrock_icon_dir (void)
{
  return icon_dir;
}

int
grub_wartburg_bedrock_mode (void)
{
  return base_mode;
}

/* Index of the stratum whose name is NAME, or -1. */
static int
stratum_by_name (const char *name)
{
  int i;

  if (! name)
    return -1;
  for (i = 0; i < n_strata; i++)
    if (strata[i].name && ! grub_strcmp (strata[i].name, name))
      return i;
  return -1;
}

int
grub_wartburg_bedrock_base_index (void)
{
  int idx;

  if (base_mode == WB_BASE_BEDROCK)
    return -1;

  if (base_mode == WB_BASE_HIJACKED)
    {
      idx = stratum_by_name (hijacked_name);
      return (idx >= 0) ? idx : 0;
    }

  /* WB_BASE_LAST: last-booted, else hijacked, else the first stratum. */
  idx = stratum_by_name (last_name);
  if (idx < 0)
    idx = stratum_by_name (hijacked_name);
  return (idx >= 0) ? idx : 0;
}

const char *
grub_wartburg_bedrock_base_class (void)
{
  int bi;

  if (base_mode == WB_BASE_BEDROCK)
    return "bedrock";
  bi = grub_wartburg_bedrock_base_index ();
  return (bi >= 0) ? strata[bi].iconclass : "bedrock";
}

void
grub_wartburg_bedrock_cycle_base (void)
{
  base_mode = (base_mode + 1) % 3;
  grub_env_set ("wartburg_bedrock_base",
		(base_mode == WB_BASE_HIJACKED) ? "hijacked" :
		(base_mode == WB_BASE_BEDROCK) ? "bedrock" : "last");
}

void
grub_wartburg_bedrock_set_last (const char *stratum)
{
  grub_command_t cmd;

  if (! stratum || ! *stratum)
    return;

  grub_env_set ("wartburg_bedrock_last", stratum);

  /* Persist to $prefix/grubenv via the loadenv module's `save_env`. */
  cmd = grub_command_find ("save_env");
  if (cmd)
    {
      char *argv[] = { (char *) "wartburg_bedrock_last", 0 };
      (cmd->func) (cmd, 1, argv);
      grub_errno = GRUB_ERR_NONE;
    }
}

/* Stamp NODE's command/index/users/stratum from the stratum at model index SI
   (selected sub-icon), so the generic Enter path boots it and records it. */
void
grub_wartburg_bedrock_stamp_node (grub_uitree_t node, int si)
{
  const struct wb_stratum *st = grub_wartburg_bedrock_get (si);
  char buf[12];

  if (! node || ! st)
    return;

  grub_uitree_set_prop (node, "command", st->command ? st->command : "true");
  grub_snprintf (buf, sizeof (buf), "%d", st->index);
  grub_uitree_set_prop (node, "index", buf);
  grub_uitree_set_prop (node, "stratum", st->name ? st->name : "");
  if (st->users)
    grub_uitree_set_prop (node, "users", st->users);
}

void
grub_wartburg_add_bedrock (grub_uitree_t menu_node)
{
  grub_uitree_t tmpl, b;
  char *p;
  int bi;

  if (! menu_node || n_strata < 1)
    return;

  b = grub_uitree_create_node ("bedrock");
  if (! b)
    return;

  /* The composite must be a DIRECT child of __menu__ (like the per-entry
     panels) so the input dispatch's directional navigation -- which keys off
     node->parent's layout direction -- treats Left/Right as horizontal item
     moves and lets the selection cross into the neighbouring entries. Inherit
     the menu item's cell geometry from template_menuitem's panel so it sizes
     like a normal item. */
  tmpl = grub_dialog_create ("template_menuitem", 1, 0, 0, 0);
  if (tmpl)
    {
      if ((p = grub_uitree_get_prop (tmpl, "width")))
	grub_uitree_set_prop (b, "width", p);
      if ((p = grub_uitree_get_prop (tmpl, "height")))
	grub_uitree_set_prop (b, "height", p);
      grub_uitree_free (tmpl);
    }

  /* Initial boot target = the base stratum (or the first when base = logo). */
  bi = grub_wartburg_bedrock_base_index ();
  if (bi < 0)
    bi = 0;
  grub_wartburg_bedrock_stamp_node (b, bi);

  grub_tree_add_child (GRUB_AS_TREE (menu_node), GRUB_AS_TREE (b), -1);
}
