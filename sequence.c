/* sequence.c - manage sequence numbers on cloud messages
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: sequence.c,v 1.14 2012-02-22 19:27:23 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: sequence.c,v 1.14 2012-02-22 19:27:23 greg Exp $";

/* implement passive sequence number processing (good) and lock-step
 * link-level active sequence numbering (optional, rarely used).
 *
 * in passive sequence number processing, each cloud message sent out
 * has a monotonically increasing sequence number attached to it.  when
 * we receive a packet, we compare what we think should have been the next
 * number with what was actually gotten, and count mismatches to calculate
 * rate of packet loss.
 *
 * the active sequence numbering is an optional addition to the cloud
 * protocol to try to improve link-level reliability at the cost of
 * bandwidth.  each payload packet is preceded by a message sequence packet.
 *
 * it is currently somewhat deprecated code and is usually left
 * turned off.  the performance penalty is very severe, and the reliability
 * benefit is not that great.  furthermore, we are at the link level, and
 * other higher parts of the protocol stack add reliability.  this whole
 * thing was done before the switch from WDS (boo) to ad-hoc (yay), and
 * that switch hugely improved reliability.
 */

#include <string.h>
#include <netinet/in.h>

#include "util.h"
#include "cloud.h"
#include "print.h"
#include "sequence.h"
#include "timer.h"
#include "cloud_msg.h"
#include "nbr.h"

/* we have an incoming message from another cloud box.
 * see if the message has a sequence number, and compare it to our
 * own sequence number for that box.  this allows us to see if we
 * got missing packets or duplicate packets from the other box.
 *
 * broadcast pings are treated specially; they have type "ping_response_msg"
 * and broadcast mac address as destination.  so, we keep track of pings
 * from all cloud boxes.
 *
 * otherwise, use the dest mac address to find the index of the sender
 * among our neighbors.
 *
 * tally received packet count and error count (number of packets missed
 * or duplicated) in three categories:
 *
 *  - broadcast pings
 *  - cloud protocol messages (received via a separate set of interfaces
 *        (eth0.1 etc.)
 *  - raw client data packets received
 */
void sequence_check(message_t *message, int device_index, int dev_index)
{
    int pind;
    int diff;
    byte last_recvd, incoming;
    mac_address_ptr_t neighbor;
    bool_t have_recv_seq;
    bool_t bcast_ping = false;

    neighbor = get_name(device_index, message->eth_header.h_source);
    if (neighbor == NULL) {
        ddprintf("sequence_check; couldn't find neighbor for ");
        mac_dprint(eprintf, stderr, message->eth_header.h_source);
    }

    if (mac_equal(message->eth_header.h_dest, mac_address_bcast)
        && message->message_type == ping_response_msg)
    {
        bcast_ping = true;
    }

    if ((!bcast_ping
        && mac_equal(message->eth_header.h_dest, mac_address_bcast))
        || mac_equal(message->eth_header.h_dest, mac_address_zero))
    {
        return;
    }

    pind = status_find_by_mac(perm_io_stat, perm_io_stat_count, neighbor);

    if (pind == -1) {
        if (db[48].d) {
            ddprintf("sequence_check; no pind; returning.\n");
        }
        return;
    }

    if (bcast_ping) {
        perm_io_stat[pind].ping_packets_received++;
        have_recv_seq = have_recv_ping_sequence[pind];
        last_recvd = recv_ping_sequence[pind];

    } else if (message->eth_header.h_proto == htons(CLOUD_MSG)) {
        perm_io_stat[pind].packets_received++;
        have_recv_seq = have_recv_sequence[pind];
        last_recvd = recv_sequence[pind];

    } else {
        perm_io_stat[pind].data_packets_received++;
        have_recv_seq = have_recv_data_sequence[pind];
        last_recvd = recv_data_sequence[pind];
    }

    incoming = message->sequence_num;

    #ifdef DEBUG_48
    if (db[48].d) {
        ddprintf("sequence_check; seq got <%d %d %d> "
                "expected <%d %d %d>\n",
                incoming, device_index, dev_index,
                last_recvd, db_i[pind], db_dev_index[pind]);
    }
    #endif

    if (have_recv_seq) {

        if (incoming != (last_recvd + 1) % 256) {
            diff = ((int) incoming) - ((int) last_recvd);

            if (diff != 0) {
                if (diff < 0) { diff += 256; }
                if (bcast_ping) {
                    perm_io_stat[pind].ping_packets_lost += diff;

                } else if (message->eth_header.h_proto == htons(CLOUD_MSG)) {
                    perm_io_stat[pind].packets_lost += diff;

                } else {
                    perm_io_stat[pind].data_packets_lost += diff;
                }
            }

            if (db[48].d) {
                ddprintf("seq error; diff %d\n", diff);
            }
        }
    }

    if (bcast_ping) {
        have_recv_ping_sequence[pind] = true;
        recv_ping_sequence[pind] = incoming;

    } else if (message->eth_header.h_proto == htons(CLOUD_MSG)) {
        have_recv_sequence[pind] = true;
        recv_sequence[pind] = incoming;

    } else {
        have_recv_data_sequence[pind] = true;
        recv_data_sequence[pind] = incoming;
    }

    #ifdef DEBUG_48
        memcpy(&db_messages[pind], message, sizeof(message));
        db_i[pind] = device_index;
        db_dev_index[pind] = dev_index;
    #endif

} /* sequence_check */

