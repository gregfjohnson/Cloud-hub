/* scan.c - reformat reports of local wifi activity
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: scan.c,v 1.5 2012-02-22 18:55:25 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: scan.c,v 1.5 2012-02-22 18:55:25 greg Exp $";

/* a small utility program that reads the output of "wl scanresults", which
 * reports on other wifi activity in the area, and reformats it into a
 * pipe-delimited file.
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "util.h"

/* assume the string is of the form "blah field_name: field space*"
 * put the field into out.  (don't exceed out_len).
 * return true iff we found the field.
 * null-terminate the input string at the beginning of field_name.
 */
static bool_t get_field(char *in, char *field_name, char *out, int out_len)
{
    char *p, *q;
    if ((p = strstr(in, field_name)) == NULL) { return false; }
    q = in + strlen(in) - 1;
    *p = '\0';
    p += strlen(field_name);
    while (*p && isspace(*p)) { p++; }
    while (q >= p && isspace(*q)) { q--; }
    q++;
    *q = '\0';
    strncpy(out, p, out_len-1);
    out[out_len-1] = '\0';

    return true;
}

int main(int argc, char **argv)
{
    char buf[256];
    char ssid[256], mode[256], rssi[256], noise[256], channel[256];
    bool_t have_ssid = false;
    int irssi, inoise;

    while (fgets(buf, 256, stdin)) {
        if (!have_ssid) {
            if (get_field(buf, "SSID: \"", ssid, 256)) {
                if (strlen(ssid) > 0) {
                    ssid[strlen(ssid) - 1] = '\0';
                }
                have_ssid = true;
            }
            continue;
        }

        if (!get_field(buf, "Channel:", channel, 256)) {
            have_ssid = false;
            continue;
        }

        if (!get_field(buf, "noise:", noise, 256)) {
            have_ssid = false;
            continue;
        }
        if (sscanf(noise, "%d", &inoise) != 1) {
            have_ssid = false;
            continue;
        }

        if (!get_field(buf, "RSSI:", rssi, 256)) {
            have_ssid = false;
            continue;
        }
        if (sscanf(rssi, "%d", &irssi) != 1) {
            have_ssid = false;
            continue;
        }


        if (!get_field(buf, "Mode:", mode, 256)) {
            have_ssid = false;
            continue;
        }
        printf("%s|%s|%d|%s|%s|%s\n", ssid, mode, irssi - inoise, rssi, noise,
                channel);
        have_ssid = false;
    }

    return 0;
}
