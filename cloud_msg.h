/* cloud_msg.h - send and receive cloud protocol messages
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: cloud_msg.h,v 1.11 2012-02-22 18:55:24 greg Exp $
 */
#ifndef CLOUD_MSG_H
#define CLOUD_MSG_H

#include "cloud.h"
#include "device.h"
#include "cloud_msg_data.h"

extern unsigned short originator_sequence_num;

extern char *message_type_string(message_type_t msg);
extern void process_cloud_message(message_t *message, int device_index);
extern int send_cloud_message(message_t *message);
extern void print_msg(char *title, message_t *message);
extern int send_message(message_t *message, int msg_len, device_t *device);
extern bool_t update_k_for_n_state(message_t *message, int *msg_len,
        int dev_index);
extern void bcast_forward_message(message_t *message, int msg_len, int d,
        bool_t originated_locally);

#endif