/* see if we are waiting for an ack message from any other cloud box */
bool_t awaiting_ack()
{
    int i;

    bool_t result = false;

    for (i = 0; i < stp_list_count; i++) {
        if (stp_list[i].box.awaiting_ack) {
            result = true;
            break;
        }
    }

    return result;
}

/* we have received a sequence message from another cloud box.  see if
 * it is the sequence message we expected.  tally any errors we detect
 * if we weren't expecting a sequence message, if we got one that didn't
 * agree with what we expected, etc.  if everything is fine, update our
 * state to indicate that we are not expecting a sequence number from
 * the cloud box that sent it any more.
 */
void process_sequence_msg(message_t *message, int d)
{
    int i;

    if (db[23].d) {
        ddprintf("process_sequence_msg processing <%d %d>\n",
                message->v.seq.sequence_num,
                message->v.seq.message_len);
    }

    if ((!ad_hoc_mode && device_list[d].device_type != device_type_wds)
        || (ad_hoc_mode && device_list[d].device_type != device_type_ad_hoc))
    {
        ddprintf("process_sequence_msg:  message from non-wds device.\n");
        return;
    }

    for (i = 0; i < stp_list_count; i++) {

        if (stp_list[i].box.has_eth_mac_addr
            || !mac_equal(stp_list[i].box.name, device_list[d].mac_address))
        {
            continue;
        }

        if (!stp_list[i].box.expect_seq) {
            ddprintf("process_sequence_msg:  stp device not expecting seq.\n");
            stp_list[i].box.recv_error++;
            return;
        }

        /* is this a duplicate? */
        if (stp_list[i].box.recv_sequence
            == (byte) (message->v.seq.sequence_num + 1))
        {
            ddprintf("process_sequence_msg:  got duplicate message.\n");
            stp_list[i].box.received_duplicate = true;
            stp_list[i].box.recv_error++;

        } else if (stp_list[i].box.recv_sequence
                != message->v.seq.sequence_num
                && stp_list[i].box.recv_sequence_error < MAX_SEQUENCE_ERROR)
        {
            ddprintf("process_sequence_msg:  expecting sequence num %d, "
                    "got %d.\n",
                    stp_list[i].box.recv_sequence,
                    message->v.seq.sequence_num);
            stp_list[i].box.recv_sequence_error++;
            stp_list[i].box.recv_error++;
            return;

        } else if (stp_list[i].box.recv_sequence
                != message->v.seq.sequence_num
                && stp_list[i].box.recv_sequence_error >= MAX_SEQUENCE_ERROR)
        {
            ddprintf("process_sequence_msg:  expecting sequence num %d, "
                    "got %d.  but, sequence_error threshold exceeded.\n"
                    "resetting recv_sequence.\n",
                    stp_list[i].box.recv_sequence,
                    message->v.seq.sequence_num);
            stp_list[i].box.recv_sequence = message->v.seq.sequence_num;
            stp_list[i].box.recv_error++;

        } else {
            stp_list[i].box.recv_sequence_error = 0;
            if (db[23].d) {
                ddprintf("process_sequence_msg:  got sequence num %d, "
                        "which we expected.\n",
                        message->v.seq.sequence_num);
            }
        }

        stp_list[i].box.expect_seq = false;
        stp_list[i].box.recv_message_len = message->v.seq.message_len;

        break;
    }
}

