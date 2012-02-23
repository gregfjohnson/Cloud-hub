/* cloud_mod.c - change and optimize connections among the boxes in the cloud
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: cloud_mod.c,v 1.16 2012-02-22 19:27:22 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: cloud_mod.c,v 1.16 2012-02-22 19:27:22 greg Exp $";

#include <sys/time.h>
#include <string.h>
#include <errno.h>

#include "util.h"
#include "cloud.h"
#include "cloud_mod.h"
#include "print.h"
#include "lock.h"
#include "random.h"
#include "timer.h"
#include "nbr.h"
#include "stp_beacon.h"
#include "cloud_box.h"

char force_local_change = 0;
char old_db5;

/* are we in the midst of a protocol activity to change the connectivity
 * of the stp tree?
 */
bool_t doing_stp_update()
{
    int i;
    if (locks_owned_count > 0) { return true; }
    if (locks_granted_count > 0) { return true; }

    for (i= 0; i < pending_request_count; i++) {
        lockable_resource_t *l = &pending_requests[i];

        if (l->type == local_stp_add_request_msg
            || l->type == local_lock_req_new_msg
            || l->type == local_lock_req_old_msg
            || l->type == local_stp_add_changed_request_msg
            || l->type == local_stp_delete_request_msg)
        {
            return true;
        }
    }

    return false;
}

/* initiate the process of possibly swapping arcs to adjacent cloud boxes,
 * if an arc not being used is better than one that is being used.
 * see merge_cloud.notes.
 */
void check_local_improvement()
{
    int i, j, k;
    char found;
    mac_address_ptr_t old, new;
    int max_strength, min_strength;
    int max_diff, new_sig_strength, old_sig_strength;

    if (db[11].d) {
        ddprintf("check_local_improvement..\n");
        print_cloud_list(nbr_device_list, nbr_device_list_count,
                "neighbor list:");
        node_list_print(eprintf, stderr, "stp_list", stp_list, stp_list_count);
    }

    /* don't initiate a local improvement if we are already
     * in the middle of another stp update operation.
     */
    if (doing_stp_update()) {
        if (db[11].d) {
            ddprintf("doing something else already.\n");
        }
        goto done;
    }

    /* look at each neighbor we are getting stp beacons from that
     * we don't currently have an stp link to.  these are the candidates for
     * being added as new arcs.  consider swapping with the stp
     * neighbor we currently receive stp beacons for the neighbor from.
     * bug:  syntax of above sentence.
     */

    if (db[11].d) {
        ddprintf("check_local_improvement; nbr_device_list_count %d\n",
                nbr_device_list_count);
    }

    max_diff = -1;
    for (i = 0; i < nbr_device_list_count; i++) {

        if (db[11].d) {
            ddprintf("check_local_improvement; doing neighbor %s..\n",
                    mac_sprintf(mac_buf1, nbr_device_list[i].name));
        }

        /* if this neighbor is already an stp list connection, don't consider
         * it.
         */
        found = 0;
        for (j = 0; j < stp_list_count; j++) {
            if (mac_equal(nbr_device_list[i].name, stp_list[j].box.name)) {
                found = 1;
                break;
            }
        }
        if (found) {
            if (db[11].d) { ddprintf("already an stp neighbor.\n"); }
            continue;
        }

        /* if we aren't getting stp beacons from this neighbor, don't consider
         * it.
         */
        found = 0;
        for (j = 0; j < stp_recv_beacon_count; j++) {
            if (mac_equal(nbr_device_list[i].name,
                stp_recv_beacons[j].stp_beacon.originator))
            {
                found = 1;
                break;
            }
        }
        if (!found) {
            if (db[11].d) { ddprintf("no stp beacons.\n"); }
            continue;
        }

        /* find the stp neighbor that stp beacons from this guy are
         * passing through, and see if the connection to this guy is
         * stronger.
         */
        found = 0;
        for (k = 0; k < stp_list_count; k++) {
            if (mac_equal(stp_recv_beacons[j].neighbor, stp_list[k].box.name)) {
                found = 1;
                break;
            }
        }
        if (!found) {
            if (db[11].d) { ddprintf("couldn't find stp neighbor.\n"); }
            continue;
        }

        old_sig_strength = get_sig_strength(stp_list[k].box.name);
        new_sig_strength = nbr_device_list[i].signal_strength;
        if (db[11].d) {
            ddprintf("new %d, old %d.\n", new_sig_strength, old_sig_strength);
        }

        /* sig_strength 1 is default weak signal.  want a sorta real signal. */
        if (new_sig_strength > 1
            && old_sig_strength > 1
            && new_sig_strength > old_sig_strength
            && (new_sig_strength - old_sig_strength) > max_diff)
        {
            new = nbr_device_list[i].name;
            old = stp_list[k].box.name;
            max_diff = new_sig_strength - old_sig_strength;
            max_strength = new_sig_strength;
            min_strength = old_sig_strength;
        } else {
            if (db[11].d) { ddprintf("didn't update.\n"); }
        }
    }

    if (db[11].d) {
        ddprintf("check_local_improvement got max diff %d\n", max_diff);
        
        if (max_diff != -1) {
            ddprintf("check_local_improvement got max strength %d to node %s, "
                    "min strength %d to node %s\n",
                    max_strength, mac_sprintf(mac_buf1, new),
                    min_strength, mac_sprintf(mac_buf2, old));
        }
    }

    if (max_diff == -1) {
        goto done;
    }

    if (!force_local_change && !random_eval(max_diff, stp_recv_beacon_count)) {
        if (db[11].d) {
            ddprintf("random_eval wasn't big enough; not making change.\n");
        }
        goto done;
    }

    if (force_local_change) {
        force_local_change = 0;
        db[5].d = old_db5;
    }

    ddprintf("check_local_improvement; SETTING UP TO MAKE THE CHANGE.\n");
    print_state();

    add_local_lock_request(old, local_lock_req_old_msg);

    add_local_lock_request(new, local_lock_req_new_msg);

    ddprintf("check_local_improvement; AFTER SETTING UP TO MAKE THE CHANGE:\n");
    print_state();

    done :
    if (db[11].d) {
        ddprintf("end check_local_improvement.\n");
    }
} /* check_local_improvement */

/* during improvement, guy in the middle sends lock requests to the other
 * two guys using this routine.  (local_lock_req_{old,new}_msg).
 *
 * if there isn't already a pending request or lock granted to this guy,
 * add a pending request to him of type 'type' and send him the message.
 */
