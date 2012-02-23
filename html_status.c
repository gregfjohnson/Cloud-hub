/* html_status.c - print an html file showing the status of the whole cloud
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: html_status.c,v 1.13 2012-02-22 19:27:22 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: html_status.c,v 1.13 2012-02-22 19:27:22 greg Exp $";

/* print an html file showing the status of the whole cloud.
 *
 * the file will contain an ascii-art rendering of the stp tree for the
 * whole cloud, and two matrices giving packet counts and signal strengths
 * between boxes in the cloud.
 */

#include <signal.h>
#include <errno.h>
#include <string.h>

#include "util.h"
#include "cloud.h"
#include "print.h"
#include "graphit.h"
#include "timer.h"
#include "html_status.h"

/* this is a set of work data structures used while updating cloud_stp_list */
static message_t new_stp_list[MAX_CLOUD];
static int new_stp_child_count[MAX_CLOUD], new_stp_child_start[MAX_CLOUD];
static char new_node_names[MAX_CLOUD][20];
static int new_stp_list_count;

/* this is the whole cloud, based on stp_beacons received.
 * it describes the stp topology of the cloud.
 *
 * it is the spanning tree of the cloud based on the most recent stp
 * beacons received from nodes in the cloud.
 *
 * nodes we are not stp-connected to are not in cloud_stp_list.
 *
 * a claimed arc in the spanning tree must have two valid links
 * (two stp beacons where each is in the status list of the other).
 *
 * ad-hoc clients are also represented in these data structures.
 *
 * it is required that cloud_stp_list[i], node_names[i], and
 * cloud_stp_tree[i] refer to the same cloud node.
 */
static message_t cloud_stp_list[MAX_CLOUD + 1];
static int cloud_stp_child_count[MAX_CLOUD], cloud_stp_child_start[MAX_CLOUD];
static char node_names[MAX_CLOUD][20];
static int cloud_stp_list_count = 0;

/* used to build graphical representation of stp tree */
graphit_node_t cloud_stp_tree[MAX_CLOUD];

/* the array of cloud boxes describes a spanning tree, with children of
 * each node in a contiguous block in the array.  print each cloud box
 * and the index range of its children.
 */
static void dprint_db_cloud_stats(ddprintf_t *fn, FILE *f, message_t *cloud,
    int *child_start, int *child_count, int cloud_count)
{
    int i;

    for (i = 0; i < cloud_count; i++) {
        mac_dprint_no_eoln(fn, f, cloud[i].v.stp_beacon.originator);
        ddprintf(" child start %d; child count %d\n",
                child_start[i], child_count[i]);
    }
}

/* debug-print the list of cloud boxes that we think are in our cloud.
 * for each one, print its wireless mac address, and a list of
 * packets received and lost to other boxes.  so, this is a quadratic
 * output in the size of the cloud.
 */
void db_print_cloud_stp_list(ddprintf_t *fn, FILE *f)
{
    int i, j;

    fn(f, "db_print_cloud_stp_list; cloud_stp_list_count %d\n",
            cloud_stp_list_count);

    for (i = 0; i < cloud_stp_list_count; i++) {
        stp_beacon_t *beacon = &cloud_stp_list[i].v.stp_beacon;
        mac_dprint_no_eoln(fn, f, beacon->originator);
        fn(f, "; status count %d\n", beacon->status_count);

        for (j = 0; j < beacon->status_count; j++) {
            status_t *s = &cloud_stp_list[i].v.stp_beacon.status[j];
            int packets_received = s->packets_received
                    /*+ s->cloud_packets_received */;
            int packets_lost = s->packets_lost + s->data_packets_lost;
            fn(f, "    %s:  ", device_type_string(s->device_type));
            mac_dprint_no_eoln(fn, f, s->name);
            fn(f, "; recd %d, lost %d; nbr_type _d", packets_received,
                    packets_lost, s->neighbor_type);
        }
        fn(f, "\n");
    }
} /* db_print_cloud_stp_list */

/* print the cloud boxes in our cloud, and for each one print
 * the boxes it can see 802.11 beacons for, i.e., its wireless neighbors.
 */
