/* mac_list.c - maintain a list of mac addresses and related data in a file
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: mac_list.c,v 1.7 2012-02-22 18:55:25 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: mac_list.c,v 1.7 2012-02-22 18:55:25 greg Exp $";

/* maintain a list of mac addresses in a file.
 * the mac addresses have expiration times (granularity usec), and if they
 * are not refreshed soon enough they will be timed out and expunged
 * from the file.
 *
 * this is an API that is usable generically and is not specific to the
 * cloud_hub project.
 *
 * the mac addresses can have extra information associated with them;
 * when a mac_list is initialized its specified type indicates what
 * extra information should be kept with each mac address:
 *
 * mac_list_beacon:
 *    no extra information; just a file of mac addresses
 *
 * mac_list_beacon_name:
 *    a second mac address
 *
 * mac_list_beacon_desc:
 *    a string with no blanks, giving a name or description of the mac address
 *
 * mac_list_beacon_signal_strength:
 *    an integer value (i.e., a signal strength)
 */
#include <sys/time.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>

#include "mac_list.h"
#include "util.h"

// #define DEBUG

#ifdef DEBUG
    static FILE *debug_file;
    static bool_t debug_file_inited = false;
#endif

/* for mac_lists with write_delay true, write to disk at most once 
 * per second.
 */
#define WRITE_DELAY 1000000

/* find and delete any mac addresses that have not been refreshed since
 * the expiration time for the mac_list.
 * (time granularity is usecs.)
 */
int mac_list_expire_timed_macs(mac_list_t *mac_list)
{
    struct timeval tv;
    struct timezone tz;

    if (mac_list->timeout == -1) { return 0; }

    if (gettimeofday(&tv, &tz)) {
        fprintf(stderr, "expire_timed_macs:  gettimeofday failed\n");
        return -1;
    }

    while (1) {
        int i;
        int did_something = 0;
        for (i = 0; i < mac_list->next_beacon; i++) {
            if (usec_diff(tv.tv_sec, tv.tv_usec,
                mac_list->beacons[i].tv_sec, mac_list->beacons[i].tv_usec)
                        > mac_list->timeout)
            {
                int j;
                #ifdef DEBUG
                if (debug_file_inited) {
                    fprintf(debug_file, "%s; timeout ", mac_list->fname);
                    mac_print_no_eoln(debug_file,
                            mac_list->beacons[i].mac_addr);
                    fprintf(debug_file, " at ");
                    util_print_time(debug_file, &tv);
                    fprintf(debug_file, "\n");
                    fflush(debug_file);
                }
                #endif
                for (j = i; j < mac_list->next_beacon - 1; j++) {
                    mac_list->beacons[j] = mac_list->beacons[j + 1];
                }
                mac_list->next_beacon--;
                did_something = 1;
                break;
            }
        }
        if (!did_something) break;
    }

    return 0;
}

/* read the mac_list in from its associated file.
 * return 0 on success, -1 on failure.
 * read up to a maximum of MAX_CLOUD mac addresses,
 * and read the extra info associated with each mac address, based on
 * the type of the mac_list.
 * initialize the mac address times to "now".
 */
int mac_list_read(mac_list_t *mac_list)
{
    int result = 0;
    struct timeval tv;
    struct timezone tz;
    FILE *f;

    if (gettimeofday(&tv, &tz)) {
        fprintf(stderr, "mac_list_read; gettimeofday failed\n");
        return -1;
    }

    f = fopen(mac_list->fname, "r");

    if (f == NULL) {
        perror("mac_list_read fopen failed");
        fprintf(stderr, "    could not open %s\n", mac_list->fname);
        result = -1;
        goto done;
    }

    mac_list->next_beacon = 0;

    while (1) {
        mac_address_t mac_addr;

        if (1 != mac_read(f, mac_addr)) {
            break;
        }

        if (mac_list->next_beacon >= MAX_CLOUD) {
            fprintf(stderr, "mac_list_read:  too many entries in file\n");
            result = -1;
            goto done;
        }

        mac_copy(mac_list->beacons[mac_list->next_beacon].mac_addr, mac_addr);
        mac_list->beacons[mac_list->next_beacon].tv_sec = tv.tv_sec;
        mac_list->beacons[mac_list->next_beacon].tv_usec = tv.tv_usec;

        /* check scanf results etc. to bullet-proof */
        switch (mac_list->type) {

        case mac_list_beacon_desc :
            fscanf(f, "%s", mac_list->desc[mac_list->next_beacon]);
            break;

        case mac_list_beacon_name :
            mac_read(f, mac_list->names[mac_list->next_beacon]);
            break;

        case mac_list_beacon_signal_strength :
            fscanf(f, " %d\n",
                    &mac_list->signal_strength[mac_list->next_beacon]);
            break;

        case mac_list_beacon :
            break;

        default :
            fprintf(stderr, "mac_list_read:  invalid type %d\n",
                    mac_list->type);
            break;
        }

        mac_list->next_beacon++;
    }

    done:

    if (f != NULL) { fclose(f); }

    return result;
}

