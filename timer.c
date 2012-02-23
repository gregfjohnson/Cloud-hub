/* timer.c - schedule periodic events based on timer interrupts
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: timer.c,v 1.16 2012-02-22 19:27:23 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: timer.c,v 1.16 2012-02-22 19:27:23 greg Exp $";

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include "util.h"
#include "cloud.h"
#include "timer.h"
#include "print.h"
#include "random.h"
#include "sequence.h"
#include "stp_beacon.h"

#include <sys/time.h>

int intervals[TIMER_COUNT] = {
    0,
    WRT_UPDATE_INTERVAL,
    ETH_UPDATE_INTERVAL,
    0,
    0,
    0,
    0,
    0,
};

struct timeval times[TIMER_COUNT] = {
    {0, 0},
    {0, 0},
    {0, 0},
    {-1, 0},
    {-1, 0},
    {0, 0},
    {0, 0},
    {0, 0},
};
struct timeval now;
struct timeval start;

char got_interrupt[TIMER_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0};

int interrupt_pipe[2];

/* this routine is called periodically based on timed interrupts.
 * it sends a character into a pipe we use to talk to ourselves.
 * the arrival of a character causes the main routine to break out
 * of its "select()" statement and do periodic activities that we
 * are supposed to initiate, such as sending out stp beacons.
 */
void send_interrupt_pipe_char()
{
    char c = 'x';
    int result = write(interrupt_pipe[1], &c, 1);
    if (result == -1) {
        ddprintf("send_interrupt_pipe_char; write failed:  %s\n",
                strerror(errno));
        return;
    }
}

/* this is the routine that gets called when a timer interrupt takes place.
 * to avoid the complications of having to worry about messing with data
 * structures at the interrupt level when the mainline code might be
 * manipulating them, we simply notify that mainline code that a timer
 * interrupt took place, and let it handle it.  we do this by putting a
 * character into a pipe that is one of the things the mainline routine
 * selects on when it is looking for input.
 *
 * also, set the next timer alarm.
 */
void repeated(int arg)
{
    send_interrupt_pipe_char();
    set_next_alarm();
}

/* create the pipe we use to send interrupts to ourselves. */
void timer_init()
{
    if (pipe(interrupt_pipe) == -1) {
        ddprintf("main; pipe creation failed:  %s\n", strerror(errno));
        exit(1);
    }
}

/* block or unblock timer interrupts based on "todo" */
void block_timer_interrupts(int todo)
{   
    sigset_t blockum;
    int result;

    while (true) {
        if ((result = sigemptyset(&blockum)) == -1) { continue; }
        if ((result = sigaddset(&blockum, SIGALRM)) == -1) { continue; }
        if ((result = sigprocmask(todo, &blockum, NULL)) == -1) {
            ddprintf("block_timer_interrupts problem:  %s\n",
                    strerror(errno));
        }
        break;
    }
}

void stop_alarm(void)
{
    int result;
    struct itimerval timer = {{0, 0}, {0, 0}};

    result = setitimer(ITIMER_REAL, &timer, 0);
}

/* set an alarm to go off msec milliseconds in the future. */
void set_alarm(int msec)
{
    int result;
    struct itimerval timer;
    if (msec == 0) { msec = 1; }

    timer.it_value.tv_sec = msec / 1000;
    timer.it_value.tv_usec = (msec % 1000) * 1000;

    /* make sure that we get an interrupt at least once per second. */
    timer.it_interval.tv_sec = SAFETY_INTERVAL;
    timer.it_interval.tv_usec = 0;

    result = setitimer(ITIMER_REAL, &timer, 0);
}

/* debug-print the amount of time that has transpired since the box was
 * turned on in seconds, with fraction showing hundreth's of seconds.
 */
void ptime(char *title, struct timeval time)
{
    ddprintf("%s:  %.2f; ",
            title,
            (time.tv_sec == -1)
                ? -1
                : ((double) usec_diff(time.tv_sec, time.tv_usec,
                        start.tv_sec, start.tv_usec) / 1000000.));

}

