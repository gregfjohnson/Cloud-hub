/* update_wrt_wds.c - update wds connections among the cloud boxes
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: update_wrt_wds.c,v 1.7 2012-02-22 18:55:25 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: update_wrt_wds.c,v 1.7 2012-02-22 18:55:25 greg Exp $";

/* update wds connections and output the results so that other programs
 * can use them.
 *
 * inputs:
 *
 * devices we are seeing beacons from and should set wds connections to:
 * (the creator of this file should filter on essid etc.)
 *     /tmp/sig_strength:  nn:nn:nn:nn:nn:nn N
 *
 * the current wds interfaces (wds0.N):
 *     ifconfig | grep '^wds'
 *     wds0.2    ((<- spaces) blah blah) 
 *
 * the current wds mac addresses:
 *    wl wds
 *    wds nn:nn:nn:nn:nn:nn
 *
 * our output:
 *    /tmp/wds
 *    wds0.N nn:nn:nn:nn:nn:nn
 *
 * and, we have to do "system" calls to make the above true.
 *
 * this is basically a deprecated program.  after lots of struggle with WDS
 * we switched over to ad-hoc and communication reliability went way up.
 *
 * according to Matthew Gast's great O'Reilly 802.11 Wireless Networks book,
 * in WDS mode the contention-free period is not used, which sounds to me
 * like the whole collision-avoidance logic of 802.11 is not really used.
 * this may be why we had so much trouble with WDS mode.  (we were using the
 * boxes as both access points and WDS nodes, and so there were mobile
 * stations and WDS modes in the mix at the same time.)
 */

#include <sys/socket.h>
#include <sys/time.h>
#include <features.h>    /* for the glibc version number */
#include <asm/types.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>   /* The L2 protocols */
#include <sys/ioctl.h>
#include <net/if.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "mac.h"
#include "mac_list.h"
#include "util.h"

#define MAX_WDS 10

#define DEBUG0 0

#define MIN(m, n) ((m) < (n) ? (m) : (n))

static char_str_t db[] = {
    {0, "update_wds"},
    {0, "print get_macs"},
    {0, "one-shot debug"},
    {-1, NULL},
};

/* <seconds, useconds> between beacon broadcasts. (and, when to start.) */
static struct itimerval timer = { {0, 1000000}, {0, 1} };

static char *sig_strength_file = "/tmp/sig_strength";

/* this a file we output, to reflect the wds state of the box as we have
 * created it.  each line is "wds0.N nn:nn:nn:nn:nn:nn"
 */
static char *wds_file = "/tmp/wds";
static char *wds_hist_file = "/tmp/wds_hist";

#define MAX_CLOUD 32

static mac_list_t sig_strength_mac_list;
static mac_list_t wds_hist_mac_list;
static mac_list_t wds_mac_list;

typedef mac_address_t mac_array_t[MAX_CLOUD];

#define IF_LIST_NAME_LEN 10
typedef char interface_list_t[MAX_CLOUD][IF_LIST_NAME_LEN];

static char use_pipes = 0;

static mac_address_t zero_mac_addr = {0,0,0,0,0,0,};

/* read the file fname for a list of mac addresses.
 * don't exceed list_len.  should not be a problem, since for now
 * the only use is after 'wl wds > /tmp/wds.$$', which has short length.
 *
 * return 0 if everything went ok, -1 if not.
 */
static int get_macs(char *fname, mac_array_t dev_list, int list_len,
        int *list_count, int offset)
{
    FILE *f;
    char buf[128];
    int result = 0;

    *list_count = 0;

    f = fopen(fname, "r");

    if (f == NULL) {
        fprintf(stderr, "get_macs:  could not open %s\n", fname);
        result = -1;
        goto done;
    }

    while (1) {
        char *p = fgets(buf, 128, f);
        if (p == NULL) { break; }
        if (*list_count >= list_len) {
            fprintf(stderr, "get_macs:  too many devices\n");
            goto done;
        }
        p += offset;
        if (1 != mac_sscanf(dev_list[*list_count], p)) {
            result = -1;
            goto done;
        }
        (*list_count)++;
    }

    done:

    if (f != NULL) { fclose(f); }

    return result;
}

