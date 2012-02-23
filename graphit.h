/* graphit.h - ASCII prettyprint the STP tree connecting the cloud boxes
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: graphit.h,v 1.9 2012-02-22 19:27:22 greg Exp $
 */
#ifndef GRAPHIT_H
#define GRAPHIT_H

#include <stdio.h>
#include "util.h"

#define GRAPHIT_CHILD_COUNT 10

typedef struct _graphit_node_t {
    char *node_name;
    struct _graphit_node_t *parent;
    int parent_value;

    struct _graphit_node_t *child[GRAPHIT_CHILD_COUNT];
    int child_value[GRAPHIT_CHILD_COUNT];
    int child_count;

    /* private data for use by treee_print.[hc] to improve efficiency
     * of prettyprinting
     */
    void *print_state;

} graphit_node_t;

void graphit_print(FILE *f, graphit_node_t *node);
void graphit_dprint(ddprintf_t *fn, FILE *f, graphit_node_t *node);

#endif
