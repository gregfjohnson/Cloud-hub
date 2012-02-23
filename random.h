/* random.h - generate random numbers from various distributions
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: random.h,v 1.7 2012-02-22 18:55:25 greg Exp $
 */
#ifndef RANDOM_H
#define RANDOM_H

#include "mac.h"

extern double neg_exp(int mean);
extern int discrete_unif(int max);
extern char random_eval(int diff, int cloud_count);
extern void improve_prob_init(int cloud_count);
extern void init_random(mac_address_t my_wlan_mac_address);

#endif
