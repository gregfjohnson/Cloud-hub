/* print_tree.c - a package to prettyprint trees in ascii
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: print_tree.c,v 1.8 2012-02-22 18:55:25 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: print_tree.c,v 1.8 2012-02-22 18:55:25 greg Exp $";

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "print_tree.h"

/* a package to prettyprint trees in ascii.
 *
 * this is an API that is usable generically and is not specific to the
 * cloud_hub project.
 *
 * works with any node type;
 * we never see the node type, but only see 'void *'s.  we traverse the
 * the tree by calling caller-provided functions:
 *
 *     int child_count(void *node) 
 *     void *child(void *node, int i)   (0 <= i < child_count())
 *     char *name(void *node)
 *     void **state(void *node) (optional)
 *     int printer(FILE *f, const char *msg, ...) (optional)
 *
 * in the name() function, the caller must malloc a string, which this package
 * guarantees to free.
 *
 * efficiency is not normally a concern with this package,
 * but if it is, the caller can implement the state() function.
 *
 * using the state() function, the caller gives
 * the print_tree package a per-node pointer for use inside the print_tree
 * abstraction.  print_tree malloc's an internal-use struct and saves
 * stuff into it on a per-node basis.  this saves traversals of the tree,
 * and reduces calls to name().  (print_tree frees the structs it allocates.)
 * (the savings can be significant; use of state() reduces the polynomial
 * degree of execution time of the print_tree algorithm.)
 *
 * the optional method printer() can be defined if there are special needs
 * such as sending messages over a network connection.  default is fprintf().
 *
 * see the unit test below for an example of usage.
 *
 * to run it:
 *   gcc -DUNIT_TEST -o print_tree print_tree.c
 *   print_tree
 */

/* struct for internal per-node state if the caller memoizes */
typedef struct {
    int width;
    int depth;
    char *name;
} node_memo_t;

/* print "indent" spaces */
static void space(FILE *f, int indent, print_tree_fns_t *fns)
{
    int i;
    for (i = 0; i < indent; i++) { fns->printer(f, " "); }
}

/* function call counts to see how much benefit you get from memo-izing */
static int mdepth = 0, all_depth = 0,
           mwidth = 0, all_width = 0,
           mname  = 0, all_name  = 0;

/* return the number of lines that it takes to print the tree rooted at node */
static int depth(void *node, print_tree_fns_t *fns)
{
    int result = 0;
    int i, child_count;
    node_memo_t **memo = NULL;

    if (fns->memo != NULL) {
        memo = (node_memo_t **) fns->memo(node);
        if (*memo != NULL && (*memo)->depth != -1) {
            mdepth++;
            return (*memo)->depth;
        }
    }

    child_count = fns->child_count(node);

    if (child_count == 0) {
        result = 1;

    } else {

        for (i = 0; i < child_count; i++) {
            int cdepth = depth(fns->child(node, i), fns);
            if (result < cdepth) { result = cdepth; }
        }

        result += 4;
    }

    if (memo != NULL) {
        (*memo)->depth = result;
    }

    all_depth++;
    return result;
}

/* ask the caller to malloc and give us a string containing the name of
 * this node.  we guarantee to free this string.
 */
static char *get_name(void *node, print_tree_fns_t *fns)
{
    char *result = NULL;
    node_memo_t **memo = NULL;

    if (fns->memo != NULL) {
        memo = (node_memo_t **) fns->memo(node);
        if (*memo != NULL && (*memo)->name != NULL) {
            mname++;
            return (*memo)->name;
        }
    }

    result = fns->name(node);

    if (memo != NULL) {
        (*memo)->name = result;
    }

    all_name++;
    return result;
}

/* free the string the caller malloced that contains this node's name */
static void free_name(char *name, void *node, print_tree_fns_t *fns)
{
    node_memo_t **memo = NULL;

    if (fns->memo != NULL) {
        memo = (node_memo_t **) fns->memo(node);
        if (*memo != NULL && (*memo)->name != NULL) {
            return;
        }
    }

    free(name);
}

static int width(void *node, print_tree_fns_t *fns);

/* we want visually pleasing symmetry in the tree.  if a node has an
 * even number of children, see if we need to add a pad character to make
 * sure that the line under the node is exactly halfway between the
 * hangers of its two middle children.
 */