void add_local_lock_request(mac_address_t node_mac, message_type_t type)
{
    lockable_resource_t *p;
    struct timeval tv;
    struct timezone tz;
    message_t message;
    int i;

    memset(&message, 0, sizeof(message));

    if (db[3].d || db[21].d) {
        ddprintf("add_local_lock_request %s to ", message_type_string(type));
        mac_dprint(eprintf, stderr, node_mac);
    }

    for (i = 0; i < pending_request_count; i++) {
        lockable_resource_t *p = &pending_requests[i];

        if (mac_equal(p->node_1, node_mac) && p->type == type) {
            ddprintf("add_local_lock_request:  lock already pending; "
                    "returning.\n");
            return;
        }
    }

    for (i = 0; i < locks_granted_count; i++) {
        if (mac_equal(locks_granted[i].node_1, node_mac)) {
            ddprintf("add_local_lock_request:  lock already granted "
                    "to someone; returning.\n");
            return;
        }
    }

    if (gettimeofday(&tv, &tz)) {
        printf("add_local_lock_request:  gettimeofday failed\n");
        return;
    }
    if (pending_request_count > MAX_CLOUD) {
        ddprintf("too many pending requests\n");
        return;
    }

    p = &pending_requests[pending_request_count++];

    p->type = type;
    mac_copy(p->node_1, node_mac);
    set_lock_timer(&pending_requests[pending_request_count - 1]);

    if (db[21].d) {
        ddprintf("add_local_lock_request:  new pending request %s to node ",
                message_type_string(type));
        mac_dprint(eprintf, stderr, node_mac);
    }

    if (db[3].d) { print_state(); }

    message.message_type = type;
    mac_copy(message.dest, node_mac);

    if (db[21].d) { print_msg("add_local_lock_request", &message); }

    send_cloud_message(&message);

} /* add_local_lock_request */

/* local improvement; i'm not the guy in the middle, but am one of the
 * two guys he's trying to swap around.  he has issued each of us
 * lock requests.  decide whether to grant this lock and let him know,
 * possibly adding the lock to our 'locks_granted' list.
 */
void process_local_lock_req(message_t *message, int device_index)
{
    lockable_resource_t *p;
    int i;
    mac_address_ptr_t sender;
    bool_t deny = false;
    message_t response;

    struct timeval tv;
    struct timezone tz;

    memset(&response, 0, sizeof(response));

    if (gettimeofday(&tv, &tz)) {
        printf("process_local_lock_req:  gettimeofday failed\n");
        return;
    }

    sender = get_name(device_index, message->eth_header.h_source);

    if (sender == NULL) {
        ddprintf("process_local_lock_req:  could not get device name\n");
        return;
    }

    if (db[21].d) {
        ddprintf("process_local_lock_req; message type %s from ",
                message_type_string(message->message_type));
        mac_dprint(eprintf, stderr, sender);
    }

    for (i = 0; i < locks_granted_count; i++) {
        if (mac_equal(sender, locks_granted[i].node_1)) {
            // if (db[21].d) {
                ddprintf("    lock already granted to ");
                mac_dprint(eprintf, stderr, sender);
            // }
            deny = true;
            break;
        }
    }

    if (!deny) {
        for (i = 0; i < pending_request_count; i++) {
            if (mac_equal(sender, pending_requests[i].node_1)) {
                // if (db[21].d) {
                    ddprintf("    request already pending to ");
                    mac_dprint(eprintf, stderr, sender);
                // }
                deny = true;
                break;
            }
        }
    }

    if (!deny) {
        for (i = 0; i < locks_owned_count; i++) {
            if (mac_equal(sender, locks_owned[i].node_1)) {
                // if (db[21].d) {
                    ddprintf("    lock already owned to ");
                    mac_dprint(eprintf, stderr, sender);
                // }
                deny = true;
                break;
            }
        }
    }

    if (!deny && locks_granted_count >= MAX_CLOUD) {
        ddprintf("process_local_lock_request:  too many nodes.\n");
        deny = true;
    }

    if (deny) {
        ddprintf("process_local_lock_req; denying..\n");
        response.message_type = local_lock_deny_msg;
    } else {
        ddprintf("process_local_lock_req; granting..\n");
        p = &locks_granted[locks_granted_count];

        p->type = local_lock_grant_msg;
        // p->sec = tv.tv_sec;
        // p->usec = tv.tv_usec;
        mac_copy(p->node_1, sender);
        locks_granted_count++;
        set_lock_timer(&locks_granted[locks_granted_count - 1]);
        if (db[21].d) {
            ddprintf("process_local_lock_req; "
                    "adding granted request local_lock_grant_msg to node ");
            mac_dprint(eprintf, stderr, sender);
        }

        response.message_type = local_lock_grant_msg;
    }
    mac_copy(response.dest, sender);

    if (db[21].d) {
        ddprintf("process_local_lock_req; sending message %s to node ",
                message_type_string(response.message_type));
        mac_dprint(eprintf, stderr, sender);
    }
    send_cloud_message(&response);

} /* process_local_lock_req */

/* we asked another node to grant us a lock.  they responded in the affirmative
 * with this message.  look for a pending lock request to that node.
 * if not found, tell the other guy we are releasing his lock and give up.
 * if found, delete that pending request and add a lock on the other guy
 * to our list of locks held.
 */
void process_local_lock_grant(message_t *message, int device_index)
{
    int i;
    mac_address_ptr_t sender;
    char found;
    message_t response;
    message_type_t pending_type;
    memset(&response, 0, sizeof(response));

    if (db[3].d) { ddprintf("process_local_lock_grant\n"); }

    sender = get_name(device_index, message->eth_header.h_source);

    if (sender == NULL) {
        ddprintf("process_local_lock_grant:  get_name"
                " returned null\n");
        return;
    }

    if (db[21].d) {
        ddprintf("process_local_lock_grant; message type %s from ",
                message_type_string(message->message_type));
        mac_dprint(eprintf, stderr, sender);
    }

    found = 0;
    for (i = 0; i < pending_request_count; i++) {
        lockable_resource_t *p = &pending_requests[i];
        if (mac_equal(sender, p->node_1)
            && (p->type == local_lock_req_new_msg
                || p->type == local_lock_req_old_msg))
        {
            found = 1;
            pending_type = pending_requests[i].type;
            break;
        }
    }

    if (!found) {
        if (db[3].d || db[21].d) {
            ddprintf("process_local_lock_grant:  didn't find "
                    "pending message; release lock and forget it\n");
        }
        response.message_type = local_lock_release_msg;
        mac_copy(response.dest, sender);
        send_cloud_message(&response);

        goto done;
    }

    if (pending_type == local_lock_req_new_msg
        || pending_type == local_lock_req_old_msg)
    {
        process_local_connect(message, sender);
    }

    done :;
} /* process_local_lock_grant */

/* as part of clearing any pending stp update state, send messages to any
 * cloud boxes we had been granted locks from that we release those locks.
 */
static void send_lock_release_messages()
{
    int i;

    for (i = 0; i < locks_owned_count; i++) {
        lockable_resource_t *l = &locks_owned[i];
        message_t response;
        memset(&response, 0, sizeof(response));

        response.message_type = local_lock_release_msg;
        mac_copy(response.dest, l->node_1);
        send_cloud_message(&response);
    }
}

