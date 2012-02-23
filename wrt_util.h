/* wrt_util.h - detect other cloud boxes based on their 802.11 beacons
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: wrt_util.h,v 1.8 2012-02-22 18:55:25 greg Exp $
 */
#ifndef WRT_UTIL_H
#define WRT_UTIL_H

#include "mac.h"

extern void wrt_util_init(char *beacon_file, char *sig_strength_file);
extern void wrt_util_process_message(unsigned char *message, int len);
extern void wrt_util_update_time(mac_address_t mac_addr);
extern bool_t wrt_util_beacon_message(unsigned char *message, int msg_len);
extern void wrt_util_interrupt();
extern int wrt_util_set_my_channel();
extern int wrt_util_get_my_channel();

/* print out the essids we've gotten beacons from, and sorta log-style
 * histograms of inter-arrival times of beacons from the essids.
 */
extern void wrt_util_print_beacons(void);

/* turn a wrt_util internal debugging flag on or off */
extern void wrt_util_set_debug(int i, bool_t value);

/* get the value of wrt_util internal debugging flag "i" */
extern bool_t wrt_util_get_debug(int i);

#endif
