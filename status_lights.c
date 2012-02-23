/* status_lights.c - control lights on the router to convey cloud status
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: status_lights.c,v 1.6 2012-02-22 18:55:25 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: status_lights.c,v 1.6 2012-02-22 18:55:25 greg Exp $";

/* cause lights on the front of the box to blink, indicating state of the
 * box and the cloud.
 *
 * /tmp/cloud_status has 3 numbers on successive lines indicating status.
 *
 * Example:
 * 3   # three boxes in the cloud
 * 200 # my weakest stp link
 * 1   # number of boxes that have a below-threshold stp link
 *
 * the dmz light blinks out the number of boxes in the cloud, and then
 * the number of boxes that have no weak stp links.  In the above example,
 * the dmz light would blink 3 times then 2 times.
 *
 * the power light reflects the status of the box itself.  If its weakest
 * stp link is above threshold (175 currently), it stays on solid.  If
 * not, it alternates between on solid and fast-blink.
 */

/* in debug mode, set things up for execution on a development box without
 * real lights, and don't do interrupt-based timing.  instead, do things
 * interactively from an interactive prompt.
 * in normal (non-debug) mode, set up timer-based periodic interrupts and
 * have that drive the program.
 */
// #define DEBUG

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>

#include "util.h"

#define INT_FREQ_USEC 500000

enum { DMZ, SESSION, DIAG };

enum { START_LED, STOP_LED };

static int version = 2;

static int my_weakest_stp_link;
static int status_threshold = 175;

static int cloud_weak_box_count;
static int cloud_box_count;
static char *cloud_status_file = "/tmp/cloud_status";

static int count = 10;

#define MY_STATUS_ON_DSEC 8
#define MY_STATUS_OFF_DSEC 2

/* set power light to solid-on or to fast-blink */
#define POWER_LIGHT_ON_SOLID 0
#define POWER_LIGHT_BLINK 1

/* set dmz light on or off */
#define DMZ_LIGHT_ON 1
#define DMZ_LIGHT_OFF 0

static int status_state = POWER_LIGHT_ON_SOLID;
static int status_tick_count = 0;

/* dfa states for the cloud status (dmz light) */
#define CLOUD_PRE_COUNT_GAP 1
#define CLOUD_PRE_COUNT_GAP1 2
#define CLOUD_COUNT 3
#define CLOUD_PRE_OK_GAP 4
#define CLOUD_PRE_OK_GAP1 5
#define CLOUD_PRE_OK_GAP2 6
#define CLOUD_OK_COUNT 7

/* amount of times to spend in each state */
#define CLOUD_STATUS_FLICKER_ON_DSEC 4
#define CLOUD_STATUS_FLICKER_OFF_DSEC 2
#define CLOUD_STATUS_ON_DSEC 2
#define CLOUD_STATUS_OFF_DSEC 2
#define CLOUD_STATUS_OFF_GAP_DSEC 4

static int cloud_state = CLOUD_PRE_COUNT_GAP;

static int cloud_light_state = DMZ_LIGHT_OFF;

static int cloud_count;
static int cloud_tick_count = 0;

/* get the state of the lights (on, off, blinking etc.),
 * for wrt54g v 2.0 and later i think
 */
static unsigned int read_gpio(char *device)
{
        FILE *fp;
        unsigned int val;

        if( (fp=fopen(device, "r")) ){
            fread(&val, 4, 1, fp);
            fclose(fp);
            // fprintf(stderr, "----- gpio %s = [%X]\n",device,val);
            return val;
        }
        else{
            perror(device);
            return 0;
        }
}

/* set the state of the lights (on, off, blinking etc.),
 * for wrt54g v 2.0 and later i think
 */
static unsigned int write_gpio(char *device, unsigned int val)
{
        FILE *fp;

        if( (fp=fopen(device, "w")) ){
            fwrite(&val, 4, 1, fp);
            fclose(fp);
            //fprintf(stderr, "----- set gpio %s = [%X]\n",device,val);
            return 1;
        }
        else{
            perror(device);
            return 0;
        }
}

/* set the state of the specified light (power light or dmz light)
 * on or off.
 * for wrt54g v 2.0 and later i think
 */
static void set_light_v2(bool_t on_off_light, bool_t dmz_light)
{
    unsigned int control,in,outen,out;

    control=read_gpio("/dev/gpio/control");
    in=read_gpio("/dev/gpio/in");
    out=read_gpio("/dev/gpio/out");
    outen=read_gpio("/dev/gpio/outen");

    if (dmz_light) {
        /* dmz light is gpio 7 */
        write_gpio("/dev/gpio/control",control & 0x7f);
        write_gpio("/dev/gpio/outen",outen | 0x80);
        if (!on_off_light) {
            write_gpio("/dev/gpio/out",out | 0x80);
        } else /*if (act == START_LED)*/ {
            write_gpio("/dev/gpio/out",out & 0x7f);
        }
    } else {
        write_gpio("/dev/gpio/control",control & 0xfd);
        write_gpio("/dev/gpio/outen",outen | 0x02);
        if (!on_off_light) { // stop blinking
            write_gpio("/dev/gpio/out",out | 0x02);
        } else /* if (act == START_LED) */ { // start blinking
            write_gpio("/dev/gpio/out",out & 0xfd);
        }
    }
}

