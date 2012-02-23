/* ad_hoc_client.c - manage ad-hoc devices that are not part of the cloud
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: ad_hoc_client.c,v 1.17 2012-02-22 19:27:22 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: ad_hoc_client.c,v 1.17 2012-02-22 19:27:22 greg Exp $";

#include <string.h>

#include "cloud.h"
#include "ad_hoc_client.h"
#include "print.h"
#include "lock.h"
#include "timer.h"
#include "nbr.h"
#include "device.h"
#include "stp_beacon.h"
#include "random.h"

ad_hoc_client_t ad_hoc_clients[AD_HOC_CLIENTS_ACROSS_CLOUD];
int ad_hoc_client_count = 0;

/* for each ad-hoc client we know about, indicate its mac address, which
 * cloud box it is attached to, its signal strength from me, and my
 * idea of its status (is it my client?  someone else's?  unknown status?)
 */
void print_ad_hoc_clients()
{
    int i;

    ddprintf("%d ad-hoc clients", ad_hoc_client_count);
    if (ad_hoc_client_count == 0) {
        ddprintf("\n");
        goto done;
    }

    ddprintf(":\n");

    for (i = 0; i < ad_hoc_client_count; i++) {
        char buf1[20], buf2[20];
        ad_hoc_client_t *c = &ad_hoc_clients[i];
        ddprintf("    %s; served by %s; "
                "sig_strength from me:  %d; my client:  %s\n",
                mac_sprintf(buf1, c->client),
                mac_sprintf(buf2, c->server),
                c->sig_strength,
                c->my_client == AD_HOC_CLIENT_UNKNOWN ? "AD_HOC_CLIENT_UNKNOWN"
                : c->my_client == AD_HOC_CLIENT_MINE ? "AD_HOC_CLIENT_MINE"
                : c->my_client == AD_HOC_CLIENT_OTHER ? "AD_HOC_CLIENT_OTHER"
                : "(bad value)");
    }

    done:;
}

/* mark as having "no server" any ad-hoc clients that we thought were being
 * served by the cloud box that seems to have gotten turned off.
 */
void ad_hoc_clients_unserved(stp_recv_beacon_t *b)
{
    stp_beacon_t *beacon = &b->stp_beacon;
    int i, j;

    /* mark any ad-hoc clients this box serves as now not being served */
    for (i = 0; i < beacon->status_count; i++) {
        status_t *s = &beacon->status[i];
        if (s->neighbor_type != STATUS_NON_CLOUD_CLIENT) { continue; }

        for (j = 0; j < ad_hoc_client_count; j++) {
            ad_hoc_client_t *c = &ad_hoc_clients[j];
            if (j > AD_HOC_CLIENTS_ACROSS_CLOUD) {
                ddprintf("delete_stp_recv_beacon; bad ad_hoc_client_count\n");
                ad_hoc_client_count = AD_HOC_CLIENTS_ACROSS_CLOUD;
                goto done;
            }
            if (mac_equal(c->server, beacon->originator)) {
                c->my_client = AD_HOC_CLIENT_UNKNOWN;
            }
        }
    }

    done:;
}

static void delete_ad_hoc_client(mac_address_t mac_address)
{
    int i, j;
    bool_t found;
    bool_t did_something, print_end_db_msg = false;

    do {
        did_something = false;

        //if (db[51].d) {
            //ddprintf("delete_ad_hoc_client; ad_hoc_client_count %d\n",
                    //ad_hoc_client_count);
        //}
        found = false;
        for (i = 0; i < ad_hoc_client_count; i++) {
            if (mac_equal(ad_hoc_clients[i].client, mac_address)) {
                found = true;
                break;
            }
        }

        if (found) {
            if (db[51].d) {
                ddprintf("delete_ad_hoc_client; trimming something.\n");
                print_ad_hoc_clients();
            }

            for (j = i; j < ad_hoc_client_count - 1; j++) {
                ad_hoc_clients[j] = ad_hoc_clients[j + 1];
            }
            ad_hoc_client_count--;

            did_something = true;
            print_end_db_msg = true;
            if (db[51].d) {
                ddprintf("delete_ad_hoc_client done trimming; "
                        "ad_hoc_client_count now %d\n",
                        ad_hoc_client_count);
                print_ad_hoc_clients();
            }
        }
    } while (did_something);

    if (db[51].d && print_end_db_msg) {
        ddprintf("done delete_ad_hoc_client; ad_hoc_client_count %d\n",
                ad_hoc_client_count);
    }
} /* delete_ad_hoc_client */

