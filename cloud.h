/* cloud.h - cloud protocol message type, exports from merge_cloud.d
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: cloud.h,v 1.25 2012-02-22 18:55:24 greg Exp $
 */
#ifndef CLOUD_H
#define CLOUD_H

#include <linux/if_ether.h>

#include "mac.h"
#include "util.h"
#include "status.h"
#include "pio.h"
#include "cloud_box.h"
#include "cloud_msg_data.h"
#include "cloud_data.h"
#include "stp_beacon_data.h"
#include "lock_data.h"
#include "scan_msg_data.h"
#include "sequence_data.h"
#include "parm_change_data.h"

// #define RETRY_TIMED_OUT_LOCKABLES

/* cloud messages (negotiate connectivity, stp beacons, stp pings, etc. */
#define CLOUD_MSG 0x2983

/* eth beacon messages (allow cloud boxes to notice each other via cat-5 */
#define ETH_BCN_MSG 0x2984

/* ll_shell_ftp messages (link-level utility to do interactive shell
 * and file transfer
 */
#define LL_SHELL_MSG 0x2985

/* wrapped client payload messages being passed through the cloud */
#define WRAPPED_CLIENT_MSG 0x2986

/* 802.11 beacon message; use it to find mac addresses of other boxes, and
 * to evaluate signal strengths to other boxes
 */
#define PRISM_MSG 1

/* payload message received directly from a client wireless box */
#define OTHER_MSG 2

/* should we temporarily ignore incoming messages from an ad-hoc client?
 * (want to avoid erroneously assuming copies of a message originating from our
 * ad-hoc client sent by other cloud boxes to their ad-hoc clients are
 * from our ad-hoc client)
 */
#define AD_HOC_BLOCK_MSG 3

/* maximum number of ad-hoc client boxes that can associate with one
 * cloud box
 */
#define AD_HOC_CLIENTS_PER_BOX 4

/* maximum number of ad-hoc clients allowed into the entire cloud */
#define AD_HOC_CLIENTS_ACROSS_CLOUD 32

/* for simulation infrastructure; maximum number of pipe-based simulated
 * connections to other cloud boxes per box
 */
#define MAX_OUT_PIPE 8

/* size of buffer into which we read raw socket packets
 * (our basic communication mechanism)
 */
#define MAX_RECVFROM_BUF 2048

/* size of buffer we use to send stuff using raw socket packets */
#define MAX_SENDTO_BUF 2048

/* largest number of bytes we will give to the "sendto" system call */
#define MAX_SENDTO 1514

/* payload message; a client packet that we are transporting */
typedef struct {
    /* since we add a little bit to messages, we may need
     * to break messages up, send the pieces separately, and
     * re-assemble them on the other end.  I.e., "7 of 9".
     */ 
    byte k, n;

    /* this is to support nonlocal message propagation */
    mac_address_t originator;
    unsigned short originator_sequence_num;

    /* the payload data */
    byte msg_body[CLOUD_BUF_LEN];
} payload_msg_t;

/* if we are using wds, we can take non-cloud messages from eth or wireless ap,
 * and pass them around unchanged through the cloud.  the cloud doesn't care
 * about ethhdr source and dest fields in this case, because it can tell
 * all it needs to know based on the device_list index that the message came
 * in from.
 *
 * on the other hand, in the ad-hoc case, we can't do that.  so, to pass
 * around non-cloud messages inside the cloud, after we receive them in
 * as global inputs to the cloud (from somebody's eth interface; no wireless
 * clients in this case) we have to wrap them and put our own ethhdr on them.
 * 
 * doing the same thing in the wds case will let us do passive packet
 * error detection, so let's try it.
 */
typedef struct {
    struct ethhdr eth_header;

    /* sequence number of this message for passive checking of
     * received and dropped packets
     */
    byte sequence_num;

    /* the cloud node we are trying to send a message to.  may not
     * be a neighbor; may need to pass it along via spanning tree
     * neighbor.
     */
    mac_address_t dest;

    message_type_t message_type;

    union {
        /* manage locking of logical arcs connecting cloud boxes */
        lock_message_t lock_message;

        /* cloud protocol message */
        stp_beacon_t stp_beacon;

        /* in case we are trying to do re-sends based on message
         * sequence counting.  seems better to let other higher up
         * the protocol stack handle this in general.
         */
        seq_t seq;

        /* payload message containing a client packet we are transporting */
        payload_msg_t msg;

        /* results of wifi scan */
        scan_msg_t scan;

        /* change wifi parameters across the cloud */
        parm_change_msg_t parm_change;
    } v;
} message_t;

/* runtime-settable configuration and debugging options */
extern char_str_t db[];

#define DEBUG_SPARSE_PRINT(msg)                                         \
        {   static int db_count = 0, print_count = 0;                   \
            print_count++;                                              \
            if (!db[41].d || (db_count = (db_count + 1) % 20) == 1) {   \
                                                                        \
                msg                                                     \
            }                                                           \
        }                                                               \

/* these are our stp neighbors */
extern node_t stp_list[MAX_CLOUD];
extern int stp_list_count;

extern bool_t ad_hoc_mode;

extern mac_address_t my_wlan_mac_address;

extern message_t my_beacon;
extern bool_t have_my_beacon;

extern message_t beacon;
extern bool_t have_beacon;

extern char do_wrt_beacon;

/* command-line argument values */
extern char *eth_device_name;
extern char *wds_file;
extern char use_pipes;
extern char *pipe_directory;
extern char *wlan_device_name;
extern char *eth_device_name;
extern bool_t have_mon_device;
extern char *wlan_mon_device_name;
extern char *sig_strength_fname;
extern char *perm_log_fname;
extern char *eth_fname;
extern mac_address_t my_eth_mac_address;

extern short my_weakest_stp_link;
extern bool_t update_cloud_db;
extern int cloud_db_ind;

extern char did_com_util_init;
extern char do_ll_shell;

extern int max_packet_len;

extern int wrapper_len;

extern void print_state();
extern void to_nominal_state();
extern void clear_state(mac_address_ptr_t name);
extern void check_msg_count();

#endif
