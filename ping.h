/* ping.h - link-level liveness pings among the cloud boxes
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: ping.h,v 1.9 2012-02-22 18:55:25 greg Exp $
 */
#ifndef PING_H
#define PING_H

#include "cloud.h"

extern void process_ping_msg(message_t *message, int device_index);
extern void process_ping_response_msg(message_t *message, int device_index);
extern void do_ping_neighbors(void);
extern void send_ping(char *buf);
extern void send_nbr_pings();

#endif