/* totally clean out any pending stp-change state.  'doing_stp_state()'
 * should return false after this.  this routine assumes that we only
 * do at most one stp change at a time.  we clear out all locks owned,
 * locks granted, and any pending requests that have to do with changing
 * stp connectivity.
 */
void clear_stp_update_state()
{
    bool_t did_something;

    send_lock_release_messages();

    locks_owned_count = 0;
    locks_granted_count = 0;

    do {
        int i, j;
        did_something = false;

        for (i= 0; i < pending_request_count; i++) {
            lockable_resource_t *l = &pending_requests[i];

            if (l->type == local_stp_add_request_msg
                || l->type == local_lock_req_new_msg
                || l->type == local_lock_req_old_msg
                || l->type == local_stp_add_changed_request_msg
                || l->type == local_stp_delete_request_msg)
            {

                for (j = i; j < pending_request_count - 1; j++) {
                    pending_requests[j] = pending_requests[j + 1];
                }
                pending_request_count--;

                did_something = true;
                break;
            }
        }
    } while (did_something);

    reset_lock_timer();
}

/* we are the guy in the middle on a local improvement.  we sent messages
 * to two neighbor nodes requesting locks from both of them.
 * one of them sent back a message refusing our lock request.
 * so, we cancel the whole attempt and clean up stp-changing state
 * to start over fresh later.
 */
void process_local_lock_deny(message_t *message, int device_index)
{
    mac_address_ptr_t sender;
    bool_t found_sender = true;

    ddprintf("process_local_lock_deny\n");

    sender = get_name(device_index, message->eth_header.h_source);

    if (sender == NULL) {
        ddprintf("process_local_lock_deny:  get_name returned null\n");
        found_sender = false;
    }

    clear_stp_update_state();
}

/* doing a local improvement; this routine processes affirmative messages
 * the guy in the middle hears from the other two guys.
 * if we already had the lock from the other guy, clean up the pending
 * request we were waiting from from this guy, the lock we held on the
 * other guy, and finish up by sending local_stp_add_changed_request_msg
 * to new guy and local_stp_delete_request_msg to old guy.
 * (if this is the first response from the two guys, set the lock on him
 * and wait to hear from the second guy.)
 */
void process_local_connect(message_t *message, mac_address_ptr_t sender)
{
    char have_other_lock;
    int i, pend_ind, lock_ind;
    message_type_t pending_type, other_type, pending_response, other_response;
    char found;
    mac_address_t old_mac_address, new_mac_address;

    // if (db[10].d) {
        ddprintf("process_local_connect..\n");
        print_state();
    // }

    found = 0;
    for (pend_ind = 0; pend_ind < pending_request_count; pend_ind++) {
        if (mac_equal(sender, pending_requests[pend_ind].node_1)) {
            found = 1;
            pending_type = pending_requests[pend_ind].type;
            break;
        }
    }

    if (!found) {
        ddprintf("process_local_connect; didn't find pending request\n");
        goto done;
    }

    if (pending_type == local_lock_req_new_msg) {
        other_type = local_lock_req_old_msg;
        pending_response = local_add_release_msg;
        other_response = local_delete_release_msg;

    } else if (pending_type == local_lock_req_old_msg) {
        other_type = local_lock_req_new_msg;
        pending_response = local_delete_release_msg;
        other_response = local_add_release_msg;

    } else {
        ddprintf("process_local_connect; found pending request of weird type "
                "%s\n", message_type_string(pending_type));
        goto done;
    }

    have_other_lock = 0;
    for (lock_ind = 0; lock_ind < locks_owned_count; lock_ind++) {
        if (locks_owned[lock_ind].type == other_type) {
            have_other_lock = 1;
            break;
        }
    }

    if (have_other_lock) {
        // message_t response;

        // if (db[3].d) {
            ddprintf("local_add_release_msg.. have other lock.\n");
            print_state();
        // }

        if (pending_type == local_lock_req_new_msg) {
            mac_copy(new_mac_address, pending_requests[pend_ind].node_1);
            mac_copy(old_mac_address, locks_owned[lock_ind].node_1);
        } else {
            mac_copy(old_mac_address, pending_requests[pend_ind].node_1);
            mac_copy(new_mac_address, locks_owned[lock_ind].node_1);
        }
        ddprintf("old_mac_address:  ");
        mac_dprint(eprintf, stderr, old_mac_address);
        ddprintf("new_mac_address:  ");
        mac_dprint(eprintf, stderr, new_mac_address);

        #if 0
        response.message_type = pending_response;
        mac_copy(response.dest, pending_requests[pend_ind].node_1);
        send_cloud_message(&response);

        response.message_type = other_response;
        mac_copy(response.dest, locks_owned[lock_ind].node_1);
        send_cloud_message(&response);
        #endif

        // add_stp_link(new_mac_address);

        delete_me = old_mac_address;
        trim_list(stp_list, &stp_list_count, delete_node, NULL);

        ddprintf("process_local_connect.. have both locks now..\n");

        #if 0
        for (i = pend_ind; i < pending_request_count - 1; i++) {
            pending_requests[i] = pending_requests[i + 1];
        }
        pending_request_count--;

        for (i = lock_ind; i < locks_owned_count - 1; i++) {
            locks_owned[i] = locks_owned[i + 1];
        }
        locks_owned_count--;

        reset_lock_timer();
        #endif

        local_stp_add_request(new_mac_address,
                local_stp_add_changed_request_msg);
        local_stp_add_request(old_mac_address, local_stp_delete_request_msg);

    } else {
        ddprintf("process_local_connect.. waiting for other lock.\n");
    }

    if (locks_owned_count >= MAX_CLOUD) {
        ddprintf("process_local_connect:  too many locks owned.\n");
        /* XXX clean up explicitly rather than letting everything time out?
         */
        to_nominal_state();
        goto done;
    }
    locks_owned[locks_owned_count++] = pending_requests[pend_ind];
    set_lock_timer(&locks_owned[locks_owned_count - 1]);

    for (i = pend_ind; i < pending_request_count - 1; i++) {
        pending_requests[i] = pending_requests[i + 1];
    }
    pending_request_count--;

    reset_lock_timer();

    done :;

    // if (db[10].d) {
        ddprintf("process_local_connect returning; state:\n");
        print_state();
    // }
} /* process_local_connect */

/* get the name of the guy sending the message (give up if can't).
 * trim first pending request to the guy that we can find in our
 * pending_requests list.  (?)
 * (if no pending request found, return.)
 * (maybe should check that it's the right kind of pending request)
 *
 * do an add_stp_link.  (re-init link statistics.)
 *
 * to update other nodes' model of the new topology,
 *     send_stp_beacon (send my beacon).
 *     send_stp_beacons (send all beacons I have from my old sub-graph
 *     to the sender and thence to all nodes in the sender's old sub-graph).
 */