void dprint_cloud_stats_short(ddprintf_t *fn, FILE *f)
{
    bool_t first = true;
    int i, j;
    int sent = 0, dropped = 0;
    float fraction;

    for (i = 0; i < cloud_stp_list_count; i++) {
        if (first) {
            first = false;
        } else {
            fn(f, "\n");
        }
        mac_dprint_no_eoln(fn, f, cloud_stp_list[i].v.stp_beacon.originator);

        for (j = 0; j < cloud_stp_list[i].v.stp_beacon.status_count; j++) {
            // status_t *s = &cloud_stp_list[i].v.stp_beacon.status[j];
            // sent += s->noncloud_packets_sent;
            // dropped += s->dropped_noncloud_packets;
        }

        if (sent == 0 && dropped == 0) {
            fraction = 1.;
        } else {
            fraction = 1. - ((float) dropped) / (float) (dropped + sent);
        }

        fn(f, "; %.1f percent of packets delivered; stp connections:\n",
                fraction * 100.);

        for (j = 0; j < cloud_stp_list[i].v.stp_beacon.status_count; j++) {
            status_t *s = &cloud_stp_list[i].v.stp_beacon.status[j];
            if (!mac_equal(s->name,
                    cloud_stp_list[i].v.stp_beacon.originator)
                && s->device_type != device_type_wlan
                && s->device_type != device_type_eth)
            {
                fn(f, "    ");
                mac_dprint(fn, f, s->name);
            }
        }
    }
}

/* print our model of the current topology of the cloud */
void dprint_cloud_stats(ddprintf_t *fn, FILE *f)
{
    bool_t first = true;
    int i, j;

    // status_dprint_title(fn, f, strlen("                        "));

    for (i = 0; i < cloud_stp_list_count; i++) {
        if (first) {
            first = false;
        } else {
            fn(f, "\n");
        }
        mac_dprint(fn, f, cloud_stp_list[i].v.stp_beacon.originator);

        fn(f, "stp connections:\n");
        for (j = 0; j < cloud_stp_list[i].v.stp_beacon.status_count; j++) {
            status_t *s = &cloud_stp_list[i].v.stp_beacon.status[j];
            if (s->device_type == device_type_wds
                && s->neighbor_type == STATUS_CLOUD_NBR
                && !mac_equal(s->name,
                    cloud_stp_list[i].v.stp_beacon.originator))
            {
                fn(f, "    ");
                mac_dprint(fn, f, s->name);
            }
        }

        fn(f, "outgoing arc values:\n");
        status_dprint_short_array(fn, f,
                cloud_stp_list[i].v.stp_beacon.status,
                cloud_stp_list[i].v.stp_beacon.status_count);
    }
}

/* for pretty-printing the cloud as we see it */
void init_cloud_stp_tree()
{
    graphit_node_t *node = &cloud_stp_tree[0];
    mac_sprintf(node_names[0], my_wlan_mac_address);
    node->node_name = node_names[0];
    node->child_count = 0;
}

/* create graphit_node_t representation of the cloud so that we can
 * pretty print the graph when we need to.
 */
static void build_cloud_stp_tree()
{
    int i, j;

    if (db[32].d) {
        ddprintf("node_names before:\n");
        for (i = 0; i < cloud_stp_list_count; i++) {
            ddprintf("    ");
            for (j = 0; j < 20; j++) {
                if (!node_names[i][j]) { break; }
                ddprintf("%c", node_names[i][j]);
            }
            ddprintf("\n");
        }
    }

    for (i = 0; i < cloud_stp_list_count; i++) {
        graphit_node_t *node = &cloud_stp_tree[i];
        // mac_sprintf(node_names[i],
                // cloud_stp_list[i].v.stp_beacon.originator);

        /* just to be safe */
        node_names[i][19] = '\0';

        node->node_name = node_names[i];
        node->child_count = cloud_stp_child_count[i];
        for (j = 0; j < cloud_stp_child_count[i]; j++) {
            node->child[j] = &cloud_stp_tree[cloud_stp_child_start[i] + j];
        }
    }

    if (db[32].d) {
        ddprintf("node_names after:\n");
        for (i = 0; i < cloud_stp_list_count; i++) {
            ddprintf("    ");
            for (j = 0; j < 20; j++) {
                if (!node_names[i][j]) { break; }
                ddprintf("%c", node_names[i][j]);
            }
            ddprintf("\n");
        }
    }
} /* build_cloud_stp_tree */

