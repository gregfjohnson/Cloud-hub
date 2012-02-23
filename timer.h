/* timer.h - schedule periodic events based on timer interrupts
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: timer.h,v 1.14 2012-02-22 18:55:25 greg Exp $
 */
#ifndef TIMER_H
#define TIMER_H

#include "lock.h"

#define USE_TIMER 1

/* these are timing parameters that control protocol message frequencies
 * and timeouts
 */

/* in micro-seconds; how fast to timeout a request that should
 * be responded to immediately
 */
#define RECV_TIMEOUT_USEC 2000000

/* in milli-seconds; how often on average to wake up, check connectivity,
 * send stp beacons, etc.
 */
#define MEAN_WAKEUP_TIME 500

/* in micro-seconds; how fast to time out a received stp message */
#define STP_TIMEOUT 5000000LL

/* in milliseconds; how fast to time out waiting for an ack to a message
 * not used unless we do explicit flow control, which we never do
 * at this point.
 */
#define ACK_TIMEOUT_MSEC 100

/* in milliseconds; how often to update wds based on incoming beacons */
#define WRT_UPDATE_INTERVAL 750

/* in milliseconds; how often to update eth_beacons based on incoming beacons */
#define ETH_UPDATE_INTERVAL 500

/* in milliseconds; how often to print cloud tree */
#define CLOUD_PRINT_INTERVAL 5000

/* in milliseconds; when turn off printing of cloud tree if it isn't turned
 * back on.  (if no one in the cloud is looking at cloud.asp, save bandwidth
 * by not sending around info required to generate the cloud tree display.)
 */
#define CLOUD_PRINT_DISABLE 18000

/* in milliseconds; how often to ping_respond neighbors */
#define PING_INTERVAL_MIN 1000
#define PING_INTERVAL_MAX 2000

/* in milliseconds; how often to do a wif scan */
#define SCAN_INTERVAL_MIN 55000
#define SCAN_INTERVAL_MAX 65000

/* 10 times TIME_BASE rounded up to seconds; backup timer interrupt */
#define SAFETY_INTERVAL 1000

#define UNROUTABLE_MAX 100

/* when we are supposed to next do something for each of the functions */
#define NEXT_WAKEUP_TIME 0
#define NEXT_WRT_UPDATE_TIME 1
#define NEXT_ETH_UPDATE_TIME 2

#define TIMER_COUNT 9

#define send_stp 0
#define process_beacon 1
#define process_eth_beacon 2
#define noncloud_message 3
#define lockable_timeout 4
#define print_cloud 5
#define ping_neighbors 6
#define disable_print_cloud 7
#define wifi_scan 8

/* we maintain multiple streams of timed events.  for each event,
 * this array saves the next time it needs to be performed.
 * we schedule a timer interrupt to notify us when the next timed
 * event is due to take place.
 */
extern struct timeval times[TIMER_COUNT];

extern struct timeval now;
extern struct timeval start;

/* for periodic timed events, the amount of time between events. */
extern int intervals[TIMER_COUNT];

extern int interrupt_pipe[2];

extern char got_interrupt[];

extern void send_interrupt_pipe_char();

extern void repeated(int arg);
extern void timer_init(void);
extern void block_timer_interrupts(int todo);
extern void block_timer_interrupts(int todo);
extern void set_alarm(int msec);
extern void ptime(char *title, struct timeval time);
extern void dptime(char *title, long sec, long usec);
extern void timer_print();
extern void stop_alarm();
extern void set_next_alarm();
extern void set_next_cloud_print_alarm(void);
extern void set_next_scan_alarm(void);
extern void restart_disable_print_cloud();
extern void ensure_disable_print_cloud();
extern void set_next_ping_alarm(void);
extern void update_ack_timer();
extern void turn_off_ack_timer();
extern void reset_lock_timer();
extern void set_lock_timer(lockable_resource_t *l);

#endif