/* this mac address is from a cloud message.  we take it as evidence
 * that its sender is not a potential ad-hoc client.  we remove it from
 * the list of ad-hoc clients.
 */
void trim_ad_hoc_client(mac_address_t mac_address)
{
    delete_ad_hoc_client(mac_address);
}

/* update signal strength to an ad-hoc client.  if we have a recent
 * (non-timed out) signal strength from the client, use that.  otherwise
 * reduce our last signal strength by 0.9.  The idea is that we want to
 * avoid precipitously giving away a client if we don't have a recent
 * beacon message we've noticed from it indicating its signal strength.
 *
 * (receiving and processing 802.11 beacons is unreliable.  these guys
 * are lower priority than cloud messages or payload messages, so we
 * process them via a separate interface, which creates a separate circular
 * buffer for them in the kernel, and we read from this device with low
 * priority.  so, we might still be getting 802.11 beacons from this
 * guy, but we may not have noticed one for a while.)
 *
 * the hope is that during the times when we don't see beacons from a
 * client, as we attenuate our idea of signal strength to it we don't
 * fall below a fresh signal strength to a cloud box farther away.
 * on the other hand, if the ad-hoc box moves to become closer to another
 * cloud box, out of range from us so that we never hear beacons, our
 * signal strength will dwindle to a low value (5).
 *
 * when we finally hit that low value, assume that the device has been
 * turned off or gone away, and trim the device from out ad_hoc_client list.
 */
void update_sig_strength(ad_hoc_client_t *c)
{
    int strength = get_sig_strength(c->client);

    if (strength > 1) {
        c->sig_strength = strength;
    } else {
        /* minimum value is 5 assuming it started out higher */
        c->sig_strength = (int) (0.5 + 0.9 * (float) c->sig_strength);
    }

    if (c->sig_strength == 5) {
        delete_ad_hoc_client(c->client);
    }
}

/* make this ad-hoc wireless device a client of this cloud box. */
static bool_t add_client(ad_hoc_client_t *c)
{
    int i;
    int my_client_count = 0;

    for (i = 0; i < ad_hoc_client_count; i++) {
        if (ad_hoc_clients[i].my_client == AD_HOC_CLIENT_MINE) {
            my_client_count++;
        }
    }
    if (my_client_count >= AD_HOC_CLIENTS_PER_BOX) {
        //if (db[51].d) { ddprintf("    add_client couldn't.\n"); }
        return false;
    }

    c->my_client = AD_HOC_CLIENT_MINE;
    mac_copy(c->server, my_wlan_mac_address);

    //if (db[51].d) { ddprintf("    add_client could.\n"); }
    return true;
}

/* see if we have better signal strength to a client ad-hoc box, and if
 * so take that client box over.  (use randomization to prevent thrashing
 * of two boxes trying to claim the same ad-hoc box from each other.)
 */
void check_ad_hoc_client_improvement()
{
    bool_t did_something = false;
    int i, max_i;
    int diff, max_diff = -1;

    //if (db[51].d) { ddprintf("check_ad_hoc_client_improvement..\n"); }

    /* first, claim any ad-hoc orphans as our clients. */
    for (i = 0; i < ad_hoc_client_count; i++) {
        ad_hoc_client_t *c = &ad_hoc_clients[i];

        if (c->my_client == AD_HOC_CLIENT_UNKNOWN) {
            //if (db[51].d) { ddprintf("    add unknown..\n"); }
            if (!add_client(c)) { goto done; }
            did_something = true;
        }
    }

    /* now, see about taking a client from another cloud box.
     * first, find the biggest difference between our signal strength and
     * the signal strength of the current owner.
     */

    for (i = 0; i < ad_hoc_client_count; i++) {
        ad_hoc_client_t *c = &ad_hoc_clients[i];

        if (c->my_client != AD_HOC_CLIENT_OTHER) { continue; }

        diff = c->sig_strength - c->owner_sig_strength;

        if (diff < 0) { continue; }

        if (diff > max_diff) {
            max_diff = diff;
            max_i = i;
        }
    }

    if (max_diff == -1) { goto done; }

    if (!random_eval(max_diff, stp_recv_beacon_count)) { goto done; }

    //if (db[51].d) { ddprintf("    do improvement..\n"); }
    if (add_client(&ad_hoc_clients[max_i])) { did_something = true; }

    done:

    /* if we changed connectivity to ad-hoc client boxes, let everyone know */
    if (did_something) {
        send_stp_beacon(false /* no disconnected nbr */);
    }

    //if (db[51].d) { ddprintf("done check_ad_hoc_client_improvement..\n"); }
}

