/* sequence.h - manage sequence numbers on cloud messages
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: sequence.h,v 1.10 2012-02-22 18:55:25 greg Exp $
 */
#ifndef SEQUENCE_H
#define SEQUENCE_H

#include "cloud.h"
#include "sequence_data.h"

#define MAX_SEQUENCE_ERROR 16

extern void process_sequence_msg(message_t *message, int d);
extern void process_ack_sequence_msg(message_t *message, int d);
extern bool_t awaiting_ack();
extern bool_t ack_read_ok(int d);
extern bool_t message_ok(int d, int message_len);
extern void resend_packets();
extern void send_seq(int stp_ind, byte *message, int msg_len);
extern void sequence_check(message_t *message, int device_index, int dev_index);

#endif