/* debug-print the amount of time that has transpired since the box was
 * turen on in seconds, with fraction showing hundreth's of seconds.
 */
void dptime(char *title, long sec, long usec)
{
    struct timeval time;
    time.tv_sec = sec;
    time.tv_usec = usec;
    ptime(title, time);
}

/* debug print current timer values */
void timer_print()
{
    ptime("now                ", now);                   ddprintf("\n");
    ptime("send_stp           ", times[0]);              ddprintf("\n");
    ptime("process_beacon     ", times[1]);              ddprintf("\n");
    ptime("process_eth_beacon ", times[2]);              ddprintf("\n");
    ptime("noncloud_message   ", times[3]);              ddprintf("\n");
    ptime("lockable_timeout   ", times[4]);              ddprintf("\n");
    ptime("print_cloud        ", times[5]);              ddprintf("\n");
    ptime("ping_neighbors     ", times[6]);              ddprintf("\n");
    ptime("disable_print_cloud", times[7]);              ddprintf("\n");
    ptime("wifi_scan          ", times[8]);              ddprintf("\n");
}

/* of the various timers (currently 8), figure out which one is due to
 * happen soonest, and set a timer interrupt to go off to wake us up then.
 */
void set_next_alarm()
{
    long long next_interrupt, maybe_next;

    while (!checked_gettimeofday(&now));

    if (db[2].d) {
        ptime("now", now);
        ptime("t0", times[0]);
        ptime("t1", times[1]);
        ptime("t2", times[2]);
        ptime("t3", times[3]);
        ptime("t4", times[4]);
        ptime("t5", times[5]);
        ptime("t6", times[6]);
        ptime("t7", times[7]);
        ddprintf("\n");
    }

    /* see if we are past time of next expected interrupt here, and if so
     * note it and reset timer for the future.
     */

    /* this is the exponential timer for stp_beacons */
    while ((next_interrupt = usec_diff(times[0].tv_sec, times[0].tv_usec,
            now.tv_sec, now.tv_usec)) < 0)
    {
        double wait_time;
        int iwait_time;
        if (db[39].d) {
            int mean_wait_time = MEAN_WAKEUP_TIME;
            if (stp_recv_beacon_count > 0) {
                mean_wait_time *= stp_recv_beacon_count;
            }
            wait_time = neg_exp(mean_wait_time);
        } else {
            wait_time = neg_exp(MEAN_WAKEUP_TIME);
        }
        iwait_time = (int) wait_time;
        if (db[40].d) {
            iwait_time *= 20;
            ddprintf("set stp_send timeout to %d\n", iwait_time);
        }
        usec_add_msecs(&times[0].tv_sec, &times[0].tv_usec, iwait_time);
        if (!db[54].d) {
            got_interrupt[send_stp] = 1;
        }
    }

    /* same as above */
    /* this is the wrt_beacon processing interval */
    while ((maybe_next = usec_diff(times[1].tv_sec, times[1].tv_usec,
            now.tv_sec, now.tv_usec)) < 0)
    {
        usec_add_msecs(&times[1].tv_sec, &times[1].tv_usec, intervals[1]);
        got_interrupt[process_beacon] = 1;
    }
    if (maybe_next < next_interrupt) { next_interrupt = maybe_next; }

    /* same as above */
    /* this is the eth_beacon processing interval */
    while ((maybe_next = usec_diff(times[2].tv_sec, times[2].tv_usec,
            now.tv_sec, now.tv_usec)) < 0)
    {
        usec_add_msecs(&times[2].tv_sec, &times[2].tv_usec, intervals[2]);
        got_interrupt[process_eth_beacon] = 1;
    }
    if (maybe_next < next_interrupt) { next_interrupt = maybe_next; }

    /* non-cloud message timeout */
    if (times[3].tv_sec != -1) {

        maybe_next = usec_diff(times[3].tv_sec, times[3].tv_usec,
                now.tv_sec, now.tv_usec);

        if (maybe_next < 0) {
            times[3].tv_sec = -1;
            got_interrupt[noncloud_message] = 1;

        } else if (maybe_next < next_interrupt) {
            next_interrupt = maybe_next;
        }
    }

    /* lockable resource timeout */
    if (times[4].tv_sec != -1) {

        maybe_next = usec_diff(times[4].tv_sec, times[4].tv_usec,
                now.tv_sec, now.tv_usec);

        if (maybe_next < 0) {
            times[4].tv_sec = -1;
            got_interrupt[lockable_timeout] = 1;
            if (db[28].d) {
                ddprintf("\nset_next_alarm; "
                        "detected lockable resource timeout..\n");
            }

        } else if (maybe_next < next_interrupt) {
            next_interrupt = maybe_next;
        }
    }

    /* print to cloud.asp timeout */
    if (times[5].tv_sec != -1) {

        maybe_next = usec_diff(times[5].tv_sec, times[5].tv_usec,
                now.tv_sec, now.tv_usec);

        if (maybe_next < 0) {
            times[5].tv_sec = -1;
            got_interrupt[print_cloud] = 1;
            if (db[28].d) {
                ddprintf("\nset_next_alarm; "
                        "detected print_cloud timeout..\n");
            }

        } else if (maybe_next < next_interrupt) {
            next_interrupt = maybe_next;
        }
    }

    /* ping neighbor timeout */
    if (times[6].tv_sec != -1) {

        maybe_next = usec_diff(times[6].tv_sec, times[6].tv_usec,
                now.tv_sec, now.tv_usec);

        if (maybe_next < 0) {
            times[6].tv_sec = -1;
            got_interrupt[ping_neighbors] = 1;
            if (db[28].d) {
                ddprintf("\nset_next_alarm; "
                        "detected ping_neighbors timeout..\n");
            }

        } else if (maybe_next < next_interrupt) {
            next_interrupt = maybe_next;
        }
    }

    /* disable cloud.asp printing timeout */
    if (times[7].tv_sec != -1) {

        maybe_next = usec_diff(times[7].tv_sec, times[7].tv_usec,
                now.tv_sec, now.tv_usec);

        if (maybe_next < 0) {
            times[7].tv_sec = -1;
            got_interrupt[disable_print_cloud] = 1;
            if (db[28].d) {
                ddprintf("\nset_next_alarm; "
                        "detected disable print cloud timeout..\n");
            }

        } else if (maybe_next < next_interrupt) {
            next_interrupt = maybe_next;
        }
    }

    /* wifi scan timeout */
    if (times[8].tv_sec != -1) {

        maybe_next = usec_diff(times[8].tv_sec, times[8].tv_usec,
                now.tv_sec, now.tv_usec);

        if (maybe_next < 0) {
            times[8].tv_sec = -1;
            got_interrupt[wifi_scan] = 1;
            if (db[28].d) {
                ddprintf("\nset_next_alarm; "
                        "detected wifi_scan timeout..\n");
            }

        } else if (maybe_next < next_interrupt) {
            next_interrupt = maybe_next;
        }
    }

    set_alarm((int) (next_interrupt / 1000));

    if (db[2].d) {
        int i;
        ptime("end", now);
        ptime("t0", times[0]);
        ptime("t1", times[1]);
        ptime("t2", times[2]);
        ptime("t3", times[3]);
        ptime("t4", times[4]);
        ptime("t5", times[5]);
        ptime("t6", times[6]);
        ptime("t7", times[7]);
        ptime("t8", times[8]);
        ddprintf("\ninterrupts pending: ");
        for (i = 0; i < TIMER_COUNT; i++) {
            if (got_interrupt[i]) {
                ddprintf(" %d", i);
            }
        }
        ddprintf("\nwaiting %d msec\n", (int) (next_interrupt / 1000));
    }
} /* set_next_alarm */

