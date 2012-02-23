/* scan_msg.c - send reports of local wifi activity through the cloud
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: scan_msg.c,v 1.13 2012-02-22 19:27:23 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: scan_msg.c,v 1.13 2012-02-22 19:27:23 greg Exp $";

/* Each cloud box does Wifi scans about once per minute.
 * send the scans of individual boxes around the cloud, and build
 * a web page on each box that describes the Wifi environment
 * that the whole cloud sees.
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "util.h"
#include "timer.h"
#include "nbr.h"
#include "scan_msg.h"

typedef struct {
    long sec, usec;
    scan_struct_t scan;
} timed_scan_t;

/* internal record of a Wifi network someone in the cloud has scanned.
 * entries time out if they are not refreshed.
 */
static timed_scan_t scans[MAX_SCAN];
static int scan_count = 0;

/* debug-print the contents of the "scans" array */
void scan_msg_db_print(void)
{
    int i;

    ddprintf("%d scans:\n", scan_count);
    ddprintf("SSID | channel | signal quality | RSSI | noise\n");
    for (i = 0; i < scan_count; i++) {
        scan_struct_t *scan = &scans[i].scan;
        ddprintf("%s | %s | %d | %d | %d | %d\n",
                scan->ssid,
                scan->mode == MODE_AD_HOC ? "Ad hoc"
                    : scan->mode == MODE_MANAGED ? "Managed"
                    : "unknown",
                scan->channel,
                scan->rssi - scan->noise,
                scan->rssi,
                scan->noise);
    }
}

static void output_html_scan(void);

/* for sorting the scans array; sort by signal strength (rssi - noise) */
static int scan_cmp(const void *vs1, const void *vs2)
{
    timed_scan_t *s1 = (timed_scan_t *) vs1;
    timed_scan_t *s2 = (timed_scan_t *) vs2;

    return (s1->scan.rssi - s1->scan.noise) - (s2->scan.rssi - s2->scan.noise);
}

/* see if two scan structs have the same ssid, mode, and channel */
static bool_t scan_eq_keys(scan_struct_t *s1, scan_struct_t *s2)
{
    return (strcmp(s1->ssid, s2->ssid) == 0
            && s1->mode == s2->mode
            && s1->channel == s2->channel);
}

/* delete from the "scans" array any scan entries that are old and stale
 * and have not been refreshed recently.
 */
static void timeout_old_scans(void)
{
    int past_packed = 0;
    int next;
    struct timeval now;

    while (!checked_gettimeofday(&now));

    /* invariant:  [0 .. past_packed) are keepable, packed, and done.
     * [past_packed .. next) can be overwritten.
     * [next .. list_count) are yet to be processed and as originally
     */
    for (next = 0; next < scan_count; next++) {
        timed_scan_t *l = &scans[next];

        if ((now.tv_sec - l->sec) < MAX_SCAN_TIME) {
            /* keep it. */
            if (past_packed != next) {
                scans[past_packed] = *l;
            }
            past_packed++;
        }
    }

    scan_count = past_packed;
}

/* add scan to the "scans" array.  update the time stamp of the new
 * (or already existing) entry to "now".  (if the entry already existed,
 * at least update signal quality information.)
 */
static void add_scan(scan_struct_t *scan)
{
    int i;
    bool_t found = false;
    timed_scan_t *tscan;
    struct timeval now;

    while (!checked_gettimeofday(&now));

    for (i = 0; i < scan_count; i++) {
        tscan = &scans[i];
        if (scan_eq_keys(scan, &tscan->scan)) {
            found = true;
            tscan->scan.noise = scan->noise;
            tscan->scan.rssi = scan->rssi;
        }
    }
    if (!found) {
        if (scan_count < MAX_SCAN) {
            tscan = &scans[scan_count++];
            copy_string(tscan->scan.ssid, scan->ssid, MAX_SSID);
            tscan->scan.noise = scan->noise;
            tscan->scan.rssi = scan->rssi;
            tscan->scan.mode = scan->mode;
            tscan->scan.channel = scan->channel;
            found = true;
        }
    }

    if (found) {
        tscan->sec = now.tv_sec;
        tscan->usec = now.tv_usec;
    }
}