/* write the mac address list to stream f, including optional extra
 * info with each mac address depending on mac_list's type.
 */
int mac_list_print(FILE *f, mac_list_t *mac_list)
{
    int i;

    for (i = 0; i < mac_list->next_beacon; i++) {

        mac_print_no_eoln(f, mac_list->beacons[i].mac_addr);

        switch (mac_list->type) {

        case mac_list_beacon_desc :
            fprintf(f, "    %s", mac_list->desc[i]);
            if (strchr(mac_list->desc[i], '\n') == NULL) {
                fprintf(f, "\n");
            }

            break;

        case mac_list_beacon_name :
            fprintf(f, "    ");
            mac_print(f, mac_list->names[i]);

            break;

        case mac_list_beacon_signal_strength :
            fprintf(f, " %d\n", mac_list->signal_strength[i]);
            break;

        case mac_list_beacon :
            fprintf(f, "\n");
            break;

        default :
            fprintf(stderr, "mac_list_print:  invalid type %d\n",
                    mac_list->type);
            break;
        }
    }

    return 0;
}

/* write the mac address list out to its specified file on disk,
 * including optional extra info with each mac address depending on 
 * mac_list's type.
 *
 * if the mac_list has write_delay == true , this routine is a
 * no-op if it's too soon since the last time we did the write.
 * see WRITE_DELAY above; max write frequency is currently once per second.
 */
int mac_list_write(mac_list_t *mac_list)
{
    struct timeval tv;
    struct timezone tz;
    char tmp_fname[PATH_MAX];
    FILE *f;
    int i;

    if (gettimeofday(&tv, &tz)) {
        fprintf(stderr, "mac_list_write:  gettimeofday failed\n");
        return -1;
    }

    if (mac_list->write_delay
        && usec_diff(tv.tv_sec, tv.tv_usec,
            mac_list->last_time_sec, mac_list->last_time_usec) < WRITE_DELAY)
    {
        return 0;
    }

    snprintf(tmp_fname, PATH_MAX, "/tmp/mac_list.%d", getpid());

    f = fopen(tmp_fname, "w");
    if (f == NULL) {
        fprintf(stderr, "mac_list_write:  fopen(\"%s\") failed\n", tmp_fname);
        return -1;
    }

    for (i = 0; i < mac_list->next_beacon; i++) {

        mac_print_no_eoln(f, mac_list->beacons[i].mac_addr);

        switch (mac_list->type) {

        case mac_list_beacon_name :
            fprintf(f, "    ");
            mac_print(f, mac_list->names[i]);

            break;

        case mac_list_beacon_signal_strength :
            fprintf(f, " %d\n", mac_list->signal_strength[i]);
            break;

        case mac_list_beacon :
            fprintf(f, "\n");
            break;

        case mac_list_beacon_desc :
            fprintf(f, "    %s", mac_list->desc[i]);
            if (strchr(mac_list->desc[i], '\n') == NULL) {
                fprintf(f, "\n");
            }

            break;

        default :
            fprintf(stderr, "mac_list_write:  invalid type %d\n",
                    mac_list->type);
            break;
        }
    }
    fclose(f);

    if (rename(tmp_fname, mac_list->fname)) {
        fprintf(stderr, "mac_list_write:  "
                "could not create %s:  %d\n", mac_list->fname, errno);
        perror("error:  ");
        unlink(tmp_fname);
        return -1;
    }

    mac_list->last_time_sec = tv.tv_sec;
    mac_list->last_time_usec = tv.tv_usec;

    return 0;
}

/* create a new mac_list that will be saved on disk in /tmp/fname,
 * and saves extra information with each mac address as specified
 * by "type".  Mac addresses that are not refreshed within "timeout"
 * usecs are deleted.  (if timeout is -1, they don't ever time out.)
 * if write_delay is true, mac_list will be written to disk at most
 * once per second.
 * mac list (including disk version) is initially empty.
 */ 
