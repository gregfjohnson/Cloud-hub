/* stp_beacon.c - manage periodic beacons propagated through the cloud
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: stp_beacon.c,v 1.21 2012-02-22 19:27:23 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: stp_beacon.c,v 1.21 2012-02-22 19:27:23 greg Exp $";

#include <string.h>
#include <errno.h>
#include <sys/time.h>

#include "util.h"
#include "cloud.h"
#include "print.h"
#include "ad_hoc_client.h"
#include "lock.h"
#include "html_status.h"
#include "timer.h"
#include "nbr.h"
#include "cloud_msg.h"
#include "stp_beacon.h"

/* have an entry for every box in our cloud.  so, stp_recv_beacon_count is
 * the count of the number of boxes in our cloud.
 */
stp_recv_beacon_t stp_recv_beacons[MAX_CLOUD];
int stp_recv_beacon_count = 0;

/* we had sent out an stp beacon and recorded locally that we were waiting
 * for an acknowledgement of receipt of the beacon.
 * for whatever reason, we are no longer waiting, so delete the entry
 * indicating that we are waiting.
 */
static void clear_pending_stp_beacon(mac_address_ptr_t neighbor,
        message_t *message)
{
    int i;
    bool_t did_something;

    do {
        did_something = false;
        for (i = 0; i < pending_request_count; i++) {
            lockable_resource_t *p = &pending_requests[i];
            if (p->type != stp_beacon_msg
                || !mac_equal(neighbor, p->node_1)
                /* the message is a stp_beacon_nak_msg or stp_beacon_recv_msg,
                 * containing only a message type and the source and
                 * dest mac addresses.
                 */
                /*|| !mac_equal(message->v.stp_beacon.originator,
                    p->stp_beacon.originator)*/)
            {
                continue;
            }
            did_something = true;

            for (; i < pending_request_count - 1; i++) {
                pending_requests[i] = pending_requests[i + 1];
            }
            pending_request_count--;
            break;
        }
    } while (did_something);

    reset_lock_timer();
}

/* we have sent an stp beacon to an stp neighbor.  we expect to hear back
 * an acknowledgement of receipt of our message.  this routine makes note
 * locally of our hope to hear back.
 */
void add_pending_stp_beacon(mac_address_ptr_t dest_name, message_t *beacon)
{
    #ifdef RETRY_TIMED_OUT_LOCKABLES
    int i;
    lockable_resource_t *p;
    bool_t have_stp_link;

    if (db[6].d) {
        ddprintf("add_pending_stp_beacon to ");
        mac_dprint(eprintf, stderr, dest_name);
        ddprintf("pending_requests before:\n");
        print_lockable_list(pending_requests, pending_request_count,
                "pending_requests");
    }

    have_stp_link = false;
    for (i = 0; i < stp_list_count; i++) {
        cloud_box_t *stp_link = &stp_list[i].box;
        if (mac_equal(dest_name, stp_link->name)) {
            have_stp_link = true;
            break;
        }
    }

    if (!have_stp_link) {
        // if (db[6].d) {
            ddprintf("add_pending_stp_beacon; no stp link!  "
                    "deleting any pending stp_request.\n");
        // }

        clear_pending_stp_beacon(dest_name, beacon);
        goto done;
    }

    for (i = 0; i < pending_request_count; i++) {
        lockable_resource_t *p = &pending_requests[i];
        if (p->type == stp_beacon_msg
            && mac_equal(dest_name, p->node_1)
            && mac_equal(beacon->v.stp_beacon.originator,
                    p->stp_beacon.originator))
        {
            #if 0
            ddprintf("add_pending_stp_beacon; already have pending request.\n");
            #endif
            goto done;
        }
    }

    if (pending_request_count >= MAX_CLOUD) {
        ddprintf("add_pending_stp_beacon; too many pending requests.\n");
        goto done;
    }

    p = &pending_requests[pending_request_count++];
    p->type = stp_beacon_msg;
    p->stp_beacon = beacon->v.stp_beacon;
    mac_copy(p->node_1, dest_name);
    set_lock_timer(&pending_requests[pending_request_count - 1]);

    done:

    if (db[6].d) {
        ddprintf("pending_requests after:\n");
        print_lockable_list(pending_requests, pending_request_count,
                "pending_requests");
    }
    #endif
}

