/* io_stat.c - maintain and print i/o message counts and status
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: io_stat.c,v 1.10 2012-02-22 19:27:22 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: io_stat.c,v 1.10 2012-02-22 19:27:22 greg Exp $";

#include <string.h>

#include "util.h"
#include "io_stat.h"
#include "print.h"
#include "device.h"

io_stat_t io_stat[MAX_CLOUD];
int io_stat_count = 0;

/* print summary comm stats for each interface and the other cloud boxes we know
 * about using print function
 */
void short_print_io_stats(ddprintf_t *fn, FILE *f)
{
    int i;
    bool_t first = true;

    /* 25 -> noncloud stats; 26 -> cloud stats */
    if (!db[25].d && !db[26].d) { return; }

    for (i = 0; i < io_stat_count; i++) {
        io_stat_t *s = &io_stat[i];
        if (s->cloud_recv == 0 && s->cloud_send == 0
            && s->noncloud_recv == 0 && s->noncloud_send == 0)
        {
            continue;
        }
        if (!db[26].d && s->noncloud_recv == 0 && s->noncloud_send == 0) {
            continue;
        }
        if (!db[25].d && s->cloud_recv == 0 && s->cloud_send == 0) { continue; }

        if (first) {
            first = false;
        } else {
            fn(f, "  ");
        }
        fn(f, "%s: ", s->device_name);
        if (db[25].d) { fn(f, "<%d %d>", s->noncloud_recv, s->noncloud_send); }
        if (db[26].d) { fn(f, "<%d %d>", s->cloud_recv, s->cloud_send); }
    }
    fn(f, "\r");
}

/* print all comm stats for interface associated with stat
 * using print function.
 */
void io_stat_print_fn(ddprintf_t fn, FILE *f, io_stat_t *stat)
{
    fn(f, "    %-8s%8d%8d%8d%8d%8d%8d%8d%8d\n",
            stat->device_name,
            stat->cloud_recv, stat->cloud_send, stat->cloud_send_error,
            stat->noncloud_recv, stat->noncloud_send, stat->noncloud_send_error,
            stat->recv_error,
            stat->delivery_error);
}

/* print comm stats for for interface associated with stat to stream f */
void io_stat_print(FILE *f, io_stat_t *stat)
{
    io_stat_print_fn(eprintf, f, stat);
}

/* print all comm stats for each interface and the other cloud boxes we know
 * about using print function.
 */
void print_io_stats(ddprintf_t *fn, FILE *f)
{
    int i;

    fn(f, "i/o statistics:\n");
    fn(f,
"    dev           cr      cs     cse     ncr     ncs    ncse      re     del\n"
    );

    for (i = 0; i < io_stat_count; i++) {
        io_stat_print_fn(fn, f, &io_stat[i]);
    }
    fn(f, "max_packet_len:  %d\n", max_packet_len);
}

/* if not already present, add a comm-statistics gathering struct for the
 * device, which may be a local interface or another cloud box that
 * we know about.
 */
void add_io_stat(device_t *device)
{
    int i;
    int found = 0;

    for (i = 0; i < io_stat_count; i++) {
        if (strcmp(device->device_name, io_stat[i].device_name) == 0) {
            found = 1;
            break;
        }
    }

    if (!found) {
        int j, k;
        if (io_stat_count >= MAX_CLOUD) {
            ddprintf("add_io_stat:  too many io_stat devices!\n");
            /* find an io_stat device that is no longer in device_list and
             * commandeer it.
             */
             for (j = 0; j < io_stat_count; j++) {
                found = 0;
                for (k = 0; k < device_list_count; k++) {
                    if (strcmp(io_stat[j].device_name,
                            device_list[k].device_name) == 0)
                    {
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    ddprintf("reusing io_stat; "
                            "its last current contents:\n");
                    io_stat_print(stderr, &io_stat[j]);
                }
                i = j;
            }
        } else {
            i = io_stat_count++;
        }
    }

    io_stat[i].cloud_recv = io_stat[i].noncloud_recv = 0;
    io_stat[i].cloud_send = io_stat[i].noncloud_send = 0;
    io_stat[i].cloud_send_error = io_stat[i].noncloud_send_error = 0;
    io_stat[i].recv_error = 0;
    io_stat[i].delivery_error = 0;
    strcpy(io_stat[i].device_name, device->device_name);

    device->stat_index = i;
}

/* zero comm stats for all local interfaces and other cloud boxes we
 * know about.
 */
void reset_io_stats()
{
    int i;

    for (i = 0; i < io_stat_count; i++) {
        io_stat[i].cloud_recv = io_stat[i].noncloud_recv = 0;
        io_stat[i].cloud_send = io_stat[i].noncloud_send = 0;
        io_stat[i].cloud_send_error = io_stat[i].noncloud_send_error = 0;
        io_stat[i].delivery_error = 0;
        io_stat[i].recv_error = 0;
    }
}

