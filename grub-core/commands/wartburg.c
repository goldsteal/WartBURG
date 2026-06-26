/* wartburg.c - WartBURG: a reversible BURG-theme menu for GRUB.
 *
 *  WartBURG is a self-contained, removable GRUB addon. It claims GRUB's single
 *  graphical-menu hook (grub_gfxmenu_try_hook) with save/restore so that
 *  `rmmod wartburg` cleanly reverts to whatever menu was active before. It
 *  renders unmodified BURG themes via a ported BURG engine (parser + layout +
 *  components) drawing through GRUB's exported low-level primitives, and runs
 *  its own input loop (arrows + vim hjkl) booting via grub_script_execute_-
 *  sourcecode with per-entry auth. It patches no GRUB core file.
 */

#include <grub/types.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/env.h>
#include <grub/command.h>
#include <grub/dl.h>
#include <grub/err.h>
#include <grub/file.h>
#include <grub/video.h>
#include <grub/menu.h>
#include <grub/menu_viewer.h>
#include <grub/normal.h>
#include <grub/wartburg_theme.h>
#include <grub/wartburg_widget.h>
#include <grub/wartburg_bedrock.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_command_t cmd;
static grub_command_t cmd_parse;
static grub_command_t cmd_render;
static grub_command_t cmd_switch;
static grub_command_t cmd_discover;
static grub_command_t cmd_bootonce;
static int wb_ui_registered;

/* Saved predecessor so the hook is fully reversible (restored in MOD_FINI).  */
static grub_err_t (*wb_prev_try_hook) (int entry, grub_menu_t menu, int nested);

static grub_err_t wartburg_try (int entry, grub_menu_t menu, int nested);

/* Sniff a theme file: GRUB2 gfxmenu themes always declare a `boot_menu`
   component, which BURG themes never do (they use `screen`/`__menu__`). Used to
   decide whether WartBURG renders the theme or hands it to stock gfxmenu.  */
static int
wb_is_grub2_theme (const char *path)
{
  grub_file_t file;
  char *buf;
  grub_off_t sz;
  int grub2 = 0;

  file = grub_file_open (path, GRUB_FILE_TYPE_THEME);
  if (! file)
    {
      grub_errno = GRUB_ERR_NONE;
      return 0;
    }
  sz = grub_file_size (file);
  buf = grub_malloc (sz + 1);
  if (buf)
    {
      if (grub_file_read (file, buf, sz) == (grub_ssize_t) sz)
	{
	  buf[sz] = '\0';
	  if (grub_strstr (buf, "boot_menu"))
	    grub2 = 1;
	}
      grub_free (buf);
    }
  grub_file_close (file);
  grub_errno = GRUB_ERR_NONE;
  return grub2;
}

/* Hand a GRUB2 theme to stock gfxmenu via the saved predecessor hook. Robust to
   load order: if gfxmenu wasn't loaded before us, load it now, capture its hook,
   and re-assert ourselves as the active hook so we keep dispatching.  */
static grub_err_t
wb_delegate_gfxmenu (int entry, grub_menu_t menu, int nested)
{
  grub_dprintf ("wartburg",
		"GRUB2 theme.txt detected -> delegating to stock gfxmenu\n");
  if (! wb_prev_try_hook)
    {
      grub_dl_load ("gfxmenu");
      grub_errno = GRUB_ERR_NONE;
      if (grub_gfxmenu_try_hook && grub_gfxmenu_try_hook != wartburg_try)
	wb_prev_try_hook = grub_gfxmenu_try_hook;
      grub_gfxmenu_try_hook = wartburg_try;
    }

  if (! wb_prev_try_hook)
    return grub_error (GRUB_ERR_BAD_MODULE,
		       "WartBURG: gfxmenu not available for GRUB2 theme");

  return wb_prev_try_hook (entry, menu, nested);
}

/* Resolve the `theme` env value to a theme-file path: an absolute/device path
   is used as-is, a bare name maps to /boot/burg/themes/<name>/theme.  */
