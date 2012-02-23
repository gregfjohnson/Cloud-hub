/* mac.c - mac address print, read, write, compare utilities
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: mac.c,v 1.8 2012-02-22 19:27:23 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: mac.c,v 1.8 2012-02-22 19:27:23 greg Exp $";

/* an API to deal with MAC addresses.  print them, read them, parse
 * them, compare them, etc.
 *
 * this is an API that is usable generically and is not specific to the
 * cloud_hub project.
 */

#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "mac.h"
#include "util.h"

mac_address_t mac_address_zero = {0,0,0,0,0,0};
mac_address_t mac_address_bcast = {0xff,0xff,0xff,0xff,0xff,0xff};

/* peek and see if we are at EOF on f.  used for mac_read below. */
static char eof(FILE *f)
{
    int result;
    int c = fgetc(f);

    if (c == EOF) { return 1; }

    result = ungetc(c, f);

    if (result == EOF) {
        fprintf(stderr, "error:  ungetc from eof in mac.c failed\n");
    }

    return 0;
}

/* read past white space on f.  used for mac_read below. */
static void skip_white(FILE *f)
{
    while (1) {
        int c = fgetc(f);
        if (c == EOF) { return; }
        if (!isspace(c)) {
            int result = ungetc(c, f);
            if (result == EOF) {
                fprintf(stderr, "error:  ungetc from mac:skip_white failed\n");
            }
            return;
        }
    }
}

/* read and parse a mac address from f and put it in mac_address */
int mac_read(FILE *f, mac_address_t mac_address)
{
    int i;
    int result;
    int b;

    skip_white(f);

    if (eof(f)) { return 0; }

    for (i = 0; i < 6; i++) {
        result = fscanf(f, "%x", &b);
        if (result != 1) {
            fprintf(stderr, "mac_read:  read failed\n");
            return 0;
        }
        mac_address[i] = (byte) b;
        if (i < 5) {
            result = fgetc(f);
            if (result == EOF) {
                fprintf(stderr, "mac_read:  read failed\n");
                return 0;
            }
        }
    }

    return 1;
}

/* read and parse a mac address from string.  return 1 iff found. */
int mac_sscanf(mac_address_t mac_address, char *str)
{
    int i;
    int result;
    int b;
    int chars_read;

    for (i = 0; i < 6; i++) {
        result = sscanf(str, "%x%n", &b, &chars_read);
        if (result != 1) {
            fprintf(stderr, "mac_sscanf:  sscanf failed\n");
            return 0;
        }
        mac_address[i] = (byte) b;

        str += chars_read;
        if (i < 5) { str++; }
    }

    return 1;
}

/* copy src to dest.  if src is null, zero dest. */
void mac_copy(mac_address_t dest, mac_address_t src)
{
    int i;
    if (src == NULL) {
        for (i = 0; i < 6; i++) { dest[i] = 0; }
    } else {
        for (i = 0; i < 6; i++) { dest[i] = src[i]; }
    }
}

/* test the mac addresses for equality.  if both are null, they are considered
 * equal.
 */
char mac_equal(mac_address_t a, mac_address_t b)
{
    int i;

    if ((a == NULL) && (b == NULL)) { return 1; }

    if ((a == NULL) || (b == NULL)) { return 0; }

    for (i = 0; i < 6; i++) {
        if (a[i] != b[i]) { return 0; }
    }

    return 1;
}

/* qsort-compatible mac address comparison routine.
 * null address is less than any non-null mac address;
 * two null addresses are considered equal.
 */
int mac_cmp(mac_address_t a, mac_address_t b)
{
    int i;

    if (a == NULL && b == NULL) { return 0; }
    if (a == NULL && b != NULL) { return -1; }
    if (a != NULL && b == NULL) { return  1; }

    for (i = 0; i < 6; i++) {
        if (a[i] != b[i]) {
            return a[i] - b[i];
        }
    }

    return 0;
}

/* a few convenient scratch buffers to use with mac_sprintf */
char mac_buf1[20], mac_buf2[20], mac_buf3[20];

/* print mac address to string s, which is assumed to have at least
 * 19 characters.
 */
char *mac_sprintf(char *s, mac_address_ptr_t b)
{
    if (b == NULL) {
        sprintf(s, "NULL_MAC_ADDRESS");
    } else {
        sprintf(s, "%02x:%02x:%02x:%02x:%02x:%02x",
                0xff & b[0],
                0xff & b[1],
                0xff & b[2],
                0xff & b[3],
                0xff & b[4],
                0xff & b[5]);
    }

    return s;
}

/* print a mac address using the function fn and possibly stream f */
void mac_dprint_no_eoln(ddprintf_t *fn, FILE *f, mac_address_ptr_t b)
{
    if (b == NULL) {
        fn(f, "NULL_MAC_ADDRESS");
    } else {
        fn(f, "%02x:%02x:%02x:%02x:%02x:%02x",
                0xff & b[0],
                0xff & b[1],
                0xff & b[2],
                0xff & b[3],
                0xff & b[4],
                0xff & b[5]);
    }
}

/* print the mac address to stream f */
void mac_print_no_eoln(FILE *f, mac_address_ptr_t b)
{
    mac_dprint_no_eoln(fprint, f, b);
}

/* print mac address to stream f followed by linefeed */
void mac_print(FILE *f, mac_address_ptr_t b)
{
    mac_print_no_eoln(f, b);
    fprintf(f, "\n");
}

/* print a mac address using the function fn and possibly stream f,
 * followed by line feed
 */
void mac_dprint(ddprintf_t fn, FILE *f, mac_address_ptr_t b)
{
    mac_dprint_no_eoln(fn, f, b);
    fn(f, "\n");
}

/* discover the mac address associated with the device, which is something
 * like "eth0" etc.
 * return 0 on success, -1 on failure.
 * (execute "ifconfig" on the device and parse the result to find the
 * mac address)
 */
int mac_get(mac_address_t mac_address, char *device_name)
{
    char buf[256];
    char tmp_fname[256] = "";
    int result;
    int retval = 0;
    char *p;
    FILE *f = NULL;
    sprintf(buf, "/sbin/ifconfig %s > /tmp/mac_get.%d", device_name,
            (int) getpid());
    result = system(buf);
    if (result == -1) {
        fprintf(stderr, "mac_get:  system failed.\n");
        retval = -1;
        goto finish;
    }
    sprintf(tmp_fname, "/tmp/mac_get.%d", (int) getpid());
    f = fopen(tmp_fname, "r");
    if (f == NULL) {
        fprintf(stderr, "mac_get:  could not read %s.\n", tmp_fname);
        retval = -1;
        goto finish;
    }
    p = fgets(buf, 256, f);
    if (p == NULL) {
        fprintf(stderr, "mac_get:  could not read %s.\n", tmp_fname);
        retval = -1;
        goto finish;
    }
    p = strstr(buf, "HWaddr");
    if (p == NULL) {
        fprintf(stderr, "mac_get:  could not find HWaddr in %s.\n", buf);
        retval = -1;
        goto finish;
    }
    p += strlen("HWaddr");
    result = mac_sscanf(mac_address, p);
    if (result != 1) {
        fprintf(stderr, "mac_get:  mac_read failed.\n");
        retval = -1;
        goto finish;
    }

    finish:

    if (f != NULL) { fclose(f); }
    if (strlen(tmp_fname) > 0) {
        result = unlink(tmp_fname);
        if (result != 0) {
            fprintf(stderr, "mac_get:  unlink(%s) failed.\n", tmp_fname);
        }
    }

    return retval;
}
