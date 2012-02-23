/* eth_util.h - utilities to communicate via cat-5 among cloud boxes
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: eth_util.h,v 1.6 2012-02-22 18:55:24 greg Exp $
 */
#ifndef ETH_UTIL_H
#define ETH_UTIL_H

#include "cloud.h"
#include "pio.h"

#define ETH_TIMEOUT_USEC 10000000

extern void eth_util_init(char *beacon_file, int socket, int if_index,
        pio_t *pio,
        mac_address_t eth_mac_addr, mac_address_t box_wlan_mac_addr);
extern void eth_util_interrupt();
extern void eth_util_interrupt_no_send();
extern void eth_util_process_message(unsigned char *message);

#endif
