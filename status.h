/* status.h - maintain information on connections other cloud boxes
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: status.h,v 1.9 2012-02-22 19:27:23 greg Exp $
 */
#ifndef STATUS_H
#define STATUS_H

#include "util.h"
#include "mac.h"
#include "device_type.h"

#define STATUS_PRINT_DATA  1
#define STATUS_PRINT_CLOUD 2
#define STATUS_PRINT_PINGS 3
#define STATUS_PRINT_ALL   4

#define STATUS_UNKNOWN 0
#define STATUS_CLOUD_NBR 1
#define STATUS_CLOUD_NOT_NBR 2
#define STATUS_NON_CLOUD_CLIENT 3
#define STATUS_NON_CLOUD_NON_CLIENT 4

/* status information about our connection to another cloud box. */
typedef struct {

    /* this is the one true name of another cloud box. */
    mac_address_t name;

    device_type_t device_type;
    byte sig_strength;
    byte neighbor_type;
    byte see_directly;

    /* these are for passive packet counting and errors */
    int packets_received;
    int packets_lost;

    int data_packets_received;
    int data_packets_lost;

    int ping_packets_received;
    int ping_packets_lost;
} status_t;

typedef enum {
    sf_cloud_packets_received,
    sf_noncloud_packets_received,
    sf_receive_errors,

    sf_cloud_packets_owed,
    sf_cloud_packets_sent,
    sf_cloud_send_errors,
    sf_dropped_cloud_packets,
    sf_delayed_cloud_packets,
    sf_cloud_delay_time,

    sf_noncloud_packets_owed,
    sf_noncloud_packets_sent,
    sf_noncloud_send_errors,
    sf_dropped_noncloud_packets,
    sf_delayed_noncloud_packets,
    sf_noncloud_delay_time,
} status_field_t;

extern status_t perm_io_stat[];
extern int perm_io_stat_count;

extern int send_sequence[];
extern int send_data_sequence[];
extern int send_ping_sequence[];

extern byte recv_sequence[];
extern bool_t have_recv_sequence[];

extern byte recv_data_sequence[];
extern bool_t have_recv_data_sequence[];

extern byte recv_ping_sequence[];
extern bool_t have_recv_ping_sequence[];

void status_dprint_title(ddprintf_t fn, FILE *f, int indent);
void status_print_title(FILE *f, int indent);
void status_print_array(FILE *f, status_t *s, int count);
void status_dprint_array(ddprintf_t fn, FILE *f, status_t *s, int count);
void status_print_short(FILE *f, status_t *s, char *label);
void status_dprint_short(ddprintf_t fn, FILE *f, status_t *s, char *label,
        int todo);
void status_print_short_array(FILE *f, status_t *s, int count);
void status_dprint_short_array(ddprintf_t fn, FILE *f, status_t *s, int count);
void status_init(status_t *s);
int status_find_by_mac(status_t *s, int status_count, mac_address_t mac);
int status_add_by_mac(status_t *s, int *status_count, mac_address_t mac,
        device_type_t type);
void add_perm_io_stat_index(int *nbr_perm_io_stat_index,
        mac_address_t neighbor_name, device_type_t type);

#endif