/* another cloud box is about to send an ad-hoc client broadcast message.
 * it has told us (and any other cloud box that can hear it) to ignore
 * the client broadcast message; it's not really originating from an
 * ad-hoc client.
 */
void process_ad_hoc_bcast_block_msg(message_t *message, int d)
{
    mac_address_ptr_t sender;
    lockable_resource_t *l;

    if (db[56].d) { ddprintf("process_ad_hoc_bcast_block_msg..\n"); }

    if (locks_granted_count >= MAX_CLOUD) {
        ddprintf("process_ad_hoc_bcast_block_msg; too many locks granted.\n");
        return;
    }

    sender = get_name(d, message->eth_header.h_source);

    if (sender == NULL) {
        ddprintf("process_ad_hoc_bcast_block_msg:  get_name returned null\n");
        return;
    }

    l = &locks_granted[locks_granted_count++];

    l->type = ad_hoc_bcast_block_msg;
    mac_copy(l->node_1, sender);
    mac_copy(l->node_2, &message->v.msg.msg_body[0]);
    set_lock_timer(l);

    if (db[56].d) {
        ddprintf("process_ad_hoc_bcast_block_msg returning; state:\n");
        print_state();
    }
}

/* another cloud box needed to send out a broadcast message on behalf of
 * an ad-hoc client.  before doing so it told all cloud boxes within
 * hearing distance of it to ignore the broadcast message, in particular
 * the cloud box that is the server for that ad-hoc client.  this is the
 * "all-clear" message from that cloud box.
 */
void process_ad_hoc_bcast_unblock_msg(message_t *message, int d)
{
    mac_address_ptr_t sender;
    int i;
    bool_t found;

    if (db[56].d) { ddprintf("process_ad_hoc_bcast_unblock_msg..\n"); }

    sender = get_name(d, message->eth_header.h_source);

    if (sender == NULL) {
        ddprintf("process_ad_hoc_bcast_unblock_msg:  get_name returned null\n");
        return;
    }

    found = false;
    for (i = 0; i < locks_granted_count; i++) {
        lockable_resource_t *l = &locks_granted[i];
        if (l->type == ad_hoc_bcast_block_msg
            && mac_equal(l->node_1, sender)
            && mac_equal(l->node_2, &message->v.msg.msg_body[0]))
        {
            found = true;
            break;
        }
    }

    if (found) {
        for(; i < locks_granted_count - 1; i++) {
            locks_granted[i] = locks_granted[i + 1];
        }
        locks_granted_count--;
    }

    reset_lock_timer();

    if (db[56].d) {
        ddprintf("process_ad_hoc_bcast_unblock_msg returning; state:\n");
        print_state();
    }
}

/* we noticed a message from an an ad-hoc device for the first time.
 * not yet sure if it is a client ad-hoc device or another cloud box.
 * add it to our list of potential ad-hoc clients.
 */
void ad_hoc_client_add(mac_address_t client)
{
    int i;
    bool_t found = false;
    //if (db[51].d) {
        //ddprintf("thinking about adding ad-hoc device..\n");
    //}
    for (i = 0; i < ad_hoc_client_count; i++) {
        if (mac_equal(client, ad_hoc_clients[i].client)) {
            found = true;
            break;
        }
    }

    /* go figure. */
    if (mac_equal(client, my_wlan_mac_address)) {
        found = true;
    }

    if (!found) {
        //if (db[51].d) {
            //ddprintf("adding ad-hoc device %s; ad_hoc_client_count %d..\n",
                    //mac_sprintf(mac_buf1, client),
                    //ad_hoc_client_count);
        //}
        if (ad_hoc_client_count == AD_HOC_CLIENTS_ACROSS_CLOUD) {
            ddprintf("too many ad-hoc clients; did not add %s\n",
                    mac_sprintf(mac_buf1, client));
        } else {
            mac_copy(ad_hoc_clients[ad_hoc_client_count].client,
                    client);

            ad_hoc_clients[ad_hoc_client_count].my_client
                    = AD_HOC_CLIENT_UNKNOWN;

            /* start with default weak signal; send_stp_beacon updates it */
            ad_hoc_clients[ad_hoc_client_count].sig_strength = 1;

            ad_hoc_client_count++;
            if (db[51].d) {
                ddprintf("ad_hoc_client_count inc in add_device; now %d\n",
                    ad_hoc_client_count);
                print_ad_hoc_clients();
            }
        }
    }
    //if (db[51].d) {
        //ddprintf("done thinking about adding ad-hoc device..\n");
    //}
}