/* update my_beacon based on current stp_list and send it to my current
 * stp neighbors.
 */
void send_stp_beacon(bool_t see_disconnected_nbr)
{
    int i, result;
    int count = 0;

    have_my_beacon = true;

    memset(&my_beacon, 0, sizeof(my_beacon));

    mac_copy(my_beacon.v.stp_beacon.originator, my_wlan_mac_address);

    my_beacon.message_type = stp_beacon_msg;

    my_beacon.v.stp_beacon.weakest_stp_link = my_weakest_stp_link;

    if (update_cloud_db) {
        my_beacon.v.stp_beacon.tweak_db = cloud_db_ind;
        update_cloud_db = false;

    } else if (db[38].d) {
        /* if we are trying to print cloud topology, always tell everyone
         * to turn on announcing their local cloud connectivity
         */
        my_beacon.v.stp_beacon.tweak_db = 2030;
        db[30].d = true;
    }

    my_beacon.v.stp_beacon.status_count = stp_list_count;

    /* if send cloud status with stp beacons.. (i.e., if some other box is
     * creating cloud.asp)
     *
     * or, if we are supposed to send a list of the cloud boxes we can
     * see directly with reasonable signal strength, for non-local
     * short-cutting of messages..
     */
    if (db[30].d || db[60].d) {
        int j;
        
        /* make sure a timer is set to automatically turn this off if it
         * is not periodically turned on.
         */
        ensure_disable_print_cloud();

        /* these are our own eth device and wlan device */
        for (j = 0; j < perm_io_stat_count; j++) {
            if (perm_io_stat[j].device_type == device_type_wlan
                || perm_io_stat[j].device_type == device_type_eth)
            {
                my_beacon.v.stp_beacon.status[count] = perm_io_stat[j];
                my_beacon.v.stp_beacon.status[count].neighbor_type
                        = STATUS_CLOUD_NOT_NBR;
                count++;
            }
        }

        /* these are our stp neighbors.  for each stp_list entry, find its
         * perm_io_stat entry and append that the my_beacon.
         */
        for (i = 0; i < stp_list_count; i++) {
            cloud_box_t *c = &stp_list[i].box;
            int io_stat_ind = c->perm_io_stat_index;
            if (io_stat_ind < 0 || io_stat_ind >= perm_io_stat_count
                || count >= MAX_CLOUD)
            {
                ddprintf("send_stp_beacon; bad io_stat_ind %d\n", io_stat_ind);
                continue;
            }

            my_beacon.v.stp_beacon.status[count] = perm_io_stat[io_stat_ind];
            my_beacon.v.stp_beacon.status[count].neighbor_type
                    = STATUS_CLOUD_NBR;

            for (j = 0; j < nbr_device_list_count; j++) {
                if (mac_equal(nbr_device_list[j].name,
                    my_beacon.v.stp_beacon.status[count].name))
                {
                    my_beacon.v.stp_beacon.status[count].sig_strength
                        = (byte) nbr_device_list[j].signal_strength;
                }
            }

            count++;
        }

        /* these are our non-stp-neighbors. */
        for (i = 0; i < nbr_device_list_count; i++) {
            bool_t found_um = false;
            for (j = 0; j < count; j++) {
                if (mac_equal(nbr_device_list[i].name,
                    my_beacon.v.stp_beacon.status[j].name))
                {
                    found_um = true;
                    break;
                }
            }
            if (!found_um) {
                int io_stat_ind = nbr_device_list[i].perm_io_stat_index;
                if (io_stat_ind < 0 || io_stat_ind >= perm_io_stat_count
                    || count >= MAX_CLOUD)
                {
                    ddprintf("send_stp_beacon; bad io_stat_ind %d\n",
                            io_stat_ind);
                    continue;
                }
                my_beacon.v.stp_beacon.status[count]
                        = perm_io_stat[io_stat_ind];

                /* for now, assume it's a cloud box.  will change this below
                 * if we find it's an ad-hoc client.
                 */
                my_beacon.v.stp_beacon.status[count].neighbor_type
                        = STATUS_CLOUD_NOT_NBR;

                my_beacon.v.stp_beacon.status[count].sig_strength
                    = (byte) nbr_device_list[i].signal_strength;

                count++;
            }
        }

        my_beacon.v.stp_beacon.status_count = count;
    }

    if (db[50].d) {
        ad_hoc_client_update_my_beacon(&count);
    }

    my_beacon.v.stp_beacon.status_count = count;

    if (db[6].d) {
        ddprintf("send_stp_beacon; originator ");
        mac_dprint(eprintf, stderr, my_beacon.v.stp_beacon.originator);
    }

    /* now, send this stp message out to my stp neighbors */

    for (i = 0; i < stp_list_count; i++) {
        mac_copy(my_beacon.dest, stp_list[i].box.name);
        result = send_cloud_message(&my_beacon);

        if (result == 0 || errno != EHOSTUNREACH) {
            add_pending_stp_beacon(stp_list[i].box.name, &my_beacon);
        } else {
            bump_stp_link_unroutable(stp_list[i].box.name);
        }
    }
} /* send_stp_beacon */

