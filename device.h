/* device.h - manage local interfaces and other cloud boxes and clients
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: device.h,v 1.10 2012-02-22 19:27:22 greg Exp $
 */
#ifndef DEVICE_H
#define DEVICE_H

#include "mac.h"
#include "device_type.h"
#include "print.h"
#include "cloud.h"

/* typedef for the devices on this box; eth0, wlan0, wlan0wds_i */
typedef struct {
    char device_name[64];
    int if_index;
    int fd;
    int out_fd;
    pio_t in_pio, out_pio;

    /* for wlan0wds_i devices this is the mac address of the other end of
     * the connection.
     * this is used to make sure that we have the correct
     * <wlan0_wds_i, other_guy_mac_address> pairs represented in core.
     * (these bindings can change by update_wds.)
     * for wlan0 and eth0, this is the mac address of the device on our box.
     * it is used to set the source address for outgoing messages we create.
     */
    mac_address_t mac_address;
    char device_type;

    /* should we simulate this device? */
    char sim_device;

    int stat_index;

    int expect_k;
    int expect_n;
    message_t message;
    int msg_len;
} device_t;

extern device_t device_list[MAX_CLOUD];
extern int device_list_count;

extern void add_device(char *device_name, mac_address_t mac_address,
        device_type_t device_type);
extern char check_devices(void);
extern void delete_device(int d);
extern void print_devices(void);
extern void print_device(ddprintf_t *fn, FILE *f, device_t *device);
extern void test_devices(void);
extern int sendum(byte *message, int msg_len, device_t *device);
extern int send_to_interface(byte *message, int msg_len,
        device_type_t device_type);
extern int is_eth(device_t *device);
extern int is_wlan(device_t *device);
extern void promiscuous(char *device_name, bool_t on);

#endif