/* we include each cloud box's ad-hoc clients in its stp beacon, so that
 * other cloud boxes can see which cloud box is serving which ad-hoc client,
 * and with what signal strength.  (the cloud may use this information to
 * optimize which cloud box is serving which ad-hoc clients.)
 * this routine adds our ad-hoc clients to our stp beacon before it gets
 * sent out.
 */
void ad_hoc_client_update_my_beacon(int *count)
{
    int my_clients = 0;
    int i, j;

    /* add my ad-hoc clients to status array.  we need to do this whether
     * or not someone is building cloud.asp.
     */
    for (i = 0; i < ad_hoc_client_count; i++) {
        bool_t found;

        ad_hoc_client_t *c = &ad_hoc_clients[i];
        if (c->my_client == AD_HOC_CLIENT_MINE) {
            if (my_clients >= AD_HOC_CLIENTS_PER_BOX)
            {
                ddprintf("internal error:  too many ad-hoc clients\n");
                break;
            }
            my_clients++;
        }

        found = false;
        for (j = 0; j < *count; j++) {
            if (mac_equal(my_beacon.v.stp_beacon.status[j].name, c->client)) {
                found = true;
                break;
            }
        }

        if (!found && *count >= MAX_CLOUD) {
            ddprintf("send_stp_beacon; too many status entries.\n");
            continue;
        }

        if (!found) {
            j = *count;
            (*count)++;
        }

        if (c->my_client == AD_HOC_CLIENT_MINE) {
            my_beacon.v.stp_beacon.status[j].neighbor_type
                    = STATUS_NON_CLOUD_CLIENT;
        } else {
            my_beacon.v.stp_beacon.status[j].neighbor_type
                    = STATUS_NON_CLOUD_NON_CLIENT;
        }

        mac_copy(my_beacon.v.stp_beacon.status[j].name, c->client);

        update_sig_strength(c);

        my_beacon.v.stp_beacon.status[j].sig_strength = c->sig_strength;

        if (db[51].d) {
            ddprintf("send_stp_beacon; "
                    "added ad-hoc client %s to my beacon\n",
                    mac_sprintf(mac_buf1, c->client));
        }
    }
    if (db[51].d) {
        ddprintf("send_stp_beacon; total of %d ad-hoc clients; %d mine\n",
                *count, my_clients);
    }
}

/* process the list of ad-hoc clients gotten from another cloud box
 * in its stp beacon.
 */