/* send all of the stp beacons we have received, and an stp beacon from
 * ourself, to new_nbr.  this is done when two disconnected subgraphs are
 * first connected with a new stp arc.  we want to tell the nodes in the
 * other sub-graph as soon as possible about our sub-graph, in an attempt
 * to minimize the likelihood that other connections between the sub-graphs
 * will sprout and cause cycles that we then have to clean up.
 */
void send_stp_beacons(mac_address_t new_nbr)
{
    int i, result;
    message_t message;

    memset(&message, 0, sizeof(message));

    if (db[6].d || db[21].d) {
        ddprintf("send_stp_beacons.\n");
    }

    memset(&message, 0, sizeof(message));
    message.message_type = stp_beacon_msg;
    mac_copy(message.dest, new_nbr);

    mac_copy(message.v.stp_beacon.originator, my_wlan_mac_address);
    result = send_cloud_message(&message);

    if (result == 0 || errno != EHOSTUNREACH) {
        add_pending_stp_beacon(new_nbr, &message);
    } else {
        bump_stp_link_unroutable(new_nbr);
    }

    for (i = 0; i < stp_recv_beacon_count; i++) {

        if (mac_cmp(stp_recv_beacons[i].neighbor, new_nbr) == 0) { continue; }

        mac_copy(message.v.stp_beacon.originator,
                stp_recv_beacons[i].stp_beacon.originator);
        if (db[6].d) { print_msg("    send_stp_beacons", &message); }
        result = send_cloud_message(&message);
        if (result == 0 || errno != EHOSTUNREACH) {
            add_pending_stp_beacon(new_nbr, &message);
        }
    }
} /* send_stp_beacons */

/* send a message acknowledging receipt of an stp beacon.  the sender
 * will be waiting to hear this.
 */
static void ack_stp_beacon(mac_address_ptr_t neighbor, message_t *message)
{
    message_t response = *message;

    if (db[6].d) {
        ddprintf("ack_stp_beacon; send stp_beacon_recv_msg to ");
        mac_dprint(eprintf, stderr, neighbor);
    }

    /* debug; simulate lost stp_beacon_recv_msg */
    if (db[29].d) {
        db[29].d = false;
        return;
    }

    response.message_type = stp_beacon_recv_msg;
    mac_copy(response.dest, neighbor);
    send_cloud_message(&response);
}

/* send a message back to the neighbor who sent us an stp beacon indicating
 * that we don't have the neighber listed as one of our spanning tree
 * neighbors.
 */
static void nak_stp_beacon(mac_address_ptr_t neighbor)
{
    message_t response;

    memset(&response, 0, sizeof(response));

    if (db[6].d) {
        ddprintf("nak_stp_beacon; send stp_beacon_nak_msg to ");
        mac_dprint(eprintf, stderr, neighbor);
    }

    response.message_type = stp_beacon_nak_msg;
    mac_copy(response.dest, neighbor);
    send_cloud_message(&response);
}

/* we have heard back from a cloud neighbor that they received our stp
 * beacon message, so clear our local note that we were waiting to hear.
 */
