/* nbr.h - track cloud hub neighbors that can be communicated with directly
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: nbr.h,v 1.9 2012-02-22 18:55:25 greg Exp $
 */
#ifndef NBR_H
#define NBR_H

#include "mac.h"
#include "cloud.h"

/* Paul and I found that a raw linksys strength of about 179 or 180 equals
 * a mac stumbler signal strength of 10, and a raw linksys strength of about 190
 * equals a mac stumbler signal strength of about 20.  so, although 180
 * probably would be ok, 185 is a bit safer.
 */
#define weak_threshold 185

#define good_threshold 190

#define max_sig_strength 255

/* devices from which we are receiving beacons.
 * (regular beacons, not stp beacons.  this info is gotten from the wds
 * file and the eth_beacons file.)
 * nbr_device_list is the list of cloud boxes that we see directly,
 * either wirelessly or via our ethernet connection.
 */
extern cloud_box_t nbr_device_list[];
extern int nbr_device_list_count;

extern char check_nbr_devices();
extern void update_nbr_signal_strength();
extern int get_sig_strength(mac_address_t addr);
extern void update_stp_nbr_connectivity();
extern mac_address_ptr_t get_name(int device_index, mac_address_t mac_addr);

#endif