void ad_hoc_client_process_stp_beacon(message_t *message)
{
    int i, j;

    /* update list of ad-hoc clients from originator of this beacon */
    for (i = 0; i < message->v.stp_beacon.status_count; i++) {
        status_t *s = &message->v.stp_beacon.status[i];
        bool_t found = false;

        if (s->neighbor_type != STATUS_NON_CLOUD_CLIENT) { continue; }

        for (j = 0; j < ad_hoc_client_count; j++) {
            if (mac_equal(ad_hoc_clients[j].client, s->name)) {
                found = true;
                //ad_hoc_clients[j] = message->v.stp_beacon.clients[i];
                ad_hoc_clients[j].my_client = AD_HOC_CLIENT_OTHER;
                mac_copy(ad_hoc_clients[j].server,
                        message->v.stp_beacon.originator);
                ad_hoc_clients[j].owner_sig_strength = s->sig_strength;
                break;
            }
        }

        if (mac_equal(s->name, my_wlan_mac_address)) {
            found = true;
        }

        if (!found) {
            if (ad_hoc_client_count == AD_HOC_CLIENTS_ACROSS_CLOUD) {
                ddprintf("too many ad-hoc clients across the cloud.\n");
            } else {
                if (db[51].d) {
                    ddprintf("process_stp_beacon_msg; going to add ad-hoc "
                            "client %s\n",
                            mac_sprintf(mac_buf1, s->name));
                    print_ad_hoc_clients();
                }
                mac_copy(ad_hoc_clients[ad_hoc_client_count].client,
                        s->name);
                mac_copy(ad_hoc_clients[ad_hoc_client_count].server,
                        message->v.stp_beacon.originator);
                ad_hoc_clients[ad_hoc_client_count].my_client
                        = AD_HOC_CLIENT_OTHER;
                ad_hoc_clients[ad_hoc_client_count].owner_sig_strength
                        = s->sig_strength;

                /* we've never seen this guy.  he may be out of radio range
                 * from us.
                 */
                ad_hoc_clients[ad_hoc_client_count].sig_strength = 1;

                ad_hoc_client_count++;

                if (db[51].d) {
                    ddprintf("process_stp_beacon_msg; added ad-hoc "
                            "client %s; ad_hoc_client_count now %d\n",
                            mac_sprintf(mac_buf1, s->name),
                            ad_hoc_client_count);
                    print_ad_hoc_clients();
                }
            }
        }
    }

    /* if we thought the originator of this beacon was serving an ad-hoc
     * client that he is not serving, update that ad-hoc client record.
     */
    for (i = 0; i < ad_hoc_client_count; i++) {
        bool_t found;
        ad_hoc_client_t *c = &ad_hoc_clients[i];

        if (!mac_equal(c->server, message->v.stp_beacon.originator))
        { continue; }

        found = false;
        for (j = 0; j < message->v.stp_beacon.status_count; j++) {
            status_t *s = &message->v.stp_beacon.status[j];

            if (s->neighbor_type != STATUS_NON_CLOUD_CLIENT) { continue; }

            if (mac_equal(c->client, s->name)) {
                found = true;
                break;
            }
        }

        if (!found) {
            c->my_client = AD_HOC_CLIENT_UNKNOWN;
        }
    }
}

/* send a payload message to this cloud box's ad-hoc clients.
 */
