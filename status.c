/* status.c - maintain information on connections other cloud boxes
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: status.c,v 1.9 2012-02-22 19:27:23 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: status.c,v 1.9 2012-02-22 19:27:23 greg Exp $";

/* maintain information on connection to another cloud box;
 * that cloud box's "name" (i.e., wireless interface mac address),
 * signal strength, the number of packets received,
 * and the number of packets dropped (based on packet sequence numbers).
 *
 * if a cloud box is configured to print cloud status, these status
 * structs get sent around the cloud in stp_beacon messages.
 */

#include <string.h>

#include "status.h"
#include "cloud.h"
#include "util.h"
#include "print.h"
#include "device_type.h"

/* permanent record of communication to a neighbor identified by
 * their one true name (mac address of wlan device).
 *
 * entries are preserved across disappearance and re-appearance of
 * even the raw devices used to communicate with neighbors.
 *
 * there is also one entry for wlan and one entry for eth.  these are
 * to count communication events to non-cloud devices.  (if a receive
 * error occurs on the eth device it is tallied here, since we
 * can't read the message and so don't know if it was a cloud message or not.)
 *
 * at some point, time-stamp last modification of these guys, and if
 * we run out reclaim the one with oldest last-modification time.
 */

status_t perm_io_stat[MAX_CLOUD];
int perm_io_stat_count = 0;

/* these guys use indices that match those of perm_io_stat.  they are
 * separate because instance of status_t go in packets and other places
 * where this information is not needed.
 */
int send_sequence[MAX_CLOUD];
int send_data_sequence[MAX_CLOUD];
int send_ping_sequence[MAX_CLOUD];

byte recv_sequence[MAX_CLOUD];
bool_t have_recv_sequence[MAX_CLOUD];

byte recv_data_sequence[MAX_CLOUD];
bool_t have_recv_data_sequence[MAX_CLOUD];

byte recv_ping_sequence[MAX_CLOUD];
bool_t have_recv_ping_sequence[MAX_CLOUD];

/* initialize all fields of the struct to zero */
void status_init(status_t *s)
{
    memset(s, 0, sizeof(status_t));
}

/* print column titles for a status report */
void status_dprint_short_title(ddprintf_t fn, FILE *f, int indent)
{
    int i;
    for (i = 0; i < indent; i++) { fn(f, " "); }
    fn(f, "packets received    packets lost    percent received\n");
}

/* print data right justified in a field of width field_width */
static void print_int(ddprintf_t fn, FILE *f, int data, int field_width)
{
    int len = 0, test, i;

    test = data;
    if (test == 0) {
        len = 1;
    } else {
        if (test > 0) { 
            len = 0; 
        } else { 
            len = 1;
            test = -test;
        }
        while (test != 0) {
            test /= 10;
            len++;
        }
    }
    if (len >= field_width) {
        fn(f, " ");
    } else {
        for (i = len; i < field_width; i++) { fn(f, " "); }
    }
    fn(f, "%d", data);
}

/* pretty-print the status struct using printing function fn and
 * (optionally) stream f
 */
void status_dprint_short(ddprintf_t fn, FILE *f, status_t *s, char *label,
        int todo)
{
    int percent;
    float den;
    int packets_received;
    int packets_lost;

    if (todo == STATUS_PRINT_DATA) {
        packets_received = s->data_packets_received;
        packets_lost = s->data_packets_lost;
    } else if (todo == STATUS_PRINT_CLOUD) {
        packets_received = s->packets_received;
        packets_lost = s->packets_lost;
    } else if (todo == STATUS_PRINT_PINGS) {
        packets_received = s->ping_packets_received;
        packets_lost = s->ping_packets_lost;
    } else {
        packets_received = s->packets_received + s->data_packets_received
                + s->ping_packets_received;
        packets_lost = s->packets_lost + s->data_packets_lost
                + s->ping_packets_lost;
    }

    fn(f, "%s", label);
    print_int(fn, f, packets_received, 16);
    print_int(fn, f, packets_lost, 16);

    den = (float) (packets_received + packets_lost);

    if (den == 0) {
        percent = 100;
    } else {
        percent = (int) (.5 + 100. * ((float) packets_received) / den);
    }

    print_int(fn, f, percent, 19);
    fn(f, "%c\n", '%');
}

