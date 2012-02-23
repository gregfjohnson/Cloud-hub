/* scan_msg.h - send reports of local wifi activity through the cloud
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: scan_msg.h,v 1.5 2012-02-22 18:55:25 greg Exp $
 */
#ifndef SCAN_MSG_H
#define SCAN_MSG_H

#include "cloud_msg.h"
#include "scan_msg_data.h"

extern void process_scanresults_msg(message_t *message, int device_index);
extern void scan_timer(void);

#endif
