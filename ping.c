/* ping.c - link-level liveness pings among the cloud boxes
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: ping.c,v 1.11 2012-02-22 19:27:23 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: ping.c,v 1.11 2012-02-22 19:27:23 greg Exp $";

#include <string.h>

#include "util.h"
#include "cloud.h"
#include "ping.h"
#include "print.h"
#include "wrt_util.h"
#include "nbr.h"
#include "timer.h"
#include "cloud_msg.h"
#include "device.h"

/* this is a light-weight message that sometimes is sent even if we sent
 * no ping out.
 */
void process_ping_response_msg(message_t *message, int device_index)
{
    mac_address_ptr_t sender;

    sender = get_name(device_index, message->eth_header.h_source);

    if ((sender != NULL) && do_wrt_beacon) {
        wrt_util_update_time(sender);
    }

    if (db[7].d) {
        ddprintf("got ping response from ");
        if (sender == NULL) {
            ddprintf(" <NULL> (from get_name)\n");
        } else {
            mac_dprint(eprintf, stderr, sender);
        }
    }
}

/* someone sent a cloud ping message to us.  we send a ping response back. */
void process_ping_msg(message_t *message, int device_index)
{
    mac_address_ptr_t sender;
    message_t response;
    memset(&response, 0, sizeof(response));

    sender = get_name(device_index, message->eth_header.h_source);

    if (db[7].d) {
        ddprintf("got ping from ");
        if (sender == NULL) {
            ddprintf(" <NULL> (from get_name)\n");
        } else {
            mac_dprint(eprintf, stderr, sender);
        }
    }

    if (sender != NULL) {
        if (db[7].d) {
            ddprintf("send response..\n");
        }
        response.message_type = ping_response_msg;
        mac_copy(response.dest, sender);
        send_cloud_message(&response);
    } else {
        ddprintf("process_ping_msg; get_name could not find mac address "
                "of sender\n");
    }
}

/* broadcast a ping message so that other cloud boxes can see that we are
 * still alive.  these messages are received via cloud protocol interface,
 * so they are more reliable than 802.11 beacons.
 * we just want to send them a little keep-alive message, and don't need
 * a response.  So we send ping_response_msg.
 */
void do_ping_neighbors()
{
    message_t message;

    memset(&message, 0, sizeof(message));

    got_interrupt[ping_neighbors] = 0;

    if (!db[46].d) { return; }

    message.message_type = ping_response_msg;

     mac_copy(message.dest, mac_address_bcast);
     send_cloud_message(&message);

    set_next_ping_alarm();
}

/* debugging (non-protocol) routine to send a ping message to the
 * device_list[] element indexed by the int in "buf"
 */
void send_ping(char *buf)
{
    int device_index;
    int msg_len;
    message_t message;
    int result;

    memset(&message, 0, sizeof(message));

    result = sscanf(buf, "%d", &device_index);
    if (result != 1) {
        ddprintf("send_ping; no device index in '%s'\n", buf);
        return;
    }
    if (device_index < 0 || device_index >= device_list_count) {
        ddprintf("send_ping; device index '%d' out of range\n", device_index);
        return;
    }

    message.message_type = ping_msg;
    msg_len = ((byte *) &message.message_type)
            + sizeof(message.message_type)
            - ((byte *) &message);

    ddprintf("pinging device_list[%d]:  ", device_index);
    print_device(eprintf, stderr, &device_list[device_index]);
    sendum((byte *) &message, msg_len, &device_list[device_index]);

} /* send_ping */

/* debugging (non-protocol) routine to send a ping message to every
 * neighbor box
 */
void send_nbr_pings()
{
    int i;
    message_t message;

    memset(&message, 0, sizeof(message));

    memset(&message, 0, sizeof(message));
    message.message_type = ping_msg;

    for (i = 0; i < nbr_device_list_count; i++) {
        if (db[7].d) {
            ddprintf("send ping to ");
                mac_dprint(eprintf, stderr, nbr_device_list[i].name);
        }
        mac_copy(message.dest, nbr_device_list[i].name);
        send_cloud_message(&message);
    }
}
