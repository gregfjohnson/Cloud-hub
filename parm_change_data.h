/* parm_change_data.h - propagate wifi parmater changes throughout the cloud
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: parm_change_data.h,v 1.6 2012-02-22 18:55:25 greg Exp $
 */
#ifndef PARM_CHANGE_MSG_DATA_H
#define PARM_CHANGE_MSG_DATA_H

#include "util.h"
#include "mac.h"
#include "scan_msg_data.h"

typedef struct {
    mac_address_t originator;

    char orig_ssid[MAX_SSID];
    char new_ssid[MAX_SSID];

    byte orig_channel;
    byte new_channel;

    /* mode B, mode G, mixed mode */
    byte orig_wireless_mode;
    byte new_wireless_mode;

    /* WEP64, WEP128, DISABLED */
    byte orig_security_mode;
    byte new_security_mode;

    char orig_wep_key[MAX_WEP_KEY];
    char new_wep_key[MAX_WEP_KEY];

    /* make sure we have enough room to encrypt this struct.
     * (encrypted data must be a multiple of 8 in length; we use as much
     * or as little of "pad" as required.)
     */
    byte pad[8];

} parm_change_msg_t;

#endif
