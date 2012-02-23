/* cloud_msg.c - send and receive cloud protocol messages
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: cloud_msg.c,v 1.18 2012-02-22 19:27:22 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: cloud_msg.c,v 1.18 2012-02-22 19:27:22 greg Exp $";

#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netpacket/packet.h>
#include <net/ethernet.h>

#include "print.h"
#include "ad_hoc_client.h"
#include "cloud_mod.h"
#include "stp_beacon.h"
#include "ping.h"
#include "sequence.h"
#include "cloud.h"
#include "cloud_msg.h"
#include "io_stat.h"
#include "nbr.h"
#include "scan_msg.h"
#include "parm_change.h"
#include "timer.h"

unsigned short originator_sequence_num = 0;

/* static string representation of cloud protocol msg */
char *message_type_string(message_type_t msg)
{
    char *p;
    switch (msg) {
    default :
        ddprintf("message_type_string; got unknown message %d\n", msg);
        p = "unknown_msg"; break;
    case unknown_msg :
        ddprintf("message_type_string; got actual unknown message %d\n", msg);
        p = "unknown_msg"; break;
    case local_stp_delete_request_msg : p = "local_stp_delete_request_msg";
            break;
    case local_stp_deleted_msg : p = "local_stp_deleted_msg"; break;
    case local_stp_add_request_msg : p = "local_stp_add_request_msg"; break;
    case local_stp_added_msg : p = "local_stp_added_msg"; break;
    case local_stp_add_changed_request_msg :
            p = "local_stp_add_changed_request_msg"; break;
    case local_stp_added_changed_msg : p = "local_stp_added_changed_msg"; break;
    case local_stp_refused_msg : p = "local_stp_refused_msg"; break;
    case local_lock_req_new_msg : p = "local_lock_req_new_msg"; break;
    case local_lock_req_old_msg : p = "local_lock_req_old_msg"; break;
    case local_lock_grant_msg : p = "local_lock_grant_msg"; break;
    case local_lock_deny_msg : p = "local_lock_deny_msg"; break;
    case local_delete_release_msg : p = "local_delete_release_msg"; break;
    case local_add_release_msg : p = "local_add_release_msg"; break;
    case local_lock_release_msg : p = "local_lock_release_msg"; break;
    case stp_beacon_msg : p = "stp_beacon_msg"; break;
    case stp_beacon_recv_msg : p = "stp_beacon_recv_msg"; break;
    case stp_beacon_nak_msg : p = "stp_beacon_nak_msg"; break;
    case ad_hoc_bcast_block_msg : p = "ad_hoc_bcast_block_msg"; break;
    case ad_hoc_bcast_unblock_msg : p = "ad_hoc_bcast_unblock_msg"; break;
    case nonlocal_lock_req_msg : p = "nonlocal_lock_req_msg"; break;
    case nonlocal_lock_grant_msg : p = "nonlocal_lock_grant_msg"; break;
    case nonlocal_lock_deny_msg : p = "nonlocal_lock_deny_msg"; break;
    case nonlocal_delete_release_msg : p = "nonlocal_delete_release_msg"; break;
    case nonlocal_add_release_msg : p = "nonlocal_add_release_msg"; break;
    case ping_msg : p = "ping_msg"; break;
    case ping_response_msg : p = "ping_response_msg"; break;
    case stp_arc_delete_msg : p = "stp_arc_delete_msg"; break;
    case sequence_msg : p = "sequence_msg"; break;
    case ack_sequence_msg : p = "ack_sequence_msg"; break;
    case scanresults_msg : p = "scanresults_msg"; break;
    case parm_change_start_msg : p = "parm_change_start_msg"; break;
    case parm_change_ready_msg : p = "parm_change_ready_msg"; break;
    case parm_change_not_ready_msg : p = "parm_change_not_ready_msg"; break;
    case parm_change_go_msg : p = "parm_change_go_msg"; break;
    }
    return p;
}

/* print type and destination of cloud protocol message, labeled by "title" */
void print_msg(char *title, message_t *message)
{
    ddprintf("%s; sending message %s to node ",
            title, message_type_string(message->message_type));
    mac_dprint(eprintf, stderr, message->dest);
}

/* this box has received a cloud protocol message from another cloud box.
 * call the function that is to deal with that message type.
 */
