/* cloud_msg_data.h - send and receive cloud protocol messages
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: cloud_msg_data.h,v 1.9 2012-02-22 18:55:24 greg Exp $
 */
#ifndef CLOUD_MSG_DATA_H
#define CLOUD_MSG_DATA_H

/* the types of all cloud protocol messages */
typedef enum {
    unknown_msg = 0,
    local_lock_grant_msg = 2,
    local_lock_deny_msg = 3,
    local_delete_release_msg = 4,
    local_add_release_msg = 5,
    local_lock_release_msg = 6,
    stp_beacon_msg = 7,
    stp_beacon_recv_msg = 8,
    nonlocal_lock_req_msg = 9,
    nonlocal_lock_grant_msg = 10,
    nonlocal_lock_deny_msg = 11,
    nonlocal_delete_release_msg = 12,
    nonlocal_add_release_msg = 13,
    ping_msg = 14,
    ping_response_msg = 15,
    local_lock_req_new_msg = 16,
    local_lock_req_old_msg = 17,
    stp_arc_delete_msg = 18,
    sequence_msg = 19,
    ack_sequence_msg = 20,
    local_stp_add_request_msg = 21,
    local_stp_added_msg = 22,
    local_stp_add_changed_request_msg = 23,
    local_stp_added_changed_msg = 24,
    local_stp_delete_request_msg = 25,
    local_stp_deleted_msg = 26,
    local_stp_refused_msg = 27,
    stp_beacon_nak_msg = 28,
    ad_hoc_bcast_block_msg = 29,
    ad_hoc_bcast_unblock_msg = 30,
    scanresults_msg = 31,
    parm_change_start_msg = 32,
    parm_change_ready_msg = 33,
    parm_change_not_ready_msg = 34,
    parm_change_go_msg = 35,
} message_type_t;

#endif
