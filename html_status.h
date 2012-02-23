/* html_status.h - print an html file showing the status of the whole cloud
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: html_status.h,v 1.8 2012-02-22 19:27:22 greg Exp $
 */
#ifndef HTML_STATUS_H
#define HTML_STATUS_H

#include <stdio.h>
#include "graphit.h"
#include "print.h"

extern void build_stp_list();
extern void init_cloud_stp_tree();
extern void db_print_cloud_stp_list(ddprintf_t *fn, FILE *f);
extern void dprint_cloud_stats_short(ddprintf_t *fn, FILE *f);
extern void dprint_cloud_stats(ddprintf_t *fn, FILE *f);
extern void do_print_cloud(void);

extern graphit_node_t cloud_stp_tree[MAX_CLOUD];

#endif
