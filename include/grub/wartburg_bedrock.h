/* wartburg_bedrock.h - Bedrock Linux awareness for WartBURG.
 *
 * Groups the stratum boot entries of a Bedrock host into one composite menu
 * item: a large "base" icon (the last-booted stratum, the hijacked/"injected"
 * stratum, or the Bedrock logo) with the remaining strata drawn as smaller,
 * individually selectable icons. The base is chosen by a config default and a
 * live runtime toggle. Additive; no GRUB core file is patched.
 */

#ifndef GRUB_WARTBURG_BEDROCK_HEADER
#define GRUB_WARTBURG_BEDROCK_HEADER 1

#include <grub/menu.h>
#include <grub/wartburg_theme.h>

#define WB_BEDROCK_MAX		16

/* Base-icon modes (which stratum/logo is drawn large). */
#define WB_BASE_LAST		0	/* last-booted stratum (grubenv)        */
#define WB_BASE_HIJACKED	1	/* the hijacked ("injected") stratum    */
#define WB_BASE_BEDROCK		2	/* the Bedrock logo (all strata below)  */

struct wb_stratum
{
  char *name;		/* stratum name: arch / debian / fedora / ...   */
  char *iconclass;	/* icon-class for large_/small_/grey_ lookup    */
  char *command;	/* the entry's sourcecode (boots this stratum)  */
  char *users;		/* restricted-entry userlist, or 0              */
  int index;		/* position in the GRUB menu (for `chosen`)     */
};

/* True if menu entry E is tagged as a Bedrock stratum (carries `--class
   bedrock`). */
int grub_wartburg_bedrock_is_stratum (grub_menu_entry_t e);

/* (Re)build the stratum model from MENU; derive the icon dir from THEME_PATH
   (.../themes/<name>/theme -> .../themes/icons); read prefs from the env. */
void grub_wartburg_bedrock_build (grub_menu_t menu, const char *theme_path);
void grub_wartburg_bedrock_free (void);

/* Bedrock mode is on (env wartburg_bedrock=1) and at least two strata exist. */
int grub_wartburg_bedrock_active (void);

int grub_wartburg_bedrock_count (void);
const struct wb_stratum *grub_wartburg_bedrock_get (int i);
const char *grub_wartburg_bedrock_icon_dir (void);

/* Current base mode + the stratum index it resolves to (-1 = Bedrock logo). */
int grub_wartburg_bedrock_mode (void);
int grub_wartburg_bedrock_base_index (void);
/* Icon-class for the large base icon: "bedrock" or a stratum's iconclass. */
const char *grub_wartburg_bedrock_base_class (void);

/* Cycle base mode (last -> hijacked -> bedrock -> ...), for the runtime toggle. */
void grub_wartburg_bedrock_cycle_base (void);

/* Persist the last-booted stratum to grubenv (wartburg_bedrock_last). */
void grub_wartburg_bedrock_set_last (const char *stratum);

/* Stamp NODE's command/index/users/stratum props from stratum model index SI
   (the currently selected sub-icon) so the generic Enter path boots/records it. */
void grub_wartburg_bedrock_stamp_node (grub_uitree_t node, int si);

/* Append the composite Bedrock item (a `bedrock` widget) to MENU_NODE. */
void grub_wartburg_add_bedrock (grub_uitree_t menu_node);

#endif /* GRUB_WARTBURG_BEDROCK_HEADER */