void process_local_stp_added(message_t *message, int device_index)
{
    int i, j;
    mac_address_ptr_t sender;
    char found = 0;

    // ddprintf("process_local_lock_add_release\n");

    sender = get_name(device_index, message->eth_header.h_source);

    if (sender == NULL) {
        ddprintf("process_local_lock_add_release:  get_name returned"
                " null\n");
        goto done;
    }

    if (db[21].d) {
        ddprintf("process_local_stp_added; message type %s from ",
                message_type_string(message->message_type));
        mac_dprint(eprintf, stderr, sender);
        print_state();
    }

    for (i = 0; i < pending_request_count; i++) {
        lockable_resource_t *p = &pending_requests[i];
        if (mac_equal(sender, p->node_1)
            && p->type == local_stp_add_request_msg)
        {
            found = 1;
            break;
        }
    }

    if (!found) {
        message_t msg;
        memset(&msg, 0, sizeof(msg));
        ddprintf("process_local_stp_added; did not find a pending request.\n");
        // #if 0
        ddprintf("    telling other guy to delete his end of the stp arc\n");

        msg.message_type = stp_arc_delete_msg;
        mac_copy(msg.dest, sender);
        send_cloud_message(&msg);
        // #endif

        goto done;
    }

    if (db[21].d) {
        lockable_resource_t *p = &pending_requests[i];
        ddprintf("process_local_stp_added; deleting pending request %s "
                "to ", message_type_string(p->type));
        mac_dprint(eprintf, stderr, p->node_1);
    }

    for (j = i; j < pending_request_count - 1; j++) {
        pending_requests[j] = pending_requests[j + 1];
    }
    pending_request_count--;
    reset_lock_timer();

    add_stp_link(sender);

    /* immediately send out an stp beacon to let everyone reachable know
     * and try to reduce the probability that some other neighbor from
     * the other side will try to connect to me.
     *
     * but only if we are connecting previously unconnected subgraphs.
     * (don't do this if we are re-wiring a connected subgraph.)
     */
    send_stp_beacon(false /* no disconnected nbr */);
    send_stp_beacons(sender);

    print_state();

    done :;
} /* process_local_stp_added */

/* send a message of type "type" to the cloud box "node_mac".
 * add the request to pending_requests, and if it is not a 
 * local_stp_add_request_msg set the request to time out.
 *
 * handle local_stp_add_request_msg
 *        local_stp_add_changed_request_msg
 *        local_stp_delete_request_msg
 *
 * send the message to the other guy.
 * if the send attempt came back with 'unreachable', give up.
 * create a pending request entry.  (reuse old pending request entry of
 *     same type if there is one.)
 * if the request type is local_stp_add_request_msg, set timeout to -1.
 *     after box's idea of time gets a little bigger than right after midnight,
 *     this means the pending request times out the first time
 *     timeout_lockables() gets called.  (happens in post_repeated_cloud_maint,
 *     before check_connectivity().  i guess we hope to hear back from the
 *     other guy before we do another interrupt ourselves.)
 * otherwise, set the timeout on the pending request.
 */
void local_stp_add_request(mac_address_t node_mac, message_type_t type)
{
    lockable_resource_t *p;
    message_t message;
    int i, pend_ind;
    int result;
    bool_t have_pend_ind = false;

    if (db[3].d || db[21].d) {
        ddprintf("local_stp_add_request %s to ", message_type_string(type));
        mac_dprint(eprintf, stderr, node_mac);
    }

    memset(&message, 0, sizeof(message));

    for (i = 0; i < pending_request_count; i++) {
        if (mac_equal(pending_requests[i].node_1, node_mac)
            && pending_requests[i].type == type)
        {
            DEBUG_SPARSE_PRINT(
                ddprintf("local_stp_add_request(%d);  request type %s to ",
                        print_count, message_type_string(type));
                mac_dprint(eprintf, stderr, node_mac);
                ddprintf("already pending; doing but with current pending.\n");
            )

            pend_ind = i;
            have_pend_ind = true;
            break;
        }
    }

    message.message_type = type;
    mac_copy(message.dest, node_mac);

    if (db[21].d) {
        print_msg("local_stp_add_request", &message);
    }

    result = send_cloud_message(&message);

    if (result == 0 || errno != EHOSTUNREACH) {
        if (have_pend_ind) {
            p = &pending_requests[pend_ind];
        } else {
            if (pending_request_count > MAX_CLOUD) {
                ddprintf("local_stp_add_request:  too many pending requests\n");
                return;
            }
            p = &pending_requests[pending_request_count++];
        }

        p->type = type;
        mac_copy(p->node_1, node_mac);

        if (type == local_stp_add_request_msg) {
            pending_requests[pending_request_count - 1].sec = -1;
        } else {
            set_lock_timer(&pending_requests[pending_request_count - 1]);
        }

        if (db[21].d) {
            ddprintf("local_stp_add_request:  new pending request %s to node ",
                    message_type_string(type));
            mac_dprint(eprintf, stderr, node_mac);
        }
    } else {
        if (db[21].d) {
            ddprintf("local_stp_add_request:  no route; not adding pending "
            " request %s to node ", message_type_string(type));
            mac_dprint(eprintf, stderr, node_mac);
        }
    }

    if (db[3].d) { print_state(); }

} /* local_stp_add_request */

/* if we have any pending local_stp_add_request_msg's, trim them.
 * we've gotten a connect request from another box, and these
 * local_stp_add_request_msg are light-weight things that may be waiting
 * around for a response from a box that is sending beacons but not running
 * cloud software.  (i.e., a potential ad-hoc client.)
 */
static void trim_pending_subgraph_connect_invitations()
{
    bool_t did_something;
    
    do {
        int i, j;
        did_something = false;
        for (i = 0; i < pending_request_count; i++) {
            lockable_resource_t *p = &pending_requests[i];
            if (p->type == local_stp_add_request_msg) {

                for (j = i; j < pending_request_count - 1; j++) {
                    pending_requests[j] = pending_requests[j + 1];
                }
                pending_request_count--;
                reset_lock_timer();

                did_something = true;
                break;
            }
        }
    } while (did_something);
}

/* got a request from another box to build an stp arc.
 *
 * if the other box wants to build a new arc to connect two previously
 * unconnected sub-graphs, the message will be 'local_stp_add_request_msg'.
 *
 * if the other box wants to improve connectivity by deleting an arc and
 * replacing it with a better one, message will be 
 * 'local_stp_add_changed_request_msg', which we process.
 * (we should find that we had granted the guy a lock in that case.
 * we free the lock.)
 * (process_local_stp_delete_request() processes the message to delete the
 * old other stp arc to the other box.)
 */