/* set the state of the specified light (power light or dmz light)
 * on or off.
 * for wrt54g earlier than v 2.0 i think
 */
static void set_lights(int power_light_setting, int dmz_light_setting)
{
    if (version == 2) {
        set_light_v2(power_light_setting == 1, false /* diag light */);
        set_light_v2(dmz_light_setting == 1, true /* dmz light */);
    } else {
        /* put value in /proc/sys/diag:
         * 0 - power light solid, dmz light off
         * 1 - power light solid, dmz light on
         * 4 - power light blink, dmz light off
         * 5 - power light blink, dmz light on
         */
        FILE *f;
        int set_um = 0;
        // fprintf(stderr, "hi from set_lights..\n");
        if (dmz_light_setting) { set_um = 1; }
        if (power_light_setting) { set_um += 4; }

        if ((f = fopen("/proc/sys/diag", "w")) == NULL) {
            fprintf(stderr, "could not open /proc/sys/diag:  %s\n",
                    strerror(errno));
            return;
        }
        fprintf(f, "%d\n", set_um);
        fclose(f);
    }
}

#if 0
/* for running the program on a development box with no real lights */
static void db_set_lights(bool_t on_off_light, bool_t dmz_light)
{
    printf("    %c    %c%c", on_off_light ? '*' : 'o', dmz_light ? '*' : 'o',
            #ifdef DEBUG
                '\n'
            #else
                '\r'
            #endif

            );
    fflush(stdout);
}
#endif

/* this is the routine that periodically gets called from the timer interrupt.
 * it periodically reads the file containing cloud box status information,
 * and uses a state machine to decide how to set the lights on the front
 * of the box to reflect state.
 */
static void repeated(int arg)
{
    FILE *f;
    int result;

    if (++count >= 10) {
        bool_t bad = false;
        count = 0;

        f = fopen(cloud_status_file, "r");

        if (f == NULL) {
            fprintf(stderr, "could not open %s:  %s\n",
                    cloud_status_file, strerror(errno));
            bad = true;
            goto done;
        }

        if (1 != fscanf(f, "%d", &cloud_box_count)) { goto done; }
        while ((result = fgetc(f)) != '\n' && result != EOF);
        if (result == EOF) { bad = true; goto done; }

        if (1 != fscanf(f, "%d", &my_weakest_stp_link)) { goto done; }
        while ((result = fgetc(f)) != '\n' && result != EOF);
        if (result == EOF) { bad = true; goto done; }

        if (1 != fscanf(f, "%d", &cloud_weak_box_count)) { goto done; }
        while ((result = fgetc(f)) != '\n' && result != EOF);
        if (result == EOF) { bad = true; goto done; }

        done:
        if (f != NULL) { fclose(f); }
        if (bad) { return; }
    }

    if (--status_tick_count <= 0) {
        status_tick_count = MY_STATUS_ON_DSEC + MY_STATUS_OFF_DSEC;

        if (my_weakest_stp_link >= status_threshold
            || my_weakest_stp_link == -1)
        {
            status_state = POWER_LIGHT_ON_SOLID;
        } else {
            status_state = POWER_LIGHT_BLINK;
        }
    } else if (status_tick_count <= MY_STATUS_ON_DSEC) {
        status_state = POWER_LIGHT_ON_SOLID;
    }

    #if 0
    fprintf(stderr, "cloud_tick_count:  %d, cloud_state %d, "
            "cloud_light_state %d, cloud_count %d\n",
            cloud_tick_count, cloud_state,
            cloud_light_state, cloud_state);
    #endif

    if (--cloud_tick_count <= 0) {
        switch (cloud_state) {

        case CLOUD_PRE_COUNT_GAP :
            cloud_state = CLOUD_PRE_COUNT_GAP1;
            /* we know we have at least one, namely ourselves */
            cloud_light_state = DMZ_LIGHT_ON;
            cloud_tick_count = CLOUD_STATUS_FLICKER_ON_DSEC;
            break;

        case CLOUD_PRE_COUNT_GAP1 :
            if (cloud_light_state == DMZ_LIGHT_ON) {
                cloud_light_state = DMZ_LIGHT_OFF;
                cloud_tick_count = CLOUD_STATUS_FLICKER_OFF_DSEC;
            } else {
                cloud_state = CLOUD_COUNT;
                /* we know we have at least one, namely ourselves */
                cloud_light_state = DMZ_LIGHT_ON;
                cloud_tick_count = CLOUD_STATUS_ON_DSEC;
                cloud_count = 0;
            }
            break;

        case CLOUD_COUNT :
            if (cloud_light_state == DMZ_LIGHT_ON) {
                cloud_light_state = DMZ_LIGHT_OFF;
                cloud_tick_count = CLOUD_STATUS_OFF_DSEC;
            } else {
                cloud_count++;
                if (cloud_count >= cloud_box_count) {
                    cloud_state = CLOUD_PRE_OK_GAP;
                    cloud_tick_count = CLOUD_STATUS_OFF_GAP_DSEC
                            - CLOUD_STATUS_OFF_DSEC;
                } else {
                    cloud_light_state = DMZ_LIGHT_ON;
                    cloud_tick_count = CLOUD_STATUS_ON_DSEC;
                }
            }
            break;

        case CLOUD_PRE_OK_GAP :
            cloud_state = CLOUD_PRE_OK_GAP2;
            cloud_light_state = DMZ_LIGHT_ON;
            cloud_tick_count = CLOUD_STATUS_FLICKER_ON_DSEC;
            break;

        case CLOUD_PRE_OK_GAP1 :
            if (cloud_light_state == DMZ_LIGHT_ON) {
                cloud_light_state = DMZ_LIGHT_OFF;
                cloud_tick_count = CLOUD_STATUS_FLICKER_ON_DSEC;
            } else {
                cloud_state = CLOUD_PRE_OK_GAP2;
                cloud_light_state = DMZ_LIGHT_ON;
                cloud_tick_count = CLOUD_STATUS_FLICKER_ON_DSEC;
            }
            break;

        case CLOUD_PRE_OK_GAP2 :
            if (cloud_light_state == DMZ_LIGHT_ON) {
                cloud_light_state = DMZ_LIGHT_OFF;
                cloud_tick_count = CLOUD_STATUS_FLICKER_OFF_DSEC;
            } else if (cloud_box_count == cloud_weak_box_count) {
                cloud_state = CLOUD_PRE_COUNT_GAP;
                cloud_tick_count = CLOUD_STATUS_OFF_GAP_DSEC;
            } else {
                cloud_state = CLOUD_OK_COUNT;
                cloud_light_state = DMZ_LIGHT_ON;
                cloud_tick_count = CLOUD_STATUS_ON_DSEC;
                cloud_count = 0;
            }
            break;

        case CLOUD_OK_COUNT :
            if (cloud_light_state == DMZ_LIGHT_ON) {
                cloud_light_state = DMZ_LIGHT_OFF;
                cloud_tick_count = CLOUD_STATUS_OFF_DSEC;
            } else {
                cloud_count++;
                if (cloud_count >= cloud_box_count - cloud_weak_box_count) {
                    cloud_state = CLOUD_PRE_COUNT_GAP;
                    cloud_tick_count = CLOUD_STATUS_OFF_GAP_DSEC
                            - CLOUD_STATUS_OFF_DSEC;
                } else {
                    cloud_light_state = DMZ_LIGHT_ON;
                    cloud_tick_count = CLOUD_STATUS_ON_DSEC;
                }
            }
            break;

        }
    }

    // fprintf(stderr, "after:  %d, cloud_state %d, cloud_light_state %d, "
            // "cloud_count %d\n", cloud_tick_count, cloud_state,
            // cloud_light_state, cloud_state);

    set_lights(status_state, cloud_light_state);
    // db_set_lights(status_state, cloud_light_state);
}

