/* parm_change.h - propagate wifi parmater changes throughout the cloud
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: parm_change.h,v 1.9 2012-02-22 19:27:23 greg Exp $
 */
#ifndef PARM_CHANGE_H
#define PARM_CHANGE_H

#include "parm_change_data.h"
#include "print.h"

extern bool_t start_parm_change(char *fname);
extern void process_parm_change_start_msg(message_t *msg, int device_index);
extern void process_parm_change_ready_msg(message_t *msg, int device_index);
extern void process_parm_change_not_ready_msg(message_t *msg, int device_index);
extern void process_parm_change_go_msg(message_t *msg, int device_index);
extern void parm_change_dprint(ddprintf_t *fn, FILE *f,
        parm_change_msg_t *change);
extern void process_parm_change_start_msg(message_t *msg, int device_index);
extern void process_parm_change_ready_msg(message_t *msg, int device_index);
extern void process_parm_change_go_msg(message_t *msg, int device_index);
extern void parm_change_result(bool_t result);

#endif
