/* stp_beacon_data.h - manage periodic beacons propagated through the cloud
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: stp_beacon_data.h,v 1.7 2012-02-22 18:55:25 greg Exp $
 */
#ifndef STP_BEACON_DATA_H
#define STP_BEACON_DATA_H

#include "status.h"
#include "cloud_data.h"

/* cloud protocol message body */  
typedef struct {

    /* mac address of cloud box that created this message */
    mac_address_t originator;

    /* signal strength to weakest link from me to the rest of the stp tree;
     * used for reporting cloud status.  it's a bad thing if some of the
     * cloud nodes have to connect to the cloud via very weak links.
     */
    short weakest_stp_link;

    /* for propagating debug variable setting through the cloud.
     * zero is nominal; positive is do something;
     * db_index + 1000 is "turn it off", db_index + 2000 is "turn it on".
     */
    short tweak_db;

    /* number of records in status array below actually sent in this message */
    int status_count;

    /* we send perm_io_stat records for our own eth and wlan devices, and
     * for our stp neighbors.
     *
     * we only actually send "status_count" of these using message len.
     */
    status_t status[MAX_CLOUD];
} stp_beacon_t;

/* an stp_beacon received in from the given neighbor, which arrived at the
 * specified time.
 */
typedef struct {
    /* time it was received, so we can time it out */
    long sec, usec;

    /* the beacon received (actually, we just copy and store a subset of it) */
    stp_beacon_t stp_beacon;

    /* the neighbor from which we received the beacon */
    mac_address_t neighbor;

    /* the number of the most recently sent wrapped client packet that
     * we have seen from this other cloud box
     */
    unsigned short orig_sequence_num;

    /* does the above field contain valid data? */
    bool_t orig_sequence_num_inited;

    /* the number of other cloud boxes that this one can see directly */
    int direct_sight_count;

    /* the names of the other cloud boxes that this one can see directly */
    mac_address_t direct_sight[MAX_CLOUD];

} stp_recv_beacon_t;

#endif
