/* wrt_util.c - detect other cloud boxes based on their 802.11 beacons
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: wrt_util.c,v 1.13 2012-02-22 18:55:25 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: wrt_util.c,v 1.13 2012-02-22 18:55:25 greg Exp $";
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include "wrt_util.h"
#include "mac.h"
#include "mac_list.h"

static char_str_t db[] = {
    /*  0 */ {0, "print histogram of beacon inter-arrival times"},
    /*  1 */ {0, "debug wrt_util_process_message"},
             {-1, NULL},
};

static int db_count = -1;

static mac_list_t beacons;
static mac_list_t signal_strength;
static bool_t have_my_ssid = false;

#define SSID_LEN 32
static char my_ssid[SSID_LEN];

/* 100 seconds */
// #define WRT_TIMEOUT_USEC 100000000

/* try 10 seconds */
#define WRT_TIMEOUT_USEC 10000000

static mac_address_t zero_mac_addr = {0,0,0,0,0,0};
static int my_channel = -1;

static int my_rate = -1;

/* debug stuff for 802.11 beacons.
 * compute a histogram of inter-arrival times of 802.11 beacons.
 * keep track of separate essids.
 * do a sorta logarithmic histogram;
 *     first histogram is 10 1-second buckets,
 *     next is 10 10-second buckets,
 *     next, is 10 100-second buckets,
 *     last is longer than 100 seconds.
 */
#define AP_COUNT 6
static char wrt_util_essids[AP_COUNT][SSID_LEN];
static int wrt_util_essid_count = 0;
static int wrt_util_beacons[AP_COUNT][31];
static struct timeval last_essid[AP_COUNT];
static bool_t beacons_inited = false;

/* turn a wrt_util internal debugging flag on or off */
void wrt_util_set_debug(int i, bool_t value)
{
    if (db_count == -1) {
        for (db_count = 0; db[db_count].d != -1; db_count++);
    }

    if (i < 0 || i >= db_count) {
        fprintf(stderr, "wrt_util_debug; invalid index %d\n", i);
        return;
    }

    db[i].d = value;
}

/* get the value of wrt_util internal debugging flag "i" */
bool_t wrt_util_get_debug(int i)
{
    if (db_count == -1) {
        for (db_count = 0; db[db_count].d != -1; db_count++);
    }

    if (i < 0 || i >= db_count) {
        fprintf(stderr, "wrt_util_debug; invalid index %d\n", i);
        return false;
    }

    return (db[i].d);
}

/* zero out histograms */
static void init_beacons(void)
{
    int i, j;
    for (i = 0; i < AP_COUNT; i++) {
        for (j = 0; j < 31; j++) {
            wrt_util_beacons[i][j] = 0;
        }
        last_essid[i].tv_sec = -1;
    }

    beacons_inited = true;
}

/* we got a beacon from "essid"; figure out amount of time since last time
 * we got a beacon from that essid, and bump appropriate histogram bucket.
 */
static void update_beacon_count(char *essid)
{
    int i, ind;
    bool_t found = false;
    struct timeval tv;
    long long diff;

    if (!beacons_inited) { init_beacons(); }

    for (i = 0; i < wrt_util_essid_count; i++) {
        if (strcmp(essid, wrt_util_essids[i]) == 0) {
            found = true;
            break;
        }
    }

    if (!found) {
        if (wrt_util_essid_count == AP_COUNT) {
            return;
        }
        strncpy(wrt_util_essids[wrt_util_essid_count++], essid, SSID_LEN);
        i = wrt_util_essid_count - 1;
    }

    while (!checked_gettimeofday(&tv));

    if (last_essid[i].tv_sec == -1) {
        last_essid[i] = tv;
        return;
    }

    diff = timeval_diff(&tv, &last_essid[i]);
    last_essid[i] = tv;
    if (diff < 1000000) {
        ind = (int) (diff / 100000);
        wrt_util_beacons[i][ind]++;
    } else if (diff < 10000000) {
        ind = (int) (diff / 1000000);
        wrt_util_beacons[i][ind + 10]++;
    } else if (diff < 100000000) {
        ind = (int) (diff / 10000000);
        wrt_util_beacons[i][ind + 20]++;
    } else {
        wrt_util_beacons[i][30]++;
    }
}

/* print out the essids we've gotten beacons from, and the sorta log-style
 * histograms (see above)
 */
void wrt_util_print_beacons(void)
{
    int i, j;
    fprintf(stderr, "hi from wrt_util_print_beacons..\n");
    for (i = 0; i < wrt_util_essid_count; i++) {
        fprintf(stderr, "%s:  ", wrt_util_essids[i]);
        for (j = strlen(wrt_util_essids[i]); j < 9; j++) {
            fprintf(stderr, " ");
        }
        for (j = 0; j < 10; j++) {
            fprintf(stderr, "%6d ", wrt_util_beacons[i][j]);
        }
        fprintf(stderr, "\n            ");
        for (j = 10; j < 20; j++) {
            fprintf(stderr, "%6d ", wrt_util_beacons[i][j]);
        }
        fprintf(stderr, "\n            ");
        for (j = 20; j < 31; j++) {
            fprintf(stderr, "%6d ", wrt_util_beacons[i][j]);
        }
        fprintf(stderr, "\n");
    }
}