/* read from the "in" character string up to end of string or the next
 * field delimiter "|" character.  update the "in" pointer to point to
 * the start of the next field if any.  return true iff we were able to
 * find a next field.
 */
static bool_t get_field(char **in, char *out, int out_len)
{
    char *end;
    end = *in;
    if (*end == '\0') { return false; }
    while (*end != '\0' && *end != '\n' && *end != '|') { end++; }
    if (out_len < end - *in + 1) { return false; }

    strncpy(out, *in, end - *in);
    *(out + (end - *in)) = '\0';

    *in = end;
    if (end != '\0') { (*in)++; }
    return true;
}

/* read one line of the scan file and return it in "scan".  return true iff
 * we were able to find a new scan value.
 */
static bool_t read_scan(FILE *f, scan_struct_t *scan)
{
    char buf[256], out_buf[256];
    char *p;
    int idata;

    if ((p = fgets(buf, 256, f)) == NULL) { return false; }

    if (!get_field(&p, out_buf, 256)) { return false; }
    copy_string(scan->ssid, out_buf, MAX_SSID);
    if (!get_field(&p, out_buf, 256)) { return false; }
    if (strcmp(out_buf, "Ad Hoc") == 0) {
        scan->mode = MODE_AD_HOC;
    } else if (strcmp(out_buf, "Managed") == 0) {
        scan->mode = MODE_MANAGED;
    } else {
        scan->mode = MODE_UNKNOWN;
    }
    if (!get_field(&p, out_buf, 256)) { return false; }

    if (!get_field(&p, out_buf, 256)) { return false; }
    if (sscanf(out_buf, "%hd", &scan->rssi) != 1) { return false; }

    if (!get_field(&p, out_buf, 256)) { return false; }
    if (sscanf(out_buf, "%hd", &scan->noise) != 1) { return false; }

    if (!get_field(&p, out_buf, 256)) { return false; }
    if (sscanf(out_buf, "%d", &idata) != 1) { return false; }
    else { scan->channel = (byte) idata; }

    return true;
}

/* read the /tmp/local_scan file, which contains wifi networks this cloud box
 * sees.
 */
static void add_local_scans(void)
{
    scan_struct_t scan;
    FILE *f = fopen("/tmp/local_scan", "r");
    if (f == NULL) { return; }

    while (read_scan(f, &scan)) {
        add_scan(&scan);
    }
    fclose(f);

    qsort(scans, scan_count, sizeof(scans[0]), scan_cmp);
}

/* package up our locally detected wifi networks, and send them out to the
 * rest of the cloud.
 */
static void send_scan_message(void)
{
    message_t msg;
    scan_struct_t scan;
    FILE *f = fopen("/tmp/local_scan", "r");
    if (f == NULL) { return; }

    memset(&msg, 0, sizeof(msg));
    msg.message_type = scanresults_msg;
    msg.v.scan.count = 0;
    while (read_scan(f, &scan)) {
        if (scan_count >= MAX_SCAN) { break; }
        msg.v.scan.list[msg.v.scan.count++] = scan;
    }
    fclose(f);

    mac_copy(msg.dest, mac_address_bcast);
    send_cloud_message(&msg);
}

/* handle scan timer event, which happens about once per minute.
 * trim stale wifi scans, send out the wifi environment we are seeing
 * to the rest of the cloud, build a web page for the current wifi
 * environment seen by the whole cloud.
 */
void scan_timer(void)
{
    timeout_old_scans();

    add_local_scans();

    output_html_scan();

    send_scan_message();

    set_next_scan_alarm();
}

/* another cloud box has sent out a message with the wifi environment it
 * is seeing.  update our internal list of scanned wifi networks to reflect
 * that.
 */