void process_local_stp_add_request(message_t *message, int device_index)
{
    int i;
    mac_address_ptr_t sender;
    bool_t already_linked = false;
    char found = 0;
    bool_t lock_granted;
    bool_t deny = false;
    message_t response;

    struct timeval tv;
    struct timezone tz;

    memset(&response, 0, sizeof(response));

    // ddprintf("process_local_stp_add_request\n");

    sender = get_name(device_index, message->eth_header.h_source);

    if (sender == NULL) {
        ddprintf("process_local_stp_add_request:  get_name returned"
                " null\n");
        goto done;
    }

    if (db[21].d) {
        ddprintf("process_local_stp_add_request; message type %s from ",
                message_type_string(message->message_type));
        mac_dprint(eprintf, stderr, sender);
        ddprintf("h_dest ");
        mac_dprint(eprintf, stderr, message->eth_header.h_dest);
    }

    if (gettimeofday(&tv, &tz)) {
        printf("process_local_stp_add_request:  gettimeofday failed\n");
        return;
    }

    /* if this is a connect-subgraphs invitation, get rid of any
     * pending connect-subgraphs invitations we have issued, on the
     * chance that they are to zombie nodes.  if by chance we get a
     * response from one of these guys, we won't remember the pending
     * invitation, so we will just tell them to tear down their end.
     */

     if (message->message_type == local_stp_add_request_msg) {
        trim_pending_subgraph_connect_invitations();
     }

    /* if this is local-improvement, if things are normal we will have
     * granted him a lock.  one way or the other, we don't need it any more.
     * we're done with the local-improvement attempt.
     */
    lock_granted = false;
    for (i = 0; i < locks_granted_count; i++) {
        if (mac_equal(sender, locks_granted[i].node_1)) {
            if (db[21].d) {
                ddprintf("    found lock that had been granted to ");
                mac_dprint(eprintf, stderr, sender);
            }
            lock_granted = true;
            break;
        }
    }

    if (lock_granted) {
        int j;
        for (j = i; j < locks_granted_count - 1; j++) {
            locks_granted[j] = locks_granted[j + 1];
        }
        locks_granted_count--;
        reset_lock_timer();
    }

    /* was there anything else going on besides that granted lock? */
    if (doing_stp_update()) {
        deny = true;
    }

    /* is this from a node that we've been getting beacons from?  if so,
     * it may be that he just timed out our beacon.  if he isn't an
     * stp neighbor of ours, deny the request.  he'll get an stp beacon
     * from us eventually and be happy.  he shouldn't have an arc to us.
     * if we think he is an stp neighbor, grant him the request, so that
     * he rebuilds his arc to us.  we will say to his add_release message
     * that we already have the arc, but that's OK.
     */
    if (!deny) {

        /* do we have a beacon from this guy? */
        for (i = 0; i < stp_recv_beacon_count; i++) {
            if (mac_equal(stp_recv_beacons[i].stp_beacon.originator, sender))
            {
                found = 1;
                break;
            }
        }

        if (found) {
            ddprintf("\n\nGOT A CONNECT REQUEST FROM A GUY WE HAVE "
                    "A BEACON FROM.\n");
        }

        /* is he one of our stp neighbors? */
        if (found) {
            for (i = 0; i < stp_list_count; i++) {
                if (mac_equal(stp_list[i].box.name, sender)) {
                    already_linked = true;
                    found = 0;
                    break;
                }
            }
        }

        if (found && !lock_granted) {
            ddprintf("HE'S NOT AN STP NEIGHBOR; NO LOCK GRANTED.  REFUSING.\n");
            deny = true;
        } else if (!found) {
            ddprintf("NOT FOUND.  GRANTING.\n");
        } else /* found && lock_granted */ {
            ddprintf("HE'S NOT AN STP NEIGHBOR, BUT LOCK GRANTED.  GRANTING.\n");
        }
    }

    if (deny) {
        ddprintf("process_local_stp_add_request; denying..\n");
        response.message_type = local_stp_refused_msg;

    } else if (message->message_type == local_stp_add_request_msg) {
        ddprintf("process_local_stp_add_request; adding..\n");
        response.message_type = local_stp_added_msg;

    } else {
        ddprintf("process_local_stp_add_request; add_changing..\n");
        response.message_type = local_stp_added_changed_msg;
    }
    mac_copy(response.dest, sender);

    if (db[21].d) {
        ddprintf("process_local_stp_add_request; sending message %s to node ",
                message_type_string(response.message_type));
        mac_dprint(eprintf, stderr, sender);
    }
    send_cloud_message(&response);


    if (!deny) {
        /* do this even if we already had an stp link to this guy,
         * to reset flow control counts in the stp link
         */
        add_stp_link(sender);

        if (message->message_type == local_stp_add_request_msg) {
            /* immediately send out an stp beacon to let everyone reachable know
             * and try to reduce the probability that some other neighbor from
             * the other side will try to connect to me.
             *
             * (only do this if we are connecting previously unconnected
             * subgraphs; if we are rearranging arcs in a connected subgraph,
             * don't do this.)
             */
            send_stp_beacon(false /* no disconnected nbr */);
            send_stp_beacons(sender);
        }
    }

    print_state();

    done :;
}

/* if the link was already there, delete it.
 * if couldn't find the guy in our nbr_list, give up.
 * add the link in, time-stamp it, and initialize link state.
 */
bool_t add_stp_link(mac_address_ptr_t sender)
{
    struct timeval tv;
    bool_t result;
    cloud_box_t *p;
    char found;
    int i;

    if (db[21].d) {
        ddprintf("add_stp_link to node ");
        mac_dprint(eprintf, stderr, sender);
    }

    found = 0;
    for (i = 0; i < stp_list_count; i++) {
        if (mac_equal(stp_list[i].box.name, sender)) {
            found = 1;
            break;
        }
    }

    if (found) {
        ddprintf("add_stp_link:  already have stp link; delete and re-add\n");
        delete_me = stp_list[i].box.name;
        trim_list(stp_list, &stp_list_count, delete_node, NULL);
    }

    if (stp_list_count >= MAX_CLOUD) {
        ddprintf("add_stp_link:  too many stp links from this node.\n");

        goto finish;
    }

    p = &stp_list[stp_list_count].box;

    found = 0;
    for (i = 0; i < nbr_device_list_count; i++) {
        if (mac_equal(sender, nbr_device_list[i].name)) {
            found = 1;
            break;
        }
    }

    if (!found) {
        ddprintf("add_stp_link:  could not find node as a neighbor.\n");

        result = false;
        goto finish;
    }

    stp_list[stp_list_count].box = nbr_device_list[i];
    ddprintf("perm_io_stat %d\n",
            stp_list[stp_list_count].box.perm_io_stat_index);

    while (!checked_gettimeofday(&tv));
    stp_list[stp_list_count].sec = tv.tv_sec;
    stp_list[stp_list_count].usec = tv.tv_usec;

    stp_list[stp_list_count].box.send_sequence = 0;
    stp_list[stp_list_count].box.recv_sequence = 0;
    stp_list[stp_list_count].box.recv_sequence_error = 0;
    stp_list[stp_list_count].box.send_error = 0;
    stp_list[stp_list_count].box.recv_error = 0;
    stp_list[stp_list_count].box.received_duplicate = false;
    stp_list[stp_list_count].box.awaiting_ack = false;
    stp_list[stp_list_count].box.unroutable_count = 0;
    stp_list[stp_list_count].box.pending_ack = false;
    stp_list[stp_list_count].box.expect_seq = true;

    // print_cloud_list(stp_list, stp_list_count, "stp list:");
    result = true;

    stp_list_count++;

    finish :
    return result;
}