void process_cloud_message(message_t *message, int device_index)
{
    if (db[8].d) {
        // DEBUG_SPARSE_PRINT(
            mac_address_ptr_t sender;
            ddprintf("process_cloud_message dev %d got %s from neighbor ",
                    device_index, message_type_string(message->message_type));
            mac_dprint(eprintf, stderr, message->eth_header.h_source);
            sender = get_name(device_index, message->eth_header.h_source);
            ddprintf("originator ");
            mac_dprint(eprintf, stderr, sender);
        // )
    }

    trim_ad_hoc_client(message->eth_header.h_source);

    switch (message->message_type) {

    case unknown_msg :

        ddprintf("process_cloud_message:  received unknown_msg\n");
        break;

    case stp_arc_delete_msg :
        process_stp_arc_delete(message, device_index);
        break;

    case local_stp_delete_request_msg :
        process_local_stp_delete_request(message, device_index);
        break;

    case local_stp_deleted_msg :
        process_local_stp_deleted(message, device_index);
        break;

    case local_stp_add_request_msg :
    case local_stp_add_changed_request_msg :
        process_local_stp_add_request(message, device_index);
        break;

    case local_stp_added_changed_msg :
        process_local_stp_added_changed(message, device_index);
        break;

    case local_stp_added_msg :
        process_local_stp_added(message, device_index);
        break;

    case local_stp_refused_msg :
        process_local_stp_refused(message, device_index);
        break;

    case local_lock_req_new_msg :
    case local_lock_req_old_msg :
        process_local_lock_req(message, device_index);

        break;

    case local_lock_grant_msg :
        process_local_lock_grant(message, device_index);

        break;

    case local_lock_deny_msg :
        process_local_lock_deny(message, device_index);

        break;

    case local_add_release_msg :
        process_local_lock_add_release(message, device_index);

        break;

    case local_delete_release_msg :
        process_local_lock_delete_release(message, device_index);
        break;

    case local_lock_release_msg :
        process_local_lock_release(message, device_index);
        break;

    case stp_beacon_msg :
        process_stp_beacon_msg(message, device_index);
        break;

    case stp_beacon_recv_msg :
        process_stp_beacon_recv_msg(message, device_index);
        break;

    case ad_hoc_bcast_block_msg :
        process_ad_hoc_bcast_block_msg(message, device_index);
        break;

    case ad_hoc_bcast_unblock_msg :
        process_ad_hoc_bcast_unblock_msg(message, device_index);
        break;

    case stp_beacon_nak_msg :
        process_stp_beacon_nak_msg(message, device_index);
        break;

    case nonlocal_lock_req_msg :
        break;

    case nonlocal_lock_grant_msg :
        break;

    case nonlocal_delete_release_msg :
        break;

    case ping_msg :
        process_ping_msg(message, device_index);
        break;

    case ping_response_msg :
        process_ping_response_msg(message, device_index);
        break;

    case nonlocal_add_release_msg :
        break;

    case sequence_msg :
        process_sequence_msg(message, device_index);
        break;

    case ack_sequence_msg :
        process_ack_sequence_msg(message, device_index);
        break;

    case parm_change_start_msg :
        process_parm_change_start_msg(message, device_index);
        break;

    case parm_change_ready_msg :
        process_parm_change_ready_msg(message, device_index);
        break;

    case parm_change_not_ready_msg :
        process_parm_change_not_ready_msg(message, device_index);
        break;

    case parm_change_go_msg :
        process_parm_change_go_msg(message, device_index);
        break;

    case scanresults_msg :
        process_scanresults_msg(message, device_index);
        break;

    default :
        ddprintf("process_cloud_message:  unknown message type %s\n",
                message_type_string(message->message_type));
    }
}

