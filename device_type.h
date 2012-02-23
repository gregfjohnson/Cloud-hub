/* device_type.h - print readable versions of device types
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: device_type.h,v 1.6 2012-02-22 18:55:24 greg Exp $
 */
#ifndef DEVICE_TYPE_H
#define DEVICE_TYPE_H

/* types of interfaces (eth0, wlan0, etc.) */
typedef enum {
    device_type_wds,
    device_type_ad_hoc,
    device_type_wlan,
    device_type_cloud_wlan,
    device_type_wlan_mon,
    device_type_eth,
    device_type_cloud_eth,
    device_type_cloud_wds,
    device_type_unknown,
} device_type_t;

extern char *device_type_string(device_type_t device);

#endif