/* see if mac address "name" is the originator field of an element of
 * the array "new_stp_list".
 */
static bool_t in_mac_list(message_t *new_stp_list, int new_stp_list_count,
        mac_address_t name)
{
    int i;

    for (i = 0; i < new_stp_list_count; i++) {
        if (mac_equal(new_stp_list[i].v.stp_beacon.originator, name)) {
            return true;
        }
    }

    return false;
}

/* see if mac address "src" is the "name" field of any element in the
 * status array of the stp beacon.
 */
static bool_t in_status_list(mac_address_t src, stp_beacon_t *stp_beacon)
{
    int i;

    for (i = 0; i < stp_beacon->status_count; i++) {
        if (mac_equal(stp_beacon->status[i].name, src)) {
            return true;
        }
    }

    return false;
}

/* we have an arc <src -> dest>.  wish to see if <dest -> src> is also
 * valid.  we do this by finding a node whose originator is dest, and
 * which has src among the names in its status records.
 * valid places to look for dest are my_beacon (we never get here or
 * try this if we don't yet have a beacon), beacon, as long as have_beacon
 * is true, and entries in cloud_stp_list that are not shadowed by
 * those first two options.
 */
static bool_t has_valid_back_pointer(mac_address_t src, mac_address_t dest)
{
    int i;

    if (mac_equal(dest, my_wlan_mac_address)) {
        return in_status_list(src, &my_beacon.v.stp_beacon);

    } else if (have_beacon
        && mac_equal(dest, beacon.v.stp_beacon.originator))
    {
        return in_status_list(src, &beacon.v.stp_beacon);
    }

    /* else, look through cloud_stp_list */

    for (i = 0; i < cloud_stp_list_count; i++) {
        if (mac_equal(dest, cloud_stp_list[i].v.stp_beacon.originator)) {
            return in_status_list(src, &cloud_stp_list[i].v.stp_beacon);
        }
    }

    return false;
}

/* add children of new_stp_list[ind].  we do this by looking at
 * the status array of new_stp_list[ind].  for each of those, we see
 * if we have a valid back pointer, based on my_beacon, beacon if we have
 * a new one, or cloud_stp_list.  for valid dest nodes we find, see which
 * ones are not yet in new_stp_list, and add those as children.  the
 * new_stp_list entries we copy will be beacon if we have it and the
 * dest matches beacon's originator field, or from cloud_stp_list, our
 * old version of this data structure that contains most recent beacons
 * from all nodes we were connected to the last time this set of routines
 * was run.
 */