void ad_hoc_client_forward_msg(message_t *message, int msg_len, int d)
{
    message_t block_msg;
    int i;

    message_t *msg_buffer = (message_t *) &message->v.msg.msg_body;
    bool_t found = false;
    bool_t bcast = false;
    int dest_dev;

    if (db[52].d) {
        ddprintf("ad_hoc_client_forward_message; send %s -> %s?\n",
                mac_sprintf(mac_buf1, msg_buffer->eth_header.h_source),
                mac_sprintf(mac_buf2, msg_buffer->eth_header.h_dest));
    }

    /* if we have no ad-hoc clients, don't put the message out.
     * (this test is a special case of the one below.  trim it when
     * you have the guts.)
     */
    if (ad_hoc_client_count == 0) {
        found = false;
        goto maybe_doit;
    }

    /* do we have an ad-hoc client that is not the sender of this message?
     */
    for (i = 0; i < ad_hoc_client_count; i++) {
        if (ad_hoc_clients[i].my_client == AD_HOC_CLIENT_MINE
            && !mac_equal(msg_buffer->eth_header.h_source,
                ad_hoc_clients[i].client))
        {
            found = true;
            break;
        }
    }
    /* if not, definitely don't send the message. */
    if (!found) {
        goto maybe_doit;
    }

    /* if this is a broadcast message, (gulp) send it.  but, there
     * is a chance other cloud boxes will notice the message and think
     * it came from a real client and try to re-send it themselves.
     *
     * but first, send a message to any other cloud boxes that can hear,
     * the cloud box that is the server of this ad-hoc client in particular,
     * telling them to ignore this broadcast message.
     * other cloud boxes will store this blocking message
     * (with timeout), and they won't re-broadcast messages they
     * get that match the mac address.  then, send out an all-clear message.
     * this is probably ok, because broadcast messages are rare.
     */
    if (mac_equal(msg_buffer->eth_header.h_dest, mac_address_bcast)
        || mac_equal(msg_buffer->eth_header.h_dest, mac_address_zero))
    {
        found = true;
        bcast = true;

        /* send hash of the message to any cloud box that can hear,
         * that this message is not coming from their ad-hoc client!
         * presumably, the cloud boxes that hear the message we are
         * about to transmit will have heard this hash message.
         */
        memset(&block_msg, 0, sizeof(block_msg));
        block_msg.message_type = ad_hoc_bcast_block_msg;
        mac_copy(&block_msg.v.msg.msg_body[0],
                msg_buffer->eth_header.h_source);
        mac_copy(block_msg.dest, mac_address_bcast);

        if (db[56].d) {
            ddprintf("sending ad_hoc_bcast_block_msg...\n");
        }
        send_cloud_message(&block_msg);

        goto maybe_doit;
    }

    found = false;

    /* see if the h_dest of this message indicates that it is one of
     * our ad-hoc clients.  in that case, send it.
     */
    if (!found) {
        /* is the dest of this message an ad_hoc device? */
        for (dest_dev = 0; dest_dev < device_list_count; dest_dev++) {
            if (device_list[dest_dev].device_type == device_type_ad_hoc
               && mac_equal(msg_buffer->eth_header.h_dest,
                    device_list[dest_dev].mac_address))
            {
                found = true;
                break;
            }
        }

        if (db[52].d) { ddprintf("   found %d\n", (int) found); }

        /* if so, is this one of our ad-hoc clients? */
        if (found) {
            int j;
            bool_t found_again = false;
            for (j = 0; j < ad_hoc_client_count; j++) {
                if (mac_equal(msg_buffer->eth_header.h_dest,
                    ad_hoc_clients[j].client)
                    && ad_hoc_clients[j].my_client == AD_HOC_CLIENT_MINE)
                {
                    found_again = true;
                    break;
                }
            }
            found = found_again;
        }
    }

    maybe_doit:

    // ddprintf("    found %d, bcast %d, dest_dev %d, d %d..\n",
            // (int) found, (int) bcast, dest_dev, d);

    /* if it's one of our ad-hoc clients, and we didn't just receive
     * this message in from him (?), send it to him.
     */
    if (found && (bcast || dest_dev != d)) {
        if (db[52].d) {
            int len = msg_len - wrapper_len;
            ddprintf("    sending to client (abbreviated):\n");
            fn_print_message(eprintf, stderr,
                    (unsigned char *) msg_buffer,
                    (len > 100) ? 100 : len);
        }

        send_to_interface((byte *) msg_buffer, msg_len - wrapper_len,
                device_type_wlan);

        if (bcast) {
            /* send all-clear message with the message's hash, so
             * that other cloud boxes that heard this message can stop
             * ignoring messages with the same hash.
             */
            block_msg.message_type = ad_hoc_bcast_unblock_msg;
            send_cloud_message(&block_msg);
        }

    } else {
        if (db[52].d) {
            ddprintf("   not sending it:  %d, %d =? %d\n", (int) found,
                    i, d);
        }
    }

    if (db[52].d) {
        ddprintf("ad_hoc_client_forward_message; done send to client\n");
    }
}

/* see if the message is a broadcast message whose source mac address indicates
 * it is from an ad-hoc client we are supposed to ignore broadcast messages
 * from.
 *
 * the message might have been sent out from another cloud box, attempting
 * to transmit broadcast messages to ad-hoc clients that are out of range
 * of the ad-hoc client that initiated the message.
 * to handle that situation, we have a little bit of protocol in which the
 * cloud box that sends the message first sends a cloud-protocol message
 * instructing other boxes to ignore it, and then sends an all-clear message
 * after sending it.
 */
bool_t ignore_ad_hoc_bcast(message_t *msg, int msg_len)
{
    int i;

    if (db[56].d) {
        ddprintf("ignore_ad_hoc_bcast; message:\n");
        fn_print_message(eprintf, stderr, (unsigned char *) msg, msg_len);
        ddprintf("state:\n");
        print_state();
    }

    if (!mac_equal(msg->eth_header.h_dest, mac_address_bcast)
        && !mac_equal(msg->eth_header.h_dest, mac_address_zero))
    {
        if (db[56].d) { ddprintf("not a bcast message; returning false.\n"); }
        return false;
    }

    for (i = 0; i < locks_granted_count; i++) {
        lockable_resource_t *l = &locks_granted[i];
        if (l->type == ad_hoc_bcast_block_msg
            && mac_equal(l->node_2, msg->eth_header.h_source))
        {
            if (db[56].d) { ddprintf("found lock; returning true.\n"); }
            return true;
        }
    }

    if (db[56].d) { ddprintf("returning false.\n"); }
    return false;
}
