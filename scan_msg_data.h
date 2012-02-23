/* scan_msg_data.h - send reports of local wifi activity through the cloud
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: scan_msg_data.h,v 1.9 2012-02-22 18:55:25 greg Exp $
 */
#ifndef SCAN_MSG_DATA_H
#define SCAN_MSG_DATA_H

#define MAX_SSID 33

#define MAX_SCAN 32

#define MAX_SCAN_TIME 120

#define MODE_UNKNOWN 0
#define MODE_AD_HOC 1
#define MODE_MANAGED 2

#define MAX_WEP_KEY 27

#define WIFI_MODE_UNKNOWN 0
#define WIFI_MODE_B_ONLY 1
#define WIFI_MODE_G_ONLY 2
#define WIFI_MODE_MIXED 3

#define WIFI_SECURITY_UNKNOWN 0
#define WIFI_SECURITY_DISABLED 1
#define WIFI_SECURITY_WEP 2

/* description of wifi signals we detect, to be passed around the cloud
 * so that cloud boxes can create local web pages showing nearby wifi
 * activity
 */
typedef struct {
    char ssid[MAX_SSID];
    short noise, rssi;
    byte mode;
    byte channel;
} scan_struct_t;

/* an array of scan_struct_t's, with a count of how many are in the array.
 * sent in message_t cloud messages.
 */
typedef struct {
    int count;
    scan_struct_t list[MAX_SCAN];
} scan_msg_t;

#endif