static void add_children(int ind)
{
    int i, j;
    int prev_new_stp_list_count, next_new_stp_list_count;
    stp_beacon_t *node = &new_stp_list[ind].v.stp_beacon;
    char buf[20];

    if (db[32].d) { ddprintf("add_children..\n"); }

    prev_new_stp_list_count = new_stp_list_count;

    new_stp_child_start[ind] = new_stp_list_count;
    new_stp_child_count[ind] = 0;

    mac_sprintf(new_node_names[ind], node->originator);
    if (db[32].d) {
        ddprintf("added new_node_names[%d]:  %s\n", ind,
                mac_sprintf(buf, node->originator));
    }

    for (i = 0; i < node->status_count; i++) {
        status_t *s = &node->status[i];
        message_t *new_node = NULL;

        if (s->device_type != device_type_wds) { continue; }

        if (s->neighbor_type != STATUS_CLOUD_NBR) { continue; }

        if (in_mac_list(new_stp_list, new_stp_list_count, s->name)) {
            continue;
        }

        if (!has_valid_back_pointer(node->originator, s->name)) {
            continue;
        }

        if (have_beacon
            && mac_equal(s->name, beacon.v.stp_beacon.originator))
        {
            new_node = &beacon;

        } else {
            for (j = 0; j < cloud_stp_list_count; j++) {
                if (mac_equal(s->name,
                    cloud_stp_list[j].v.stp_beacon.originator))
                {
                    new_node = &cloud_stp_list[j];
                    break;
                }
            }
        }

        if (new_node == NULL || new_stp_list_count >= MAX_CLOUD) { break; }

        new_stp_list[new_stp_list_count] = *new_node;
        new_stp_child_count[new_stp_list_count] = 0;
        new_stp_list_count++;

        new_stp_child_count[ind]++;

        if (db[32].d) {
            ddprintf("add ");
            mac_dprint(eprintf, stderr,
                    new_stp_list[new_stp_list_count - 1]
                            .v.stp_beacon.originator);
        }
    }

    next_new_stp_list_count = new_stp_list_count;

    /* add any ad-hoc clients that this box serves */
    for (i = 0; i < node->status_count; i++) {
        status_t *s = &node->status[i];
        char buf[20];

        if (s->neighbor_type != STATUS_NON_CLOUD_CLIENT) { continue; }

        if (in_mac_list(new_stp_list, new_stp_list_count, s->name))
        { continue; }

        if (new_stp_list_count >= MAX_CLOUD) { break; }

        new_stp_child_count[ind]++;

        /* not really necessary since we now init new_node_names[i] here */
        mac_copy(new_stp_list[new_stp_list_count].v.stp_beacon.originator,
                s->name);

        new_stp_child_count[new_stp_list_count] = 0;

        sprintf(new_node_names[new_stp_list_count], "(%s)",
                mac_sprintf(buf, s->name));

        new_stp_list_count++;
    }

    for (i = prev_new_stp_list_count; i < next_new_stp_list_count; i++) {
        add_children(i);
    }

    if (db[32].d) {
        ddprintf("done add_children(%d)..\n", ind);
        dprint_db_cloud_stats(eprintf, stderr, new_stp_list,
                new_stp_child_start, new_stp_child_count,
                new_stp_list_count);
    }
}

/* we have a freshly built stp beacon we've just constructed based on an
 * up-to-date stp_list indicating our stp neighbors, or we've just received
 * an stp beacon from another node indicating their freshly built up-to-date
 * stp list indicating their stp neighbors.
 *
 * combine that with our historical knowledge of the overall cloud based
 * the current cloud_stp_list.  update cloud_stp_list based on that.
 *
 * we have an existing cloud_stp_list describing our current model of
 * the overall cloud, i.e., the current spanning tree.  we don't tear
 * down the tree because of timed out stp beacons, because that can
 * happen probabilistically and it is in fact a disruption to the stream
 * of bits traveling across the stp tree if we have to tear apart the tree
 * and rebuild it.  
 */