/* look at dest field and message_type of the message, and send to that node. */
int send_cloud_message(message_t *message)
{
    int return_value = 0;
    int i, j, pind = -1, result;
    char found;
    bool_t bcast = false;
    struct sockaddr_ll send_arg;
    mac_address_ptr_t next_step, eth_mac_addr;
    char has_eth_mac_addr;
    int msg_len;

    errno = 0;

    if (db[8].d
        && (db[7].d || message->message_type != ping_msg)
        && (db[12].d || message->message_type != stp_beacon_msg))
    {
        ddprintf("send_cloud_message sending %s to ",
                message_type_string(message->message_type));
        mac_dprint(eprintf, stderr, message->dest);
    }

    has_eth_mac_addr = 0;

    found = 0;

    /* see if this is a broadcast message */
    if (mac_equal(message->dest, mac_address_bcast)) {
        found = 1;
        bcast = true;
        next_step = mac_address_bcast;
    }

    /* see if the destination is one of our neighbors */
    if (!found) {
        for (i = 0; i < nbr_device_list_count; i++) {
            if (mac_equal(nbr_device_list[i].name, message->dest)) {
                next_step = nbr_device_list[i].name;
                found = 1;
                break;
            }
        }
    }

    /* if not, see if we have an stp beacon from the destination */
    if (!found) {
        for (i = 0; i < stp_recv_beacon_count; i++) {
            if (mac_equal(stp_recv_beacons[i].stp_beacon.originator,
                message->dest))
            {
                next_step = stp_recv_beacons[i].neighbor;
                found = 1;
                break;
            }
        }
    }

    /* if not, can't send this message. */
    if (!found) {
        ddprintf("send_cloud_message:  could not find route for ");
        mac_dprint(eprintf, stderr, message->dest);
        return_value = -1;
        errno = EHOSTUNREACH;
        goto finish;
    }

    /* see if the next step has an ethernet connection */
    if (!bcast) {
        for (i = 0; i < nbr_device_list_count; i++) {
            if (mac_equal(nbr_device_list[i].name, next_step)) {
                if (nbr_device_list[i].has_eth_mac_addr) {
                    eth_mac_addr = nbr_device_list[i].eth_mac_addr;
                    has_eth_mac_addr = 1;
                    pind = nbr_device_list[i].perm_io_stat_index;
                }
                break;
            }
        }
    }

    /* find device_list entry for the interface we will send the message to.
     * if we have to send it wirelessly, find the wds device by name.  if we
     * can send it over the wire, find our eth0 device.
     */
    found = 0;
    for (j = 0; j < device_list_count; j++) {
        if ((bcast && device_list[j].device_type == device_type_wlan)
            ||  (!has_eth_mac_addr &&
                mac_equal(device_list[j].mac_address, next_step))
            || (has_eth_mac_addr &&
                device_list[j].device_type == device_type_eth))
        {
            found = 1;
            break;
        }
    }

    if (!found) {

        DEBUG_SPARSE_PRINT(
            ddprintf("send_cloud_message:  could not find neighbor ");
            mac_dprint(eprintf, stderr, next_step);
        )

        return_value = -1;
        errno = EHOSTUNREACH;
        goto finish;
    }

    if (has_eth_mac_addr) {
        mac_copy(send_arg.sll_addr, eth_mac_addr);
        mac_copy(message->eth_header.h_dest, eth_mac_addr);
        mac_copy(message->eth_header.h_source, my_eth_mac_address);

    } else if (bcast) {
        mac_copy(send_arg.sll_addr, mac_address_bcast);
        mac_copy(message->eth_header.h_dest, mac_address_bcast);
        mac_copy(message->eth_header.h_source, my_wlan_mac_address);

    } else {
        mac_copy(send_arg.sll_addr, device_list[j].mac_address);
        mac_copy(message->eth_header.h_dest, device_list[j].mac_address);
        mac_copy(message->eth_header.h_source, my_wlan_mac_address);

        for (i = 0; i < perm_io_stat_count; i++) {
            if (perm_io_stat[i].device_type == device_type_wds
                && mac_equal(device_list[j].mac_address, perm_io_stat[i].name))
            {
                pind = i;
                break;
            }
        }
    }

    message->eth_header.h_proto = htons(CLOUD_MSG);

    if (bcast && message->message_type == ping_response_msg) {
        message->sequence_num = (send_ping_sequence[pind]++) % 256;

    } else if (pind == -1) {
        if (!bcast) {
            ddprintf("send_cloud_message; could not find perm_io_stat for ");
            mac_dprint(eprintf, stderr, next_step);
        }
        message->sequence_num = 0;

    } else {
        message->sequence_num = (send_sequence[pind]++) % 256;
    }

    if (db[48].d) {
        ddprintf("sending sequence %d for message type %x\n",
                message->sequence_num, message->eth_header.h_proto);
        fn_print_message(eprintf, stderr,
                (byte *) message->v.msg.msg_body,
                100);
    }

    switch (message->message_type) {
    case ping_msg :
    case ping_response_msg :
    case local_stp_delete_request_msg :
    case local_stp_deleted_msg :
    case local_stp_add_request_msg :
    case local_stp_added_msg :
    case local_stp_add_changed_request_msg :
    case local_stp_added_changed_msg :
    case local_stp_refused_msg :
    case local_lock_req_new_msg :
    case local_lock_req_old_msg :
    case stp_arc_delete_msg :
    case local_lock_grant_msg :
    case local_lock_deny_msg :
    case local_delete_release_msg :
    case local_add_release_msg :
    case nonlocal_lock_req_msg :
    case nonlocal_lock_deny_msg :
    case nonlocal_delete_release_msg :
    case nonlocal_add_release_msg :
    case stp_beacon_nak_msg :
        msg_len = ((byte *) &message->message_type)
                + sizeof(message->message_type)
                - ((byte *) message);
        break;

    case scanresults_msg :
        msg_len = ((byte *) &message->v.scan)
                + sizeof(message->v.scan)
                - ((byte *) message);
        break;

    case parm_change_ready_msg :
    case parm_change_not_ready_msg :
    case parm_change_go_msg :
    case parm_change_start_msg :
        msg_len = ((byte *) &message->v.parm_change)
                + sizeof(message->v.parm_change)
                - ((byte *) message);
        break;

    case ack_sequence_msg :
    case sequence_msg :
        msg_len = ((byte *) &message->v.seq)
                + sizeof(message->v.seq)
                - ((byte *) message);
        break;

    case ad_hoc_bcast_block_msg :
    case ad_hoc_bcast_unblock_msg :
        msg_len = ((byte *) &message->v.msg.msg_body[6])
                - ((byte *) message);
        break;

    case stp_beacon_recv_msg :
    case stp_beacon_msg :
        msg_len = ((byte *) &message->v.stp_beacon.status[0])
                + sizeof(status_t)
                        * message->v.stp_beacon.status_count
                - ((byte *) message);
        break;

    case nonlocal_lock_grant_msg :
    default :
    case unknown_msg :
        ddprintf("send_cloud_message:  unknown message type %d\n",
                (int) message->message_type);
        return_value = -1;
        errno = ENOMSG;
        goto finish;
    }

    // cloud_message_count++;
    if (db[25].d || db[26].d) { short_print_io_stats(eprintf, stderr); }

    check_msg_count();

    if (db[15].d) {
        // DEBUG_SPARSE_PRINT(
            ddprintf("sending cloud message ");
            fn_print_message(eprintf, stderr,
                    (unsigned char *) message, msg_len);
        // )
    }

    if (db[56].d && (message->message_type == ad_hoc_bcast_block_msg
        || message->message_type == ad_hoc_bcast_unblock_msg))
    {
        ddprintf("send_cloud_message sending ad_hoc_bcast_(un)block_msg:\n");
        fn_print_message(eprintf, stderr,
                (unsigned char *) message, msg_len);
    }

    if (use_pipes) {
        result = pio_write(&device_list[j].out_pio, message, msg_len);
    } else {
        memset(&send_arg, 0, sizeof(send_arg));
        send_arg.sll_family = AF_PACKET;
        send_arg.sll_halen = 6;

        send_arg.sll_ifindex = device_list[j].if_index;

        block_timer_interrupts(SIG_BLOCK);
        result = sendto(device_list[j].fd, message, msg_len, 0,
                (struct sockaddr *) &send_arg, sizeof(send_arg));
        block_timer_interrupts(SIG_UNBLOCK);
    }

    if (result == -1) {
        ddprintf("send_cloud_message; sendto error:  %s", strerror(errno));
        if (errno == EMSGSIZE) {
            ddprintf("; message size %d", msg_len);
        }
        ddprintf("\n");

        ddprintf("device_list[%d] of %d;\n", j, device_list_count);
        print_device(eprintf, stderr, &device_list[j]);

        io_stat[device_list[j].stat_index].cloud_send_error++;

        return_value = -1;
    }

    io_stat[device_list[j].stat_index].cloud_send++;

    finish:

    return return_value;
}

