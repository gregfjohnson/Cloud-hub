/* lock.h - request and grant locks among cloud boxes
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: lock.h,v 1.6 2012-02-22 18:55:25 greg Exp $
 */
#ifndef LOCK_H
#define LOCK_H

#include "cloud.h"
#include "mac.h"

/* when changing cloud connectivity, boxes can request temporary locks
 * on arcs between boxes.  these locks time out by themselves if they
 * are not released in a timely manner.
 */
typedef struct {
    message_type_t type;
    long sec, usec;
    mac_address_t node_1;
    mac_address_t node_2;

    stp_beacon_t stp_beacon;
    mac_address_t neighbor;
    byte repeats;
} lockable_resource_t;

extern lockable_resource_t pending_requests[];
extern int pending_request_count;

/* locks we have given to another node */
extern lockable_resource_t locks_granted[];
extern int locks_granted_count;

/* locks we own */
extern lockable_resource_t locks_owned[];
extern int locks_owned_count;

/* locks we have timed out, for use in routines that do post-processing
 * after a lock timeout
 */
extern lockable_resource_t timed_out_lockables[];
extern int timed_out_lockable_count;

extern void timeout_lockable(lockable_resource_t *lock_vector, int *len,
        int recv_timeout, char *list_name);
extern void timeout_lockables();
extern void delete_lockable(lockable_resource_t *lock_vector, int *len,
    mac_address_ptr_t name);
extern void timeout_locks();
extern void print_lockable_list(lockable_resource_t *list, int list_len,
        char *title);

#endif
