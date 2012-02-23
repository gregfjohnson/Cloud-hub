/* parm_change.c - propagate wifi parmater changes throughout the cloud
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: parm_change.c,v 1.25 2012-02-22 19:27:23 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: parm_change.c,v 1.25 2012-02-22 19:27:23 greg Exp $";

/* support propagation of updates to wifi parameters (ssid, channel, etc.)
 * to all boxes in a cloud.  the idea is that if there are several cloud
 * boxes set up and communicating with each other, and a need arises to
 * change the wifi parameters they are using, it would be nice to avoid
 * having to go to each one separately, access its web page and update the
 * wifi paramaters.
 *
 * to provide a modicum of security, and also to handle the case where
 * one cloud has multiple segments connected via cat-5 that are using
 * different sets of wifi parameters, we use encryption.
 *
 * the parm_change messages are encrypted using the administrator password
 * of the originating box, and both current and new wifi parameters are
 * sent out encrypted.  boxes receiving the message will switch to the new
 * wifi parameters only if on decrypting the message they find that the
 * current parameters from the originator match their own current parameters.
 * this means that with essentially probability 1 the boxes have the same
 * administrator password, and that they were in the same wifi segment.
 *
 * to increase the probability that the whole cloud (or at least the part
 * that should be updated as described above) does get updated, we use
 * a three-pass protocol.  the originator sends out an initial message
 * proposing the update.  if they can, all nodes grant the originator
 * locks on all of their stp links, so that no automatic cloud re-configuration
 * can happen during the update.  then, they send the originator a message
 * indicating the granting of those locks.  when the originator has a
 * full set of locks for all nodes in the cloud, it sends out the "go-ahead"
 * message encrypted as described above.  each node sends the "go-ahead"
 * message 10 times to each of its neighbors (other than the one it received
 * the message from), using probabalistic inter-message wait times.  this
 * is to avoid having everyone shout on the medium almost simultaneously,
 * and to space the messages out a little to maybe avoid some temporary
 * interference.
 *
 * after sending the message to its neighbors, the box changes its wifi
 * parameters and reboots.  when it wakes up, presumably it finds a new
 * cloud to become a member of.
 */

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include "nbr.h"
#include "cloud_msg.h"
#include "stp_beacon.h"
#include "cloud_mod.h"
#include "parm_change.h"
#include "print.h"
#include "lock.h"
#include "timer.h"
#include "random.h"
#include "encrypt.h"
#include "errno.h"

/* for the originator; "cloud_boxes" is the list of all boxes in the
 * current cloud (length "cloud_box_count"), and "have_lock" indicates
 * which boxes have responded back granting the originator locks on their
 * stp links.
 */
static mac_address_t cloud_boxes[MAX_CLOUD];
static int cloud_box_count;
static bool_t have_lock[MAX_CLOUD];

/* we only want to initiate a parm change once, the first time we hear
 * that we are supposed to, even though our neighbors will tell us to
 * do it multiple times (to up the likelihood that at least one of those
 * messages gets through).
 */
static bool_t doing_go = false;

/* debug-print a parm_change message, including current and proposed new
 * wifi parameters
 */
void parm_change_dprint(ddprintf_t *fn, FILE *f, parm_change_msg_t *change)
{
    fn(f, "originator ");
    mac_dprint(fn, f, change->originator);
    fn(f, "    old ssid %s\n", change->orig_ssid);
    fn(f, "    old channel %d\n", change->orig_channel);
    fn(f, "    old wireless mode %d\n", change->orig_wireless_mode);
    fn(f, "    old security mode %d\n", change->orig_security_mode);
    fn(f, "    old wep key %s\n\n", change->orig_wep_key);

    fn(f, "    new ssid %s\n", change->new_ssid);
    fn(f, "    new channel %d\n", change->new_channel);
    fn(f, "    new wireless mode %d\n", change->new_wireless_mode);
    fn(f, "    new security mode %d\n", change->new_security_mode);
    fn(f, "    new wep key %s\n", change->new_wep_key);
}

/* write a file indicating success or failure of the parm_change, so that
 * the internal web page can give feedback to the user
 */
void parm_change_result(bool_t result)
{
    FILE *f;
    if (result) {
        f = fopen("/tmp/parm_change_success", "w");
    } else {
        f = fopen("/tmp/parm_change_failure", "w");
    }
    fclose(f);
}

