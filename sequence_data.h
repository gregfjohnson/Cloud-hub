/* sequence_data.h - manage sequence numbers on cloud messages
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: sequence_data.h,v 1.6 2012-02-22 18:55:25 greg Exp $
 */
#ifndef SEQUENCE_DATA_H
#define SEQUENCE_DATA_H

#include "util.h"

/* if we are trying to to resends using sequence numbers (bad idea it turns
 * out), or passively track lost packets (good idea)
 */
typedef struct {
    byte sequence_num;
    short message_len;
} seq_t;

#endif