/* send a 298x_msg to device */
int send_message(message_t *message, int msg_len, device_t *device)
{
    int result = -1;
    message_t msg_tail;
    byte *p_body;
    int tail_body_len;
    int pind;

    if (db[44].d) { ddprintf("send_message..\n"); }

    pind = status_find_by_mac(perm_io_stat, perm_io_stat_count,
            message->eth_header.h_dest);

    if (pind == -1) {
        if (!db[60].d) {
            ddprintf("send_message; yuck.  could not find perm_io_stat for ");
            mac_dprint(eprintf, stderr, message->eth_header.h_dest);
        }
        message->sequence_num = 0;

    }

    if (msg_len <= MAX_SENDTO) {
        if (db[44].d) { ddprintf("send_message; sending 1-for-1..\n"); }
        message->v.msg.n = 1;
        message->v.msg.k = 1;
        if (pind != -1) {
            if (message->eth_header.h_proto == htons(CLOUD_MSG)) {
                message->sequence_num = (send_sequence[pind]++) % 256;
            } else {
                message->sequence_num = (send_data_sequence[pind]++) % 256;
            }
        }
        if (db[48].d) {
            ddprintf("sending sequence %d for message type %x\n",
                    message->sequence_num, message->eth_header.h_proto);
            fn_print_message(eprintf, stderr,
                    (byte *) message->v.msg.msg_body,
                    100);
        }
        result = sendum((byte *) message, msg_len, device);
        return result;
    }

    if (message->eth_header.h_proto != htons(WRAPPED_CLIENT_MSG)) {
        ddprintf("send_message; "
                "non-0x2986 (wrapped-client) message too long.\n");
        return -1;
    }

    message->v.msg.k = 1;
    message->v.msg.n = 2;
    if (pind != -1) {
        if (message->eth_header.h_proto == htons(CLOUD_MSG)) {
            message->sequence_num = (send_sequence[pind]++) % 256;
        } else {
            message->sequence_num = (send_data_sequence[pind]++) % 256;
        }
    }
    if (db[48].d) {
        ddprintf("sending sequence %d for message type %x\n",
                message->sequence_num, message->eth_header.h_proto);
        fn_print_message(eprintf, stderr,
                (byte *) message->v.msg.msg_body,
                100);
    }
    result = sendum((byte *) message, MAX_SENDTO, device);
    if (result == -1) {
        return result;
    }

    if (db[44].d) {
        ddprintf("sent first piece: (not)\n");
        fn_print_message(eprintf, stderr, (byte *) message, MAX_SENDTO);
    }

    memcpy(&msg_tail, message, wrapper_len);
    msg_tail.v.msg.k = 2;
    if (pind != -1) {
        if (msg_tail.eth_header.h_proto == htons(CLOUD_MSG)) {
            msg_tail.sequence_num = (send_sequence[pind]++) % 256;
        } else {
            msg_tail.sequence_num = (send_data_sequence[pind]++) % 256;
        }
    }
    p_body = ((byte *) message) + MAX_SENDTO;
    tail_body_len = msg_len - MAX_SENDTO;
    memcpy(msg_tail.v.msg.msg_body, p_body, tail_body_len);

    if (db[44].d) {
        ddprintf("doing sendum(%d)\n", tail_body_len + wrapper_len);
    }

    if (db[48].d) {
        ddprintf("sending sequence %d for message type %x\n",
                msg_tail.sequence_num, msg_tail.eth_header.h_proto);
        fn_print_message(eprintf, stderr, (byte *) &msg_tail, 100);
    }

    result = sendum((byte *) &msg_tail, tail_body_len + wrapper_len, device);

    if (db[44].d) {
        ddprintf("sent second piece (result %d):\n", result);
        fn_print_message(eprintf, stderr, (byte *) &msg_tail,
                tail_body_len + wrapper_len);
    }

    return result;

} /* send_message */

