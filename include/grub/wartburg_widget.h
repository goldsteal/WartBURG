/* wartburg_widget.h - BURG widget (layout/draw) layer, ported.
 *
 * Adapts bean's BURG include/grub/widget.h to GRUB 2.15: class registration is
 * a manual singly-linked push/remove (the class struct is next-only, and 2.15's
 * grub_list is doubly-linked). Original copyright 2009 Bean Lee; GPLv3+.
 */

#ifndef GRUB_WARTBURG_WIDGET_HEADER
#define GRUB_WARTBURG_WIDGET_HEADER 1

#include <grub/err.h>
#include <grub/menu.h>
#include <grub/wartburg_theme.h>
#include <grub/wartburg_region.h>

#define GRUB_WIDGET_FLAG_TRANSPARENT	1
#define GRUB_WIDGET_FLAG_EXTEND		2
#define GRUB_WIDGET_FLAG_FIXED_X	4
#define GRUB_WIDGET_FLAG_FIXED_Y	8
#define GRUB_WIDGET_FLAG_FIXED_WIDTH	16
#define GRUB_WIDGET_FLAG_FIXED_HEIGHT	32
#define GRUB_WIDGET_FLAG_ANCHOR		64
#define GRUB_WIDGET_FLAG_NODE		128
#define GRUB_WIDGET_FLAG_ROOT		256
#define GRUB_WIDGET_FLAG_SELECTED	512
#define GRUB_WIDGET_FLAG_MARKED		1024
#define GRUB_WIDGET_FLAG_HIDDEN		2048
#define GRUB_WIDGET_FLAG_DYNAMIC	4096

#define GRUB_WIDGET_FLAG_FIXED_XY	\
  (GRUB_WIDGET_FLAG_FIXED_X | GRUB_WIDGET_FLAG_FIXED_Y)

#define GRUB_WIDGET_RESULT_DONE		-1
#define GRUB_WIDGET_RESULT_SKIP		-2

/* Sentinel returned by grub_widget_input to pop out of a nested dialog/submenu
   loop (replaces BURG's removed GRUB_ERR_MENU_ESCAPE). Distinct from grub_err_t
   values and the negative GRUB_WIDGET_RESULT_* codes.  */
#define WB_MENU_ESCAPE			0x10000

struct grub_widget
{
  struct grub_widget_class *class;
  struct grub_uitree *node;
  void *data;
  int org_x;
  int org_y;
  int x;
  int y;
  int width;
  int height;
  int inner_x;
  int inner_y;
  int inner_width;
  int inner_height;
};
typedef struct grub_widget *grub_widget_t;

struct grub_widget_class
{
  struct grub_widget_class *next;
  const char *name;
  int (*get_data_size) (void);
  void (*init_size) (grub_widget_t widget);
  void (*fini_size) (grub_widget_t widget);
  void (*free) (grub_widget_t widget);
  void (*draw) (grub_widget_t widget, grub_menu_region_update_list_t *head,
		int x, int y, int width, int height);
  void (*draw_cursor) (grub_widget_t widget);
  int (*onkey) (grub_widget_t widget, int key);
  void (*set_timeout) (grub_widget_t widget, int total, int left);
};
typedef struct grub_widget_class *grub_widget_class_t;

extern grub_widget_class_t grub_widget_class_list;

/* Manual singly-linked registration (class struct is next-only). */
static inline void
grub_widget_class_register (grub_widget_class_t class)
{
  class->next = grub_widget_class_list;
  grub_widget_class_list = class;
}

static inline void
grub_widget_class_unregister (grub_widget_class_t class)
{
  grub_widget_class_t *p = &grub_widget_class_list;
  while (*p && *p != class)
    p = &(*p)->next;
  if (*p)
    *p = class->next;
}

#define GRUB_WIDGET_REFRESH		1
#define GRUB_WIDGET_TOGGLE_MODE		2
#define GRUB_WIDGET_RELOAD_MODE		3

extern grub_uitree_t grub_widget_current_node;
extern grub_uitree_t grub_widget_screen;
extern int grub_widget_refresh;

/* Optional overlay drawn by grub_widget_draw into each (double-buffered) pass,
   just before present -- used for the type-to-search query box. NULL = none. */
extern void (*grub_wb_overlay_hook) (void);

/* Type-to-search overlay state (wartburg_region.c): q != NULL shows
   "Search: q_" and installs the overlay hook; q == NULL clears both. */
void grub_wartburg_search_set (const char *q);

grub_err_t grub_widget_create (grub_uitree_t node);
void grub_widget_init (grub_uitree_t node);
void grub_widget_free (grub_uitree_t node);
void grub_widget_draw (grub_uitree_t node);
void grub_widget_draw_region (grub_menu_region_update_list_t *head,
			      grub_uitree_t node, int x, int y,
			      int width, int height);
/* Scroll a (selected) node into view inside its scrollable ancestors; returns
   the topmost node that was scrolled (or NODE if nothing scrolled). */
grub_uitree_t grub_widget_scroll (grub_uitree_t node);

/* Resolve a container's layout direction (horizontal / reverse) from its
   `direction` prop -- used by the input dispatch's directional navigation. */
void grub_widget_get_direction (grub_uitree_t node, int *horizontal,
				int *reverse);

/* Defined in M3 (input/selection layer): */
void grub_widget_select_node (grub_uitree_t node, int selected);
int grub_widget_input (grub_uitree_t root, int nested);
char *grub_widget_get_prop (grub_uitree_t node, const char *name);

/* Register/unregister the ported component widget classes (wartburg_ui.c). */
void grub_wartburg_ui_init (void);
void grub_wartburg_ui_fini (void);

/* Dialog template/parameter machinery + menu population (wartburg_menu.c). */
grub_uitree_t grub_dialog_create (const char *name, int copy, int index,
				  grub_uitree_t *menu, grub_uitree_t *save);
int grub_dialog_set_parm (grub_uitree_t node, char *parm, char *name,
			  const char *value);
char *grub_dialog_get_parm (grub_uitree_t node, char *parm, char *name);
grub_err_t grub_dialog_popup (grub_uitree_t node);
void grub_dialog_free (grub_uitree_t node, grub_uitree_t menu,
		       grub_uitree_t save);
void grub_dialog_message (const char *text);
void grub_wartburg_add_item (grub_uitree_t menu_node, const char *title,
			     const char *iconclass, const char *command,
			     int index);

/* M3 interactive menu (wartburg_input.c). */
void grub_wartburg_add_entry (grub_uitree_t menu_node, grub_menu_entry_t entry,
			      int index);
int grub_wartburg_run (grub_uitree_t root, int default_num);

/* Advance `theme' to the next entry in $wartburg_themes (env-only); shared by
   the F2 switcher and the injected switch-theme menu entry (wartburg_input.c). */
int grub_wartburg_advance_theme (void);

/* Zero-config EFI OS discovery: scan FAT/ESP partitions for known boot loaders
   and add chainloader menu entries. Returns count added (wartburg_discover.c). */
int grub_wartburg_discover (void);

/* M1.3: Inject a "UEFI Firmware Setup" entry (fwsetup) if the running firmware
   advertises BOOT_TO_FW_UI support in OsIndicationsSupported. No-op otherwise.
   (wartburg_discover.c) */
void grub_wartburg_add_firmware_entry (void);

/* M1.3: Inject a "Boot Once…" picker entry if $wartburg_bootonce is set.
   (wartburg_discover.c) */
void grub_wartburg_add_bootonce_entry (void);

#endif /* GRUB_WARTBURG_WIDGET_HEADER */