/* convenience function indicating an error or problem occurred */
static void err(char *msg)
{
    if (db[64].d) { ddprintf("parm_change; %s\n", msg); }

    parm_change_result(false);
}

/* have we granted the originator a parm_change lock already? */
static bool_t has_lock(mac_address_t originator)
{
    int i;
    bool_t found = false;

    for (i = 0; i < locks_granted_count; i++) {
        lockable_resource_t *l = &locks_granted[i];

        if (l->type == parm_change_start_msg
            && mac_equal(originator, l->node_1))
        {
            found = true;
            break;
        }
    }

    return found;
}

/* grant the originator a parm_change lock, and send a message back to
 * him that we have done so.
 */
static bool_t grant_lock(mac_address_t originator)
{
    lockable_resource_t *l;

    if (db[64].d) { ddprintf("parm_change:grant_lock..\n"); }

    if (has_lock(originator)) { return true; }

    if (locks_granted_count >= MAX_CLOUD) {
        ddprintf("parm_change:grant_lock; too many locks granted.\n");
        return false;
    }

    l = &locks_granted[locks_granted_count++];

    l->type = parm_change_ready_msg;
    mac_copy(l->node_1, originator);
    set_lock_timer(l);

    if (db[64].d) {
        ddprintf("parm_change:grant_lock returning; state:\n");
        print_state();
    }

    return true;
}

/* use the "system()" function to get a value from nvram.
 * used to get current wifi parameters and administrator password.
 */
static void get_nvram_string(char *dest, int len, char *key)
{
    FILE *f;
    char buf[256];
    bool_t done;

    sprintf(buf, "nvram get %s > /tmp/foobar", key);

    done = false;
    while (!done) {
        if (system(buf) != 0) { continue; }
        f = fopen("/tmp/foobar", "r");
        if (f == NULL) { continue; }
        if (fgets(buf, 256, f) != NULL) {
            copy_string(dest, buf, len);
            done = true;
        }
        fclose(f);
        unlink("/tmp/foobar");
    }
}

/* translate a string to a byte encoding of net mode.
 * "mixed"  -> WIFI_MODE_MIXED
 * "b-only" -> WIFI_MODE_B_ONLY
 * "g-only" -> WIFI_MODE_G_ONLY
 * other    -> WIFI_MODE_UNKNOWN, return error value -1
 */
static int string_to_net_mode(char *buf, byte *net_mode)
{
    int result = 0;

    if (strcmp(buf, "mixed") == 0) {
        *net_mode = WIFI_MODE_MIXED;
    } else if (strcmp(buf, "b-only") == 0) { 
        *net_mode = WIFI_MODE_B_ONLY;
    } else if (strcmp(buf, "g-only") == 0) { 
        *net_mode = WIFI_MODE_G_ONLY;
    } else {
        if (db[64].d) { ddprintf("invalid net mode '%s'\n", buf); }
        *net_mode = WIFI_MODE_UNKNOWN;
        result = -1;
    }
    return result;
}

/* translate a string to wep mode.
 * "disabled" -> WIFI_SECURITY_DISABLED
 * "wep"      -> WIFI_SECURITY_WEP
 * other      -> WIFI_SECURITY_UNKNOWN, return error value -1
 */
static int string_to_wep_mode(char *buf, byte *wep_mode)
{
    int result = 0;

    if (strcmp(buf, "disabled") == 0) {
        *wep_mode = WIFI_SECURITY_DISABLED;
    } else if (strcmp(buf, "wep") == 0) { 
        *wep_mode = WIFI_SECURITY_WEP;
    } else {
        if (db[64].d) { ddprintf("invalid security mode '%s'\n", buf); }
        *wep_mode = WIFI_SECURITY_UNKNOWN;
        result = -1;
    }
    return result;
}

/* look up net mode (g-only, b-only, mixed) in nvram, return integer encoding
 * of the value.
 */
static int get_net_mode(byte *net_mode)
{
    char buf[64];

    get_nvram_string(buf, 64, "wl_net_mode");
    return string_to_net_mode(buf, net_mode);
}

/* look up wep mode (wep, disabled) in nvram, return integer encoding
 * of the value.
 */
static int get_wep_mode(byte *wep_mode)
{
    char buf[64];

    get_nvram_string(buf, 64, "security_mode2");
    return string_to_wep_mode(buf, wep_mode);
}

/* look up and return an integer-valued nvram variable */
static int get_nvram_int(char *key, int *value)
{
    char buf[16];
    int result = 0;

    get_nvram_string(buf, 16, key);
    if (sscanf(buf, "%d", value) != 1) { result = -1; }

    return result;
}

