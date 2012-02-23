/* random.c - generate random numbers from various distributions
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: random.c,v 1.9 2012-02-22 19:27:23 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: random.c,v 1.9 2012-02-22 19:27:23 greg Exp $";

#include <stdlib.h>
#include <math.h>

#include "print.h"
#include "random.h"
#include "timer.h"

/* cumulative distribution for whether to update.  in milliseconds
 * initially, scaled by MEAN_WAKEUP_TIME at initialization.
 */

#define IMPROVE_MAX 8
static int improve_vec[IMPROVE_MAX] = {
    0,
    1,
    4,
    5,
    6,
    7,
    8,
    10,
};

static double base_improve_prob[IMPROVE_MAX] = {
    0.,
    86400000.,
    14400000.,
    60000.,
    30000.,
    5000.,
    1000.,
    100.,
};

static int improve_prob_mult = -1;
static double improve_prob[IMPROVE_MAX] = {
    0.,
    86400000.,
    14400000.,
    60000.,
    30000.,
    5000.,
    1000.,
    100.,
};

void improve_prob_init(int cloud_count)
{
    int i;
    double mean_wakeup_time;

    if (!db[39].d) {
        if (improve_prob_mult != -1) { return; }
        improve_prob_mult = 1;
    } else {
        if (cloud_count == improve_prob_mult) { return; }
        improve_prob_mult = cloud_count;
        if (db[40].d) improve_prob_mult *= 20;
    }

    mean_wakeup_time = (double) MEAN_WAKEUP_TIME;
    if (improve_prob_mult > 1) {
        mean_wakeup_time *= improve_prob_mult;
    }

    for (i = 0; i < IMPROVE_MAX; i++) {
        double d = base_improve_prob[i];
        if (d > 0.) {
            d /= mean_wakeup_time;
            improve_prob[i] = 1. / (1. + d);
        }
    }
}

/* initialize the random number generator using this box's mac address */
void init_random(mac_address_t my_wlan_mac_address)
{
    int i;
    unsigned int seed = 0;

    for (i = 2; i < 6; i++) {
        seed = seed << 8;
        seed += my_wlan_mac_address[i];
    }
    /* maybe also use /dev/random */

    srandom(seed);
}

/* return a discrete uniform random deviate in the range [0 .. max-1]. */
int discrete_unif(int max)
{
    long int r = random();
    double unif = (r / (double) RAND_MAX) * max;
    int retval = (int) unif;
    if (retval < 0) { retval = 0; }
    if (retval >= max) { retval = max - 1; }
    return retval;
}

/* return a negative exponential random deviate whose mean value is "mean".
 * (so, input parameter is 1/lamda)
 */
double neg_exp(int mean)
{
    long int r = random();
    double unif = r / (double) RAND_MAX;
    return -log(unif) * (double) mean;
}

#define BIG_INT 2147483647

/* return 1 if we should change, 0 if not. */
char random_eval(int diff, int cloud_count)
{
    char result = 0;
    long int r = random();
    double u = (r / (double) RAND_MAX);
    int i;

    for (i = 0; i < IMPROVE_MAX - 1; i++) {
        if (diff <= improve_vec[i]) {
            break;
        }
    }

    /* if we are taking into account the number of cloud boxes, adjust
     * improvement probability based on our current idea of the number of
     * cloud boxes in our cloud.
     */
    if (db[39].d) { improve_prob_init(cloud_count); }

    result = (u <= improve_prob[i]);
    if (db[11].d) {
        ddprintf("random_eval:  diff of %d; u %f <? improve_prob %f:  %d\n",
                diff, u, improve_prob[i], (int) result);
    }

    return result;
}