/* we got a payload message from another cloud box.  payload messages
 * can get broken up into multiple pieces and sent separately if they are
 * too long to be sent in one gulp.  each message has a header that says
 * "I am piece K of an N-piece message".  for each interface (including
 * ad-hoc pseudo-interfaces to other cloud boxes), we remember what
 * is the next K-for-N message we are expecting.  this routine looks at
 * the next incoming message and returns true iff that next message completes
 * an entire N-piece message.
 * it also updates our state based on having successfully received
 * that message.
 */
bool_t update_k_for_n_state(message_t *message, int *msg_len, int dev_index)
{
    device_t *dp = &device_list[dev_index];
    int len = *msg_len;

    if (message->eth_header.h_proto != htons(WRAPPED_CLIENT_MSG)) {
        return true;
    }

    if (db[44].d) {
        if (dp->expect_n == -1) {
            ddprintf("update_k_for_n_state.  expected <1 ?>, got <%d %d>\n",
                    message->v.msg.k, message->v.msg.n);
        } else {
            ddprintf("update_k_for_n_state.  expected <%d %d>, got <%d %d>\n",
                    dp->expect_k, dp->expect_n,
                    message->v.msg.k, message->v.msg.n);
        }
    }

    /* we expected a fresh new message and got a complete message */
    if (dp->expect_n == -1 && message->v.msg.n == 1) {
        if (db[44].d) {
            ddprintf("update_k_for_n_state; expected new, got n=1; true.\n");
        }
        return true;
    }

    /* we expected a fresh new message and got the first part of a
     * multiple message
     */
    if (dp->expect_n == -1
        && message->v.msg.k == 1
        && message->v.msg.n > 1)
    {
        if (len > sizeof(dp->message)) {
            ddprintf("update_k_for_n_state; got too-long message, len %d\n",
                    len);
            return false;
        }
        memcpy((byte *) &dp->message, message, len);
        dp->msg_len = len;
        dp->expect_n = message->v.msg.n;
        dp->expect_k = 2;
        if (db[44].d) {
            ddprintf("update_k_for_n_state; expected new, got n > 1; false.  "
                    "partial message: (not)\n");
            fn_print_message(eprintf, stderr, (byte *) &dp->message, len);
        }

        return false;
    }

    /* we expected the next piece of a message and got the piece we expected */
    if (dp->expect_n != -1
        && message->v.msg.k == dp->expect_k
        && message->v.msg.n == dp->expect_n)
    {
        byte *p = ((byte *) &dp->message) + dp->msg_len;
        if (dp->msg_len + len - wrapper_len > sizeof(dp->message)) {
            ddprintf("multiple-piece message too long\n");
            dp->expect_n = -1;
            return false;
        }

        if (db[44].d) {
            ddprintf("next piece of multi-part message:\n");
            fn_print_message(eprintf, stderr, (byte *) message, len);
        }

        memcpy(p, message->v.msg.msg_body, len - wrapper_len);
        dp->msg_len += len - wrapper_len;

        /* this was the last piece. */
        if (dp->expect_n == dp->expect_k) {
            dp->expect_n = -1;
            if (dp->msg_len > sizeof(*message)) {
                ddprintf("multiple-piece message too long\n");
                dp->expect_n = -1;
                return false;
            }
            memcpy(message, &dp->message, dp->msg_len);
            *msg_len = dp->msg_len;

            if (db[44].d) {
                fn_print_message(eprintf, stderr, (byte *) message, *msg_len);
            }

            return true;

        } else {
            dp->expect_k++;
            return false;
        }
    }

    /* anything else is an error. */

    dp->expect_n = -1;
    return false;

} /* update_k_for_n_state */

