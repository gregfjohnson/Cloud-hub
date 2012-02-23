/* nbr.c - track cloud hub neighbors that can be communicated with directly
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: nbr.c,v 1.8 2012-02-22 19:27:23 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: nbr.c,v 1.8 2012-02-22 19:27:23 greg Exp $";

#include <ctype.h>
#include <errno.h>
#include <string.h>

#include "util.h"
#include "cloud.h"
#include "print.h"
#include "device.h"
#include "nbr.h"

/* devices from which we are receiving beacons.
 * (regular beacons, not stp beacons.  this info is gotten from the wds
 * file and the eth_beacons file.)
 * nbr_device_list is the list of cloud boxes that we see directly,
 * either wirelessly or via our ethernet connection.
 */
cloud_box_t nbr_device_list[MAX_CLOUD];
int nbr_device_list_count = 0;

/* look through nbr_device_list[] array for an entry with "name" field
 * equal to "addr", and return the signal_strength field of that entry.
 * return 1 (fake very weak "signal strength") if not found.
 */
int get_sig_strength(mac_address_t addr)
{
    int result = 1;  /* default extremely weak signal */
    int i;

    for (i = 0; i < nbr_device_list_count; i++) {
        cloud_box_t *nbr = &nbr_device_list[i];
        if (!mac_equal(addr, nbr->name)) { continue; }
        result = nbr->signal_strength;
        break;
    }

    return result;
}

/* see which devices we are getting beacons from (both wlan beacons and
 * eth_beacons).  update nbr_device_list, an array of cloud_box_t's.
 * nbr_device_list is the list of cloud boxes that we see directly,
 * either wirelessly or via our ethernet connection.
 *
 * this routine also adds perm_io_stats for any and all cloud boxes that
 * are our neighbors based either on wireless beacons or eth beacons.
 * in the latter case, whether or not we can see them wirelessly we will
 * still put the right one true name in the perm_io_stat slot.
 *
 * carefully see if anything has changed, i.e., a new box is seen
 * wirelessly or via ethernet (or both), a box is now seen differently
 * from the way we used to see it, etc.  i.e., if we had previously only
 * been seeing a box via wireless, and we now also see it via ethernet,
 * note that as a change.
 *
 * return change status (true if something changed false otherwise).
 */