/* in subgraph-connection mode, we got a refusal to our request to build
 * an stp link.
 *
 * in local improvement mode, we had been granted both locks.  but, one
 * of those guys then refused the change to the stp arcs.
 *
 * in either case, send lock releases if we have any locsk and
 * clear all stp-update state.
 */
void process_local_stp_refused(message_t *message, int device_index)
{
    mac_address_ptr_t sender;

    // ddprintf("process_local_stp_refused\n");

    sender = get_name(device_index, message->eth_header.h_source);

    if (sender == NULL) {
        ddprintf("process_local_stp_refused:  get_name returned null\n");
    }

    if (db[21].d) {
        ddprintf("process_local_stp_refused; message type %s from ",
                message_type_string(message->message_type));
        mac_dprint(eprintf, stderr, sender);
    }

    clear_stp_update_state();

    print_state();
}

/* if can't find name of sender, return.
 * get rid of all pending requests to the other guy.
*/
void process_local_stp_deleted(message_t *message, int device_index)
{
    bool_t found;
    mac_address_ptr_t sender;

    sender = get_name(device_index, message->eth_header.h_source);

    if (sender == NULL) {
        ddprintf("process_local_stp_deleted:  get_name returned null\n");
        goto done;
    }

    if (db[21].d) {
        ddprintf("process_local_stp_deleted; message type %s from ",
                message_type_string(message->message_type));
        mac_dprint(eprintf, stderr, sender);
        print_state();
    }

    /* get rid of all pending requests to the guy we got this message from. */
    while (true) {
        int i, j;
        found = false;
        for (i = 0; i < pending_request_count; i++) {
            if (mac_equal(sender, pending_requests[i].node_1)) {
                if (db[21].d) {
                    ddprintf("    found pending request to ");
                    mac_dprint(eprintf, stderr, sender);
                }
                found = true;
                break;
            }
        }
        if (!found) { break; }

        for (j = i; j < pending_request_count - 1; j++) {
            pending_requests[j] = pending_requests[j + 1];
        }
        pending_request_count--;
        reset_lock_timer();
    }

    done:;
    if (db[21].d) {
        ddprintf("process_local_stp_deleted done;\n");
        print_state();
    }
}

/* if can't get sender's name, return.
 *
 * get rid of all granted locks to the other guy.
 *
 * if we have locks granted to someone else,
 * or if we have a pending request to the other guy that is not
 *     local_stp_delete_request_msg
 * or if we own a lock to the other guy,
 *     send local_stp_refused_msg to the other guy.
 *
 * otherwise,
 *     send local_stp_deleted_msg to the other guy and delete the stp arc
 *     to him.
 */
void process_local_stp_delete_request(message_t *message, int device_index)
{
    int i;
    mac_address_ptr_t sender;
    char found = 0;
    message_t response;
    memset(&response, 0, sizeof(response));

    // ddprintf("process_local_delete_add_request\n");

    sender = get_name(device_index, message->eth_header.h_source);

    if (sender == NULL) {
        ddprintf("process_local_stp_delete_request:  get_name returned null\n");
        goto done;
    }

    if (db[21].d) {
        ddprintf("process_local_stp_delete_request; message type %s from ",
                message_type_string(message->message_type));
        mac_dprint(eprintf, stderr, sender);
    }

    /* get rid of all granted locks to the guy we got this message from */
    while (true) {
        found = 0;
        for (i = 0; i < locks_granted_count; i++) {
            if (mac_equal(sender, locks_granted[i].node_1)) {
                if (db[21].d) {
                    ddprintf("    found granted lock to ");
                    mac_dprint(eprintf, stderr, sender);
                }
                found = 1;
                break;
            }
        }
        if (!found) { break; }

        if (found) {
            int j;
            for (j = i; j < locks_granted_count - 1; j++) {
                locks_granted[j] = locks_granted[j + 1];
            }
            locks_granted_count--;
            reset_lock_timer();
            found = 0;
        }
    }

    found = 0;

    if (locks_granted_count > 0) {
        found = 1;
    }

    if (!found) {
        for (i = 0; i < pending_request_count; i++) {
            if (mac_equal(sender, pending_requests[i].node_1)) {
                if (pending_requests[i].type != local_stp_delete_request_msg) {
                    if (db[21].d) {
                        ddprintf("    different request already pending to ");
                        mac_dprint(eprintf, stderr, sender);
                    }
                    found = 1;
                    break;
                }
            }
        }
    }

    if (!found) {
        for (i = 0; i < locks_owned_count; i++) {
            if (mac_equal(sender, locks_owned[i].node_1)) {
                if (db[21].d) {
                    ddprintf("    found lock owned to ");
                    mac_dprint(eprintf, stderr, sender);
                }
                found = 1;
                break;
            }
        }
    }

    if (found) {
        ddprintf("process_local_stp_delete_request; denying..\n");
        response.message_type = local_stp_refused_msg;
    } else {
        ddprintf("process_local_stp_delete_request; granting..\n");
        response.message_type = local_stp_deleted_msg;
    }
    mac_copy(response.dest, sender);

    if (db[21].d) {
        ddprintf("process_local_stp_delete_request; sending message %s to node ",
                message_type_string(response.message_type));
        mac_dprint(eprintf, stderr, sender);
    }
    send_cloud_message(&response);


    if (!found) {
        delete_me = sender;
        trim_list(stp_list, &stp_list_count, delete_node, NULL);
    }

    print_state();

    done :;

} /* process_local_stp_delete_request */

/* we had granted a lock to another cloud box that wanted to change the
 * cloud topology.  we have received a message from that box to successfully
 * complete the change.  in response, we delete the granted lock, and
 * build our side of the new stp connection.
 * we send out our stp beacon and all stp beacons we have from other
 * cloud boxes, so that other cloud boxes can update their model of the
 * new cloud topology.
 */