static char *
wb_theme_path (const char *theme)
{
  if (grub_strchr (theme, '/') || grub_strchr (theme, '('))
    return grub_strdup (theme);
  return grub_xasprintf ("/boot/burg/themes/%s/theme", theme);
}

/* Derive the theme's shared font directory from its theme-file path:
   .../themes/<name>/theme -> .../themes/fonts (where BURG keeps font.lst +
   the .pf2s). Returns NULL if the path is too shallow (auto-load disabled).  */
static char *
wb_font_dir_for (const char *theme_path)
{
  const char *p, *slash1 = 0, *slash2 = 0;
  char *dir, *out;
  grub_size_t len;

  for (p = theme_path; *p; p++)
    if (*p == '/')
      {
	slash2 = slash1;
	slash1 = p;
      }
  if (! slash2)
    return 0;
  /* GRUB printf has no "%.*s", so cut the substring by hand. */
  len = slash2 - theme_path;
  dir = grub_malloc (len + 1);
  if (! dir)
    return 0;
  grub_memcpy (dir, theme_path, len);
  dir[len] = '\0';
  out = grub_xasprintf ("%s/fonts", dir);
  grub_free (dir);
  return out;
}

/* Id of the synthetic "Switch theme" entry WartBURG injects into the live menu
   so a delegated GRUB 2 theme (driven by stock run_menu) still has an F2 way
   back. WartBURG renders its own F2 for BURG themes, so this entry is skipped
   when building a BURG menu. */
#define WB_SWITCH_ID "wartburg_switch_theme"

static int
wb_is_switch_entry (grub_menu_entry_t e)
{
  return e->id && ! grub_strcmp (e->id, WB_SWITCH_ID);
}

static void
wb_populate_menu (grub_uitree_t screen, grub_menu_t menu)
{
  grub_uitree_t menunode;
  grub_menu_entry_t e;
  int i;

  menunode = grub_uitree_find_id (screen, "__menu__");
  if (grub_wartburg_bedrock_active ())
    {
      int added = 0;
      for (i = 0, e = menu->entry_list; e; e = e->next, i++)
	{
	  if (wb_is_switch_entry (e))
	    continue;
	  if (grub_wartburg_bedrock_is_stratum (e))
	    {
	      if (! added)
		{
		  grub_wartburg_add_bedrock (menunode);
		  added = 1;
		}
	    }
	  else
	    grub_wartburg_add_entry (menunode, e, i);
	}
    }
  else
    for (i = 0, e = menu->entry_list; e; e = e->next, i++)
      {
	if (wb_is_switch_entry (e))
	  continue;
	grub_wartburg_add_entry (menunode, e, i);
      }
}

/* Inject the "Switch theme" entry (hotkey F2) into the live menu once, so a
   delegated GRUB 2 theme has a visible, keyboard-driven way to cycle back to a
   BURG theme. Idempotent: skipped if already present. */
static void
wb_inject_switch_entry (grub_menu_t menu)
{
  grub_menu_entry_t e;
  const char *args[] = { "\xe2\x86\xbb Switch theme", 0 };  /* U+21BB */
  char *classes[] = { (char *) "theme", 0 };

  for (e = menu->entry_list; e; e = e->next)
    if (wb_is_switch_entry (e))
      return;

  grub_normal_add_menu_entry (1, args, classes, WB_SWITCH_ID, 0, "f2", 0,
			      "wartburg_switch_theme\n", 0, 0);
  grub_errno = GRUB_ERR_NONE;
}