void build_stp_list()
{
    int i;

    if (db[32].d) {
        ddprintf("build_stp_list start.\n");
        db_print_cloud_stp_list(eprintf, stderr);
        if (have_beacon) {
            ddprintf("beacon ");
            mac_dprint(eprintf, stderr, beacon.v.stp_beacon.originator);
            for (i = 0; i < beacon.v.stp_beacon.status_count; i++) {
                ddprintf("    ");
                mac_dprint(eprintf,
                        stderr, beacon.v.stp_beacon.status[i].name);
            }
        } else {
            ddprintf("no beacon.\n");
        }

        ddprintf("cloud_stp_list at start..\n");
        dprint_db_cloud_stats(eprintf, stderr, cloud_stp_list,
                cloud_stp_child_start, cloud_stp_child_count,
                cloud_stp_list_count);
        ddprintf("done cloud_stp_list at start..\n");
    }

    new_stp_list_count = 0;

    if (!have_my_beacon) {
        ddprintf("don't have_my_beacon\n");
        goto done;
    }

    if (db[32].d) {
        ddprintf("add my_beacon ");
        mac_dprint(eprintf, stderr, my_beacon.v.stp_beacon.originator);
    }

    /* add my beacon first.  i.e., I am the root of the tree I am building */
    new_stp_list[0] = my_beacon;
    new_stp_child_start[0] = 1;
    new_stp_list_count = 1;

    add_children(0);

    /* done creating new tree; copy it to cloud_stp_list. */
    for (i = 0; i < new_stp_list_count; i++) {
        cloud_stp_list[i] = new_stp_list[i];
        cloud_stp_child_start[i] = new_stp_child_start[i];
        cloud_stp_child_count[i] = new_stp_child_count[i];
        strcpy(node_names[i], new_node_names[i]);
    }
    cloud_stp_list_count = new_stp_list_count;

    have_beacon = false;

    /* call the routine that sets up a version of the tree appropriate for
     * graphit.
     */
    build_cloud_stp_tree();

    done :
    if (db[32].d) {
        ddprintf("done.\n");
        dprint_db_cloud_stats(eprintf, stderr, cloud_stp_list,
                cloud_stp_child_start, cloud_stp_child_count,
                cloud_stp_list_count);
        dprint_cloud_stats(eprintf, stderr);
        db[32].d = false;
    }
} /* build_stp_list */

/* the mac addresses for rows and columns of the matrices */
static mac_address_t mac_list[MAX_CLOUD];
static int mac_count;

/* put mac into mac_list if it's not already there. */
static void add_mac_addr(mac_address_t mac)
{
    int i;

    for (i = 0; i < mac_count; i++) {
        if (mac_equal(mac, mac_list[i])) {
            ddprintf("add_mac_addr warning: duplicat mac addresses.\n");
            // return;
        }
    }

    if (mac_count >= MAX_CLOUD) {
        ddprintf("add_mac_addr; too many mac addresses.\n");
        return;
    }

    mac_copy(mac_list[mac_count++], mac);
}

/* write an html page to /tmp/cloud.tmp giving an ascii-art representation
 * of the entire cloud, and two matrices giving packets received and lost,
 * and signal strengths among the boxes in the cloud.
 */