void process_stp_beacon_recv_msg(message_t *message, int device_index)
{
    mac_address_ptr_t neighbor;

    neighbor = get_name(device_index, message->eth_header.h_source);

    if (db[6].d) {
        ddprintf("process_stp_beacon_recv_msg; got recv from ");
        mac_dprint(eprintf, stderr, neighbor);
        ddprintf("pending_requests before:\n");
        print_lockable_list(pending_requests, pending_request_count,
                "pending_requests");
    }

    clear_pending_stp_beacon(neighbor, message);

    if (db[6].d) {
        ddprintf("pending_requests after:\n");
        print_lockable_list(pending_requests, pending_request_count,
                "pending_requests");
    }
} /* process_stp_beacon_recv_msg */

/* someone we thought was a spanning tree neighbor told us they aren't.
 * so, we delete the stp_list entry that points at them.
 */
void process_stp_beacon_nak_msg(message_t *message, int device_index)
{
    mac_address_ptr_t neighbor;

    neighbor = get_name(device_index, message->eth_header.h_source);

    if (db[6].d) {
        ddprintf("process_stp_beacon_nak_msg; got nak from ");
        mac_dprint(eprintf, stderr, neighbor);
        ddprintf("pending_requests before:\n");
        print_lockable_list(pending_requests, pending_request_count,
                "pending_requests");
    }

    clear_pending_stp_beacon(neighbor, message);

    delete_me = neighbor;
    trim_list(stp_list, &stp_list_count, delete_node, NULL);

    if (db[6].d) {
        ddprintf("pending_requests after:\n");
        print_lockable_list(pending_requests, pending_request_count,
                "pending_requests");
    }
} /* process_stp_beacon_nak_msg */

/* we just received an stp beacon message from a neighbor, which was
 * originated either by that neighbor or by some cloud node on the
 * other side of the neighbor.
 *
 * process information from the stp beacon locally and then
 * send it along to stp neighbors of ours other than the one we just
 * received it from.
 *
 * the local processing includes sending an ack (or nak if we think there
 * was an error), updating our local model of ad-hoc client associativity,
 * overall cloud topology from the view of the node originating the stp
 * beacon, etc.
 */