/* we got an acknowledgement of our sequence message from another cloud box. */
void process_ack_sequence_msg(message_t *message, int d)
{
    int i;

    if (db[23].d) {
        ddprintf("process_ack_sequence_msg processing <%d %d>\n",
                message->v.seq.sequence_num,
                message->v.seq.message_len);
    }

    for (i = 0; i < stp_list_count; i++) {
        if (!stp_list[i].box.awaiting_ack) { continue; }

        if (stp_list[i].box.has_eth_mac_addr
            || !mac_equal(stp_list[i].box.name, device_list[d].mac_address))
        {
            continue;
        }
        if (db[23].d) { ddprintf("   found it; bumping send_sequence..\n"); }
        stp_list[i].box.awaiting_ack = false;
        stp_list[i].box.send_sequence++;

        // stp_list[i].box.non_cloud_packets++;
    }

    /* maybe we only have at most one box awaiting an ack.  right now we
     * may cause the whole cloud to operate in lock step.
     */
    turn_off_ack_timer();
}

/* is it ok to read from the current file descriptor, which gives us
 * data received from another cloud box?  if we are awaiting an
 * acknowledgement of a sequence packet we sent to another box, we
 * don't want to read from other devices until we get it.
 */
bool_t ack_read_ok(int d)
{
    int i;

    /* if we're not doing flow control, just go for it. */
    if (!db[22].d) { return true; }

    /* if we're not waiting for any ack's, it's ok to read input */
    if (!awaiting_ack()) { return true; }

    /* if we are pretending we have no ethernet connection and this is an
     * ethernet connection, it's ok to read input
     */
    if (device_list[d].device_type == device_type_eth && db[20].d) {
        return true;
    }

    /* if this isn't a wds device, it won't be giving us an ack.
     * don't read anything from any upstream devices if we are awaiting an
     * ack from a downstream wds device.
     */
    if (device_list[d].device_type != device_type_wds) {
        return false;
    }

    /* see if we can find the stp node for this device, and if we are
     * awaiting an ack from that stp node.  if so, it's ok to read from
     * this device, because we may get the ack we are hoping for.
     */
    for (i = 0; i < stp_list_count; i++) {

        if (!stp_list[i].box.has_eth_mac_addr
            && mac_equal(stp_list[i].box.name, device_list[d].mac_address)
            && stp_list[i].box.awaiting_ack)
        {
            return true;
        }
    }

    return false;
}

/* send a sequence number message.  this is an addition to the protocol
 * to attempt to improve message delivery robustness at the link level,
 * at the expense of bandwidth.
 */
void send_seq(int stp_ind, byte *message, int msg_len)
{
    message_t seq_msg;

    if (!db[22].d) { return; }

    if (db[23].d) {
        ddprintf("send_seq sending <%d %d> to ",
                stp_list[stp_ind].box.send_sequence,
                msg_len);
        mac_dprint(eprintf, stderr, stp_list[stp_ind].box.name);
    }

    #if 0
    /* test-harness code; don't send a sequence message, to test the
     * protocol when a sequence message gets dropped.
     */
    if (db[24].d) {
        ddprintf("send_seq intentionally dropping packet..\n");
        db[24].d = false;
        return;
    }
    #endif

    seq_msg.message_type = sequence_msg;
    seq_msg.v.seq.sequence_num = stp_list[stp_ind].box.send_sequence;
    stp_list[stp_ind].box.awaiting_ack = true;

    if (msg_len > CLOUD_BUF_LEN) {
        seq_msg.v.seq.message_len = 0;
        stp_list[stp_ind].box.message_len = 0;
        ddprintf("send_seq got message with invalid length %d\n", msg_len);

    } else {
        seq_msg.v.seq.message_len = msg_len;
        stp_list[stp_ind].box.message_len = msg_len;
        memmove((void *) &stp_list[stp_ind].box.message, message, msg_len);
    }

    mac_copy(seq_msg.dest, stp_list[stp_ind].box.name);
    send_cloud_message(&seq_msg);

    update_ack_timer();

} /* send_seq */