/* run the command 'wl wds' to find out the mac addresses of our current
 * wds partners.
 *
 * return 0 if everything went ok, -1 if not.
 */
static int get_wds_macs(mac_array_t dev_list, int list_len, int *list_count)
{
    char buf[128];
    char fname[64];
    int result = 0;
    int i;

    retry:

    sprintf(buf, "/usr/sbin/wl wds > /tmp/wds_macs.%d", getpid());
    if (0 != system(buf)) {
        result = -1;
        goto done;
    }

    sprintf(fname, "/tmp/wds_macs.%d", getpid());

    result = get_macs(fname, dev_list, list_len, list_count, 4);

    for (i = 0; i < *list_count; i++) {
        if (0 == mac_cmp(dev_list[i], zero_mac_addr)) {
            fprintf(stderr, "got a mac address of 00:00:00:00:00:00.  "
                    "retrying.\n");
            sleep(1);
            goto retry;
        }
    }

    done:
    unlink(fname);

    return result;
}

/* run the command 'ifconfig | grep '^wds' to get current wds0.N
 * devices.
 *
 * return 0 if everything went ok
 * return -1 if expect was not matched.
 * return -2 if some other error occurred.
 */
static int get_wds_interfaces(interface_list_t dev_list, int list_len,
        int *list_count, int expect)
{
    FILE *f = NULL;
    int result = 0;
    char buf[128];
    char fname[64];
    int tries;
    int line_count;
    int ok;

    /* sometimes this command returns weird wrong answers.  maybe because
     * the kernel takes a little while to set up the wds0.N devices after
     * 'wl wds nn:nn:nn:nn:nn:nn' or something.  So, we try 10 times to
     * see if we can get the number we expect to get.
     */
    for (tries = 0; tries < 10; tries++) {

        ok = 1;

        *list_count = 0;

        sprintf(buf, "/sbin/ifconfig | grep wds > /tmp/wds_macs.%d", getpid());
        system(buf);

        sprintf(fname, "/tmp/wds_macs.%d", getpid());

        f = fopen(fname, "r");
        if (f == NULL) {
            fprintf(stderr, "get_wds_macs:  could not open %s\n", fname);
            result = -2;
            goto done;
        }

        line_count = 0;
        while (1) {
            char *p = fgets(buf, 128, f);
            if (p == NULL) { break; }
            line_count++;
        }
        fclose(f);
        f = NULL;

        if (expect != -1 && line_count != expect) {
            fprintf(stderr, "get_wds_interfaces:  REDO.  "
                    "(expected %d, got %d)\n", expect, line_count);
            sleep(1);
            ok = 0;
        }

        f = fopen(fname, "r");
        if (f == NULL) {
            fprintf(stderr, "get_wds_macs:  could not open %s\n", fname);
            result = -2;
            goto done;
        }

        while (1) {
            char *p = fgets(buf, 128, f);
            if (p == NULL) { break; }

            if (*list_count >= list_len) {
                fprintf(stderr, "get_wds_interfaces:  too many devices\n");
                goto done;
            }

            sscanf(buf, "%s", dev_list[*list_count]);
            (*list_count)++;
        }

        fclose(f);
        f = NULL;

        if (ok) { break; }
    }

    if (!ok) {
        fprintf(stderr, "not ok.\n");
        result = -1;
    }

    done:

    if (f != NULL) { fclose(f); }

    unlink(fname);

    return result;
}

/* print <if_list, if_list_len> to stderr.
 */
static void print_if_list(interface_list_t if_list, int if_list_len)
{
    int i;
    fprintf(stderr, "if_list_len:  %d\n", if_list_len);
    for (i = 0; i < if_list_len; i++) {
        fprintf(stderr, "%s\n", if_list[i]);
    }
}

/* print <mac_list, mac_list_len> to stderr.
 */