/* get current wifi parameters from nvram, and build them into the
 * parm_change message
 */
static int build_old_parm_change_msg(parm_change_msg_t *msg)
{
    int idata;
    get_nvram_string(msg->orig_ssid, MAX_SSID, "wl_ssid");
    get_nvram_string(msg->orig_wep_key, MAX_WEP_KEY, "wl_key1");
    get_nvram_int("wl_channel", &idata);
    msg->orig_channel = (byte) idata;
    if (get_net_mode(&msg->orig_wireless_mode) != 0) { return -1; }
    if (get_wep_mode(&msg->orig_security_mode) != 0) { return -1; }

    return 0;
}

/* the file "fname" has a new set of wifi parameters.  (it was written by
 * the web server for us, based on instructions from the user.)
 * read that file, and put the values into the parm_change message.
 * return true if successful, false if any problems occur.
 */
static bool_t build_new_parm_change_msg(parm_change_msg_t *msg, char *fname)
{
    char buf[256], buf2[256];
    bool_t result = false;
    int idata;

    if (db[64].d) { ddprintf("build_new_parm_change_msg..\n"); }

    FILE *f = fopen(fname, "r");
    if (f == NULL) {
        if (db[64].d) { ddprintf("couldn't open %s:  %m\n", fname); }
        goto done;
    }

    /* read the channel number from the file */
    if (fgets(buf, 256, f) == NULL) { err("fgets failed"); goto done; }
    if (sscanf(buf, "%d", &idata) != 1) { err("fgets failed"); goto done; }
    msg->new_channel = (byte) idata;

    /* read the wireless net_mode ("b-only", g-only", "mixed") */
    if (fgets(buf, 256, f) == NULL) { err("fgets failed"); goto done; }
    copy_string(buf2, buf, 256);
    if (string_to_net_mode(buf2, &msg->new_wireless_mode) != 0) {
        if (db[64].d) { ddprintf("invalid net mode '%s'\n", buf); }
        goto done;
    }

    /* read the security mode ("wep", "disabled") */
    if (fgets(buf, 256, f) == NULL) { err("fgets failed"); goto done; }
    copy_string(buf2, buf, 256);
    if (string_to_wep_mode(buf2, &msg->new_security_mode) != 0) {
        if (db[64].d) { ddprintf("invalid security mode '%s'\n", buf); }
        goto done;
    }

    /* read the new ssid */
    if (fgets(buf, 256, f) == NULL) { err("fgets failed"); goto done; }
    copy_string(msg->new_ssid, buf, MAX_SSID);

    /* read the new wep key */
    if (fgets(buf, 256, f) == NULL) { err("fgets failed"); goto done; }
    copy_string(msg->new_wep_key, buf, MAX_WEP_KEY);

    mac_copy(msg->originator, my_wlan_mac_address);

    if (db[64].d) {
        ddprintf("the parm change start message:\n");
        parm_change_dprint(eprintf, stderr, msg);
    }

    result = true;

    done:
    if (f != NULL) { fclose(f); }
    return result;
}

/* the message we will build up to tell everyone to do the wifi parm change */
static message_t go_message;

/* send out initial message proposing a global cloud wifi parm change.
 * other nodes will respond back saying if it's ok to proceed with the change.
 */
bool_t start_parm_change(char *fname)
{
    bool_t result = false;
    message_t msg;
    int i;

    if (db[64].d) { ddprintf("start_parm_change..\n"); }

    if (!has_lock(my_wlan_mac_address)
        && (doing_stp_update() || !grant_lock(my_wlan_mac_address)))
    {
        err("start_parm_change; doing_stp_update or couldn't grant "
                "lock to myself");
        goto done;
    }

    doing_go = false;

    memset(&go_message, 0, sizeof(go_message));

    if (!build_new_parm_change_msg(&go_message.v.parm_change, fname)) {
        err("start_parm_change; problem with build_new_parm_change_msg");
        goto done;
    }

    mac_copy(msg.v.parm_change.originator, my_wlan_mac_address);

    /* set up a local list of the current cloud boxes, and wait to hear
     * from all of them before issuing the "make the change" message
     * to the whole cloud.
     */
    cloud_box_count = 0;
    for (i = 0; i < stp_recv_beacon_count; i++) {
        stp_beacon_t *b = &stp_recv_beacons[i].stp_beacon;

        if (cloud_box_count >= MAX_CLOUD) { continue; }
        if (mac_equal(b->originator, my_wlan_mac_address)) { continue; }

        mac_copy(cloud_boxes[cloud_box_count++], b->originator);
        have_lock[i] = false;
    }

    /* if this box is in a one-box cloud all by itself, go ahead and make
     * the change immediately.
     */
    if (stp_recv_beacon_count == 0) {
        process_parm_change_ready_msg(NULL, -1);
    }

    msg.message_type = parm_change_start_msg;
    for (i = 0; i < stp_list_count; i++) {
        mac_copy(msg.dest, stp_list[i].box.name);
        send_cloud_message(&msg);
    }

    done:
    if (db[64].d) { ddprintf("start_parm_change returning..\n"); }
    return result;
}