char check_nbr_devices()
{
    char return_value = 0;

    /* these guys are all indexed together.  could have made an array
     * of structs.
     */
    char wds_devices[MAX_CLOUD][64];
    mac_address_t wds_macs[MAX_CLOUD];
    mac_address_t eth_macs[MAX_CLOUD];
    bool_t has_eth[MAX_CLOUD];

    int wds_count = 0;

    int w, d;
    FILE *eth = NULL;
    FILE *wds;

    if (db[17].d) { ddprintf("check_nbr_devices start..\n"); }

    #ifdef WRT54G
        if (ad_hoc_mode) {
            wds = fopen(sig_strength_fname, "r");
        } else {
            wds = fopen(wds_file, "r");
        }
    #else
        wds = fopen(wds_file, "r");
    #endif

    if (wds == NULL) {
        ddprintf("check_nbr_devices; could not open %s:  %s\n",
                ad_hoc_mode ? sig_strength_fname : wds_file,
                strerror(errno));
        goto finish;
    }

    /* read the mac addresses of the wds devices into local array wds_devices */
    while (1) {
        int result;
        if (wds_count >= MAX_CLOUD) {
            ddprintf("check_nbr_devices:  too many wds devices\n");
            break;
        }

        if (skip_comment_line(wds) != 0) {
            ddprintf("check_nbr_devices; problem reading file.\n");
            goto finish;
        }

        #ifdef WRT54G
            result = mac_read(wds, wds_macs[wds_count]);
        #else
            result = fscanf(wds, "%s", wds_devices[wds_count]);
        #endif

        if (result != 1) { break; }

        #ifdef WRT54G
            result = fscanf(wds, "%s", wds_devices[wds_count]);
        #else
            result = mac_read(wds, wds_macs[wds_count]);
        #endif

        if (result != 1) {
            ddprintf("check_nbr_devices:  "
                    "could not read device name from wds\n");
            goto finish;
        }

        has_eth[wds_count] = false; /* will update this below */

        wds_count++;
    }

    /* read the ethernet entries into the nbr_device array.
     * update entries we found above (seen wirelessly), add entries only
     * seen via eth_beacons.
     */
    if (eth_device_name != 0) {

        eth = fopen(eth_fname, "r");
        if (eth == NULL) {
            ddprintf("check_eth_devices:  could not open %s\n",
                    eth_fname);
            goto finish;
        }

        while (1) {
            int found;
            mac_address_t eth_mac_addr, name;

            int result = mac_read(eth, eth_mac_addr);
            if (result != 1) { break; }

            result = mac_read(eth, name);
            if (result != 1) {
                ddprintf("check_nbr_devices:  invalid file '%s'\n",
                        eth_fname);
                goto finish;
            }

            found = 0;
            for (d = 0; d < wds_count; d++) {
                if (mac_equal(wds_macs[d], name)) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                if (wds_count >= MAX_CLOUD) {
                        ddprintf("check_nbr_devices:  "
                                "too many cloud boxes\n");
                        goto finish;
                }
                mac_copy(wds_macs[wds_count], name);
                strcpy(wds_devices[wds_count], "(no wds interface)");
                d = wds_count;
                wds_count++;
            }

            mac_copy(eth_macs[d], eth_mac_addr);

            has_eth[d] = true;
        }
    }

    if (db[17].d) {
        print_cloud_list(nbr_device_list, nbr_device_list_count,
                "neighbor devices before trimming them:");

        ddprintf("\nfrom wds and eth files:\n");
        for (d = 0; d < wds_count; d++) {
            ddprintf("    %s ", wds_devices[d]);
            mac_dprint_no_eoln(eprintf, stderr, wds_macs[d]);
            if (has_eth[d]) {
                ddprintf("; eth mac ");
                mac_dprint_no_eoln(eprintf, stderr, eth_macs[d]);
            }
            ddprintf("\n");
        }
    }

    /* see if every wds device in our neighbor list matches current truth.
     * delete those elements of nbr_device_list that are not in the wds 
     * or eth file.
     *
     * (we redo ethernet neighbor status below, both updating ethernet
     * connectivity knowledge to wireless neighbors, and adding neighbors
     * we only see via ethernet.  this routine updates data structures
     * to take into account any recent changes in connectivity.)
     */
    top:
    for (d = 0; d < nbr_device_list_count; d++) {
        int found = 0;

        for (w = 0; w < wds_count; w++) {
            if (mac_equal(nbr_device_list[d].name, wds_macs[w])) {
                found = 1;
                break;
            }
        }

        if (!found) {
            int i;

            for (i = d; i < nbr_device_list_count - 1; i++) {
                nbr_device_list[i] = nbr_device_list[i + 1];
            }
            nbr_device_list_count--;
            if (db[3].d) {
                print_cloud_list(nbr_device_list, nbr_device_list_count,
                        "neighbor devices:");
            }

            /* something changed */
            return_value = 1;
            if (db[17].d) {
                ddprintf("check_nbr_devices change 1 %d\n", (int) return_value);
            }
            goto top;
        }
    }

    if (db[17].d) {
        print_cloud_list(nbr_device_list, nbr_device_list_count,
                "neighbor devices after trimming them:");
    }

    /* add neighbor wds devices that need to be added */
    for (w = 0; w < wds_count; w++) {
        int found = 0;

        for (d = 0; d < nbr_device_list_count; d++) {
            if (mac_equal(nbr_device_list[d].name, wds_macs[w])) {
                found = 1;
                break;
            }
        }

        if (!found) {
            cloud_box_t *nbr;

            if (nbr_device_list_count >= MAX_CLOUD) {
                ddprintf("check_nbr_devices:  too many neighbors\n");
                continue;
            }
            mac_copy(nbr_device_list[nbr_device_list_count].name,
                    wds_macs[w]);

            /* turns out this will add a perm_io_stat entry for boxes we
             * only see via eth, which is just fine.  they get the right
             * one true name.
             */
            nbr = &nbr_device_list[nbr_device_list_count];
            add_perm_io_stat_index(&nbr->perm_io_stat_index, nbr->name,
                    device_type_wds);

            d = nbr_device_list_count;

            nbr_device_list_count++;

            /* something changed */
            return_value = 1;

            if (db[17].d) {
                ddprintf("check_nbr_devices change 2 %d\n", (int) return_value);
            }

            if (db[3].d) {
                print_cloud_list(nbr_device_list, nbr_device_list_count,
                        "neighbor devices:");
            }
        }

        if (has_eth[w] != nbr_device_list[d].has_eth_mac_addr) {

            /* something changed */
            return_value = 1;

            if (has_eth[w]) {
                nbr_device_list[d].has_eth_mac_addr = 1;
                mac_copy(nbr_device_list[d].eth_mac_addr, eth_macs[w]);
            } else {
                nbr_device_list[d].has_eth_mac_addr = 0;
            }
        }
    }

    if (db[17].d) {
        print_cloud_list(nbr_device_list, nbr_device_list_count,
                "neighbor devices after adding to them based on the wds file:");
    }

    finish :

    if (wds != NULL) { fclose(wds); }
    if (eth != NULL) { fclose(eth); }

    if (db[17].d) {
        ddprintf("check_nbr_devices returns %d\n", (int) return_value);
    }

    return return_value;

} /* check_nbr_devices */