void process_local_lock_add_release(message_t *message, int device_index)
{
    int i, j;
    mac_address_ptr_t sender;
    char found = 0;

    // ddprintf("process_local_lock_add_release\n");

    sender = get_name(device_index, message->eth_header.h_source);

    if (sender == NULL) {
        ddprintf("process_local_lock_add_release:  get_name returned"
                " null\n");
        goto done;
    }

    if (db[21].d) {
        ddprintf("process_local_lock_add_release; message type %s from ",
                message_type_string(message->message_type));
        mac_dprint(eprintf, stderr, sender);
    }

    for (i = 0; i < locks_granted_count; i++) {
        if (mac_equal(sender, locks_granted[i].node_1)) {
            found = 1;
            break;
        }
    }

    if (!found) {
        ddprintf("process_local_lock_add_release:  "
                "did not find a granted lock.\n");

        goto done;
    }

    if (db[21].d) {
        lockable_resource_t *p = &locks_granted[i];
        ddprintf("process_local_lock_add_release; deleting granted request %s "
                "from ", message_type_string(p->type));
        mac_dprint(eprintf, stderr, p->node_1);
    }

    for (j = i; j < locks_granted_count - 1; j++) {
        locks_granted[j] = locks_granted[j + 1];
    }
    locks_granted_count--;
    reset_lock_timer();

    add_stp_link(sender);

    /* immediately send out an stp beacon to let everyone reachable know
     * and try to reduce the probability that some other neighbor from
     * the other side will try to connect to me.
     */
    send_stp_beacon(false /* no disconnected nbr */);
    send_stp_beacons(sender);

    print_state();

    done :;
}

/* end of a redo of the spanning tree.  delete a spanning tree arc. */
void process_local_lock_delete_release(message_t *message,
        int device_index)
{
    int i, j;
    mac_address_ptr_t sender;
    char found = 0;

    ddprintf("process_local_lock_delete_release\n");

    sender = get_name(device_index, message->eth_header.h_source);

    if (sender == NULL) {
        ddprintf("process_local_lock_delete_release:  get_name"
                " returned null\n");
        goto done;
    }

    for (i = 0; i < locks_granted_count; i++) {
        if (mac_equal(sender, locks_granted[i].node_1)) {
            found = 1;
            break;
        }
    }

    if (!found) {
        ddprintf("process_local_lock_delete_release:  "
                "did not find a granted lock.\n");

        goto done;
    }

    for (j = i; j < locks_granted_count - 1; j++) {
        locks_granted[j] = locks_granted[j + 1];
    }
    locks_granted_count--;
    reset_lock_timer();

    ddprintf("trimming ");
    mac_dprint(eprintf, stderr, sender);
    node_list_print(eprintf, stderr, "stp_list before", stp_list,
            stp_list_count);
    delete_me = sender;
    trim_list(stp_list, &stp_list_count, delete_node, NULL);
    node_list_print(eprintf, stderr, "stp_list after", stp_list,
            stp_list_count);

    print_state();

    done :;

} /* process_local_lock_delete_release */

/* we had presumably granted a lock to another cloud box.  we've received
 * a message from that cloud box telling us to free that lock, so we do so.
 */
void process_local_lock_release(message_t *message, int device_index)
{
    int i, j;
    mac_address_ptr_t sender;
    bool_t found;

    ddprintf("process_local_lock_release\n");

    sender = get_name(device_index, message->eth_header.h_source);

    if (sender == NULL) {
        ddprintf("process_local_lock_release:  get_name"
                " returned null\n");
        return;
    }

    do {
        found = false;
        for (i = 0; i < locks_granted_count; i++) {
            if (mac_equal(sender, locks_granted[i].node_1)) {
                found = true;
                break;
            }
        }

        if (found) {
            for (j = i; j < locks_granted_count - 1; j++) {
                locks_granted[j] = locks_granted[j + 1];
            }
            locks_granted_count--;

            reset_lock_timer();
        }
    } while (found);
}

/* go through stp_recv_beacons, and for any stp beacon we received from
 * old_mac_address, change that to new_mac_address.
 *
 * this routine is called after a local improvement, and we want to change
 * our state to reflect this change.  (make stp beacons that we received
 *in from old_mac_address look like they came from new_mac_address).
 */
static void fix_recv_beacon_nbr(mac_address_t old_mac_address,
        mac_address_t new_mac_address)
{
    int i;

    for (i = 0; i < stp_recv_beacon_count; i++) {
        if (mac_equal(stp_recv_beacons[i].neighbor, old_mac_address)) {
            mac_copy(stp_recv_beacons[i].neighbor, new_mac_address);
        }
    }
}

/* heard back from the cloud box we want to add an stp arc to that he
 * agreed and built his end, and finished the protocol as far as he was
 * concerned.  we also consider the protocol successfully completed.
 * we release the locks we owned to the old and new cloud boxes, build
 * the stp arc to the new cloud box, and do some magic locally to update
 * our local model of the cloud topology.  (we change our local stp beacon
 * table to make it look like stp beacons that had come in from the old
 * cloud box came in from the new cloud box instead.)  and, we send out
 * stp beacons so that other cloud boxes can update their models of the
 * overall cloud topology.
 */
void process_local_stp_added_changed(message_t *message, int device_index)
{
    int i, past_packed;
    mac_address_ptr_t sender;
    int new_ind = -1, old_ind = -1;
    mac_address_t new, old;

    // ddprintf("process_local_stp_added_changed\n");

    sender = get_name(device_index, message->eth_header.h_source);

    if (sender == NULL) {
        ddprintf("process_local_lock_add_release:  get_name returned null\n");
        to_nominal_state();
        goto done;
    }

    if (db[21].d) {
        ddprintf("process_local_stp_added_changed; message type %s from ",
                message_type_string(message->message_type));
        mac_dprint(eprintf, stderr, sender);
        print_state();
    }

    /* get rid of the pending request that this message is in response to. */
    past_packed = 0;
    for (i = 0; i < pending_request_count; i++) {
        lockable_resource_t *p = &pending_requests[i];
        if (p->type != local_stp_add_changed_request_msg
            || !mac_equal(sender, p->node_1))
        {
            /* keep it. */
            if (past_packed != i) {
                pending_requests[past_packed] = pending_requests[i];
            }
            past_packed++;
        } else {
            /* delete it. */
        }
    }
    pending_request_count = past_packed;

    /* find the locks we own for the two other cloud boxes we are
     * swapping stp arcs to.
     */
    for (i = 0; i < locks_owned_count; i++) {
        lockable_resource_t *p = &locks_owned[i];
        if (mac_equal(sender, p->node_1)
            && p->type == local_lock_req_new_msg)
        {
            new_ind = i;
            mac_copy(new, p->node_1);
        }

        if (p->type == local_lock_req_old_msg) {
            old_ind = i;
            mac_copy(old, p->node_1);
        }
    }

    /* if we couldn't find both of them, eek!  to nominal state. */
    if (old_ind == -1 || new_ind == -1) {
        message_t msg;
        memset(&msg, 0, sizeof(msg));
        ddprintf("process_local_stp_added_changed; "
                "did not find both locks.\n");
        // #if 0
        ddprintf("    telling other guy to delete his end of the stp arc\n");

        msg.message_type = stp_arc_delete_msg;
        mac_copy(msg.dest, sender);
        send_cloud_message(&msg);
        // #endif

        to_nominal_state();
        goto done;
    }

    /* delete both of the locks from our locks_owned list; we're done
     * with them.
     */
    past_packed = 0;
    for (i = 0; i < locks_owned_count; i++) {
        if (i != old_ind && i != new_ind) {
            /* keep it. */
            if (past_packed != i) {
                locks_owned[past_packed] = locks_owned[i];
            }
            past_packed++;
        } else {
            /* delete it. */
        }
    }
    locks_owned_count = past_packed;
    reset_lock_timer();

    /* we had already deleted the stp link to the old guy.
     * add the stp link to the new guy.
     */
    add_stp_link(sender);

    /* we now want to make sure that the stp_recv_beacons throughout the
     * new tree correctly reflect the new tree's topology.  (For the N
     * cloud nodes, each one has a list of N-1 received stp beacons from
     * the other nodes, and each of those beacons is labeled with the
     * stp neighbor it was received from.  That latter is crucial information
     * to the node about the overall shape of the cloud, and decisions get
     * made based on it.  So, we need to fix the tables in all of the
     * other nodes that may have changed.  in our local table, every
     * received beacon that came in from 'old' is made to look like it
     * came from 'new', and every received beacon that came from somewhere
     * other than old is sent to new.  and, we need to send a beacon from
     * ourselves to new.  it's easier to just send it to everybody.
     */
    fix_recv_beacon_nbr(old, new);
    send_stp_beacon(false /* no disconnected nbr */);
    send_stp_beacons(new);

    ddprintf("process_local_stp_added_changed done; state:\n");
    print_state();

    done :;
} /* process_local_stp_added_changed */