/* some other node is proposing a global parm change.  see if we can
 * grant that node a lock on our stp locks.  if so, send it a message
 * to that effect, and if not send it a nak.  pass the message along to
 * our stp neighbors other than the one we got this message from.
 */
void process_parm_change_start_msg(message_t *msg, int device_index)
{
    int i;
    mac_address_ptr_t neighbor;
    message_t response;

    doing_go = false;

    if (db[64].d) {
        ddprintf("process_parm_change_start_msg..\n");
        parm_change_dprint(eprintf, stderr, &msg->v.parm_change);
    }

    neighbor = get_name(device_index, msg->eth_header.h_source);
    if (neighbor == NULL) {
        ddprintf("process_parm_change_start_msg:  get_name returned null\n");
        goto done;

    }

    /* pass this beacon along to other stp neighbors besides the one we
     * just got it from.
     */
    for (i = 0; i < stp_list_count; i++) {
        if (mac_equal(stp_list[i].box.name, neighbor)) { continue; }
        mac_copy(msg->dest, stp_list[i].box.name);
        send_cloud_message(msg);
    }

    /* see if we can grant the originator a lock. */

    if (has_lock(my_wlan_mac_address)
        || (!doing_stp_update() && grant_lock(msg->v.parm_change.originator)))
    {
        /* tell the originator that we are ready to go. */
        if (db[64].d) {
            ddprintf("process_parm_change_start_msg; grant lock, "
                    "  send parm_change_ready_msg\n");
        }
        mac_copy(response.dest, msg->v.parm_change.originator);
        mac_copy(response.v.parm_change.originator, my_wlan_mac_address);
        response.message_type = parm_change_ready_msg;
        send_cloud_message(&response);

    } else {
        /* we are in the middle of a change to the stp tree or couldn't
         * grant a lock to the originator for some reason;
         * tell the originator that we are not ready to go.
         */
        if (db[64].d) {
            ddprintf("process_parm_change_start_msg; could not grant lock.\n"
                    "  send parm_change_not_ready_msg\n");
        }
        mac_copy(response.dest, msg->v.parm_change.originator);
        mac_copy(response.v.parm_change.originator, my_wlan_mac_address);
        response.message_type = parm_change_not_ready_msg;
        send_cloud_message(&response);
    }

    done:;
}

/* we have received a "go-ahead" message instructing us to do a wifi parm
 * change.  we use our administrator password to decrypt the message, and
 * we compare the message's old wifi parameters to our own.  if they match,
 * we update our local wifi parms with the new parms from the message and
 * reboot.  if they don't we ignore the message and don't change anything.
 */
