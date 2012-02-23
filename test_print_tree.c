/* test_print_tree.c - a program to parse and prettyprint trees
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: test_print_tree.c,v 1.7 2012-02-22 18:55:25 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: test_print_tree.c,v 1.7 2012-02-22 18:55:25 greg Exp $";

/* a cute little program to test print_tree.[hc], an ascii-art
 * tree prettyprinting API.
 *
 * this program is usable generically and is not specific to the
 * cloud_hub project.
 *
 * to compile:  gcc -o test_print_tree test_print_tree.c print_tree.c
 *
 * with no command-line arguments, print a fixed internally initialized tree:
 *
 *     test_print_tree
 *
 * (run test_print_tree to see what you get.)
 *
 * if given an argument, parse the argument into a tree and print the tree.
 *
 * the syntax of the argument is like nested function calls and constants:
 *
 *     test_print_tree 'f(1,2,3)'
 *
 *     test_print_tree 'f(g(3,4), h(5,6))'
 *
 *     test_print_tree 'apply(lambda(x, apply(x, x)), lambda(x, apply(x, x)))'
 *
 *     test_print_tree \
 *'lambda(f,apply(lambda(x,apply(f,apply(x,x))),lambda(x,apply(f,apply(x,x)))))'
 */

#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#include "print_tree.h"

#define CHILD_COUNT 16

typedef struct _node_t {
    char *node_name;

    struct _node_t *child[CHILD_COUNT];
    int child_count;

    void *print_state;
} node_t;

/* number of children for node */
static int child_count_fn(void *vnode)
{
    node_t *node = (node_t *) vnode;

    if (node == NULL) { return 0; }

    return node->child_count;
}

/* return the i'th child of node */
static void *child_fn(void *vnode, int i)
{
    node_t *node = (node_t *) vnode;

    if (node == NULL) { return NULL; }

    if (i < 0 || i >= node->child_count) { return NULL; }

    return node->child[i];
}

/* malloc and return a copy of the string representation of node */
static char *name_fn(void *vnode)
{
    node_t *node = (node_t *) vnode;

    if (node == NULL) { return strdup(""); }

    return strdup(node->node_name);
}

/* return a pointer to per-node pointer for private use by the print_tree API */
static void **memo_fn(void *vnode)
{
    node_t *node = (node_t *) vnode;

    if (node == NULL) { return NULL; }

    return (&node->print_state);
}

/* test example */
static node_t root;
static node_t     c0;
static node_t         c00;
static node_t         c01;
static node_t     c1;
static node_t         c10;
static node_t     c2;
static node_t         c20;
static node_t         c21;
static node_t     c3;

/* set up test tree */
static void init_tree()
{
    root.node_name = "root";
    root.child_count = 4;
    root.child[0] = &c0;
    root.child[1] = &c1;
    root.child[2] = &c2;
    root.child[3] = &c3;

        c0.node_name = "child_0";
        /* optional super-long test case */
        // c0.node_name
        //    = "012345678901234567890123456789012345678901234567890123456789";
        c0.child_count = 2;
        c0.child[0] = &c00;
        c0.child[1] = &c01;

            c00.node_name = "child_0_0";
            c00.child_count = 0;

            c01.node_name = "child_0_1";
            c01.child_count = 0;

        c1.node_name = "child_1";
        c1.child_count = 1;
        c1.child[0] = &c10;

            c10.node_name = "child_1_0";
            c10.child_count = 0;

        c2.node_name = "child_2";
        c2.child_count = 2;
        c2.child[0] = &c20;
        c2.child[1] = &c21;

            c20.node_name = "child_2_0";
            c20.child_count = 0;

            c21.node_name = "child_2_1";
            c21.child_count = 0;

        c3.node_name = "child_3";
        c3.child_count = 0;

}

/* recursive descent parser for command-line expressions */