/* open the file sig_strength_fname, read signal strength values from there,
 * and update nbr_device_list[i].signal_strength based on that.
 */
void update_nbr_signal_strength()
{
    int i;

    char buf[1024], *p;
    int result;
    FILE *ap;

    /* init every neighbor that has an ethernet connection to us with
     * max signal strength out here, in case we aren't on the same channel
     * and so don't find them in signal_strength file.
     */
    for (i = 0; i < nbr_device_list_count; i++) {
        if (nbr_device_list[i].has_eth_mac_addr) {
            nbr_device_list[i].signal_strength = max_sig_strength;
        }
    }

    ap = fopen(sig_strength_fname, "r");
    if (ap == NULL) {
        ddprintf("update_nbr_signal_strength; could not open ap file:  %s\n",
                strerror(errno));
        return;
    }

    #ifndef WRT54G
    p = fgets(buf, 1024, ap);
    if (p == NULL) {
        // ddprintf("update_nbr_signal_strength:  no first line\n");
        goto done;
    }
    #endif

    while (1) {
        char found;
        mac_address_t mac_addr;
        int signal;
        #ifndef WRT54G
        int chan, noise, rate;
        #endif

        p = fgets(buf, 1024, ap);
        if (p == NULL) { break; }

        /* if line is blank or first non-blank char on the line is '#', 
         *skip the line.
         */
        while (*p && isspace(*p)) { p++; }
        if (*p == '\0' || *p == '#') { continue; }

        result = mac_sscanf(mac_addr, buf);
        if (result != 1) {
            ddprintf("update_nbr_signal_strength:  "
                    "invalid mac address on line %s\n", buf);
            continue;
        }

        /* get into mac address by looking for ':', then find first space */
        p = strchr(buf, (int) ':');
        if (p == NULL || (p = strchr(p, (int) ' ')) == NULL) {
            ddprintf("update_nbr_signal_strength:  "
                    "no space after mac address; invalid line %s\n", buf);
            continue;
        }

        #ifdef WRT54G
            result = sscanf(p, "%d", &signal);
            if (result != 1)
        #else
            result = sscanf(p, "%d %d %d %d", &chan, &signal, &noise, &rate);
            if (result != 4)
        #endif
        {

            ddprintf("update_nbr_signal_strength:  "
                    "no int signal strength found; invalid line %s\n", buf);
            continue;
        }

        found = 0;
        for (i = 0; i < nbr_device_list_count; i++) {
            if (mac_equal(nbr_device_list[i].name, mac_addr)) {
                found = 1;
                break;
            }
        }
        if (found) {
            if (nbr_device_list[i].has_eth_mac_addr) {
                nbr_device_list[i].signal_strength = max_sig_strength;
            } else {
                nbr_device_list[i].signal_strength = signal;
            }
        }
    }

    #ifndef WRT54G
        done:
    #endif

    if (ap != NULL) { fclose(ap); }

} /* update_nbr_signal_strength */