int mac_list_init(mac_list_t *mac_list, char *fname, mac_list_type_t type,
        long long timeout, bool_t write_delay)
{
    #ifdef DEBUG
        if (!debug_file_inited) {
            if ((debug_file = fopen("/tmp/mac_list_debug", "w")) == NULL) {
                fprintf(stderr, "mac_list_init; could not open debug:  %s\n",
                        strerror(errno));
            } else {
                debug_file_inited = true;
            }
        }
    #endif

    mac_list->next_beacon = 0;
    mac_list->last_time_sec = 0;
    mac_list->last_time_usec = 0;
    mac_list->write_delay = write_delay;
    if (strlen(fname) >= (PATH_MAX - 1)) {
        fprintf(stderr, "mac_list_init:  file name %s too long\n", fname);
        return -1;
    }
    strcpy(mac_list->fname, fname);

    mac_list->type = type;
    mac_list->timeout = timeout;

    mac_list_write(mac_list);

    return 0;
}

/* initialize a mac_list as above, but assume the file already has stuff
 * in it, and read that stuff in.
 */
int mac_list_init_read(mac_list_t *mac_list, char *fname, mac_list_type_t type,
        long long timeout, bool_t write_delay)
{
    mac_list->next_beacon = 0;
    mac_list->last_time_sec = 0;
    mac_list->last_time_usec = 0;
    mac_list->write_delay = write_delay;
    if (strlen(fname) >= (PATH_MAX - 1)) {
        fprintf(stderr, "mac_list_init:  file name %s too long\n", fname);
        return -1;
    }
    strcpy(mac_list->fname, fname);

    mac_list->type = type;
    mac_list->timeout = timeout;

    mac_list_read(mac_list);

    return 0;
}

/* expire any stale mac addresses from the mac_list.
 * add mac_address to the list, or if it's already there refresh its
 * time to "now".
 *
 * include name, signal_strength, and desc in the record for this mac address.
 */
void mac_list_add(mac_list_t *mac_list, mac_address_t mac_addr,
        mac_address_t name, int signal_strength, char *desc, bool_t write_um)
{
    int i, this_beacon;
    struct timeval tv;
    struct timezone tz;

    if (gettimeofday(&tv, &tz)) {
        fprintf(stderr, "mac_list_add; gettimeofday failed\n");
        return;
    }

    if (mac_list_expire_timed_macs(mac_list)) {
        fprintf(stderr, "mac_list_add; mac_list_expire_timed_macs failed\n");
        return;
    }

    this_beacon = -1;
    for (i = 0; i < mac_list->next_beacon; i++) {
        if (mac_equal(mac_addr, mac_list->beacons[i].mac_addr)) {
            this_beacon = i;
            break;
        }
    }
    if (this_beacon == -1) {
        if (mac_list->next_beacon == MAX_CLOUD) {
            fprintf(stderr, "mac_list_add; too many mac addresses\n");
            return;
        }

        mac_copy(mac_list->beacons[mac_list->next_beacon].mac_addr, mac_addr);
        this_beacon = mac_list->next_beacon;
        mac_list->next_beacon++;
    }

    mac_copy(mac_list->names[this_beacon], name);
    if (desc != NULL) {
        strncpy(mac_list->desc[this_beacon], desc, MAX_DESC);
    }
    if (signal_strength != NO_SIGNAL_STRENGTH) {
        mac_list->signal_strength[this_beacon] = signal_strength;
    }

    mac_list->beacons[this_beacon].tv_sec = tv.tv_sec;
    mac_list->beacons[this_beacon].tv_usec = tv.tv_usec;

    #ifdef DEBUG
        if (debug_file_inited) {
            fprintf(debug_file, "%s; time update ", mac_list->fname);
            mac_print_no_eoln(debug_file,
                    mac_list->beacons[this_beacon].mac_addr);
            fprintf(debug_file, " at ");
            util_print_time(debug_file, &tv);
            fprintf(debug_file, "\n");
            fflush(debug_file);
        }
    #endif

    if (write_um) {
        if (mac_list_write(mac_list)) {
            fprintf(stderr, "mac_list_add; write_beacons failed\n");
        }
    }
}

/* print "msg" to debug_file, together with time */
void mac_list_db_msg(char *msg)
{
    #ifdef DEBUG
        struct timeval tv;
        struct timezone tz;

        if (gettimeofday(&tv, &tz)) {
            fprintf(stderr, "mac_list_db_msg; gettimeofday failed\n");
            return;
        }
        if (debug_file_inited) {
            fprintf(debug_file, "%s at ", msg);
            util_print_time(debug_file, &tv);
            fprintf(debug_file, "\n");
            fflush(debug_file);
        }
    #endif
}