void process_stp_beacon_msg(message_t *message, int device_index)
{
    char found;
    struct timeval tv;
    struct timezone tz;
    mac_address_ptr_t neighbor;
    int i;
    stp_recv_beacon_t *recv;

    if (db[6].d) {
        ddprintf("process_stp_beacon_msg from originator ");
        mac_dprint(eprintf, stderr, message->v.stp_beacon.originator);
        node_list_print(eprintf, stderr, "stp_list", stp_list, stp_list_count);

        if (ad_hoc_mode && db[50].d) {
            ddprintf("status count in this beacon:  %d\n", 
                    message->v.stp_beacon.status_count);
            for (i = 0; i < message->v.stp_beacon.status_count; i++) {
                status_t *s = &message->v.stp_beacon.status[i];
                ddprintf("    %s; nbr type %d\n", mac_sprintf(mac_buf1, s->name),
                        s->neighbor_type);
            }
        }
    }

    trim_ad_hoc_client(message->v.stp_beacon.originator);

    if (gettimeofday(&tv, &tz)) {
        printf("process_stp_beacon_msg:  gettimeofday failed\n");
        goto finish;
    }

    neighbor = get_name(device_index, message->eth_header.h_source);
    if (neighbor == NULL) {
        ddprintf("process_stp_beacon_msg:  get_name returned null\n");
        goto finish;
    }
    if (db[6].d) {
        ddprintf("via neighbor ");
        mac_dprint(eprintf, stderr, neighbor);
    }

    /* if this beacon came from a neighbor that is not in our stp list,
     * don't acknowledge it or add it to our stp_received_beacon list.
     */
    found = 0;
    for (i = 0; i < stp_list_count; i++) {
        if (mac_equal(stp_list[i].box.name, neighbor)) {
            found = 1;
            break;
        }
    }
    if (!found) {
        nak_stp_beacon(neighbor);
        goto finish;
    } else {
        ack_stp_beacon(neighbor, message);
    }

    /* see if this beacon originated from us.  if so, delete the stp
     * arc to the neighbor from which it was received.
     */

    /* XXX what if we have given someone a lock on this arc?
     * I'd say mark it as a "zombie" i.e., don't send anything out on it,
     * and drop anything that comes in on it from the other guy.
     * And, both locally and on the other guy's side, mark it for deletion
     * as soon as it is unlocked.  (either by finishing the protocol that
     * caused the lock or by timing out the lock.)
     */
    if (mac_equal(message->v.stp_beacon.originator, my_wlan_mac_address)) {
        message_t msg;
        memset(&msg, 0, sizeof(msg));

        ddprintf("\n\n CIRCULARITY DETECTED.  DELETING STP ARC.\n");

        clear_state(neighbor);

        msg.message_type = stp_arc_delete_msg;
        mac_copy(msg.dest, neighbor);
        send_cloud_message(&msg);

        goto finish;
    }

    /* see if we already have a beacon from the originator of this beacon.
     * if so, update the time stamp on it.
     */
    found = 0;
    for (i = 0; i < stp_recv_beacon_count; i++) {
        if (mac_equal(message->v.stp_beacon.originator,
            stp_recv_beacons[i].stp_beacon.originator))
        {
            found = 1;
            recv = &stp_recv_beacons[i];

            if (db[6].d) { ddprintf("found beacon; updating time..\n"); }
        }
    }

    /* if we didn't already have a beacon from this originator, create a
     * new one.
     */
    if (!found) {
        recv = &stp_recv_beacons[stp_recv_beacon_count];

        if (db[6].d) {
            ddprintf("didn't find beacon; adding a new one..\n");
        }

        if (stp_recv_beacon_count >= MAX_CLOUD) {
            ddprintf("too many stp_recv cloud boxes in view.\n");
            goto finish;
        }

        memset(recv, 0, sizeof(*recv));

        mac_copy(recv->stp_beacon.originator, message->v.stp_beacon.originator);

        stp_recv_beacon_count++;
    }

    recv->sec = tv.tv_sec;
    recv->usec = tv.tv_usec;
    mac_copy(recv->neighbor, neighbor);
    recv->stp_beacon.weakest_stp_link = message->v.stp_beacon.weakest_stp_link;

    if (message->v.stp_beacon.status_count > MAX_CLOUD) {
        ddprintf("process_stp_beacon_msg; invalid status count %d.\n",
                message->v.stp_beacon.status_count);
    } else {

        recv->direct_sight_count = 0;

        for (i = 0; i < message->v.stp_beacon.status_count; i++) {
            status_t *status = &message->v.stp_beacon.status[i];

            if (status->neighbor_type != STATUS_CLOUD_NBR
                && status->neighbor_type != STATUS_CLOUD_NOT_NBR)
            { continue; }

            if (status->sig_strength >= good_threshold) {
                mac_copy(recv->direct_sight[recv->direct_sight_count++],
                        status->name);
            }
        }
    }

    /* time-stamp the stp arc from the (stp) neighbor we just got this stp
     * beacon from.
     */
    for (i = 0; i < stp_list_count; i++) {
        if (mac_equal(stp_list[i].box.name, neighbor)) {
            stp_list[i].sec = tv.tv_sec;
            stp_list[i].usec = tv.tv_usec;
            break;
        }
    }

    /* update debug variable if requested */
    if (message->v.stp_beacon.tweak_db > 0) {
        bool_t prev;
        /* don't print if it's the thing that gets sent out regularly
         * when another box is doing cloud.asp
         */
        if (message->v.stp_beacon.tweak_db % 1000 == 30) {
            prev = util_debug_print(false);
        }
        tweak_db_int(message->v.stp_beacon.tweak_db, db);
        if (message->v.stp_beacon.tweak_db % 1000 == 30) {
            util_debug_print(prev);
        }

        if (message->v.stp_beacon.tweak_db == 2030) {
            restart_disable_print_cloud();
        }
    }

    if (db[50].d) {
        ad_hoc_client_process_stp_beacon(message);
    }

    /* pass this beacon along to other stp neighbors besides the one we
     * just got it from.
     */
    for (i = 0; i < stp_list_count; i++) {
        if (mac_equal(stp_list[i].box.name, neighbor)) { continue; }
        mac_copy(message->dest, stp_list[i].box.name);
        if (db[6].d) {
            ddprintf("passing received beacon message from ");
            mac_dprint_no_eoln(eprintf, stderr,
                    message->v.stp_beacon.originator);
            ddprintf(" to stp nbr ");
            mac_dprint(eprintf, stderr, stp_list[i].box.name);
        }
        send_cloud_message(message);
    }

    beacon = *message;
    have_beacon = true;
    if (db[31].d || db[38].d) {
        build_stp_list();
    }

    finish :;

} /* process_stp_beacon_msg */

