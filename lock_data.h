/* lock_data.h - request and grant locks among cloud boxes
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: lock_data.h,v 1.6 2012-02-22 18:55:25 greg Exp $
 */
#ifndef LOCK_DATA_H
#define LOCK_DATA_H

#include "mac.h"

/* request from one cloud box to another to lock the (logical) arc connecting
 * the boxes; we intend to change graph connectivity and want to own the
 * arc while we are doing it.
 */
typedef struct {
    mac_address_t originator;
    mac_address_t node_1;
    mac_address_t node_2;
} lock_message_t;

#endif