/* initialize internal mac_list of beacons we see from other cloud
 * boxes, and mac_list of signal strengths seen from other boxes.
 * also, figure out our wireless channel.
 */ 
void wrt_util_init(char *beacon_file, char *sig_strength_file)
{
    wrt_util_set_my_channel();
    mac_list_init(&beacons, beacon_file, mac_list_beacon, WRT_TIMEOUT_USEC,
            true);
    mac_list_init(&signal_strength, sig_strength_file,
            mac_list_beacon_signal_strength, WRT_TIMEOUT_USEC, true);
}

/* return our current wireless channel */
int wrt_util_get_my_channel()
{
    if (my_channel == -1) {
        wrt_util_set_my_channel();
    }

    return my_channel;
}

/* figure out what our essid is, and save it in static variable
 * my_ssid.  (use "system" to ask a shell, to get the information.)
 */
int wrt_util_set_my_ssid(void)
{
    char buf[128];
    char fname[64];
    int result = 0;
    FILE *f = NULL;
    int i;
    char *p;

    sprintf(buf, "/usr/sbin/wl ssid > /tmp/rate.%d", getpid());
    if (0 != system(buf)) {
        fprintf(stderr, "wrt_util_set_my_ssid; system problem\n");
        result = -1;
        goto done;
    }
    sprintf(fname, "/tmp/rate.%d", getpid());
    f = fopen(fname, "r");
    if (f == NULL) {
        fprintf(stderr, "wrt_util_set_my_ssid; fopen problem\n");
        result = -1;
        goto done;
    }
    fgets(buf, 128, f);
    if ((p = strchr(buf, (int) '"')) == NULL) {
        fprintf(stderr, "wrt_util_set_my_ssid; invalid string '%s'\n", buf);
        result = -1;
        goto done;
    }
    p++;

    for (i = 0; i < 31; i++) {
        if (*p == '"') { break; }
        if (*p == '\0' || *p == '\n') {
            fprintf(stderr, "wrt_util_set_my_ssid; invalid string '%s'\n", buf);
            goto done;
        }
        my_ssid[i] = *p++;
    }
    my_ssid[i] = '\0';
    fprintf(stderr, "wrt_util_set_my_ssid; got ssid '%s'\n", my_ssid);

    have_my_ssid = true;

    done:
    if (f != NULL) {
        fclose(f);
        unlink(fname);
    }

    return result;
}

/* figure out what our rate is, and save it in static variable
 * my_rate.  (use "system" to ask a shell, to get the information.)
 */
int wrt_util_set_my_rate()
{
    char buf[128];
    char fname[64];
    int result = 0;
    FILE *f = NULL;
    int rate;

    sprintf(buf, "/usr/sbin/wl rate > /tmp/rate.%d", getpid());
    if (0 != system(buf)) {
        fprintf(stderr, "wrt_util_set_my_rate; system problem\n");
        result = -1;
        goto done;
    }
    sprintf(fname, "/tmp/rate.%d", getpid());
    f = fopen(fname, "r");
    if (f == NULL) {
        fprintf(stderr, "wrt_util_set_my_rate; fopen problem\n");
        result = -1;
        goto done;
    }
    fgets(buf, 128, f);
    if (sscanf(buf, "rate is %d", &rate) != 1) {
        fprintf(stderr, "wrt_util_set_my_rate; fscanf problem\n");
        result = -1;
        goto done;
    }

    my_rate = rate;

    done:
    if (f != NULL) {
        fclose(f);
        unlink(fname);
    }

    return result;
}

/* figure out what our wireless channel is, and save it in static variable
 * my_channel.  (use "system" to ask a shell, to get the information.)
 */
int wrt_util_set_my_channel()
{
    char buf[128];
    char fname[64];
    int result = 0;
    FILE *f = NULL;
    int channel;

    sprintf(buf, "/usr/sbin/wl channel > /tmp/channel.%d", getpid());
    if (0 != system(buf)) {
        fprintf(stderr, "wrt_util_set_my_channel; system problem\n");
        result = -1;
        goto done;
    }
    sprintf(fname, "/tmp/channel.%d", getpid());
    f = fopen(fname, "r");
    if (f == NULL) {
        fprintf(stderr, "wrt_util_set_my_channel; fopen problem\n");
        result = -1;
        goto done;
    }

    /* skip the first line */
    fgets(buf, 128, f);

    if (fscanf(f, "current mac channel %d", &channel) != 1) {
        fprintf(stderr, "wrt_util_set_my_channel; fscanf problem\n");
        result = -1;
        goto done;
    }

    my_channel = channel;

    done:
    if (f != NULL) {
        fclose(f);
        unlink(fname);
    }

    return result;
}

/* figure out if this is an 802.11 beacon message from another cloud box.
 * first byte must be 0x41, 144'th byte must be 0x80, must have ssid starting
 * at 181'th byte that matches my ssid.
 */
