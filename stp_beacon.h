/* stp_beacon.h - manage periodic beacons propagated through the cloud
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: stp_beacon.h,v 1.10 2012-02-22 18:55:25 greg Exp $
 */
#ifndef STP_BEACON_H
#define STP_BEACON_H

#include "mac.h"
#include "status.h"
#include "cloud_msg.h"
#include "stp_beacon_data.h"

typedef bool_t (*stp_recv_predicate_t)(stp_recv_beacon_t *);

/* have an entry for every box in our cloud.  so, stp_recv_beacon_count is
 * the count of the number of boxes in our cloud.
 */
extern stp_recv_beacon_t stp_recv_beacons[];
extern int stp_recv_beacon_count;

extern void process_stp_beacon_msg(message_t *message, int device_index);
extern void process_stp_beacon_nak_msg(message_t *message, int device_index);
extern void process_stp_beacon_recv_msg(message_t *message, int device_index);
extern void delete_stp_beacon(mac_address_ptr_t name);
extern bool_t from_stp_neighbor(stp_recv_beacon_t *b);
extern void trim_stp_recv_beacons(stp_recv_predicate_t predicate);
extern void print_stp_recv_beacons();
extern void timeout_stp_recv_beacons();
extern void send_stp_beacon(bool_t see_disconnected_nbr);
extern void send_stp_beacons(mac_address_t new_nbr);
extern void bump_stp_link_unroutable(mac_address_ptr_t dest);
extern void resend_stp_beacon_msg(mac_address_ptr_t dest, stp_beacon_t *beacon);

#endif