static void print_mac_list(mac_array_t mac_list, int mac_list_len)
{
    int i;
    fprintf(stderr, "mac_list_len:  %d\n", mac_list_len);
    for (i = 0; i < mac_list_len; i++) {
        mac_print(stderr, mac_list[i]);
    }
}

/* beacons contains an array of signal_strengths and an array of
 * mac_addresses.
 */
static void sig_strength_sort(mac_list_t *beacons)
{
    int i, j;

    if (beacons->type != mac_list_beacon_signal_strength) {
        fprintf(stderr, "sig_strength_sort requires a signal_strength "
                "mac_list_t.\n");
        exit(1);
    }

    /* invariant:  [0 .. i) are the largest values, sorted decreasing. */
    for (i = 0; i < beacons->next_beacon - 1; i++) {
        int s;

        s = i;
        /* invariant:  s is index of largest value in [i .. j) */
        for (j = i + 1; j < beacons->next_beacon; j++) {
            if (beacons->signal_strength[s] < beacons->signal_strength[j]) {
                s = j;
            }
        }
        if (s != i) {
            timed_mac_t tm = beacons->beacons[s];
            int ss = beacons->signal_strength[s];
            
            beacons->beacons[s] = beacons->beacons[i];
            beacons->signal_strength[s] = beacons->signal_strength[i];
            
            beacons->signal_strength[i] = ss;
            beacons->beacons[i] = tm;
        }
    }
}

/* based on the sig_strength file, which contains mac addresses of
 * wireless interfaces of cloud boxes we are seeing 802.11 beacons
 * from, use "system()" to configure our box to have a wds association
 * with a box we should be set up to communicate with but aren't yet.
 *
 * update and write to disk "wds_mac_list", the list of cloud boxes
 * that we have wds set up for.
 */