/* is this the first time we have seen this message?
 * each wrapped_client message comes tagged with an unsigned short int
 * from its originator, and we consider a message new if the tag is
 * greater than the last tag we got from the originator (mod 32768).
 */
static bool_t new_from_originator(message_t *message)
{
    int i;
    bool_t found;
    unsigned short diff;
    bool_t result;
    stp_recv_beacon_t *recv;

    if (!db[60].d) {
        result = true;
        goto done;
    }

    if (mac_equal(message->v.msg.originator, my_wlan_mac_address)) {
        if (db[61].d) { ddprintf("new_from_originator; locally originated.\n"); }
        result = false;
        goto done;
    }

    found = false;
    for (i = 0; i < stp_recv_beacon_count; i++) {
        recv = &stp_recv_beacons[i];

        if (mac_equal(message->v.msg.originator, recv->stp_beacon.originator)) {
            found = true;
            break;
        }
    }

    if (!found) {
        if (db[61].d) { ddprintf("new_from_originator; orig not found.\n"); }
        result = true;
        goto done;
    }

    if (!recv->orig_sequence_num_inited) {
        recv->orig_sequence_num = message->v.msg.originator_sequence_num;
        recv->orig_sequence_num_inited = true;

        if (db[61].d) { ddprintf("new_from_originator; orig not inited.\n"); }
        result = true;
        goto done;
    }

    diff = message->v.msg.originator_sequence_num - recv->orig_sequence_num;

    if (db[61].d) {
        ddprintf("new_from_originator; diff %d, msg %d mine %d.\n",
                (int) diff, (int) message->v.msg.originator_sequence_num,
                (int) recv->orig_sequence_num);
    }

    if (diff == 0 || diff > 32768) {
        result = false;
        goto done;
    } else {
        recv->orig_sequence_num = message->v.msg.originator_sequence_num;
    }

    done:

    if (db[61].d) { ddprintf("new_from_originator returns %d\n", (int) result); }

    return result;
}