static grub_err_t
wb_load_theme_screen (grub_menu_t menu, char **path_out, grub_uitree_t *screen_out)
{
  const char *theme;
  char *path;
  grub_uitree_t screen;

  *path_out = 0;
  *screen_out = 0;

  theme = grub_env_get ("theme");
  if (! theme || ! *theme)
    return grub_error (GRUB_ERR_FILE_NOT_FOUND, "WartBURG: variable `theme' unset");

  path = wb_theme_path (theme);
  if (! path)
    return grub_errno;

  if (wb_is_grub2_theme (path))
    {
      grub_free (path);
      return GRUB_ERR_BAD_FILE_TYPE;
    }

  {
    char *fd = wb_font_dir_for (path);
    grub_menu_region_set_font_dir (fd);
    grub_free (fd);
  }

  grub_uitree_reset (&grub_uitree_root);
  grub_uitree_load_file (&grub_uitree_root, path, GRUB_UITREE_LOAD_FLAG_ROOT);
  if (! grub_errno)
    grub_wartburg_bedrock_build (menu, path);
  if (grub_errno)
    {
      grub_free (path);
      return grub_errno;
    }

  screen = grub_uitree_find (&grub_uitree_root, "screen");
  if (! screen)
    {
      grub_free (path);
      return GRUB_ERR_BAD_FILE_TYPE;
    }

  *path_out = path;
  *screen_out = screen;
  return GRUB_ERR_NONE;
}

static void
wb_persist_theme_if_requested (void)
{
  const char *persist;
  grub_command_t save_cmd;

  persist = grub_env_get ("wartburg_theme_persist");
  if (! persist || ! *persist)
    return;

  grub_env_unset ("wartburg_theme_persist");
  save_cmd = grub_command_find ("save_env");
  if (save_cmd)
    {
      char *argv[] = { (char *) "theme", 0 };
      (save_cmd->func) (save_cmd, 1, argv);
      grub_errno = GRUB_ERR_NONE;
    }
}

static void
wb_persist_env_if_requested (const char *request_var, const char *save_var)
{
  const char *persist;
  grub_command_t save_cmd;

  persist = grub_env_get (request_var);
  if (! persist || ! *persist)
    return;

  grub_env_unset (request_var);
  save_cmd = grub_command_find ("save_env");
  if (save_cmd)
    {
      char *argv[] = { (char *) save_var, 0 };
      (save_cmd->func) (save_cmd, 1, argv);
      grub_errno = GRUB_ERR_NONE;
    }
}

static void
wb_apply_gfxmode_if_requested (void)
{
  const char *persist;

  persist = grub_env_get ("wartburg_gfxmode_persist");
  if (! persist || ! *persist)
    return;
  grub_env_set ("wartburg_gfxmode_apply", "1");
  grub_errno = GRUB_ERR_NONE;
}

/* The graphical-menu hook: render the BURG theme and run the interactive menu.
   It owns the input loop (returns only on failure, to fall back to the text
   menu); a successful boot transfers control away.  */