static char check_middle(void *node, print_tree_fns_t *fns)
{
    char result = 0;
    int child_count = fns->child_count(node);

    if (child_count > 0 && child_count % 2 == 0) {

        int width1 = width(fns->child(node, child_count / 2 - 1), fns);
        int width2 = width(fns->child(node, child_count / 2), fns);

        /* the left middle child has an odd number of characters to the
         * right of its hanging point iff it has length 2,3, 6,7, ...
         *
         * the right middle child has an odd number of characters to the
         * left of its hanging point iff it has length 3,4, 7,8, ...
         *
         * we need to add a character iff both of the above are even or odd.
         */
        if ((((width1 + 2) % 4 < 2) && ((width2 + 1) % 4 < 2))
            || (((width1 + 2) % 4 >= 2) && ((width2 + 1) % 4 >= 2))) {
            result = 1;
        }
    }

    return result;
}

/* return the number of columns required to print the tree rooted at node */
static int width(void *node, print_tree_fns_t *fns)
{
    int result = 0;
    int i, child_count;
    char *name;
    node_memo_t **memo = NULL;

    if (fns->memo != NULL) {
        memo = (node_memo_t **) fns->memo(node);
        if (*memo != NULL && (*memo)->width != -1) {
            mwidth++;
            return (*memo)->width;
        }
    }

    child_count = fns->child_count(node);

    if (child_count > 0) {
        for (i = 0; i < child_count; i++) {
            result += width(fns->child(node, i), fns);
        }

        result += 4 * (child_count - 1);
    }

    if (check_middle(node, fns)) { result++; }

    /* is this node's name longer than the width of its entire sub-tree? */
    name = get_name(node, fns);
    if (result < (int) strlen(name)) {
        result = strlen(name);
    }

    free_name(name, node, fns);

    if (memo != NULL) {
        (*memo)->width = result;
    }

    all_width++;
    return result;
}

static int hanging_point(void *node, print_tree_fns_t *fns);

/* return the column (index-zero) of a vertical line from which the children
 * of this tree would hang.  If odd number of children,
 * it's the hanging point of middle child.
 * If even number of children, halfway
 * between the hanging points of the two middle children.
 */
static int children_hanging_point(void *node, print_tree_fns_t *fns)
{
    int result = 0;
    int i;
    int left_hanging_point, right_hanging_point;
    int child_count =fns->child_count(node);

    if (child_count == 0) {
        result = 0;

    /* odd number of children; hanging point of middle child */
    } else if (child_count % 2 == 1) {

        /* skip over children to the left of middle child */
        for (i = 0; i < child_count / 2; i++) {
            result += width(fns->child(node, i), fns) + 4;
        }

        /* hanging point of middle child */
        result += hanging_point(fns->child(node, child_count / 2), fns);

    /* even number of children; halfway between the hanging points of
     * the two middle children.
     */
    } else {
        void *left_mid_child = fns->child(node, child_count / 2 - 1);
        void *right_mid_child = fns->child(node, child_count / 2);

        /* skip children to the left of left middle child */
        for (i = 0; i < child_count / 2 - 1; i++) {
            result += width(fns->child(node, i), fns) + 4;
        }

        /* hanging point of left middle child */
        left_hanging_point = result + hanging_point(left_mid_child, fns);

        /* hanging point of right middle child */
        right_hanging_point = result + width(left_mid_child, fns) + 4
                + hanging_point(right_mid_child, fns);

        /* halfway between the hanging points */
        result = (left_hanging_point + right_hanging_point) / 2;

        if (check_middle(node, fns)) {
            result++;
        }
    }

    return result;
}

/* return the column (index-zero) of a vertical line from which this tree
 * would hang.  If leaf node, half the width.  If odd number of children,
 * the hanging point of middle child.  If even number of children, halfway
 * between the hanging points of the two middle children.
 */
static int hanging_point(void *node, print_tree_fns_t *fns)
{
    int result = 0;
    int child_count =fns->child_count(node);
    char *name;

    if (child_count > 0) {
        result = children_hanging_point(node, fns);
    }

    name = get_name(node, fns);

    /* if we have a weirdly long name, use halfway point of our name */
    if ((int) ((strlen(name) - 1) / 2) > result) {
        result = (strlen(name) - 1) / 2;
    }
    free_name(name, node, fns);

    return result;
}

/* assume cursor is at my left boundary.  print i'th line of my
 * text representation (0 is my first line.)  print full width
 * (right blanks as necessary).
 */