/* pretty-print the array of status structs using printing function fn and 
 * (optionally) stream f
 */
void status_dprint_short_array(ddprintf_t fn, FILE *f, status_t *s, int count)
{
    int i;

    status_dprint_short_title(fn, f, 20);

    for (i = 0; i < count; i++) {
        char mac_address[64], title[64];
        if (s[i].device_type == device_type_eth
            || s[i].device_type == device_type_wlan)
        {
            int j;
            for (j = 0; j < 64; j++) { title[j] = ' '; }
            sprintf(title, "%s:", device_type_string(s[i].device_type));
            title[strlen(title)] = ' ';
            title[23] = '\0';
            status_dprint_short(fn, f, &s[i], title, STATUS_PRINT_ALL);
        } else {
            mac_sprintf(mac_address, s[i].name);

            sprintf(title, "%s:     ", mac_address);
            status_dprint_short(fn, f, &s[i], title, STATUS_PRINT_DATA);

            sprintf(title, "%s(c):  ", mac_address);
            status_dprint_short(fn, f, &s[i], title, STATUS_PRINT_CLOUD);

            sprintf(title, "%s(p):  ", mac_address);
            status_dprint_short(fn, f, &s[i], title, STATUS_PRINT_PINGS);
        }
    }
}

/* pretty-print the array of status structs to stream f */
void status_print_short_array(FILE *f, status_t *s, int count)
{
    status_dprint_short_array(fprint, f, s, count);
}

/* find the index of the status struct in the array that has mac address mac */
int status_find_by_mac(status_t *s, int status_count, mac_address_t mac)
{
    int i;

    for (i = 0; i < status_count; i++) {
        if (mac_equal(s[i].name, mac)) {
            return i;
        }
    }

    return -1;
}

/* if the status array does not have an entry with mac address mac, add
 * one, with the given device type
 */
int status_add_by_mac(status_t *status, int *status_count, mac_address_t mac,
        device_type_t type)
{
    int result;

    result = status_find_by_mac(status, *status_count, mac);

    if (result != -1) { return result; }

    if (*status_count >= MAX_CLOUD) { return -1; }

    memset(&status[*status_count], 0, sizeof(status[*status_count]));
    mac_copy(status[*status_count].name, mac);
    status[*status_count].device_type = type;
    (*status_count)++;

    return *status_count - 1;
}

/* used to add wds or ad-hoc devices.  uses cloud_box_t, which would cause
 * circularities in .h files, so leave it here.
 */
void add_perm_io_stat_index(int *nbr_perm_io_stat_index,
        mac_address_t neighbor_name, device_type_t type)
{
    int i;
    bool_t found;
    status_t *p;

    ddprintf("\n\n ADD_PERM_IO_STAT_INDEX \n\n");
    found = false;
    for (i = 0; i < perm_io_stat_count; i++) {
        if (mac_equal(perm_io_stat[i].name, neighbor_name)) {
            *nbr_perm_io_stat_index = i;
            found = true;
            break;
        }
    }

    if (!found) {
        if (perm_io_stat_count >= MAX_CLOUD) {
            ddprintf("add_perm_io_stat_index; too many perm_io_stat's\n");
            /* at some point, garbage collect perm_io_stat with oldest
             * last-update time
             */
            goto done;
        }

        p = &perm_io_stat[perm_io_stat_count++];
        status_init(p);
        mac_copy(p->name, neighbor_name);
        p->device_type = type;

        *nbr_perm_io_stat_index = perm_io_stat_count - 1;
    }

    done : ;

    status_dprint_short_array(eprintf, stderr, perm_io_stat, perm_io_stat_count);
    ddprintf("perm_io_stat_count:  %d\n", perm_io_stat_count);

} /* add_perm_io_stat_index */