/* figure out when in the future to send our next ping broadcast message.
 * we do this based on a uniform[PING_INTERVAL_MIN .. PING_INTERVAL_MAX]
 * probability distribution, where the high end of the range is shorter
 * than the stp timeout window.  so, we have a fighting chance to have
 * other cloud boxes hear something from us before they time us out and
 * assume we are dead.
 */
void set_next_ping_alarm(void)
{
    int msec;

    if (times[6].tv_sec != -1) {
        if (db[28].d) { ddprintf("already set; returning.\n"); }
        return;
    }

    while (!checked_gettimeofday(&times[6]));
    msec = discrete_unif(PING_INTERVAL_MAX - PING_INTERVAL_MIN);
    msec += PING_INTERVAL_MIN;
    if (db[40].d) { msec *= 20; }

    if (db[7].d) {
        ddprintf("next time:  %d\n", msec);
    }

    usec_add_msecs(&times[6].tv_sec, &times[6].tv_usec, msec);

    if (db[7].d) {
        ptime("\nping_neighbor timer now", now);
        ptime("ping_neighbor interrupt", times[6]);
        ddprintf("\n");
    }

    block_timer_interrupts(SIG_BLOCK);
    set_next_alarm();
    block_timer_interrupts(SIG_UNBLOCK);
}