/* we heard a message from some other cloud box.  we are responsible to
 * make sure that our stp neighbor nbr has heard the message.
 * check to see if they can directly hear messages from the box we
 * heard the message from.  if not, we decide to send the message.
 *
 * and, we may have overheard this incoming message from a box other than
 * one of our stp neighbors.  check to see if nbr is the stp neighbor
 * via which we would have gotten this message.  if so, he doesn't
 * need to hear about it from us.
 */
static bool_t stp_nbr_needs_transmit(message_t *message, mac_address_t nbr)
{
    int i;
    stp_recv_beacon_t *recv;
    bool_t found;
    bool_t result;

    if (!db[60].d) { return true; }

    found = false;
    for (i = 0; i < stp_recv_beacon_count; i++) {
        recv = &stp_recv_beacons[i];
        if (mac_equal(nbr, recv->stp_beacon.originator)) {
            found = true;
            break;
        }
    }

    if (!found) {
        if (db[61].d) { ddprintf("stp_nbr_needs_transmit; orig not found.\n"); }
        result = true;
        goto done;
    }

    /* find the cloud box we heard this message from, and see if we would
     * have received it via the stp tree from nbr.  if so, we don't need
     * to send it to nbr.
     */
    found = false;
    for (i = 0; i < stp_recv_beacon_count; i++) {
        recv = &stp_recv_beacons[i];
        if (mac_equal(message->eth_header.h_source,
            recv->stp_beacon.originator))
        {
            found = true;
            break;
        }
    }

    if (found && mac_equal(nbr, recv->neighbor)) {
        if (db[61].d) {
            ddprintf("stp_nbr_needs_transmit; message came from nbr's side of"
                    " the stp tree; don't send.\n");
        }
        result = false;
        goto done;
    }

    /* see if the stp neighbor directly sees the node we heard the message
     * from.  if so, we don't need to send it for that stp neighbor's sake.
     */
    result = true;
    for (i = 0; i < recv->direct_sight_count; i++) {
        if (mac_equal(recv->direct_sight[i], message->eth_header.h_source)) {
            if (db[61].d) {
                ddprintf("stp_nbr_needs_transmit; nbr sees originator directly; "
                        "don't send\n");
            }
            result = false;
            goto done;
        }
    }

    done:
    if (db[61].d) { ddprintf("stp_nbr_needs_transmit; result %d\n", result); }
    return result;

} /* stp_nbr_needs_transmit */

/* send message out via the spanning tree.
 * and via the local wlan0 and eth0 devices (provided the message didn't
 * come in via same).
 * exclude device_index[d].  we
 * got this message from device_index d, and don't want to send it back
 * there.
 */
