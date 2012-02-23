/* mac_list.h - maintain a list of mac addresses and related data in a file
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: mac_list.h,v 1.5 2012-02-22 18:55:25 greg Exp $
 */
#ifndef MAC_LIST_H
#define MAC_LIST_H

#include "mac.h"

#include <limits.h>

/* a mac address that should be deleted after a certain amount of time.
 * for example, if another cloud box gets turned off we should delete it
 * locally after we haven't heard from it for a little while.
 */
typedef struct {
    long tv_sec, tv_usec;
    mac_address_t mac_addr;
} timed_mac_t;

/* different types of mac address lists */
typedef enum {
    mac_list_beacon,                    /* just a mac address */
    mac_list_beacon_name,               /* and mac address */
    mac_list_beacon_desc,               /* and character string */
    mac_list_beacon_signal_strength,    /* and int (signal strength) value */
} mac_list_type_t;

#define MAX_CLOUD 32
#define MAX_DESC 32
#define NO_SIGNAL_STRENGTH -2

/* a struct to hold information for each mac address in our list */
typedef struct {
    timed_mac_t beacons[MAX_CLOUD];
    mac_address_t names[MAX_CLOUD];
    int signal_strength[MAX_CLOUD];
    char desc[MAX_CLOUD][MAX_DESC];
    int next_beacon;
    long last_time_sec;
    long last_time_usec;
    bool_t write_delay;
    long long timeout;
    char fname[PATH_MAX];
    mac_list_type_t type;
} mac_list_t;

extern int mac_list_expire_timed_macs(mac_list_t *mac_list);
extern int mac_list_read(mac_list_t *mac_list);
extern int mac_list_write(mac_list_t *mac_list);
extern int mac_list_print(FILE *f, mac_list_t *mac_list);
extern void mac_list_db_msg(char *msg);
extern int mac_list_init(mac_list_t *mac_list, char *fname,
        mac_list_type_t type, long long timeout, bool_t write_delay);
extern int mac_list_init_read(mac_list_t *mac_list, char *fname,
        mac_list_type_t type, long long timeout, bool_t write_delay);
extern void mac_list_add(mac_list_t *mac_list, mac_address_t mac_addr,
        mac_address_t name, int signal_strength, char *desc, bool_t write_um);

#endif