static int diag = -1;
static int dmz = -1;

static void usage()
{
    fprintf(stderr, "usage:\nstatus_lights \\\n"
            "    [-v N] \\\n"
            "    [-d diag_light -z dmz_light] \\\n");
    exit(1);
}

static void process_args(int argc, char **argv)
{
    int c;

    while ((c = getopt(argc, argv, "d:z:v:")) != -1) {
        switch (c) {

        case 'd' :
            if (sscanf(optarg, "%d", &diag) != 1) {
                fprintf(stderr, "DEBUG:  invalid diag %s\n", optarg);
                exit(1);
            }
            break;

        case 'z' :
            if (sscanf(optarg, "%d", &dmz) != 1) {
                fprintf(stderr, "DEBUG:  invalid dmz %s\n", optarg);
                exit(1);
            }
            break;

        case 'v' :
            if (sscanf(optarg, "%d", &version) != 1) {
                fprintf(stderr, "DEBUG:  invalid version %s\n", optarg);
                exit(1);
            }
            break;

        default :
            usage();
        }
    }
}

int main(int argc, char **argv)
{
    struct itimerval timer;
    #ifndef DEBUG
        int result;
    #endif
    struct sigaction action;

    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = INT_FREQ_USEC;

    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = INT_FREQ_USEC;

    memset(&action, 0, sizeof(action));
    action.sa_handler = repeated;
    #ifndef DEBUG
        sigaction(SIGALRM, &action, NULL);
        result = setitimer(ITIMER_REAL, &timer, 0);
    #endif

    process_args(argc, argv);

    if (diag != -1 || dmz != -1) {
        if ((diag != -1) != (dmz != -1)) { usage(); }
        fprintf(stderr, "setting %d %d\n", diag, dmz);
        set_lights(diag, dmz);
        exit(0);
    }

    set_lights(status_state, cloud_light_state);
    // db_set_lights(status_state, cloud_light_state);

    /* unless in debug mode, just sleep forever and have the periodic
     * interrupts drive things.
     */
    while (true) {
        #ifdef DEBUG
            fprintf(stderr, "fire when ready..  ");
            if (getchar() == 'q') { return 0; }
            repeated(0);
        #else
            sleep(3600);
        #endif
    }
}
