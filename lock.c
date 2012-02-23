/* lock.c - request and grant locks among cloud boxes
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: lock.c,v 1.12 2012-02-22 19:27:22 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: lock.c,v 1.12 2012-02-22 19:27:22 greg Exp $";

#include <sys/time.h>

#include "timer.h"
#include "cloud.h"
#include "lock.h"
#include "print.h"
#include "cloud_msg.h"
#include "cloud_mod.h"
#include "stp_beacon.h"
#include "parm_change.h"

/* locks we are asking for from other nodes */
lockable_resource_t pending_requests[MAX_CLOUD];
int pending_request_count = 0;

/* locks we have given to another node */
lockable_resource_t locks_granted[MAX_CLOUD];
int locks_granted_count = 0;

/* locks we own */
lockable_resource_t locks_owned[MAX_CLOUD];
int locks_owned_count = 0;

lockable_resource_t timed_out_lockables[MAX_CLOUD];
int timed_out_lockable_count;

/* look at each lock that hase timed out, and take any action necessary
 * in response to the timeout.
 *
 * if a global wifi parameter change attempt timed out, provide for user
 * feedback in the configuration web page to that effect.
 *
 * for locks that were created in the middle of a part of the cloud
 * protocol and timed out, try to pick up where the timeout occurred
 * and continue with the protocol.
 * don't remember why i commented this code out with
 * "RETRY_TIMED_OUT_LOCKABLES".
 */
void process_timed_out_pending_requests()
{
    int i;

    if (db[21].d && timed_out_lockable_count > 0) {
        ddprintf("process_timed_out_pending_requests..\n");
        print_state();
    }

    for (i = 0; i < timed_out_lockable_count; i++) {
        lockable_resource_t *l = &timed_out_lockables[i];

        /* if this is a lock we granted as part of a global wifi parameter
         * change, indicate that the parm change failed.
         */
        if (l->type == parm_change_ready_msg) {
            parm_change_result(false);
        }

        #ifdef RETRY_TIMED_OUT_LOCKABLES
        /* check to see if this guy is in our neighbor list, i.e., if we
         * know about him at the hardware level.  if not, don't go into
         * an interrupt-driven loop trying to connect to him.
         *
         * this is done in local_stp_add_request().
         */
        /* if we see beacons from some other box, but he is not really a
         * cloud box, don't go into a hard timeout loop trying to connect
         * to him.  just try once per stp_beacon.  the other two message
         * types below have to do with boxes we've already had cloud
         * interactions with, so keep trying to communicate with them.
         */

        /* get some experience with how cloud behaves not doing any of this.
         * dropped packets happen every once in a while, but maybe if they
         * do it's not that big a deal.  in a second or two we will re-try
         * anyway.  and, this code has the potential to set up a hard
         * loop of generating interrupts if another box goes down in the
         * middle of the protocol.  would be ok to sorta get the best of
         * both if we created an array of timeout retries, and only did
         * a retry say 10 times for a given message type to a given box.
         * would need to clean up entries in that array when a given message
         * is deleted from pending as part of normal protocol.
         */
        if (/*l->type == local_stp_add_request_msg
            ||*/ l->type == local_stp_add_changed_request_msg
            || l->type == local_stp_delete_request_msg)
        {
            local_stp_add_request(l->node_1, l->type);

        }
        
        if (l->type == local_lock_req_new_msg
            || l->type == local_lock_req_old_msg)
        {
            resend_local_lock_grant_msg(l->node_1);

        }
        
        if (l->type == stp_beacon_msg) {
            resend_stp_beacon_msg(l->node_1,
                    &timed_out_lockables[i].stp_beacon);
        }
        #endif
    }

    timed_out_lockable_count = 0;
}

/* if we want to do anything to clean up after granted locks have timed out,
 * do it here.  currently just delete all of the timed out granted locks
 * and forget about them.
 */
void process_timed_out_locks_granted()
{
    if (db[21].d && timed_out_lockable_count > 0) {
        ddprintf("process_timed_out_locks_granted..\n");
        print_state();
    }

    timed_out_lockable_count = 0;
}

/* if we want to do anything to clean up after owned locks have timed out,
 * do it here.  currently just delete all of the timed out owned locks
 * and forget about them.
 */
void process_timed_out_locks_owned()
{
    if (db[21].d && timed_out_lockable_count > 0) {
        ddprintf("process_timed_out_locks_owned..\n");
        print_state();
    }
    timed_out_lockable_count = 0;
}