bool_t wrt_util_beacon_message(unsigned char *message, int msg_len)
{
    char buf[SSID_LEN];
    int ssid_len, len;

    if (msg_len < 182) { return false; }

    if (message[0] != 0x41) { return false; }

    if (message[144] != 0x80) { return false; }

    if (!have_my_ssid) { wrt_util_set_my_ssid(); }

    ssid_len = (int) ((unsigned char) message[181]);
    len = ssid_len < SSID_LEN ? ssid_len : 31;
    if (len + 182 > msg_len) {
        return false;
    }
    memcpy(buf, &message[182], len);
    buf[len] = '\0';
    return (strcmp(buf, my_ssid) == 0);
}

/* we have seen a wireless message from another cloud box, so we know
 * it is still alive.  update the time stamp on its mac address in our
 * beacons mac_list and our signal_strength mac_list.  we don't have a
 * signal strength, so pass a flag arg that causes the signal strength
 * field of the mac_list entry not to be updated.
 *
 * the only way we can get signal strength information is from 802.11
 * beacon messages, but they arrive unreliably.  so, we have each box
 * broadcast repeated a "ping" message as part of the cloud protocol,
 * to allow other boxes to realize it is still alive.  cloud protocol
 * messages are sent and received more reliably than 802.11 beacon messages.
 */
void wrt_util_update_time(mac_address_t mac_addr)
{
    mac_list_db_msg("wrt_util_update_time");

    mac_list_add(&beacons, mac_addr, zero_mac_addr, NO_SIGNAL_STRENGTH,
            NULL, true);

    mac_list_add(&signal_strength, mac_addr, zero_mac_addr, NO_SIGNAL_STRENGTH,
            NULL, true);
}

/* we have an 802.11 beacon from another box.  check the channel to make
 * sure it matches our channel; otherwise ignore it.  get the mac address
 * of the other box from the message, and store that in our "beacons"
 * mac_list.  get the signal strength from the
 * other box to us from the message, and store that in our "signal_strength"
 * mac_list.
 */
void wrt_util_process_message(unsigned char *message, int msg_len)
{
    unsigned char sig_strength = message[68];
    mac_address_ptr_t mac_addr = &message[154];
    char buf[SSID_LEN];
    int ssid_len = (int) ((unsigned char) message[181]);
    int len = ssid_len < SSID_LEN ? ssid_len : 31;
    int channel;

    if (my_channel == -1) { wrt_util_set_my_channel(); }
    if (my_rate == -1) { wrt_util_set_my_rate(); }
    if (!have_my_ssid) { wrt_util_set_my_ssid(); }

    if (db[1].d) {
        fprintf(stderr, "wrt_util_process_message; my channel %d, my rate %d, "
                "my ssid %s\n",
                my_channel, my_rate, my_ssid);
    }

    if (my_rate <= 11) {
        channel = (int) ((unsigned char) message[190 + ssid_len]);
    } else {
        channel = (int) ((unsigned char) message[194 + ssid_len]);
    }

    if (db[1].d) {
        fprintf(stderr, "wrt_util_process_message; his channel %d\n", channel);
    }

    memcpy(buf, &message[182], len);
    buf[len] = '\0';

    if (db[1].d) {
        fprintf(stderr, "wrt_util_process_message; his ssid %s\n", buf);
    }

    if (strcmp(buf, my_ssid) != 0) {
        if (db[1].d) {
            fprintf(stderr, "wrt_util_process_message; ssids don't match.\n");
        }
        return;
    }

    if (db[0].d) {
        update_beacon_count(buf);
    }

    /* don't know how to reliably pick the channel out of 802.11 beacons yet.
     * this caused problems with windows boxes as ad-hoc clients in mixed mode.
     */
    #if 0
    if (my_channel != channel) {
        if (db[1].d) {
            fprintf(stderr, "wrt_util_process_message; "
                    "channels don't match.\n");
        }
        return;
    }
    #endif

    mac_list_add(&beacons, mac_addr, zero_mac_addr, 0, NULL, true);

    if (sig_strength <= 2) { sig_strength = 255; }
    mac_list_add(&signal_strength, mac_addr, zero_mac_addr, sig_strength, NULL,
            true);
}

/* time out old mac addresses from boxes we haven't heard from in a while.
 * delete them from our beacons mac_list and our signal_strength mac_list.
 */
void wrt_util_interrupt()
{
    if (mac_list_expire_timed_macs(&beacons)) {
        fprintf(stderr, "process_beacon_timeout:  expire_timed_macs failed\n");
        goto done;
    }

    if (mac_list_write(&beacons)) {
        fprintf(stderr, "process_beacon_timeout:  mac_list_write failed\n");
    }

    if (mac_list_expire_timed_macs(&signal_strength)) {
        fprintf(stderr, "process_beacon_timeout:  expire_timed_macs failed\n");
        goto done;
    }

    if (mac_list_write(&signal_strength)) {
        fprintf(stderr, "process_beacon_timeout:  mac_list_write failed\n");
    }

    done:;
}