static grub_err_t
wartburg_try (int entry, grub_menu_t menu, int nested)
{
  const char *theme;
  char *path;
  grub_uitree_t screen;
  int entry_cur;
  grub_err_t err;

  theme = grub_env_get ("theme");
  if (! theme || ! *theme)
    return grub_error (GRUB_ERR_FILE_NOT_FOUND, "WartBURG: variable `theme' unset");

  /* Give every menu a keyboard-driven theme switcher. WartBURG's own loop binds
     F2 for BURG themes; this injected entry carries F2 into a delegated GRUB 2
     theme (driven by stock run_menu), where our loop never runs. */
  wb_inject_switch_entry (menu);

  path = wb_theme_path (theme);
  if (! path)
    return grub_errno;

  /* GRUB2 gfxmenu theme? Hand it to stock gfxmenu, untouched. */
  if (wb_is_grub2_theme (path))
    {
      grub_free (path);
      return wb_delegate_gfxmenu (entry, menu, nested);
    }
  grub_dprintf ("wartburg", "BURG theme detected -> rendering with WartBURG\n");
  grub_free (path);

  if (! wb_ui_registered)
    {
      grub_wartburg_ui_init ();
      wb_ui_registered = 1;
    }

  entry_cur = entry;
  while (1)
    {
      int r;
      const char *saved;

      err = grub_menu_region_gfx_init ();
      if (err)
	return err;

      err = wb_load_theme_screen (menu, &path, &screen);
      if (err == GRUB_ERR_BAD_FILE_TYPE)
	return wb_delegate_gfxmenu (entry, menu, nested);
      if (err)
	return err;
      wb_persist_theme_if_requested ();
      wb_persist_env_if_requested ("wartburg_gfxmode_persist", "gfxmode");
      grub_free (path);

      grub_widget_screen = screen;
      wb_populate_menu (screen, menu);

      err = grub_widget_create (screen);
      if (err)
	return err;
      grub_widget_init (screen);

      saved = grub_env_get ("wartburg_selected");
      if (saved && *saved)
	entry_cur = grub_strtoul (saved, 0, 0);

      grub_widget_refresh = 0;
      r = grub_wartburg_run (screen, entry_cur);
      if (grub_widget_current_node)
	{
	  char *idx = grub_uitree_get_prop (grub_widget_current_node, "index");
	  if (idx)
	    entry_cur = grub_strtoul (idx, 0, 0);
	}
      grub_widget_free (screen);
      grub_wartburg_bedrock_free ();

      if (grub_widget_refresh == GRUB_WIDGET_RELOAD_MODE)
	{
	  grub_widget_refresh = 0;
	  grub_errno = GRUB_ERR_NONE;
	  wb_apply_gfxmode_if_requested ();
	  continue;
	}

      grub_widget_refresh = 0;
      return (r == WB_MENU_ESCAPE) ? GRUB_ERR_NONE : grub_errno;
    }
}

static grub_err_t
grub_cmd_wartburg (grub_command_t command __attribute__ ((unused)),
		   int argc __attribute__ ((unused)),
		   char **argv __attribute__ ((unused)))
{
  grub_printf ("WartBURG active.\n");
  return GRUB_ERR_NONE;
}

/* wbparse <theme-file>: parse a BURG theme and dump the node tree. */
static grub_err_t
grub_cmd_wbparse (grub_command_t command __attribute__ ((unused)),
		  int argc, char **argv)
{
  grub_uitree_t root;

  if (argc < 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "usage: wbparse <theme-file>");

  root = grub_uitree_create_node ("root");
  if (!root)
    return grub_errno;

  grub_uitree_load_file (root, argv[0], 0);
  if (grub_errno)
    {
      grub_uitree_free (root);
      return grub_errno;
    }

  grub_printf ("=== WartBURG parsed theme: %s ===\n", argv[0]);
  grub_uitree_dump (root);
  grub_printf ("=== end theme dump ===\n");
  grub_uitree_free (root);
  return GRUB_ERR_NONE;
}

/* wbrender <theme-file>: parse a theme and render its `screen` statically with
   sample menu entries (no input loop) -- a non-interactive render test. */
static grub_err_t
grub_cmd_wbrender (grub_command_t command __attribute__ ((unused)),
		   int argc, char **argv)
{
  grub_uitree_t screen;
  grub_err_t err;

  if (argc < 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "usage: wbrender <theme-file>");

  err = grub_menu_region_gfx_init ();
  if (err)
    return err;

  if (! wb_ui_registered)
    {
      grub_wartburg_ui_init ();
      wb_ui_registered = 1;
    }

  {
    char *fd = wb_font_dir_for (argv[0]);
    grub_menu_region_set_font_dir (fd);
    grub_free (fd);
  }

  grub_uitree_load_file (&grub_uitree_root, argv[0], GRUB_UITREE_LOAD_FLAG_ROOT);
  if (grub_errno)
    return grub_errno;

  screen = grub_uitree_find (&grub_uitree_root, "screen");
  if (! screen)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "theme has no `screen' section");
  grub_widget_screen = screen;

  {
    grub_uitree_t menunode = grub_uitree_find_id (screen, "__menu__");
    grub_wartburg_add_item (menunode, "Arch Linux", "arch", "true", 0);
    grub_wartburg_add_item (menunode, "Ubuntu", "ubuntu", "true", 1);
    grub_wartburg_add_item (menunode, "Windows 11", "windows", "true", 2);
    grub_wartburg_add_item (menunode, "Fedora", "fedora", "true", 3);
    grub_wartburg_add_item (menunode, "Debian", "debian", "true", 4);
  }

  err = grub_widget_create (screen);
  if (err)
    return err;

  grub_widget_init (screen);
  grub_widget_draw (screen);
  grub_video_swap_buffers ();

  return GRUB_ERR_NONE;
}

