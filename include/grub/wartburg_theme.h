/* wartburg_theme.h - BURG theme parser, ported into WartBURG.
 *
 * Ports bean's BURG (GPLv3) generic UI tree + theme-file parser:
 *   - grub_tree_*    (from BURG lib/tree.c, include/grub/tree.h)
 *   - grub_uitree_*  (from BURG lib/uitree.c, include/grub/uitree.h)
 * Parser logic is verbatim; only platform glue is adapted to GRUB 2.15.
 * Copyright 2009 Bean Lee; GPLv3+.
 */

#ifndef GRUB_WARTBURG_THEME_HEADER
#define GRUB_WARTBURG_THEME_HEADER 1

#include <grub/err.h>
#include <grub/list.h>

/* --- generic intrusive tree (BURG include/grub/tree.h) --- */
struct grub_tree
{
  struct grub_tree *parent;
  struct grub_tree *child;
  struct grub_tree *next;
};
typedef struct grub_tree *grub_tree_t;

void grub_tree_add_sibling (grub_tree_t pre, grub_tree_t cur);
void grub_tree_add_child (grub_tree_t parent, grub_tree_t cur, int index);
void grub_tree_remove_node (grub_tree_t cur);
void *grub_tree_next_node (grub_tree_t root, grub_tree_t pre);

#define GRUB_AS_TREE(ptr) \
  ((GRUB_FIELD_MATCH (ptr, grub_tree_t, parent) && \
    GRUB_FIELD_MATCH (ptr, grub_tree_t, child) && \
    GRUB_FIELD_MATCH (ptr, grub_tree_t, next)) ? \
   (grub_tree_t) ptr : (grub_tree_t) grub_bad_type_cast ())

/* --- UI node tree + theme parser (BURG include/grub/uitree.h) --- */
struct grub_uiprop
{
  struct grub_uiprop *next;
  char *value;
  char name[0];
};
typedef struct grub_uiprop *grub_uiprop_t;

struct grub_uitree
{
  struct grub_uitree *parent;
  struct grub_uitree *child;
  struct grub_uitree *next;
  struct grub_uiprop *prop;
  void *data;
  int flags;
  char name[0];
};
typedef struct grub_uitree *grub_uitree_t;

extern struct grub_uitree grub_uitree_root;

#define GRUB_UITREE_LOAD_FLAG_SINGLE	1
#define GRUB_UITREE_LOAD_FLAG_ROOT	2

void grub_uitree_dump (grub_uitree_t node);
grub_uitree_t grub_uitree_find (grub_uitree_t node, const char *name);
grub_uitree_t grub_uitree_find_id (grub_uitree_t node, const char *name);
grub_uitree_t grub_uitree_create_node (const char *name);
grub_uitree_t grub_uitree_load_string (grub_uitree_t root, char *buf, int flags);
grub_uitree_t grub_uitree_load_file (grub_uitree_t root, const char *name,
				     int flags);
grub_uitree_t grub_uitree_clone (grub_uitree_t node);
void grub_uitree_free (grub_uitree_t node);
grub_err_t grub_uitree_set_prop (grub_uitree_t node, const char *name,
				 const char *value);
char *grub_uitree_get_prop (grub_uitree_t node, const char *name);

#endif /* GRUB_WARTBURG_THEME_HEADER */
