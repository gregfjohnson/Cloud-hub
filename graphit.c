/* graphit.c - ASCII prettyprint the STP tree connecting the cloud boxes
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: graphit.c,v 1.12 2012-02-22 19:27:22 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: graphit.c,v 1.12 2012-02-22 19:27:22 greg Exp $";
#include <stdio.h>
#include <string.h>

#include "util.h"
#include "graphit.h"
#include "print_tree.h"

/* return the number of children if this node */
static int graphit_child_count_fn(void *vnode)
{
    graphit_node_t *node = (graphit_node_t *) vnode;

    if (node == NULL) { return 0; }

    return node->child_count;
}

/* return a pointer to the i'th child of this node */
static void *graphit_child_fn(void *vnode, int i)
{
    graphit_node_t *node = (graphit_node_t *) vnode;

    if (node == NULL) { return NULL; }

    if (i < 0 || i >= node->child_count) { return NULL; }

    return node->child[i];
}

/* malloc and return a char string containing the name of this node */
static char *graphit_name_fn(void *vnode)
{
    graphit_node_t *node = (graphit_node_t *) vnode;

    if (node == NULL) { return strdup(""); }

    return strdup(node->node_name);
}

/* return a reference to a per-node pointer that print_tree.[hc] can use
 * memo-ize information about node to improve efficiency
 */
static void **graphit_memo_fn(void *vnode)
{
    graphit_node_t *node = (graphit_node_t *) vnode;

    if (node == NULL) { return NULL; }

    return (&node->print_state);
}

static print_tree_fns_t graphit_fns = {
    graphit_child_fn,
    graphit_child_count_fn,
    graphit_name_fn,
    graphit_memo_fn,
    NULL,  /* set up by individual graphit print routines below */

};

/* prettyprint the tree using the printing function fn */
void graphit_dprint(ddprintf_t *fn, FILE *f, graphit_node_t *node)
{
    graphit_fns.printer = fn;

    print_tree(f, node, &graphit_fns);
}

/* prettyprint the tree to stream f */
void graphit_print(FILE *f, graphit_node_t *node)
{
    graphit_dprint(fprint, f, node);
}