/* figure out when in the future to send our next ping broadcast message.
 * we do this based on a uniform[PING_INTERVAL_MIN .. PING_INTERVAL_MAX]
 * probability distribution, where the high end of the range is shorter
 * than the stp timeout window.  so, we have a fighting chance to have
 * other cloud boxes hear something from us before they time us out and
 * assume we are dead.
 */
void set_next_scan_alarm(void)
{
    int msec;

    if (times[8].tv_sec != -1) {
        if (db[28].d) { ddprintf("already set; returning.\n"); }
        return;
    }

    while (!checked_gettimeofday(&times[8]));
    msec = discrete_unif(SCAN_INTERVAL_MAX - SCAN_INTERVAL_MIN);
    msec += PING_INTERVAL_MIN;

    if (db[7].d) {
        ddprintf("next time:  %d\n", msec);
    }

    usec_add_msecs(&times[8].tv_sec, &times[8].tv_usec, msec);

    block_timer_interrupts(SIG_BLOCK);
    set_next_alarm();
    block_timer_interrupts(SIG_UNBLOCK);
}

/* if the 'display the cloud' option has been set for this box,
 * every CLOUD_PRINT_INTERVAL seconds (usually 5 seconds), we re-build an
 * ascii version of our model of the current cloud, viewable from our web
 * page.
 *
 * (if that option is set for at least one box in the cloud, all cloud boxes
 * are instructed to include their local cloud topology connectivity with
 * every stp beacon they send out, somewhat increasing protocol overhead.
 * but not too badly, since these messages only go out on average every
 * second or so.)
 */
void set_next_cloud_print_alarm(void)
{
    struct timeval tv;

    if (!db[38].d && !db[30].d) {
        times[5].tv_sec = -1;
        times[7].tv_sec = -1;
        return;
    }

    if (times[5].tv_sec != -1 && times[7].tv_sec != -1) {
        if (db[28].d) { ddprintf("already set; returning.\n"); }
        return;
    }

    while (!checked_gettimeofday(&tv));

    if (times[5].tv_sec == -1) {
        times[5] = tv;
        usec_add_msecs(&times[5].tv_sec, &times[5].tv_usec,
                CLOUD_PRINT_INTERVAL);

        if (db[28].d) {
            ptime("\nprint_cloud timer now", now);
            ptime("print_cloud interrupt", times[5]);
            ddprintf("\n");
        }
    }

    if (times[7].tv_sec == -1) {
        int disable_time;
        times[7] = tv;

        disable_time = CLOUD_PRINT_DISABLE;
        if (stp_recv_beacon_count > 1) {
            disable_time *= stp_recv_beacon_count;
        }

        usec_add_msecs(&times[7].tv_sec, &times[7].tv_usec, disable_time);

        if (db[28].d) {
            ptime("\ndisable_print_cloud timer now", now);
            ptime("disable_print_cloud interrupt", times[7]);
            ddprintf("\n");
        }
    }

    block_timer_interrupts(SIG_BLOCK);
    set_next_alarm();
    block_timer_interrupts(SIG_UNBLOCK);
}