static void update_wds(int arg)
{
    int b;

    mac_array_t start_mac_list;
    int start_mac_list_len;

    interface_list_t start_if_list;
    int start_if_list_len;

    mac_array_t new_mac_list;
    int new_mac_list_len;

    interface_list_t new_if_list;
    int new_if_list_len;
    int new_if, new_mac;

    if (db[0].d) { fprintf(stderr, "hi from update_wds..\n"); }

    /* beacons is a mac_list_t init'd to read /tmp/sig_strength.
     * might need to do a home-brew version of this read that just
     * reads the K strongest signal-strength entries, since the array
     * we are reading into is statically allocated.
     */
    if (0 != mac_list_read(&sig_strength_mac_list)) {
        fprintf(stderr, "update_wds:  mac_list_read failed.\n");
        goto done;
    }

    if (db[0].d) {
        fprintf(stderr, "sig_strength before sort:\n");
        mac_list_print(stderr, &sig_strength_mac_list);
    }

    sig_strength_sort(&sig_strength_mac_list);

    if (db[0].d) {
        fprintf(stderr, "sig_strength after sort:\n");
        mac_list_print(stderr, &sig_strength_mac_list);
    }

    /* this is supposed to reflect the current state of the box, as we
     * set it up.
     */
    if (0 != mac_list_read(&wds_hist_mac_list)) {
        fprintf(stderr, "update_wds:  mac_list_read(wds_hist_mac_list) "
                "failed.\n");
        goto done;
    }

    wds_mac_list.next_beacon = 0;

    /* if we are seeing more beacons than we can handle as wds partners,
     * see if our current wds partners are the strongest signals.  if
     * not, delete all wds and build wds back up from scratch.
     *
     * this might cause jitter if we are right on the border.
     * fix this after other more basic stuff works.
     */
    if (sig_strength_mac_list.next_beacon > MAX_WDS) {
        fprintf(stderr, "too many beacons\n");
        goto done;
    }

    if (db[0].d) { fprintf(stderr, "hi from update_wds before loop..\n"); }

    /* see if strongest beacons are in wds.  if not, put them in. */

    for (b = 0; b < MIN(sig_strength_mac_list.next_beacon, MAX_WDS); b++) {
        char cmd[256];
        char smac[64];
        int result;
        int i;

        if (db[0].d) { fprintf(stderr, "sig_strength[%d]..\n", b); }

        for (i = 0; i < wds_hist_mac_list.next_beacon; i++) {
            if (mac_cmp(wds_hist_mac_list.beacons[i].mac_addr, 
                    sig_strength_mac_list.beacons[b].mac_addr) == 0)
            {
                mac_list_add(&wds_mac_list,
                        wds_hist_mac_list.beacons[i].mac_addr,
                        NULL, 0, wds_hist_mac_list.desc[i], false);


                if (db[0].d) { fprintf(stderr, "added wds_hist..\n"); }

                goto next_beacon;
            }
        }

        if (db[0].d) { fprintf(stderr, "didn't add wds_hist!\n"); }

        /* get the "ifconfig" interface wds0.N list and the "wl wds"
         * wds mac address list before adding the new one, so we can
         * the do the same thing after, and compare to find the new ones.
         */ 
        if (0 != get_wds_interfaces(start_if_list, MAX_CLOUD,
                &start_if_list_len, -1))
        {
            fprintf(stderr, "update_wds:  get_wds_interfaces failed.\n");
            goto done;
        }

        if (0 != get_wds_macs(start_mac_list, MAX_CLOUD,
                &start_mac_list_len))
        {
            fprintf(stderr, "update_wds:  get_wds_macs failed.\n");
            goto done;
        }

        if (db[0].d) {
            fprintf(stderr, "before:\n");
            print_if_list(start_if_list, start_if_list_len);
            print_mac_list(start_mac_list, start_mac_list_len);
        }

        /* create and run the "wl wds nn:nn:nn:nn:nn:nn" command */

        mac_sprintf(smac, sig_strength_mac_list.beacons[b].mac_addr);

        sprintf(cmd, "/usr/sbin/wl wds %s", smac);

        fprintf(stderr, "running command '%s'\n", cmd);

        result = system(cmd);

        if (result == -1) {
            fprintf(stderr, "command '%s' failed\n", cmd);
            goto done;
        }
        sleep(1);

        if (0 != get_wds_macs(new_mac_list, MAX_CLOUD, &new_mac_list_len)) {
            fprintf(stderr, "update_wds:  get_wds_macs failed.\n");
            goto done;
        }

        if (new_mac_list_len != start_mac_list_len + 1) {
            fprintf(stderr, "update_wds:  new_mac_list_len %d != "
                    "start_mac_list_len %d + 1.\n",
                    new_mac_list_len, start_mac_list_len);

            /* the idea of deleting wds interfaces out from underneath
             * merge_cloud is a pretty scary idea.  if this is done,
             * probably should signal merge_cloud somehow so that it can
             * close and re-open all of its wds interfaces.
             *
             * or, maybe just call /tmp/do_cfg_cloud.  (if that is done,
             * do_cfg_cloud should do killall's to get a clean slate.)
             */
            //delete_wds();
            //goto retry;
            goto done;
        }

        for (i = 0; i < new_mac_list_len; i++) {
            if (0 == mac_cmp(new_mac_list[i], zero_mac_addr)) {
                fprintf(stderr, "got a mac address of 00:00:00:00:00:00.  "
                        "ignoring.\n");
                goto done;
            }
        }

        if (0 != (result = get_wds_interfaces(new_if_list, MAX_CLOUD,
                &new_if_list_len, start_if_list_len + 1)))
        {

            fprintf(stderr, "update_wds:  get_wds_interfaces failed %d.\n",
                    result);

            if (result == -1) {
                fprintf(stderr, "update_wds:  new_if_list_len %d != "
                        "start_if_list_len %d + 1.  use orphan?\n",
                        new_if_list_len, start_if_list_len);
            }

            goto done;
        }

        if (db[0].d) {
            fprintf(stderr, "after:\n");
            print_if_list(new_if_list, new_if_list_len);
            print_mac_list(new_mac_list, new_mac_list_len);
        }

        /* look through the new interface wds0.N list, and find the one
         * that wasn't there before we ran the "wl wds nn:nn:nn:nn:nn:nn"
         * command
         */
        for (new_if = 0; new_if < new_if_list_len; new_if++) {
            char found = 0;
            for (i = 0; i < start_if_list_len; i++) {
                if (0 == strcmp(start_if_list[i], new_if_list[new_if])) {
                    found = 1;
                    break;
                }
            }

            if (!found) { break; }
        }


        /* same for the interface from "ifconfig | grep wds" */
        for (new_mac = 0; new_mac < new_mac_list_len; new_mac++) {
            char found = 0;
            for (i = 0; i < start_mac_list_len; i++) {
                if (0 == mac_cmp(start_mac_list[i], new_mac_list[new_mac])) {
                    found = 1;
                    break;
                }
            }

            if (!found) { break; }
        }

        if (new_mac == new_mac_list_len) {
            fprintf(stderr, "did not find a new mac address.\n");
            goto done;
        }

        if (new_if == new_if_list_len) {
            fprintf(stderr, "did not find a new interface.\n");
            goto done;
        }

        mac_list_add(&wds_hist_mac_list, new_mac_list[new_mac],
                zero_mac_addr, -1, new_if_list[new_if], true);

        mac_list_add(&wds_mac_list, new_mac_list[new_mac],
                zero_mac_addr, -1, new_if_list[new_if], false);

        next_beacon: ;
    }

    // write_wds(wds_if_list, wds_mac_list, wds_list_len);
    mac_list_write(&wds_mac_list);

    if (db[0].d) {
        fprintf(stderr, "wds at end of loop:\n");
        mac_list_print(stderr, &wds_mac_list);
    }

    done :;
}