void process_scanresults_msg(message_t *msg, int device_index)
{
    int i;
    mac_address_ptr_t neighbor;

    if (db[63].d) {
        ddprintf("process_scanresults_msg start..\n");
        scan_msg_db_print();
    }

    for (i = 0; i < msg->v.scan.count; i++) {
        scan_struct_t *scan = &msg->v.scan.list[i];

        add_scan(scan);
    }

    if (db[63].d) {
        ddprintf("process_scanresults_msg after adding scan from msg..\n");
        scan_msg_db_print();
    }

    neighbor = get_name(device_index, msg->eth_header.h_source);
    if (neighbor == NULL) {
        ddprintf("process_stp_beacon_msg:  get_name returned null\n");
        goto done;

    }
    /* pass this beacon along to other stp neighbors besides the one we
     * just got it from.
     */
    for (i = 0; i < stp_list_count; i++) {
        if (mac_equal(stp_list[i].box.name, neighbor)) { continue; }
        mac_copy(msg->dest, stp_list[i].box.name);
        send_cloud_message(msg);
    }

    done:

    add_local_scans();

    if (db[63].d) {
        ddprintf("process_scanresults_msg after adding local scans..\n");
        scan_msg_db_print();
    }

    qsort(scans, scan_count, sizeof(scans[0]), scan_cmp);

    output_html_scan();
}

/* create a web page showing the current wifi environment seen by the
 * entire cloud.
 */
static void output_html_scan(void)
{
    int i;
    FILE *f = fopen("/tmp/wifi_scan.tmp", "w");
    if (f == NULL) {
        ddprintf("output_html_scan; could not open /tmp/wifi_scan.tmp:  %s\n",
                strerror(errno));
        goto done;
    }

    fprintf(f, "<html>\n");
    fprintf(f, "<head>\n");
    fprintf(f, "<META HTTP-EQUIV=\"refresh\" CONTENT=\"60\">\n");
    fprintf(f, "</head>\n");
    fprintf(f, "<body>\n");
    fprintf(f, "<p>\n");

    fprintf(f, "<h3>\n");
    fprintf(f, "Wifi networks in the area (choose settings for your cloud "
            "to avoid conflicts with other Wifi networks)\n");
    fprintf(f, "</h3>\n");

    fprintf(f, "<p>\n");

    fprintf(f, "<table frame=box rules=all>\n");
    fprintf(f, "<tr>\n");
    fprintf(f, "    <td>&nbsp;SSID&nbsp;</td>\n");
    fprintf(f, "    <td>&nbsp;Mode&nbsp;</td>\n");
    fprintf(f, "    <td>&nbsp;channel&nbsp;</td>\n");
    fprintf(f, "    <td>&nbsp;Signal quality&nbsp;</td>\n");
    fprintf(f, "    <td>&nbsp;Signal strength&nbsp;</td>\n");
    fprintf(f, "    <td>&nbsp;noise&nbsp;</td>\n");
    fprintf(f, "</tr>\n");

    for (i = 0; i < scan_count; i++) {
        scan_struct_t *scan = &scans[i].scan;
        fprintf(f, "<tr>\n");
        fprintf(f, "    <td>&nbsp;%s&nbsp;</td>\n", scan->ssid);
        fprintf(f, "    <td>&nbsp;%s&nbsp;</td>\n",
                scan->mode == MODE_AD_HOC ? "Ad hoc"
                    : scan->mode == MODE_MANAGED ? "Managed"
                    : "unknown");
        fprintf(f, "    <td>&nbsp;%d&nbsp;</td>\n", (int) scan->channel);
        fprintf(f, "    <td>&nbsp;%d&nbsp;</td>\n",
                (int) (scan->rssi - scan->noise));
        fprintf(f, "    <td>&nbsp;%d dBm&nbsp;</td>\n", (int) scan->rssi);
        fprintf(f, "    <td>&nbsp;%d dBm&nbsp;</td>\n", (int) scan->noise);
        fprintf(f, "</tr>\n");
    }

    fprintf(f, "</table>\n");

    fprintf(f, "</body>\n");
    fprintf(f, "</html>\n");

    if (fclose(f) != 0) {
        ddprintf("output_html_scan; could not fclose /tmp/wifi_scan.tmp:  %s\n",
                strerror(errno));
        goto done;
    }

    if (rename("/tmp/wifi_scan.tmp", "/tmp/wifi_scan.asp") != 0) {
        ddprintf("do_print_scan; unable to rename %s to %s:  %s\n",
                "/tmp/wifi_scan.tmp", "/tmp/wifi_scan.asp", strerror(errno));
        goto done;
    }

    done:;
}
