/* print.h - printing routines to files and to remote hosts for debugging
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: print.h,v 1.10 2012-02-22 19:27:23 greg Exp $
 */
#ifndef PRINT_H
#define PRINT_H

#include <stdio.h>
#include "util.h"

extern bool_t eth_print_cmd;
extern bool_t temp_print_cmd;
extern bool_t stdout_print_cmd;
extern FILE *temp_log_file;

int eprintf(FILE *f, const char *msg, ...) __attribute__((format(printf,2,3)));
void ddprintf(const char *msg, ...) __attribute__((format(printf,1,2)));

#endif