/* we've been told that printing of the cloud should continue.
 * kick the disable_print_cloud timer farther down the road.
 */
void restart_disable_print_cloud()
{
    times[7].tv_sec = -1;
    set_next_cloud_print_alarm();
}

/* make sure that the disable_cloud_print alarm is set.  if it is set,
 * let it run.  if it is not set, set it.
 */
void ensure_disable_print_cloud()
{
    if (times[7].tv_sec == -1) {
        set_next_cloud_print_alarm();
    }
}

/* set timer to generate alarm if we haven't heard an ack back to our
 * sequence message.
 */
void update_ack_timer()
{
    if (!db[22].d) { return; }

    if (times[3].tv_sec != -1) { return; }

    while (!checked_gettimeofday(&times[3]));
    usec_add_msecs(&times[3].tv_sec, &times[3].tv_usec, ACK_TIMEOUT_MSEC);
    block_timer_interrupts(SIG_BLOCK);
    set_next_alarm();
    block_timer_interrupts(SIG_UNBLOCK);
}

/* turn off ack timer */
void turn_off_ack_timer()
{
    if (awaiting_ack()) { return; }

    times[3].tv_sec = -1;
    block_timer_interrupts(SIG_BLOCK);
    set_next_alarm();
    block_timer_interrupts(SIG_UNBLOCK);
}

/* we have added a granted lock, or a lock request, or an owned lock.
 * all locks time out by themselves if they aren't explicitly updated
 * as part of the cloud protocol.  for the given lock, set its timeout
 * time, and schedule a timer interrupt when that time is reached.
 */
void set_lock_timer(lockable_resource_t *l)
{
    struct timeval now;
    /* don't know why we commented this out; probably a mistake */
    // if (!db[22].d) { return; }

    if (db[28].d) { ddprintf("set_lock_timer..\n"); }

    while (!checked_gettimeofday(&now));
    l->sec = now.tv_sec;
    l->usec = now.tv_usec;
    usec_add_msecs(&l->sec, &l->usec, RECV_TIMEOUT_USEC / 1000);

    reset_lock_timer();

    if (db[28].d) {
        ptime("\nset_lock_timer now", now);
        ptime("set_lock_timer interrupt", times[4]);
        ddprintf("\n");
    }

} /* set_lock_timer */

/* note next time that a lockable resource will time out, and make sure
 * we get an interrupt for it.
 */
void reset_lock_timer()
{
    int i;
    long earliest_sec = -1, earliest_usec;

    for (i = 0; i < pending_request_count; i++) {
        lockable_resource_t *l = &pending_requests[i];
        update_time(&earliest_sec, &earliest_usec, l->sec, l->usec);
    }

    for (i = 0; i < locks_owned_count; i++) {
        lockable_resource_t *l = &locks_owned[i];
        update_time(&earliest_sec, &earliest_usec, l->sec, l->usec);
    }

    for (i = 0; i < locks_granted_count; i++) {
        lockable_resource_t *l = &locks_granted[i];
        update_time(&earliest_sec, &earliest_usec, l->sec, l->usec);
    }

    times[4].tv_sec = earliest_sec;
    times[4].tv_usec = earliest_usec;

    block_timer_interrupts(SIG_BLOCK);
    set_next_alarm();
    block_timer_interrupts(SIG_UNBLOCK);

} /* reset_lock_timer */