static void do_wifi_change(parm_change_msg_t *new)
{
    // char cmd[256];
    char key[256];
    int len;
    // int result;
    parm_change_msg_t old;
    char *argv[20];
    int argc;
    char channel_buf[32];
    pid_t pid;

    get_nvram_string(key, 256, "http_passwd");

    len = sizeof(*new);
    if (len % 8 != 0) { len = len - len % 8; }

    if (do_decrypt(key, (char *) new, len) != 0) {
        ddprintf("process_parm_change_ready_msg; encryption failed.\n");
        goto done;
    }

    memset(&old, 0, sizeof(old));
    while (build_old_parm_change_msg(&old) != 0);

    if (db[64].d) {
        ddprintf("do_wifi_change; my current parameters:\n");
        parm_change_dprint(eprintf, stderr, &old);
        ddprintf("\nnew parameters off the wire:\n");
        parm_change_dprint(eprintf, stderr, new);
    }

    if (old.orig_channel != new->orig_channel
        || old.orig_wireless_mode != new->orig_wireless_mode
        || old.orig_security_mode != new->orig_security_mode
        || strcmp(old.orig_ssid, new->orig_ssid) != 0
        || strcmp(old.orig_wep_key, new->orig_wep_key) != 0)
    {
        ddprintf("do_wifi_change; parm mismatch, so not doing update.\n");
        goto done;
    }

    #if 0
    sprintf(cmd, "/usr/sbin/change_wifi_parms -s \"%s\" -c %d -m \"%s\" "
            "-w \"%s\" -k \"%s\" -b %d %s\n",
            new->new_ssid,
            new->new_channel,
            new->new_wireless_mode == WIFI_MODE_B_ONLY ? "b-only"
                : (new->new_wireless_mode == WIFI_MODE_G_ONLY ? "g-only"
                : "mixed"),
            new->new_security_mode == WIFI_SECURITY_DISABLED ? "disabled" :
                "enabled",
            new->new_wep_key,
            strlen(new->new_wep_key) == 13 ? 64 : 128,
            db[65].d ? "-d" : "");
    #endif

    argc = 0;
    argv[argc++] = "change_wifi_parms";

    argv[argc++] = "-s";
    argv[argc++] = new->new_ssid;

    argv[argc++] = "-c";
    sprintf(channel_buf, "%d", new->new_channel);
    argv[argc++] = channel_buf;

    argv[argc++] = "-m";
    argv[argc++] = (new->new_wireless_mode == WIFI_MODE_B_ONLY ? "b-only"
                : new->new_wireless_mode == WIFI_MODE_G_ONLY ? "g-only"
                : "mixed");

    argv[argc++] = "-w";
    argv[argc++] = (new->new_wireless_mode == WIFI_SECURITY_DISABLED
                ? "disabled"
                : "enabled");

    argv[argc++] = "-k";
    argv[argc++] = new->new_wep_key;

    argv[argc++] = "-b";
    argv[argc++] = (strlen(new->new_wep_key) == 13 ? "64" : "128");

    if (db[65].d) { argv[argc++] = "-d"; }

    argv[argc] = NULL;

    #if 0
    if (db[64].d) {
        ddprintf("change parms and reboot; do_wifi_change command:  '%s'\n",
                cmd);
    }

    while ((result = system(cmd)) != 0) {
        ddprintf("do_wifi_change; problem with system call:  %d\n", result);
        sleep(5);
    }

    if (db[64].d) { ddprintf("done with change_wifi_parms; reboot..\n"); }

    if (!db[65].d) {
        /* after we reboot, this helps us start httpd faster */
        unlink("/var/run/httpd.pid");

        /* reboot this box. */
        kill(1, SIGHUP);
    }
    #endif

    if (db[64].d) {
        ddprintf("change parms and reboot; do_wifi_change command:  ");
        for (argc = 0; argv[argc]; argc++) {
            ddprintf("    %d:  %s\n", argc, argv[argc]);
        }
    }
    /* if db[65] is set, we fake the parm change for debugging */

    if (!db[65].d) {
        /* after we reboot, this helps us start httpd faster */
        unlink("/var/run/httpd.pid");
    }

    if (db[65].d) { pid = fork(); }

    if (!db[65].d || pid == 0) {
        while (execv("/usr/sbin/change_wifi_parms", argv) != 0) {
            ddprintf("do_wifi_change; problem with evecv:  %d\n", errno);
            sleep(5);
        }
    }

    wait(NULL);

    done:;
}

/* pass the message to all stp neighbors.
 * we will send this message 10 times to each of our stp neighbors,
 * waiting an average of 0.5 sec each time.  (uniform[0.4 .. 0.6]).
 * this will increase the likelihood that the message actually gets through.
 * we can go to a much more * elaborate scheme if necessary, but use
 * this simple approach for now.
 */
static void send_change_message(message_t *go_message, mac_address_ptr_t nbr)
{
    int repeat, i;
    int send_count = 0;

    /* see if we actually have any stp neighbors we are supposed to send
     * the change message to.
     */
    for (i = 0; i < stp_list_count; i++) {
        if (nbr != NULL && mac_equal(nbr, stp_list[i].box.name))
        { continue; }
        send_count++;
    }

    if (send_count == 0) { return; }

    for (repeat = 0; repeat < 10; repeat++) {
        static struct timespec req = {1, 0};
        int u = discrete_unif(200000000) + 400000000;
        req.tv_nsec = u;
        if (db[64].d) { ddprintf("sleep <%ld %ld>\n", req.tv_sec, req.tv_nsec); }
        nanosleep(&req, NULL);
        if (db[64].d) { ddprintf("wake up..\n"); }

        for (i = 0; i < stp_list_count; i++) {
            if (nbr != NULL && mac_equal(nbr, stp_list[i].box.name))
            { continue; }
            if (db[64].d) {
                ddprintf("process_parm_change_ready_msg send to ");
                mac_dprint(eprintf, stderr, stp_list[i].box.name);
            }
            mac_copy(go_message->dest, stp_list[i].box.name);
            send_cloud_message(go_message);
        }
    }
}