/* if the nbr_device_list changed (particularly if our ethernet
 * connectivity to a box changed), update stp_list to reflect that change.
 */
void update_stp_nbr_connectivity()
{
    int nbr_ind, stp_ind;

    for (stp_ind = 0; stp_ind < stp_list_count; stp_ind++) {
        cloud_box_t *stp = &stp_list[stp_ind].box;

        for (nbr_ind = 0; nbr_ind < nbr_device_list_count; nbr_ind++) {
            cloud_box_t *nbr = &nbr_device_list[nbr_ind];

            if (!mac_equal(stp->name, nbr->name)) { continue; }

            if (stp->has_eth_mac_addr != nbr->has_eth_mac_addr) {
                ddprintf("update_stp_nbr_connectivity; "
                        "eth connectivity changed.\n");
                stp->has_eth_mac_addr = nbr->has_eth_mac_addr;
                if (nbr->has_eth_mac_addr) {
                    mac_copy(stp->eth_mac_addr, nbr->eth_mac_addr);
                }
            }
        }
    }
}

/* we got a message in from another cloud box via device "device_index",
 * from interface "mac_addr" on the other box.
 * find the "name" (i.e., wlan0 mac address) of the box that sent the message.
 */
mac_address_ptr_t get_name(int device_index, mac_address_t mac_addr)
{
    device_t *dp;
    int i;

    if (device_index < 0 || device_index >= device_list_count) {
        ddprintf("get_name:  bad device_index %d.  devices:\n", device_index);
        print_devices();
        return 0;
    }

    dp = &device_list[device_index];

    if (dp->device_type == device_type_eth) {
        for (i = 0; i < nbr_device_list_count; i++) {
            if (!nbr_device_list[i].has_eth_mac_addr) { continue; }
            if (mac_equal(mac_addr, nbr_device_list[i].eth_mac_addr)) {
                return nbr_device_list[i].name;
            }
        }

        ddprintf("get_name:  could not find other eth device ");
        mac_dprint(eprintf, stderr, mac_addr);
        print_cloud_list(nbr_device_list, nbr_device_list_count,
                "neighbor list:");

        return 0;
    }

    if (dp->device_type == device_type_wds
        || (ad_hoc_mode && dp->device_type == device_type_ad_hoc))
    {
        /* check to see if we know of a wds device whose name is mac_addr */
        for (i = 0; i < nbr_device_list_count; i++) {
            if (mac_equal(mac_addr, nbr_device_list[i].name)) {
                return nbr_device_list[i].name;
            }
        }

        ddprintf("get_name:  could not find other wlan device ");
        mac_dprint(eprintf, stderr, mac_addr);
        print_cloud_list(nbr_device_list, nbr_device_list_count,
                "neighbor list:");

        return 0;
    }

    ddprintf("get_name:  tried to get name of invalid device_type %d\n",
            dp->device_type);

    return 0;

} /* get_name */