static void usage()
{
    fprintf(stderr, "usage:\nupdate_wrt_wds \\\n"
            "    [-p] \\\n"
            "    [-D debug_index] \\\n");
    exit(1);
}

static void process_args(int argc, char **argv)
{
    int c;

    while ((c = getopt(argc, argv, "pd:")) != -1) {
        switch (c) {

        case 'd' :
            tweak_db(optarg, db);
            break;

        case 'p' :
            use_pipes = 1;
            break;

        default :
            usage();
        }
    }
}

int main(int argc, char **argv)
{
    int result;
    FILE *f;

    process_args(argc, argv);
    fprintf(stderr, "hi.\n");

    /* this is the /tmp/sig_strength file created by merge_cloud.c */
    mac_list_init_read(&sig_strength_mac_list, sig_strength_file,
            mac_list_beacon_signal_strength, -1, true);

    /* this is the /tmp/wds_hist file we manage to remember the wds state of the
     * box, which we create.  (any beacon we have ever seen gets
     * 'wl wds nn:nn:nn:nn:nn:nn; and goes here.)
     * if the file isn't there, create it.
     */
    f = fopen(wds_hist_file, "r");
    if (f == NULL) {
        mac_list_init(&wds_hist_mac_list, wds_hist_file,
                mac_list_beacon_desc, -1, false);
    } else {
        fclose(f);
        mac_list_init_read(&wds_hist_mac_list, wds_hist_file,
                mac_list_beacon_desc, -1, false);
        // mac_list_read(&wds_hist_mac_list);
    }

    /* this is the /tmp/wds file we manage to record boxes we are currently
     * getting beacons from.
     * if the file isn't there, create it.
     */
    f = fopen(wds_file, "r");
    if (f == NULL) {
        mac_list_init(&wds_mac_list, wds_file,
                mac_list_beacon_desc, -1, true);
    } else {
        fclose(f);
        mac_list_init_read(&wds_mac_list, wds_file,
                mac_list_beacon_desc, -1, true);
        // mac_list_read(&wds_mac_list);
    }
    fprintf(stderr, "hi.\n");

    if (db[2].d) {
        update_wds(47);
        exit(0);
    }

    signal(SIGALRM, &update_wds);
    result = setitimer(ITIMER_REAL, &timer, 0);
    printf("setitimer result %d\n", result);

    while (1) {
        sleep(60);
    }
}