/* we are the originator, and have received back a message from another
 * box in the cloud that they have granted us a parm_change lock.  if
 * this is that last box, and we now have a complete set of locks,
 * proceed with the global parm change.  otherwise, just wait for more locks.
 */
void process_parm_change_ready_msg(message_t *msg, int device_index)
{
    int i;
    int len;
    bool_t go;
    char key[256];

    if (db[64].d) {
        ddprintf("process_parm_change_ready_msg..\n");
    }

    for (i = 0; i < cloud_box_count; i++) {
        if (mac_equal(msg->v.parm_change.originator, cloud_boxes[i])) {
            have_lock[i] = true;
            break;
        }
    }

    go = true;
    for (i = 0; i < cloud_box_count; i++) {
        if (!have_lock[i]) {
            go = false;
            break;
        }
    }

    if (!go) {
        if (db[64].d) {
            ddprintf("process_parm_change_ready_msg doesn't have lock.\n");
        }
        return;
    }

    go_message.message_type = parm_change_go_msg;

    while (build_old_parm_change_msg(&go_message.v.parm_change) != 0);

    mac_copy(go_message.v.parm_change.originator, my_wlan_mac_address);

    if (db[64].d) {
        ddprintf("process_parm_change_ready_msg; here's the message:\n");
        parm_change_dprint(eprintf, stderr, &go_message.v.parm_change);
    }

    get_nvram_string(key, 256, "http_passwd");
    len = sizeof(go_message.v.parm_change);
    if (len % 8 != 0) { len = len - len % 8; }

    if (do_encrypt(key, (char *) &go_message.v.parm_change, len) != 0) {
        ddprintf("process_parm_change_ready_msg; encryption failed.\n");
        return;
    }

    stop_alarm();

    // block_timer_interrupts(SIG_BLOCK);

    parm_change_result(true);

    send_change_message(&go_message, NULL);

    do_wifi_change(&go_message.v.parm_change);

    // block_timer_interrupts(SIG_UNBLOCK);
}

/* a box in the cloud could not grant us a lock for the global parameter
 * change.  notify the web browser to give the user the message that
 * the update didn't work, and just let the locks time out on their own.
 */
void process_parm_change_not_ready_msg(message_t *msg, int device_index)
{
    mac_address_ptr_t neighbor;

    if (db[64].d) { ddprintf("process_parm_change_not_ready_msg..\n"); }

    neighbor = get_name(device_index, msg->eth_header.h_source);
    if (neighbor == NULL) {
        ddprintf("process_parm_change_not_ready_msg:  get_name returned null\n");
        goto done;

    }

    parm_change_result(false);

    done:;
}

/* we have received a "go-ahead" message to do a parm_change.  decrypt the
 * message, see if the old parms match our current parms, and if so update
 * our local wifi parms and reboot.
 */
void process_parm_change_go_msg(message_t *msg, int device_index)
{
    mac_address_ptr_t neighbor;

    if (db[64].d) { ddprintf("process_parm_change_go_msg..\n"); }

    if (doing_go) {
        if (db[64].d) {
            ddprintf("process_parm_change_go_msg already going.  returning.\n");
        }
        return;
    }

    neighbor = get_name(device_index, msg->eth_header.h_source);
    if (neighbor == NULL) {
        ddprintf("process_parm_change_go_msg:  get_name returned null\n");
        goto done;

    }

    doing_go = true;

    /* pass this beacon along to other stp neighbors besides the one we
     * just got it from.
     *
     * we will not return from this function.  after sending the message
     * to other cloud boxes, we will reboot.
     */
    stop_alarm();

    // block_timer_interrupts(SIG_BLOCK);

    parm_change_result(true);

    send_change_message(msg, neighbor);

    do_wifi_change(&msg->v.parm_change);

    // block_timer_interrupts(SIG_UNBLOCK);

    done:;
}