/* wartburg_switch_theme: advance `theme' to the next entry in $wartburg_themes.
   Backs the injected F2 "Switch theme" menu entry so a delegated GRUB 2 theme
   (driven by stock run_menu) can cycle back to a BURG theme: selecting it sets
   `theme' and returns without booting, so show_menu re-invokes the hook and
   WartBURG re-dispatches on the new theme. */
static grub_err_t
grub_cmd_switch_theme (grub_command_t command __attribute__ ((unused)),
		       int argc __attribute__ ((unused)),
		       char **argv __attribute__ ((unused)))
{
  grub_wartburg_advance_theme ();
  return GRUB_ERR_NONE;
}

/* wartburg_discover: scan ESPs for OS boot loaders and add chainloader menu
   entries (zero-config, rEFInd-style). Run from grub.cfg before the menu. */
static grub_err_t
grub_cmd_discover (grub_command_t command __attribute__ ((unused)),
		   int argc __attribute__ ((unused)),
		   char **argv __attribute__ ((unused)))
{
  grub_wartburg_discover ();
  return GRUB_ERR_NONE;
}

/* wartburg_bootonce_pick: open the boot-once picker (same as the F4 hotkey).
   Bound to the injected "Boot Once…" menu row so it works from the menu too. */
static grub_err_t
grub_cmd_bootonce_pick (grub_command_t command __attribute__ ((unused)),
			int argc __attribute__ ((unused)),
			char **argv __attribute__ ((unused)))
{
  grub_wartburg_bootonce_pick ();
  return GRUB_ERR_NONE;
}

GRUB_MOD_INIT (wartburg)
{
  grub_printf ("\n=== WartBURG Initialized ===\n");

  cmd = grub_register_command ("wartburg", grub_cmd_wartburg, 0,
			       "Activate WartBURG.");
  cmd_parse = grub_register_command ("wbparse", grub_cmd_wbparse,
				     "FILE", "Parse a BURG theme and dump it.");
  cmd_render = grub_register_command ("wbrender", grub_cmd_wbrender,
				      "FILE", "Render a BURG theme statically.");
  cmd_switch = grub_register_command ("wartburg_switch_theme",
				      grub_cmd_switch_theme, 0,
				      "Cycle to the next theme in $wartburg_themes.");
  cmd_discover = grub_register_command ("wartburg_discover", grub_cmd_discover,
					0, "Scan ESPs and add OS chainloader entries.");
  cmd_bootonce = grub_register_command ("wartburg_bootonce_pick",
					grub_cmd_bootonce_pick, 0,
					"Open the boot-once (next-boot) picker.");

  /* Reversibly claim the graphical-menu hook. */
  wb_prev_try_hook = grub_gfxmenu_try_hook;
  grub_gfxmenu_try_hook = wartburg_try;
}

GRUB_MOD_FINI (wartburg)
{
  /* Restore whatever menu was active before us. */
  grub_gfxmenu_try_hook = wb_prev_try_hook;
  if (wb_ui_registered)
    grub_wartburg_ui_fini ();
  grub_unregister_command (cmd_bootonce);
  grub_unregister_command (cmd_discover);
  grub_unregister_command (cmd_switch);
  grub_unregister_command (cmd_render);
  grub_unregister_command (cmd_parse);
  grub_unregister_command (cmd);
}