#define ID 1
#define LPAREN 2
#define RPAREN 3
#define COMMA 4
#define EOF_TOKEN 5

/* string text of identifier tokens */
static char *id_string;

/* error abort after syntax error or too many children. */
static void error(void)
{
    fprintf(stderr, "invalid expression.\n");
    exit(1);
}

static node_t *parse(char **input_tail);

/* return the next token, but don't consume input */
static int peek(char *input_tail)
{
    int result;
    while (*input_tail && isspace(*input_tail)) { input_tail++; }
    if (!*input_tail) { result = EOF_TOKEN; }
    else if (*input_tail == '(') { result = LPAREN; }
    else if (*input_tail == ')') { result = RPAREN; }
    else if (*input_tail == ',') { result = COMMA; }
    else if (isalnum(*input_tail)) { result = ID; }
    else { error(); }

    return result;
}

/* get the next token, and leave input_tail past it.  if the next token
 * is a string, malloc a copy and leave id_string pointing at that.
 */
static int get_token(char **input_tail)
{
    int result;
    char *p;

    while (**input_tail && isspace(**input_tail)) { (*input_tail)++; }

    if (!**input_tail) { result = EOF_TOKEN; }
    else if (**input_tail == '(') { result = LPAREN; (*input_tail)++; }
    else if (**input_tail == ')') { result = RPAREN; (*input_tail)++; }
    else if (**input_tail == ',') { result = COMMA; (*input_tail)++; }

    else {
        if (!**input_tail || !isalnum(**input_tail)) { error(); }

        result = ID;

        p = *input_tail;
        while (*p && isalnum(*p)) { p++; }
        id_string = malloc(p - *input_tail + 1);
        id_string[p - *input_tail] = '\0';
        strncpy(id_string, *input_tail, p - *input_tail);
        *input_tail = p;
    }

    return result;
}

/* parse an argument list 'expr [, expr]* )', and append children to
 * root.  leave input_tail pointing past the end of the expression.
 */
static void parse_args(node_t *root, char **input_tail)
{
    node_t *child;

    root->child_count = 0;

    /* read one child at a time and append to root's list of children */
    while (1) {
        int next_token;

        if (root->child_count >= CHILD_COUNT) {
            fprintf(stderr, "too many children.\n");
            exit(1);
        }

        /* get next child and attach it to root. */
        child = parse(input_tail);

        root->child[root->child_count++] = child;

        /* see if we have more args */
        next_token = get_token(input_tail);
        if (next_token == RPAREN) { break; }
        else if (next_token != COMMA) { error(); }
    }
}

/* parse an expression of the form 'id' or 'id(expr [, expr]*)'.
 * leave input_tail past the end of the expression.
 * return a heap-allocated tree corresponding to the expression.
 */
static node_t *parse(char **input_tail)
{
    int id_token;
    int next_token;
    node_t *id;

    /* get the starting identifier of the expression */
    id_token = get_token(input_tail);
    if (id_token != ID) { error(); }

    /* create the root node of the tree for this expression */
    id = (node_t *) malloc(sizeof(node_t));
    id->node_name = id_string;

    /* see what's next; if it's past the end of the current expression,
     * we're done.  otherwise, parse child expressions.
     */
    next_token = peek(*input_tail);

    switch (next_token) {

    case COMMA :
    case RPAREN :
    case EOF_TOKEN :
        break;

    case LPAREN :
        while (**input_tail != '(') { (*input_tail)++; } (*input_tail)++;
        parse_args(id, input_tail);
        break;

    default :
        error();
    }

    return id;
}

static print_tree_fns_t node_fns = {
    child_fn,
    child_count_fn,
    name_fn,
    memo_fn,
    fprintf,
};

int main(int argc, char **argv)
{
    if (argc == 1) {
        init_tree();
    } else {
        char *p = argv[1];
        root = *parse(&p);
    }

    print_tree(stdout, &root, &node_fns);

    return 0;
}