void do_print_cloud(void)
{
    int i, j, k, r;

    FILE *f = fopen("/tmp/cloud.tmp", "w");
    if (f == NULL) {
        ddprintf("do_print_cloud; could not open /tmp/cloud.tmp:  %s\n",
                strerror(errno));
        goto done;
    }

    fprintf(f, "<html>\n");
    fprintf(f, "<head>\n");
    fprintf(f, "<SCRIPT language=JavaScript>\n");
    fprintf(f, "function init()\n");
    fprintf(f, "{\n");
    fprintf(f, "    x = '<%c set_merge_cloud_db(\"d 2038\"); %c>';\n", '%',
            '%');
    fprintf(f, "}\n");
    fprintf(f, "</SCRIPT>\n");
    fprintf(f, "<META HTTP-EQUIV=\"refresh\" CONTENT=\"5\">\n");
    fprintf(f, "</head>\n");
    fprintf(f, "<body onload=init()>\n");
    fprintf(f, "<p>\n");

    fprintf(f, "<h3>\n");
    fprintf(f, "Cloud connectivity\n");
    fprintf(f, "</h3>\n");

    fprintf(f, "<pre>\n");
    graphit_dprint(eprintf, f, &cloud_stp_tree[0]);

    fprintf(f, "</pre>\n");

    if (!db[38].d) { goto almost_done; }

    /* add all of the cloud_stp_list entries to mac_list, a temporary
     * array giving mac addresses of rows and columns.
     */
    mac_count = 0;

    for (i = 0; i < cloud_stp_list_count; i++) {
        stp_beacon_t *beacon = &cloud_stp_list[i].v.stp_beacon;
        add_mac_addr(beacon->originator);
    }

    /* do the two matrices (packets sent and signal strength) */
    for (r = 0; r < 2; r++) {
        /* print the title of the packet matrix */
        fprintf(f, "<p>\n");
        fprintf(f, "<h3>\n");

        if (r == 0) {
            fprintf(f, "Packets received, packets dropped, "
                    "percent packets received\n");
            fprintf(f, "<br>\n");
            fprintf(f, "(sender at top of column, receiver at start of row)\n");
        } else {
            fprintf(f, "Signal strength\n");
            fprintf(f, "<br>\n");
            fprintf(f, "(how strongly each row entry sees each column entry)\n"
            );
        }

        fprintf(f, "</h3>\n");

        if (cloud_stp_list_count <= 1) { goto almost_done; }

        fprintf(f, "<table frame=box rules=all>\n");

        /* top row; empty first cell, then mac addresses */
        fprintf(f, "    <tr>\n");
        fprintf(f, "        <td> </td>");

        for (j = 0; j < mac_count; j++) {
            /* skip ad-hoc clients for column headers of first matrix */
            if (r == 0 && node_names[j][0] == '(') { continue; }

            fprintf(f, " <td> ");
            //mac_dprint_no_eoln(eprintf, f, mac_list[j]);
            fprintf(f, "%s", node_names[j]);
            fprintf(f, " </td> ");
        }
        fprintf(f, "\n");
        fprintf(f, "    </tr>\n");

        for (i = 0; i < mac_count; i++) {
            stp_beacon_t *src = NULL;

            /* for row entries, ignore ad-hoc clients */
            if (node_names[i][0] == '(') { continue; }

            for (j = 0; j < cloud_stp_list_count; j++) {
                stp_beacon_t *beacon = &cloud_stp_list[j].v.stp_beacon;
                if (mac_equal(mac_list[i], beacon->originator)) {
                    src = beacon;
                    break;
                }
            }
            fprintf(f, "    <tr>\n");

            fprintf(f, "        <td> ");
            // mac_dprint_no_eoln(eprintf, f, mac_list[i]);
            fprintf(f, "%s", node_names[i]);
            fprintf(f, " </td> ");

            for (j = 0; j < mac_count; j++) {
                bool_t did_something = false;

                /* for column entries, ignore ad-hoc clients in first matrix */
                if (r == 0 && node_names[j][0] == '(') { continue; }

                if (src != NULL) {
                    for (k = 0; k < src->status_count; k++) {
                        status_t *s = &src->status[k];

                        if (s->device_type != device_type_wds) { continue; }

                        if (mac_equal(mac_list[j], s->name)) {
                            if (r == 0) {
                                float den = s->packets_received
                                        + s->packets_lost
                                        + s->ping_packets_received
                                        + s->ping_packets_lost;
                                int percent;
                                if (den == 0) {
                                    percent = 100;
                                } else {
                                    float fpct;
                                    fpct = ((float) (s->packets_received
                                                + s->ping_packets_received))
                                            / den;
                                    percent = (int) (.5 + 100. * fpct);
                                }
                                fprintf(f, " <td> %d %d %d%c </td> ",
                                        s->packets_received
                                                + s->ping_packets_received,
                                        s->packets_lost + s->ping_packets_lost,
                                        percent, '%');
                                did_something = true;
                                break;
                            } else {
                                fprintf(f, " <td> %d </td> ", s->sig_strength);
                                did_something = true;
                                break;
                            }
                        }
                    }
                }

                if (!did_something) {
                    fprintf(f, " <td> </td> ");
                }
            }

            fprintf(f, "\n    </tr>\n");
        }

        fprintf(f, "</table>\n");
    }

    almost_done:

    fprintf(f, "</body>\n");
    fprintf(f, "</html>\n");

    if (fclose(f) != 0) {
        ddprintf("do_print_cloud; could not fclose /tmp/cloud.tmp:  %s\n",
                strerror(errno));
        goto done;
    }

    if (rename("/tmp/cloud.tmp", "/tmp/cloud.asp") != 0) {
        ddprintf("do_print_cloud; unable to rename %s to %s:  %s\n",
                "/tmp/cloud.tmp", "/tmp/cloud.asp", strerror(errno));
        goto done;
    }

    done:

    set_next_cloud_print_alarm();

} /* do_print_cloud */