static void print_level(FILE *f, void *node, int level, print_tree_fns_t *fns)
{
    int chp, hp, pos, i, j;
    char hanger, spacer;
    int hp_child, end, end_child;
    int left_offset;
    char widen_middle;
    int child_count =fns->child_count(node);
    char *name = get_name(node, fns);

    /* print our name, centered under our hanger position, and right-pad
     * to our tree width
     */
    if (level == 0) {
        hp = hanging_point(node, fns);

        pos = hp - (strlen(name) - 1) / 2;
        space(f, pos, fns);

        pos += strlen(name);
        fns->printer(f, "%s", name);

        /* code after 'done' label below prints spaces to our right */

        goto done;  /* avoid nested ifs going off to the right */
    }
    
    /* the level is below our lowest child.  print blanks of our width.
     * (other parts of the tree to our right may be deeper than us.)
     * (as noted above, code after 'done' prints spaces to the right,
     * which in this case is everything.)
     */
    if (level >= depth(node, fns)) {
        pos = 0;

        goto done;
    }

    /* print hanger under our node (if we have no children, we will have
     * returned above.)
     */
    if (level == 1 || (level <= 3 && child_count == 1)) {

        hp = hanging_point(node, fns);

        pos = hp;
        space(f, hp, fns);

        pos += 1;
        fns->printer(f, "|");

        goto done;
    }
    
    /* print hangers for our children, and print our children.  if our name is
     * is wider than our subtree, we need to add white space to the left
     * of our children to center them under our name.
     */

    chp = children_hanging_point(node, fns);
    if ((int) ((strlen(name) - 1) / 2) > chp) {
        left_offset = ((strlen(name) - 1) / 2) - chp;
    } else {
        left_offset = 0;
    }

    pos = left_offset;
    space(f, pos, fns);

    widen_middle = check_middle(node, fns);

    /* have each child print its part of the line */
    if (level >= 4) {

        for (i = 0; i < child_count; i++) {
            void *child = fns->child(node, i);

            pos += width(child, fns);
            print_level(f, child, level - 4, fns);

            if (i < child_count - 1) {
                pos += 4;
                space(f, 4, fns);
            }

            if (widen_middle && i == child_count / 2 - 1) {
                pos += 1;
                space(f, 1, fns);
            }
        }

        goto done;
    }

    /* print the two levels of hanger stuff for our children:
     *
     *       +-----+-----+
     *       |     |     |
     */

    if (level == 2) {
        hanger = '+';
        spacer = '-';
    } else /* level == 3 */ {
        hanger = '|';
        spacer = ' ';
    }

    hp = hanging_point(node, fns);

    /* at start of loop, pos is in column next child should start.
     * for each child, print "----+----" for it, of its exact width.
     *                   (or "    |    ").
     *
     * print "+" for our hanger where it needs to go.
     */

    for (i = 0; i < child_count; i++) {
        void *child = fns->child(node, i);

        hp_child = hanging_point(child, fns);
        end_child = pos + width(child, fns);

        /* print the stuff to the left of this child's hanger */
        for (j = 0; j < hp_child; j++) {
            if (pos == hp && level == 2) { fns->printer(f, "%c", hanger); }
            else if (i == 0) { fns->printer(f, " "); }
            else { fns->printer(f, "%c", spacer); }
            pos++;
        }

        /* print this child's hanger character */
        pos++;
        fns->printer(f, "%c", hanger);

        /* print the stuff to the right of this child's hanger */
        end = end_child - pos;
        for (j = 0; j < end; j++) {

            if (pos == hp && level == 2) { fns->printer(f, "%c", hanger); }
            else if (i == child_count - 1) { fns->printer(f, " "); }
            else { fns->printer(f, "%c", spacer); }
            pos++;
        }

        /* print four spaces between this child and the next one unless we
         * are at rightmost child
         */
        if (i < child_count - 1) {
            for (j = 0; j < 4; j++) {
                if (pos == hp && level == 2) { fns->printer(f, "%c", hanger); }
                else { fns->printer(f, "%c", spacer); }
                pos++;
            }

            if (widen_middle && i == child_count / 2 - 1) {
                fns->printer(f, "%c", spacer);
                pos++;
            }
        }
    }

    done:
    space(f, width(node, fns) - pos, fns);

    free_name(name, node, fns);
}

/* traverse the caller's tree, and malloc a struct for our private use
 * for each node in the tree.
 */