/* print local copy of an stp beacon received from another cloud box */
static void print_stp_recv_beacon(ddprintf_t *fn, FILE *f,
        stp_recv_beacon_t *stp, int indent)
{
    int i;

    print_space(fn, f, indent);
    fn(f, "source node ");
    mac_dprint_no_eoln(fn, f, stp->stp_beacon.originator);

    fn(f, " from nbr ");
    mac_dprint_no_eoln(fn, f, stp->neighbor);

    fn(f, ", weakest link %d", (int) stp->stp_beacon.weakest_stp_link);

    fn(f, "\n");

    for (i = 0; i < stp->stp_beacon.status_count; i++) {
        status_t *s = &stp->stp_beacon.status[i];
        print_space(fn, f, indent + 4);
        mac_dprint_no_eoln(fn, f, s->name);

        fn(f, " (%s)", device_type_string(s->device_type));

        fn(f, " str %d", s->sig_strength);
    }

    print_space(fn, f, indent + 4);
    fn(f, "originator seq inited:  %s",
            stp->orig_sequence_num_inited
            ? "true"
            : "false");
    if (stp->orig_sequence_num_inited) {
        fn(f, "; seq %d", stp->orig_sequence_num);
    }
    fn(f, "\n");

    for (i = 0; i < stp->direct_sight_count; i++) {
        if (i == 0) {
            print_space(fn, f, indent + 4);
            fn(f, "cloud boxes this node sees directly:\n");
        }
        print_space(fn, f, indent + 8);
        mac_dprint(fn, f, stp->direct_sight[i]);
    }

} /* print_stp_recv_beacon */

/* do any required cleanup when b is deleted.
 *
 * mark as having "no server" any ad-hoc clients that we thought were being
 * served by the cloud box that seems to have gotten turned off.
 */
static void delete_stp_recv_beacon(stp_recv_beacon_t *b)
{
    ad_hoc_clients_unserved(b);
}

/* delete stp beacons received from other cloud boxes that have gotten
 * old.  this may mean that the other box got turned off.
 */
void timeout_stp_recv_beacons()
{
    struct timeval tv;
    struct timezone tz;
    int next;
    int past_packed;

    if (gettimeofday(&tv, &tz)) {
        printf("timeout_stp_recv_beacons:  gettimeofday failed\n");
        return;
    }

    /* invariant:  [0 .. past_packed) are keepable, packed, and done.
     * [past_packed .. next) can be overwritten.
     * [next .. stp_recv_beacon_count) are yet to be processed and as originally
     */
    past_packed = 0;
    for (next = 0; next < stp_recv_beacon_count; next++) {
        stp_recv_beacon_t *l = &stp_recv_beacons[next];
        long long stp_timeout = STP_TIMEOUT;

        /* slow down timeouts based on number of cloud boxes, because
         * cloud boxes slow down frequency of stp beacons as size of
         * cloud increases
         */
        if (db[39].d && stp_recv_beacon_count >= 1) {
            stp_timeout *= stp_recv_beacon_count;
        }

        /* slow stuff down for debugging if db[40] */
        if (db[40].d) { stp_timeout *= 20; }

        if (usec_diff(tv.tv_sec, tv.tv_usec, l->sec, l->usec) <= stp_timeout
            || db[54].d)
        {
            /* keep it. */
            if (past_packed != next) {
                stp_recv_beacons[past_packed] = *l;
            }
            past_packed++;
        } else {
            /* delete it. */
            delete_stp_recv_beacon(l);

            if (db[6].d || db[3].d || db[21].d) {
                ddprintf("\n");
                ptime("timing out stp beacon; now", tv);
                dptime("; beacon", l->sec, l->usec);
                ddprintf("diff %lld, stp_timeout %lld\n",
                        usec_diff(tv.tv_sec, tv.tv_usec, l->sec, l->usec),
                        stp_timeout);
                // ddprintf("timing out stp beacon\n");
                print_stp_recv_beacon(eprintf, stderr,
                        &stp_recv_beacons[next], 4);
            }
        }
    }

    stp_recv_beacon_count = past_packed;

} /* timeout_stp_recv_beacons */

