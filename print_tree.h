/* print_tree.h - a package to prettyprint trees in ascii
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: print_tree.h,v 1.6 2012-02-22 18:55:25 greg Exp $
 */
#ifndef PRINT_TREE_H
#define PRINT_TREE_H

#include <stdio.h>

/* return the number of children the node has. */
typedef int print_tree_child_count_fn(void *);

/* return the i'th child of the node (0 <= i < child_count()) */
typedef void *print_tree_child_fn(void *, int i);

/* return a malloc'd version of this node's name; print_tree guarantees to
 * free the returned char string.
 */
typedef char *print_tree_name_fn(void *);

/* we can operate more efficiently if we can save some private state with
 * each node on the caller side.  this optional function lets the caller
 * give us a per-node pointer for our use.
 * we malloc a struct for each tree node and save per-node info there,
 * and then free the structs at the end.
 */
typedef void **print_tree_memo_fn(void *);

/* an optional method to print the tree.  if null, "fprintf" will be used.
 */
typedef int print_tree_print_fn(FILE *f, const char *msg, ...);

typedef struct {
    print_tree_child_fn *child;
    print_tree_child_count_fn *child_count;
    print_tree_name_fn *name;
    print_tree_memo_fn *memo;
    print_tree_print_fn *printer;
} print_tree_fns_t;

/* pretty-print an ascii version of the tree starting at "root".
 * traverse the tree using functions supplied in the function-pointer
 * struct "fns", where the three caller-supplied functions are described
 * in the typedef's above.
 */
void print_tree(FILE *f, void *root, print_tree_fns_t *fns);

#endif