/* look through lists of locks we have granted to others, locks
 * we own, and lock requests we have pending.  delete any that are
 * too old and likely indicate another box got turned off in the middle
 * of a cloud protocol action.
 */
void timeout_lockables()
{
    timeout_lockable(pending_requests, &pending_request_count,
            RECV_TIMEOUT_USEC,
            "pending_requests");
    if (timed_out_lockables > 0) { process_timed_out_pending_requests(); }

    timeout_lockable(locks_granted, &locks_granted_count, RECV_TIMEOUT_USEC,
            "locks_granted");
    if (timed_out_lockables > 0) { process_timed_out_locks_granted(); }

    timeout_lockable(locks_owned, &locks_owned_count, RECV_TIMEOUT_USEC,
            "locks_owned");
    if (timed_out_lockables > 0) { process_timed_out_locks_owned(); }

    if (db[3].d) { print_state(); }
}

/* debugging routine; clear all locks we have granted to others, locks
 * we own, and lock requests we have pending.
 */
void timeout_locks()
{
    pending_request_count = 0;
    locks_granted_count = 0;
    locks_owned_count = 0;

    if (db[3].d) { print_state(); }
}

/* delete from the array of locks the one with node_1 equal to name. */
void delete_lockable(lockable_resource_t *lock_vector, int *len,
    mac_address_ptr_t name)
{
    int i;

    for (i = 0; i < *len; i++) {
        if (lock_vector[i].node_1 == name) { break; }
    }

    if (i < *len) {
        for (; i < *len - 1; i++) {
            lock_vector[i] = lock_vector[i + 1];
        }
        (*len)--;
    }
}

/* delete from the lock array any locks that are older than recv_timeout.
 * add any deleted locks to timed_out_lockables[], so that we can later
 * take any required action for timed out locks based on their type.
 */
void timeout_lockable(lockable_resource_t *lock_vector, int *len,
        int recv_timeout, char *list_name)
{
    struct timeval tv;
    struct timezone tz;
    int next;
    int past_packed;

    timed_out_lockable_count = 0;

    /* right after midnight, things are temporarily messed up.  everything
     * will time out.  but that's ok.  we'll try again shortly.
     */
    if (gettimeofday(&tv, &tz)) {
        printf("timeout_lockable:  gettimeofday failed\n");
        return;
    }

    /* invariant:  [0 .. past_packed) are keepable, packed, and done.
     * [past_packed .. next) can be overwritten.
     * [next .. *len) are yet to be processed and as originally.
     */
    past_packed = 0;
    for (next = 0; next < *len; next++) {
        lockable_resource_t *l = &lock_vector[next];
        if (usec_diff(tv.tv_sec, tv.tv_usec, l->sec, l->usec) <= recv_timeout) {
            /* keep it. */
            if (past_packed != next) {
                lock_vector[past_packed] = *l;
            }
            past_packed++;
        } else {
            /* delete it. */
            if (timed_out_lockable_count >= MAX_CLOUD) { continue; }

            timed_out_lockables[timed_out_lockable_count++] = *l;

            if (db[3].d || db[21].d) {
                ddprintf("\n");
                ddprintf("deleting timed out %s lockable ", list_name);
                ddprintf("%s:  ", message_type_string(l->type));
                mac_dprint_no_eoln(eprintf, stderr, l->node_1);
                if (!mac_equal(l->node_2, mac_address_zero)) {
                    ddprintf(" ");
                    mac_dprint_no_eoln(eprintf, stderr, l->node_2);
                }
                ddprintf("\n");
            }
        }
    }

    *len = past_packed;
}

/* debug-print an array of lock objects. */
void print_lockable_list(lockable_resource_t *list, int list_len,
        char *title)
{
    int i;

    ddprintf("%s\n", title);
    for (i = 0; i < list_len; i++) {
        ddprintf("    %s:  ", message_type_string(list[i].type));
        mac_dprint_no_eoln(eprintf, stderr, list[i].node_1);
        if (!mac_equal(list[i].node_2, mac_address_zero)) {
            ddprintf(" ");
            mac_dprint_no_eoln(eprintf, stderr, list[i].node_2);
        }
        if (list[i].type == stp_beacon_msg) {
            ddprintf(", originator ");
            mac_dprint_no_eoln(eprintf, stderr, list[i].stp_beacon.originator);
        }
        ddprintf("\n");
    }
}