void bcast_forward_message(message_t *message, int msg_len, int dev,
        bool_t originated_locally)
{
    bool_t wlan_has_seen = false;
    bool_t eth_has_seen = false;
    bool_t wireless_has_seen = false;
    int i, j, result;

    if (db[13].d) {
        ddprintf("bcast_forward_message device %d, %soriginated locally\n",
                dev, originated_locally ? "" : "not ");

        if (!originated_locally) {
            ddprintf("    originator ");
            mac_dprint_no_eoln(eprintf, stderr, message->v.msg.originator);
            ddprintf(", orig seq num %d\n",
                    message->v.msg.originator_sequence_num);
        }
    }

    if (!originated_locally && !new_from_originator(message)) {
        if (db[13].d) { ddprintf("    not new_from_originator; return\n"); }
        goto done;
    }

    if (originated_locally) {
        message->v.msg.originator_sequence_num = originator_sequence_num++;
        mac_copy(message->v.msg.originator, my_wlan_mac_address);
    }

    if (device_list[dev].device_type == device_type_wlan) {
        wlan_has_seen = true;
    }

    if (device_list[dev].device_type == device_type_eth) {
        if (db[13].d) { ddprintf("    eth_has_seen true from incoming dev\n"); }
        eth_has_seen = true;
    }

    for (i = 0; i < stp_list_count; i++) {

        /* send to stp neighbors via our ethernet interface at most once.
         * this will look to them like a new client-originated message.
         */
        if (stp_list[i].box.has_eth_mac_addr && !eth_has_seen) {
            int pind;

            if (db[13].d) {
                ddprintf("    sending just message out eth for clients and "
                        "other cloud boxes\n");
            }

            result = send_to_interface(message->v.msg.msg_body,
                    msg_len - wrapper_len,
                    device_type_eth);

            pind = stp_list[i].box.perm_io_stat_index;

            if (result != -1) { eth_has_seen = true; }
        }

        /* see if we need to send the message to this stp neighbor */
        if (!stp_list[i].box.has_eth_mac_addr && !wireless_has_seen) {
            bool_t found;

            found = false;
            for (j = 0; j < device_list_count; j++) {
                if ((device_list[j].device_type == device_type_wds
                    || (ad_hoc_mode
                        && device_list[j].device_type == device_type_ad_hoc))
                    && (dev != j)
                    && mac_equal(stp_list[i].box.name,
                            device_list[j].mac_address))
                {
                    found = true;
                    break;
                }
            }
            if (!found) { continue; }

            if (db[23].d) { ddprintf("bcast_forward_msg doing send_seq..\n"); }

            send_seq(i, (byte *) &message, msg_len);

            /* db[24] is a test mode where we intentionally drop a packet
             * to see if the system responds correctly to lost packets.
             */
            if (db[24].d && device_list[j].device_type == device_type_wds) {
                db[24].d = false;
                ddprintf("    not forwarding message..\n");
                continue;
            }

            if (db[13].d) { ddprintf("    maybe forwarding message..\n"); }

            if (!originated_locally
                && !stp_nbr_needs_transmit(message, device_list[j].mac_address))
            {
                if (db[13].d) {
                    ddprintf("    nbr ");
                    mac_dprint_no_eoln(eprintf, stderr,
                            device_list[j].mac_address);
                    ddprintf(" doesn't need transmit\n");
                }
                continue;
            }

            mac_copy(message->eth_header.h_dest,
                    db[60].d
                        ? mac_address_bcast
                        : device_list[j].mac_address
                    );

            mac_copy(message->eth_header.h_source, my_wlan_mac_address);

            result = send_message(message, msg_len, &device_list[j]);

            if (db[13].d && db[60].d) {
                ddprintf("    did wireless broadcast..\n");
            }

            if (db[60].d) { wireless_has_seen = true; }
        } else {
            if (db[13].d) {
                ddprintf("    not sending.  has_eth %d, wireless_has_seen %d\n",
                        stp_list[i].box.has_eth_mac_addr, wireless_has_seen);
            }
        }
    }

    if (!eth_has_seen && eth_device_name != NULL && !db[20].d) {
        if (db[13].d) { ddprintf("    sending on eth0..\n"); }
        send_to_interface(message->v.msg.msg_body, msg_len - wrapper_len,
                device_type_eth);
    }

    if (!wlan_has_seen && !db[36].d) {

        if (db[13].d) { ddprintf("    sending on wlan..\n"); }

        send_to_interface(message->v.msg.msg_body, msg_len - wrapper_len,
                device_type_wlan);
    }

    if (ad_hoc_mode && db[50].d) {
        ad_hoc_client_forward_msg(message, msg_len, dev);
    }

    done:
    if (db[13].d) { ddprintf("bcast_forward_message return..\n"); }

} /* bcast_forward_message */
