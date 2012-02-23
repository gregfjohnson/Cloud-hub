/* cloud_mod.h - change and optimize connections among the boxes in the cloud
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: cloud_mod.h,v 1.13 2012-02-22 18:55:24 greg Exp $
 */
#ifndef CLOUD_MOD_H
#define CLOUD_MOD_H

#include "mac.h"
#include "cloud.h"
#include "cloud_msg.h"
#include "util.h"

extern char force_local_change;
extern char old_db5;

extern void check_local_improvement();
extern void add_local_lock_request(mac_address_t node_mac, message_type_t type);
extern bool_t add_stp_link(mac_address_ptr_t sender);
extern void local_stp_add_request(mac_address_t node_mac, message_type_t type);
extern void process_local_connect(message_t *message, mac_address_ptr_t sender);
extern void process_local_stp_delete_request(message_t *message,
        int device_index);
extern void process_local_stp_deleted(message_t *message, int device_index);
extern void process_local_stp_add_request(message_t *message, int device_index);
extern void process_local_stp_added(message_t *message, int device_index);
extern void process_local_stp_refused(message_t *message, int device_index);
extern void process_local_lock_req(message_t *message, int device_index);
extern void process_local_lock_grant(message_t *message, int device_index);
extern void process_local_lock_deny(message_t *message, int device_index);
extern void process_local_lock_add_release(message_t *message,
        int device_index);
extern void process_local_lock_delete_release(message_t *message,
        int device_index);
extern void process_local_lock_release(message_t *message, int device_index);
extern void process_local_stp_added_changed(message_t *message,
        int device_index);
extern void check_connectivity(void);
extern void stp_connect_to(mac_address_t node_mac);
extern void process_stp_arc_delete(message_t *message, int device_index);
extern void resend_local_lock_grant_msg(mac_address_ptr_t name);
extern bool_t doing_stp_update();

#endif
