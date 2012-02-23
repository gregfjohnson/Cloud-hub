/* io_stat.h - maintain and print i/o message counts and status
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: io_stat.h,v 1.9 2012-02-22 19:27:22 greg Exp $
 */
#ifndef IO_STAT_T
#define IO_STAT_T

#include "print.h"
#include "device.h"

/* these things are pretty closely associated with device_list entries.
 * the only thing we use to associate these things is the device_name
 * field of the device_list entry.
 */
typedef struct {
    int cloud_send, noncloud_send;
    int cloud_send_error, noncloud_send_error;

    int cloud_delayed_packets, noncloud_delayed_packets;
    int cloud_dropped_packets, noncloud_dropped_packets;

    int cloud_send_delay, noncloud_send_delay;

    int cloud_recv, noncloud_recv;
    int recv_error;
    int cloud_recv_delay, noncloud_recv_delay;

    int delivery_error;

    char device_name[64];
} io_stat_t;

extern io_stat_t io_stat[MAX_CLOUD];
extern int io_stat_count;

extern void short_print_io_stats(ddprintf_t *fn, FILE *f);
extern void io_stat_print_fn(ddprintf_t fn, FILE *f, io_stat_t *stat);
extern void io_stat_print(FILE *f, io_stat_t *stat);
extern void print_io_stats(ddprintf_t *fn, FILE *f);
extern void add_io_stat(device_t *device);
extern void reset_io_stats();

#endif