static void init_dimensions(void *node, print_tree_fns_t *fns)
{
    int i, child_count;
    node_memo_t **memo = (node_memo_t **) fns->memo(node);

    *memo = malloc(sizeof(node_memo_t));
    if (*memo != NULL) {
        (*memo)->width = -1;
        (*memo)->depth = -1;
        (*memo)->name = NULL;
    }

    child_count = fns->child_count(node);

    for (i = 0; i < child_count; i++) {
        init_dimensions(fns->child(node, i), fns);
    }
}

/* traverse the tree and free out internally malloc'd per-node state structs */
static void free_memo_structs(void *node, print_tree_fns_t *fns)
{
    int i, child_count;
    node_memo_t **memo = (node_memo_t **) fns->memo(node);

    if (*memo != NULL) {
        free(*memo);
    }

    child_count = fns->child_count(node);

    for (i = 0; i < child_count; i++) {
        free_memo_structs(fns->child(node, i), fns);
    }
}

/* pretty-print an ascii version of the tree starting at "root".
 * traverse the tree using functions supplied in the function-pointer
 * struct "fns", where the caller-supplied functions are described
 * in the typedef's in print_tree.h.
 */
void print_tree(FILE *f, void *node, print_tree_fns_t *fns)
{
    int level;
    int i;
    print_tree_fns_t *lcl_fns = fns;

    if (lcl_fns->printer == NULL) { lcl_fns->printer = &fprintf; }

    if (fns->memo != NULL) {
        init_dimensions(node, fns);
    }

    level = depth(node, fns);

    for (i = 0; i < level; i++) {
        print_level(f, node, i, fns);
        fprintf(f, "\n");
    }

    if (fns->memo != NULL) {
        free_memo_structs(node, fns);
    }
}

#ifdef UNIT_TEST

/* a node type; a little different than the print_tree's idea of nodes
 * and children on purpose, to illustrate.
 */
typedef struct _test_node_t {
    char *name;
    struct _test_node_t *c1, *c2, *c3;
    void *print_state;
} test_node_t;

/* return the i'th child of vnode (0 <= i < child_count()), NULL if none */
static void *child(void *vnode, int i)
{
    test_node_t *node = (test_node_t *) vnode;
    if (i == 0) { return node->c1; }
    else if (i == 1) { return node->c2; }
    else { return node->c3; }
}

/* return the number of children vnode has. */
static int child_count(void *vnode)
{
    test_node_t *node = (test_node_t *) vnode;
    int result = 0;

    if (node->c1 != NULL) { result++; }
    if (node->c2 != NULL) { result++; }
    if (node->c3 != NULL) { result++; }

    return result;
}

/* return a malloc'd version of this node's name; print_tree guarantees to
 * free this if state() is not used, or in free_state().
 */
static char *name(void *vnode)
{
    test_node_t *node = (test_node_t *) vnode;

    return strdup(node->name);
}

/* give the print_tree API a pointer associated with this node for its
 * private use.
 */
void **memo_state(void *vnode)
{
    test_node_t *node = (test_node_t *) vnode;
    return &node->print_state;
}

int main(int argc, char **argv)
{
    /* the functions required by this package */
    print_tree_fns_t fns = { &child, &child_count, &name, &memo_state };

    /* an example tree */

    test_node_t         b11 = { "T1", NULL, NULL, NULL };
    test_node_t         b12 = { "T2", NULL, NULL, NULL };
    test_node_t     b1 = { "lchild", &b11, &b12, NULL };
    test_node_t     b2 = { "T3", NULL, NULL, NULL };
    test_node_t boot   = { "old_root", &b1, &b2, NULL };
    test_node_t     c1 = { "T1", NULL, NULL, NULL };
    test_node_t         c21 = { "T2", NULL, NULL, NULL };
    test_node_t         c22 = { "T3", NULL, NULL, NULL };
    test_node_t     c2 = { "old_root", &c21, &c22, NULL };
    test_node_t coot   = { "lchild", &c1, &c2, NULL };

    test_node_t root   = { "lchild", &coot, &boot, NULL };

    /* print it! */
    print_tree(stdout, &root, &fns);

    printf("\n");
    printf("memoized width calls %d, actual width calculations %d\n", mwidth,
            all_width);
    printf("memoized depth calls %d, actual depth calculations %d\n", mdepth,
            all_depth);
    printf("memoized name calls %d, actual name calculations %d\n", mname,
            all_name);

    return 0;
}
#endif