/* if there is a received stp beacon we have gotten from another cloud
 * box whose originator field equals name, delete it.
 */
void delete_stp_beacon(mac_address_ptr_t name)
{
    int i;

    for (i = 0; i < stp_recv_beacon_count; i++) {
        if (stp_recv_beacons[i].stp_beacon.originator == name) { break; }
    }

    if (i < stp_recv_beacon_count) {
        for (; i < stp_recv_beacon_count - 1; i++) {
            stp_recv_beacons[i] = stp_recv_beacons[i + 1];
        }
        stp_recv_beacon_count--;
    }
}

/* print the list of stp beacons we have received from all over the cloud */
void print_stp_recv_beacons()
{
    int i;
    ddprintf("received stp beacons:\n");

    for (i = 0; i < stp_recv_beacon_count; i++) {
        print_stp_recv_beacon(eprintf, stderr, &stp_recv_beacons[i], 4);
    }
}

/* did the received stp beacon b come from a neighbor that we still have
 * an stp arc to?
 */
bool_t from_stp_neighbor(stp_recv_beacon_t *b)
{
    bool_t result = false;
    int i;

    for (i = 0; i < stp_list_count; i++) {
        if (mac_equal(b->neighbor, stp_list[i].box.name)) {
            result = true;
            break;
        }
    }

    return result;
}

/* get rid of every entry that doesn't satisfy the predicate.
 * (could redo stuff so that stp_recv_beacons are node_t's and use trim_list.)
 */
void trim_stp_recv_beacons(stp_recv_predicate_t predicate)
{
    int past_packed = 0;
    int next;

    /* invariant:  [0 .. past_packed) are keepable, packed, and done.
     * [past_packed .. next) can be overwritten.
     * [next .. list_count) are yet to be processed and as originally
     */
    for (next = 0; next < stp_recv_beacon_count; next++) {
        stp_recv_beacon_t *l = &stp_recv_beacons[next];

        if (predicate(l)) {
            /* keep it. */
            if (db[3].d) { ddprintf("accepting entry\n"); }
            if (past_packed != next) {
                stp_recv_beacons[past_packed] = *l;
            }
            past_packed++;
        } else {
            /* delete it. */
            if (db[3].d) { ddprintf("deleting entry\n"); }
            delete_stp_recv_beacon(l);
        }
    }

    stp_recv_beacon_count = past_packed;
}

/* accept this node iff its unroutable_count (number of times we tried to
 * send something to it and were told we couldn't find a route to it)
 * is less than UNROUTABLE_MAX (currently 20).
 */
char delete_unroutable(node_t *node)
{
    return (node->box.unroutable_count < UNROUTABLE_MAX);
}

void bump_stp_link_unroutable(mac_address_ptr_t dest)
{
    int i;

    for (i = 0; i < stp_list_count; i++) {
        if (mac_equal(dest, stp_list[i].box.name)) {
            stp_list[i].box.unroutable_count++;
        }
    }

    /* trim any nodes with unroutable_count that are too high.
     * (this one is the only one that's changed, and this is the only
     * place the change can happen, so it
     * is the only one that is a candidate for deletion.)
     */
    trim_list(stp_list, &stp_list_count, delete_unroutable, NULL);
}

#ifdef RETRY_TIMED_OUT_LOCKABLES
/* we timed out waiting for an ack of an stp beacon we sent to a cloud
 * neighbor.  resend the stp mesasge.
 */
void resend_stp_beacon_msg(mac_address_ptr_t dest, stp_beacon_t *beacon)
{
    int result;
    message_t message;

    memset(&message, 0, sizeof(message));

    if (db[6].d) {
        ddprintf("resend_stp_beacon_msg; resending timed out stp beacon to ");
        mac_dprint(eprintf, stderr, dest);
    }

    mac_copy(message.dest, dest);
    message.v.stp_beacon = *beacon;
    message.message_type = stp_beacon_msg;
    result = send_cloud_message(&message);
    if (result == 0 || errno != EHOSTUNREACH) {
        add_pending_stp_beacon(dest, &message);
    } else {
        bump_stp_link_unroutable(dest);
    }

    if (db[6].d) { ddprintf("done..\n"); }

} /* resend_stp_beacon_msg */
#endif
