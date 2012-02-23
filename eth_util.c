/* eth_util.c - utilities to communicate via cat-5 among cloud boxes
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: eth_util.c,v 1.6 2012-02-22 18:55:24 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: eth_util.c,v 1.6 2012-02-22 18:55:24 greg Exp $";

/* cloud boxes need to be able to find each other, figure out best
 * way to connect, etc.  when finding each other wirelessly, they rely
 * on 802.11 beacons.  for cat-5 cable connectivity, we simulate that
 * and send out "eth_beacons" over cat-5.  and, we look for eth_beacons
 * via cat-5 to see if we can see any other boxes on our cat-5 cable.
 */

#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include <linux/if_packet.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "eth_util.h"
#include "cloud.h"
#include "mac_list.h"
#include "pio.h"

static char *cooked_out_file = NULL;
static int eth_if_index;
static int packet_socket;

static char use_pipe = 0;
static pio_t *out_pio;

static mac_list_t beacons;

static mac_address_t my_mac_addr = {0,0,0,0,0,0,};
static mac_address_t name_mac_addr = {0,0,0,0,0,0,};
static mac_address_t other_mac_addr = {0,0,0,0,0,0,};

static char buf[2048];
static struct ethhdr *ethhdr_p = (struct ethhdr *) buf;
static struct sockaddr_ll send_arg;

/* send an eth_beacon out our cat-5 internal LAN interface via using a
 * link-level raw socket.  the message contains this box's "name"
 * (wireless interface mac address), and in the packet header it contains
 * our LAN interface mac address and the eth_beacon protocol short int.
 */
static void send_beacon()
{
    int result;

    #if DEBUG0
        printf("hi from send_beacon..\n");
    #endif

    mac_copy((mac_address_ptr_t) &buf[sizeof(struct ethhdr)], my_mac_addr);
    mac_copy((mac_address_ptr_t) &buf[sizeof(struct ethhdr)
            + sizeof(mac_address_t)],
            name_mac_addr);
    ethhdr_p->h_proto = htons(0x2984);
    mac_copy(ethhdr_p->h_dest, other_mac_addr);
    mac_copy(ethhdr_p->h_source, my_mac_addr);

    if (cooked_out_file == NULL) {

        memset(&send_arg, 0, sizeof(send_arg));
        send_arg.sll_family = AF_PACKET;
        send_arg.sll_ifindex = eth_if_index;
        send_arg.sll_halen = 6;
        mac_copy(send_arg.sll_addr, other_mac_addr);

        result = sendto(packet_socket, buf, sizeof(struct ethhdr) + 12, 0,
                (struct sockaddr *) &send_arg, sizeof(send_arg));
    } else {
        result = pio_write(out_pio, buf, sizeof(struct ethhdr) + 12);
    }

    if (result == -1) {
        fprintf(stderr, "sendto errno %d(%s)\n", errno, strerror(errno));
    } else {
        #if DEBUG0
            printf("sendto result %d\n", result);
        #endif
    }
}

/* initialize internal mac_list of other boxes we see eth_beacons from,
 * our internal LAN mac address, and our wireless mac address.  (we send
 * the latter out in the eth_beacons, because the wireless mac address is
 * considered the "name" or identifier of each cloud box.)
 */
void eth_util_init(char *beacon_file, int socket, int if_index, pio_t *pio,
        mac_address_t eth_mac_addr, mac_address_t box_wlan_mac_addr)
{
    mac_list_init(&beacons, beacon_file,
            mac_list_beacon_name, ETH_TIMEOUT_USEC, true);

    if (pio != NULL) {
        out_pio = pio;
        use_pipe = 1;
    }

    packet_socket = socket;
    eth_if_index = if_index;

    mac_copy(my_mac_addr, eth_mac_addr);
    mac_copy(name_mac_addr, box_wlan_mac_addr);
}

/* write the beacons to the external disk file, and time out any
 * old beacons from mac addresses that we haven't seen beacons from
 * for a while (currently 10 seconds).
 */
void eth_util_interrupt_no_send()
{
    if (mac_list_write(&beacons)) {
        fprintf(stderr, "eth_util_interrupt:  mac_list_write failed\n");
    }

    if (mac_list_expire_timed_macs(&beacons)) {
        fprintf(stderr, "eth_util_interrupt:  expire_timed_macs failed\n");
    }
}

/* called periodically based on timer interrupts; write beacons to disk
 * file, time out any old stale beacons, and send an eth_beacon out via
 * the cat-5 connection.
 */
void eth_util_interrupt()
{
    eth_util_interrupt_no_send();

    send_beacon();
}

/* we have received an inbound message containing an eth_beacon from another
 * box, as indicated by the protocol field in the message.
 * refresh the mac address entry in our list of received eth_beacons,
 * so that it won't time out over the next 10 seconds.  (if the box stays
 * alive and connected to us, we should see more eth_beacons from it
 * within 10 seconds, affirming that it is still alive.)
 */
void eth_util_process_message(unsigned char *message)
{
    mac_list_add(&beacons, &message[sizeof(struct ethhdr)],
            &message[sizeof(struct ethhdr) + 6], 0, NULL, true);
}