/* for those nodes we see hardware beacons from (wireless beacons or
 * eth_beacons, as indicated in nbr_device_list) but no stp beacons,
 * pick one and initiate the process of creating an stp link to it.
 */
void check_connectivity(void)
{
    struct timeval tv;
    struct timezone tz;
    int i, j;
    int count, max_count;

    if (db[9].d) {
        ddprintf("check_connectivity..\n");
        print_cloud_list(nbr_device_list, nbr_device_list_count,
                "neighbor list:");
        print_stp_recv_beacons();
    }

    if (gettimeofday(&tv, &tz)) {
        printf("expire_timed_macs:  gettimeofday failed\n");
        goto finish;
    }

    #if USE_TIMER
    timeout_stp_recv_beacons();
    #endif

    /* only do this half the time.  we want to serialize this box's cloud
     * stp maintenance process, and if there is an ad-hoc client out there
     * we don't want to always try to connect to him and prevent ever trying
     * a local improvement.
     */
    if (discrete_unif(2) == 1) {
        if (db[9].d) {
            ddprintf("not initiating a connection, so we can "
                    "try an improvement.\n");
        }
        goto finish;
    }

    /* don't initiate a connection to another sub-graph if we are already
     * doing another stp update operation.
     */
    if (doing_stp_update()) {
        if (db[9].d) {
            ddprintf("we appear to be doing an update already.  "
                    "not initiating a connection.\n");
        }
        goto finish;
    }

    /* find nodes that we can see beacons from but not stp_beacons;
     * first see how many there are, and then randomly
     * choose one of them to try to build an stp link to.
     */
    count = 0;
    for (i = 0; i < nbr_device_list_count; i++) {
        char found = 0;

        // if (tried_reconnection(nbr_device_list[i].name)) { continue; }

        for (j = 0; j < stp_recv_beacon_count; j++) {
            if (mac_equal(stp_recv_beacons[j].stp_beacon.originator,
                    nbr_device_list[i].name))
            {
                found = 1;
                break;
            }
        }

        if (!found) { count++; }
    }

    if (count == 0) {
        goto finish;
    }
    
    /* at this point, we have decided to try to stp-connect to a neighbor.
     * let everyone we are stp-connected to know, so that they (with
     * high probability) don't try to build a separate connection to the
     * same other connected subgraph.
     *
     * instead, try to squirt my stp beacons to the other sub-graph after
     * the stp arc is built.
     */
     // send_stp_beacon(true /* see disconnected neighbor */);

    max_count = discrete_unif(count);

    if (db[9].d) {
        ddprintf("check_connectivity found %d disconnected guys; "
                "unif %d\n",
                count, max_count);
    }

    count = 0;
    for (i = 0; i < nbr_device_list_count; i++) {
        char found = 0;

        // if (tried_reconnection(nbr_device_list[i].name)) { continue; }

        for (j = 0; j < stp_recv_beacon_count; j++) {
            if (mac_equal(stp_recv_beacons[j].stp_beacon.originator,
                    nbr_device_list[i].name))
            {
                found = 1;
                break;
            }
        }

        if (!found) {
            count++;
            if (db[9].d) {
                ddprintf("check_connectivity count %d, max_count %d\n",
                        count, max_count);
            }
            if (count > max_count) {
                if (db[9].d) {
                    ddprintf("    doing it (dest ");
                    mac_dprint_no_eoln(eprintf, stderr,
                            nbr_device_list[i].name);
                    ddprintf(")\n");
                }
                stp_connect_to(nbr_device_list[i].name);
                break;
            } else {
                if (db[9].d) { ddprintf("    not doing it..\n"); }
            }
        }
    }

    finish :
    if (db[9].d) {
        ddprintf("done check_connectivity..\n");
    }
} /* check_connectivity */

/* initiate the protocol of creating an stp link to another box.
 * (not improvement; connect two presumably disconnected sub-graphs.)
 * see merge_cloud.notes for some details.
 */
void stp_connect_to(mac_address_t node_mac)
{
    int i;

    if (db[3].d || db[21].d) {
        ddprintf("stp_connect_to ");
        mac_dprint(eprintf, stderr, node_mac);
    }

    for (i = 0; i < stp_list_count; i++) {
        if (mac_equal(stp_list[i].box.name, node_mac)) {
            if (db[3].d || db[21].d) {
                ddprintf("stp_connect_to:  already have stp arc; "
                        "returning.\n");
            }
            return;
        }
    }

    local_stp_add_request(node_mac, local_stp_add_request_msg);
}

#ifdef RETRY_TIMED_OUT_LOCKABLES
/* send a message to cloud box "name" indicating we have granted a lock
 * it requested.
 */
void resend_local_lock_grant_msg(mac_address_ptr_t name)
{
    message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.message_type = local_lock_grant_msg;
    mac_copy(msg.dest, name);
    send_cloud_message(&msg);
}
#endif

/* someone detected a circularity.  they want to delete this stp arc. */
void process_stp_arc_delete(message_t *message, int device_index)
{
    mac_address_ptr_t sender;
    
    ddprintf("\n\nprocess_stp_arc_delete:  clearing state!\n");

    sender = get_name(device_index, message->eth_header.h_source);

    if (sender == NULL) {
        ddprintf("process_stp_arc_delete:  could not get device name\n");
        return;
    }
    
    clear_state(sender);

} /* process_stp_arc_delete */
