/* cloud_box.c - utility routines to manage lists of cloud boxes
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: cloud_box.c,v 1.8 2012-02-22 19:27:22 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: cloud_box.c,v 1.8 2012-02-22 19:27:22 greg Exp $";

#include <string.h>

#include "nbr.h"
#include "print.h"
#include "cloud_box.h"

/* debug-print the cloud box, and if we think it is a neighbor
 * (i.e., if we can directly see 802.11 beacons from it) print out
 * the signal strength to it.
 */
void cloud_box_print(ddprintf_t *fn, FILE *f, cloud_box_t *cloud_box, int indent)
{
    int i;
    char found = 0;

    print_space(fn, f, indent);
    fn(f, "one true name (wifi interface):  ");
    mac_dprint_no_eoln(fn, f, cloud_box->name);

    for (i = 0; i < nbr_device_list_count; i++) {
        if (mac_equal(cloud_box->name, nbr_device_list[i].name)) {
            found = 1;
            break;
        }
    }
    if (found) {
        fn(f, "; strength %d", nbr_device_list[i].signal_strength);
    } else {
        fn(f, "; (no signal strength)");
    }
    if (cloud_box->has_eth_mac_addr) {
        fn(f, "; eth address ");
        mac_dprint_no_eoln(fn, f, cloud_box->eth_mac_addr);
    }

    fn(f, "\n");
}

/* print a table of the nodes in list using the print function.
 * use type-specific method to print each node.
 */
void node_list_print(ddprintf_t *fn, FILE *f, char *title,
        node_t *list, int list_count)
{
    int i;
    struct timeval tv;
    long age;
    if (!checked_gettimeofday(&tv)) { memset(&tv, 0, sizeof(tv)); }

    fn(f, "%s\n", title);

    for (i = 0; i < list_count; i++) {
        node_t *node = &list[i];
        age = usec_diff(tv.tv_sec, tv.tv_usec, node->sec, node->usec) / 1000;
        if (node->type == node_type_cloud_box) {
            cloud_box_print(fn, f, &node->box, 4);
        }
    }
}

/* print array of cloud boxes.  this is usually called with "nbr_device_list",
 * the boxes that we can see 802.11 beacons from directly
 */
void print_cloud_list(cloud_box_t *list, int list_len, char *title)
{
    int i;

    ddprintf("%s\n", title);
    for (i = 0; i < list_len; i++) {
        cloud_box_print(eprintf, stderr, &list[i], 4);
    }
}

/* get rid of every entry that doesn't satisfy the predicate. */
void trim_list(node_t *list, int *list_count, node_predicate_t predicate,
        node_finalizer_t finalizer)
{
    int past_packed = 0;
    int next;

    /* invariant:  [0 .. past_packed) are keepable, packed, and done.
     * [past_packed .. next) can be overwritten.
     * [next .. list_count) are yet to be processed and as originally
     */
    for (next = 0; next < *list_count; next++) {
        node_t *l = &list[next];

        if (predicate(l)) {
            /* keep it. */
            if (db[3].d) { ddprintf("accepting entry\n"); }
            if (past_packed != next) {
                list[past_packed] = *l;
            }
            past_packed++;
        } else {
            /* delete it. */
            if (db[3].d) { ddprintf("deleting entry\n"); }
            if (finalizer != NULL) {
                finalizer(l);
            }
        }
    }

    *list_count = past_packed;
}

mac_address_ptr_t delete_me;

/* accept this node iff it is not equal to "delete_me". */
char delete_node(node_t *node)
{
    return (!mac_equal(node->box.name, delete_me));
}
