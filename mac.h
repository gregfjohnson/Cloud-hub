/* mac.h - mac address print, read, write, compare utilities
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: mac.h,v 1.7 2012-02-22 19:27:23 greg Exp $
 */
#ifndef MAC_H
#define MAC_H

#include <stdio.h>
#include "util.h"

typedef byte mac_address_t[6];
typedef byte *mac_address_ptr_t;

extern mac_address_t mac_address_zero;
extern mac_address_t mac_address_bcast;

/* a few convenient scratch buffers to use with mac_sprintf */
extern char mac_buf1[20], mac_buf2[20], mac_buf3[20];

int mac_read(FILE *f, mac_address_t mac_address);
int mac_sscanf(mac_address_t mac_address, char *str);
void mac_copy(mac_address_t dest, mac_address_t src);
char mac_equal(mac_address_t a, mac_address_t b);
int mac_cmp(mac_address_t a, mac_address_t b);
void mac_print(FILE *f, mac_address_ptr_t b);
void mac_dprint(ddprintf_t *fn, FILE *f, mac_address_ptr_t b);
char *mac_sprintf(char *s, mac_address_ptr_t b);
void mac_print_no_eoln(FILE *f, mac_address_ptr_t b);
void mac_dprint_no_eoln(ddprintf_t *fn, FILE *f, mac_address_ptr_t b);
int mac_get(mac_address_t mac_address, char *device_name);

#endif