/* just got a message from raw device d.  if d is not a wds device, the
 * message is ok.  (for now we are just doing flow control over wds
 * connections.)  if it is a wds device, find the corresponding stp link
 * and return true iff we have gotten a sequence packet and are expecting
 * a message.
 */
bool_t message_ok(int d, int message_len)
{
    int i;

    if (!db[22].d) { return true; }

    if (db[23].d) { ddprintf("message_ok;\n"); }

    if (device_list[d].device_type != device_type_wds) {
        if (db[23].d) { ddprintf("    not wds; returning true\n"); }
        return true;
    }

    for (i = 0; i < stp_list_count; i++) {

        if (stp_list[i].box.has_eth_mac_addr
            || !mac_equal(stp_list[i].box.name, device_list[d].mac_address))
        {
            continue;
        }

        if (stp_list[i].box.expect_seq) {
            if (db[23].d) { ddprintf("    !expect_seq; "
                    "returning false\n"); }
            stp_list[i].box.recv_error++;
            return false;
        }

        if (stp_list[i].box.recv_message_len != message_len) {
            if (db[23].d) { ddprintf("    bad message_len; "
                    "returning false\n"); }
            stp_list[i].box.recv_error++;
            return false;
        }

        {
            message_t response;
            memset(&response, 0, sizeof(response));
            response.message_type = ack_sequence_msg;
            response.v.seq.sequence_num = stp_list[i].box.recv_sequence;
            response.v.seq.message_len = stp_list[i].box.recv_message_len;

            if (db[23].d) {
                ddprintf("    sending ack_sequence_msg <%d %d>\n",
                        stp_list[i].box.recv_sequence,
                        stp_list[i].box.recv_message_len);
            }
            mac_copy(response.dest, stp_list[i].box.name);
            if (db[24].d) {
                ddprintf("    not sending ack message..\n");
                db[24].d = false;
            } else {
                send_cloud_message(&response);
            }

            stp_list[i].box.expect_seq = true;

            if (!stp_list[i].box.received_duplicate) {
                stp_list[i].box.recv_sequence++;
            } else {
                stp_list[i].box.received_duplicate = false;
                if (db[23].d) { ddprintf("    received_duplicate; "
                        "returning false\n"); }
                return false;
            }
        }

        if (db[23].d) { ddprintf("    found it; returning true\n"); }
        return true;
    }

    if (db[23].d) { ddprintf("    didn't find it; returning false\n"); }
    return false;
}

/* if there was a timeout waiting for an acknowledgement of a sequence
 * packet we sent out, resend it.
 */
void resend_packets()
{
    int i, j;

    if (db[23].d) {
        ddprintf("resend_packets..\n");
    }

    for (i = 0; i < stp_list_count; i++) {
        if (!stp_list[i].box.awaiting_ack) { continue; }

        for (j = 0; j < device_list_count; j++) {
            if (device_list[j].device_type != device_type_wds
                || !mac_equal(stp_list[i].box.name, device_list[j].mac_address))
            {
                continue;
            }
            if (db[23].d) { ddprintf("resend_packets doing send_seq..\n"); }
            stp_list[i].box.send_error++;
            // stp_list[i].delayed_cloud_packets++;
            send_seq(i, stp_list[i].box.message, stp_list[i].box.message_len);

            if (db[23].d) { ddprintf("resend_packets sending message..\n"); }
            send_message((message_t *) stp_list[i].box.message,
                    stp_list[i].box.message_len,
                    &device_list[j]);
        }
    }
} /* resend_packets */
