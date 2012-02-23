/* device_type.c - print readable versions of device types
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: device_type.c,v 1.5 2012-02-22 18:55:24 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: device_type.c,v 1.5 2012-02-22 18:55:24 greg Exp $";

#include "device_type.h"

/* return a static string describing the device type, for debugging printouts */
char *device_type_string(device_type_t device)
{
    char *p;
    switch (device) {
    default :
    case device_type_unknown : p = "device_type_unknown"; break;
    case device_type_wds : p = "device_type_wds"; break;
    case device_type_ad_hoc : p = "device_type_ad_hoc"; break;
    case device_type_wlan : p = "device_type_wlan"; break;
    case device_type_cloud_wlan : p = "device_type_cloud_wlan"; break;
    case device_type_wlan_mon : p = "device_type_wlan_mon"; break;
    case device_type_eth : p = "device_type_eth"; break;
    case device_type_cloud_eth : p = "device_type_cloud_eth"; break;
    case device_type_cloud_wds : p = "device_type_cloud_wds"; break;
    }
    return p;
}
