/* device.c - manage local interfaces and other cloud boxes and clients
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: device.c,v 1.14 2012-02-22 19:27:22 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: device.c,v 1.14 2012-02-22 19:27:22 greg Exp $";

#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <netinet/in.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/socket.h>

#include <sys/socket.h>
#include <netpacket/packet.h>
#include <net/ethernet.h>     /* the L2 protocols */

#include "cloud.h"
#include "print.h"
#include "device.h"
#include "ad_hoc_client.h"
#include "io_stat.h"
#include "timer.h"

/* the devices on this box; eth0, wlan0, wlan0wds_i.
 * but note; wland0wds_i correspond to remote boxes.
 */
device_t device_list[MAX_CLOUD];
int device_list_count = 0;

/* is the device the local LAN cat-5 ethernet interface? */
int is_eth(device_t *device)
{
    return (device->device_type == device_type_eth);
}

/* is the device the wireless interface? (either normal or monitoring device) */
int is_wlan(device_t *device)
{
    return (device->device_type == device_type_wlan
            || device->device_type == device_type_wlan_mon);
}

/* see which wds devices we are getting beacons from.  (this routine doesn't
 * look at the ethernet connection or the wlan connection.  those devices
 * are added at startup, the latter only if we were told to do so from the
 * command line.  they don't come and go.  only the wds devices
 * come and go.)
 *
 * this routine manages the device_list array, which includes if_index,
 * file descriptor, etc. to support actual low-level communication.
 */
char check_devices(void)
{
    char result = 0;
    int retval;
    static struct ifreq get_index;
    struct sockaddr_ll bind_arg;
    char wds_devices[MAX_CLOUD][64];
    mac_address_t wds_macs[MAX_CLOUD];
    int wds_count = 0;
    int w, d;
    char buf[64];

    FILE *wds = fopen(wds_file, "r");
    if (wds == NULL) {
        ddprintf("check_devices; fopen failed:  %s\n", strerror(errno));
        ddprintf("    file:  '%s'\n", wds_file);
        goto finish;
    }

    /* read the mac addresses of the wds devices into
     * local array wds_devices
     */
    while (1) {
        if (wds_count >= MAX_CLOUD) {
            ddprintf("check_devices:  too many wds devices\n");
            break;
        }

        if (skip_comment_line(wds) != 0) {
            ddprintf("check_devices; problem reading file.\n");
            break;
        }


        #ifdef WRT54G
            retval = mac_read(wds, wds_macs[wds_count]);
        #else
            retval = fscanf(wds, "%s", wds_devices[wds_count]);
        #endif

        if (retval != 1) { break; }

        #ifdef WRT54G
            retval = fscanf(wds, "%s", wds_devices[wds_count]);
        #else
            retval = mac_read(wds, wds_macs[wds_count]);
        #endif

        if (retval != 1) {
            ddprintf("check_devices; could not read device name from wds\n");
            goto finish;
        }
        wds_count++;
    }

    top :
    /* see if every wds device in our list matches current truth */
    for (d = 0; d < device_list_count; d++) {
        int found = 0;

        if (device_list[d].device_type != device_type_wds
            && device_list[d].device_type != device_type_cloud_wds)
        { continue; }
        
        for (w = 0; w < wds_count; w++) {
            if (mac_equal(device_list[d].mac_address, wds_macs[w])) {
                found = 1;
                break;
            }
        }

        if (!found
            || strstr(device_list[d].device_name, wds_devices[w]) == NULL)
        {
            result = 1;
            delete_device(d);
            goto top;
        }

        if (!use_pipes) {
            /* check to see that the if_index is still the same.
             * (if "wds0.2" gets deleted and then re-created, it probably
             * has a different if_index.)
             */
            sprintf(get_index.ifr_name, device_list[d].device_name);
            retval = ioctl(device_list[d].fd, SIOCGIFINDEX, &get_index);
            if (retval == -1) {
                ddprintf("check_devices; could not get device index:  %s\n",
                        strerror(errno));
                goto finish;
            }
            if (device_list[d].if_index != get_index.ifr_ifindex) {
                device_list[d].if_index = get_index.ifr_ifindex;
                memset(&bind_arg, 0, sizeof(bind_arg));
                bind_arg.sll_family = AF_PACKET;
                bind_arg.sll_ifindex = get_index.ifr_ifindex;
                bind_arg.sll_protocol = htons(ETH_P_ALL);

                retval = bind(device_list[d].fd, (struct sockaddr *) &bind_arg,
                        sizeof(bind_arg));
                if (retval == -1) {
                    ddprintf("check_devices; bind failed:  %s\n",
                            strerror(errno));
                    goto finish;
                }
                result = 1;
                goto top;
            }
        }
    }

    /* see if every device out there is in our local table */
    for (w = 0; w < wds_count; w++) {
        int found = 0;

        for (d = 0; d < device_list_count; d++) {
            if (mac_equal(device_list[d].mac_address, wds_macs[w])) {
                found = 1;
                break;
            }
        }

        if (!found) {
            result = 1;
            add_device(
                    ad_hoc_mode ? "ad-hoc device" : wds_devices[w],
                    wds_macs[w],
                    ad_hoc_mode ? device_type_ad_hoc : device_type_wds);

            if (!ad_hoc_mode && db[47].d) {
                sprintf(buf, "%s:1", wds_devices[w]);
                add_device(buf, wds_macs[w], device_type_cloud_wds);
            }
        }
    }

    finish :

    if (wds != NULL) { fclose(wds); }

    return result;

} /* check_devices */

/* for interface "device_name" (i.e., eth0 etc.), open a raw socket to
 * the interface and get a file descriptor for the socket.  add the
 * interface (file descriptor and all) to device_list[].
 *
 * as a special case, if the device type is an ad-hoc, that means this
 * is a description of another cloud box that we are communicating with
 * in ad-hoc mode.  in that case, find and copy the device_list[] entry
 * for our wireless lan interface.
 */
void add_device(char *device_name, mac_address_t mac_address,
        device_type_t device_type)
{
    device_t *device;
    static struct ifreq get_index;
    int result;
    struct sockaddr_ll bind_arg;
    mac_address_t mac_addr;
    int i;
    device_t *wlan;

    if (device_list_count >= MAX_CLOUD) {
        ddprintf("add_device:  too many devices.\n");
        goto finish;
    }

    device = &device_list[device_list_count];

    device->sim_device = 0;
    device->expect_k = 0;
    device->expect_n = -1;

    // if (db[0].d) {
        ddprintf("hi from add_device(%s)..\n", device_name);
    // }

    if (strstr(device_name, "wds") != NULL && device_type == device_type_wds) {
        device->device_type = device_type_wds;

    } else if (strstr(device_name, "wds") != NULL
        && device_type == device_type_cloud_wds)
    {
        device->device_type = device_type_cloud_wds;

    } else if (strcmp(device_name, wlan_device_name) == 0
        && device_type == device_type_wlan)
    {
        device->device_type = device_type_wlan;

    } else if (eth_device_name != NULL
        && strcmp(device_name, eth_device_name) == 0)
    {
        device->device_type = device_type_eth;

    } else if (have_mon_device &&
        strcmp(device_name, wlan_mon_device_name) == 0)
    {
        device->device_type = device_type_wlan_mon;

    } else if (ad_hoc_mode && device_type == device_type_ad_hoc) {
        bool_t found = false;
        device->device_type = device_type_ad_hoc;
        for (i = 0; i < device_list_count; i++) {
            if (device_list[i].device_type == device_type_wlan) {
                found = true;
                wlan = &device_list[i];
                break;
            }
        }
        if (!found) {
            ddprintf("add_device; couldn't find wireless device in ad_hoc mode");
            goto finish;
        }

    } else if (/*ad_hoc_mode &&*/ device_type == device_type_cloud_wlan) {
        device->device_type = device_type_cloud_wlan;

    } else if (/*ad_hoc_mode &&*/ device_type == device_type_cloud_eth) {
        device->device_type = device_type_cloud_eth;

    } else {
        ddprintf("add_device; unknown device_type '%s'\n", device_name);
        goto finish;
    }

    if (mac_address == 0) {
        result = mac_get(mac_addr, device_name);
        if (result != 0) {
            ddprintf("add_device:  mac_get on %s failed.\n",
                    device_name);
            goto finish;
        }
    } else {
        mac_copy(mac_addr, mac_address);
    }

    if (ad_hoc_mode && device_type == device_type_ad_hoc) {
        sprintf(device->device_name, wlan_device_name);
    } else {
        sprintf(device->device_name, device_name);
    }

    mac_copy(device->mac_address, mac_addr);

    if (use_pipes) {
        char fname[PATH_MAX];
        int fd;
        sprintf(fname, "%s/%s.cloud", pipe_directory, device_name);
        fd = repeat_open(fname, O_RDWR);
        ddprintf("opening wlan input device %s; fd %d\n", fname, fd);
        if (fd == -1) {
            ddprintf("add_device; could not open pipe %s:  %s\n", 
                    fname, strerror(errno));
            goto finish;
        }
        device->fd = fd;
        pio_init_from_fd(&device->in_pio, fname, fd);
        ddprintf("done..\n");

        sprintf(fname, "%s/%s.cooked", pipe_directory, device_name);
        fd = repeat_open(fname, O_RDWR);
        ddprintf("opening wlan output device %s; fd %d\n", fname, fd);
        if (fd == -1) {
            ddprintf("add_device; could not open pipe %s:  %s\n", 
                    fname, strerror(errno));
            goto finish;
        }
        device->out_fd = fd;
        pio_init_from_fd(&device->out_pio, fname, fd);
        ddprintf("done..\n");

    } else if (ad_hoc_mode && device_type == device_type_ad_hoc) {
        device->fd = wlan->fd;
        device->if_index = wlan->if_index;

    } else {
        device->fd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
        if (device->fd == -1) {
            ddprintf("add_device:  socket failed\n");
            goto finish;
        }

        /* after a change of wep key, this fails. may need to delete and
         * re-create wds interface?
         */
        sprintf(get_index.ifr_name, device_name);
        result = ioctl(device->fd, SIOCGIFINDEX, &get_index);
        if (result == -1) {
            ddprintf("add_device:  could not get device index\n");
            goto finish;
        }
        device->if_index = get_index.ifr_ifindex;

        memset(&bind_arg, 0, sizeof(bind_arg));
        bind_arg.sll_family = AF_PACKET;
        bind_arg.sll_ifindex = get_index.ifr_ifindex;

        if (device_type == device_type_cloud_wlan
            || device_type == device_type_cloud_eth
            || device_type == device_type_cloud_wds)
        {
            bind_arg.sll_protocol = htons(CLOUD_MSG);

        } else if (db[47].d
            && ((ad_hoc_mode && !db[50].d && device_type == device_type_wlan)
                || device_type == device_type_wds))
        {
            bind_arg.sll_protocol = htons(WRAPPED_CLIENT_MSG);

        } else {
            bind_arg.sll_protocol = htons(ETH_P_ALL);
        }

        result = bind(device->fd, (struct sockaddr *) &bind_arg,
                sizeof(bind_arg));
        if (result == -1) {
            ddprintf("add_device:  bind failed\n");
            goto finish;
        }
    }

    if (db[50].d && device_type == device_type_ad_hoc) {
        ad_hoc_client_add(device->mac_address);
    }

    add_io_stat(device);

    device_list_count++;

    finish :

    // if (db[0].d) {
        ddprintf("this device:\n");
        print_device(eprintf, stderr, &device_list[device_list_count - 1]);
        ddprintf("all devices:\n");
        print_devices();
    // }

} /* add_device */

/* delete device_list[d], and close the file descriptor that is open on
 * that device.
 */
void delete_device(int d)
{
    int i;
    int result;
    
    if (db[0].d) { ddprintf("hi from delete_device..\n"); }

    result = close(device_list[d].fd);
    if (result != 0) {
        ddprintf("delete_device:  could not close %s\n",
                device_list[d].device_name);
    }

    for (i = d; i < device_list_count - 1; i++) {
        device_list[i] = device_list[i + 1];
    }
    device_list_count--;

    if (db[0].d) { print_devices(); }
}

/* print information about a local interface (eth0 etc.) that we have open. */
void print_device(ddprintf_t *fn, FILE *f, device_t *device)
{
    fn(f, "%s, if_index %d, fd %d, out_fd %d, %s, ",
            // " addr ",
            device->device_name,
            device->if_index,
            device->fd,
            device->out_fd,
            device_type_string(device->device_type)
            );
    mac_dprint(fn, f, device->mac_address);
}

/* print information about all local interfaces (eth0 etc.) that we have open.
 */
void print_devices(void)
{
    int i;

    ddprintf("print_devices:\n");

    for (i = 0; i < device_list_count; i++) {
        // ddprintf("    %d:  ", i);
        ddprintf("    ");
        print_device(eprintf, stderr, &device_list[i]);
    }
}

/* debugging routine; send a cloud protocol ping message out each of
 * our interfaces.
 */
void test_devices()
{
    int i;
    int msg_len;
    message_t message;

    memset(&message, 0, sizeof(message));

    message.message_type = ping_msg;
    msg_len = ((byte *) &message.message_type)
            + sizeof(message.message_type)
            - ((byte *) &message);

    for (i = 0; i < device_list_count; i++) {
        ddprintf("testing device_list[%d]:  ", i);
        print_device(eprintf, stderr, &device_list[i]);
        sendum((byte *) &message, msg_len, &device_list[i]);
    }
} /* test_devices */

/* using the fd and if_index from device, send the message.
 * this is a low-level routine used only to send non-cloud messages.
 * but, it is called to send packets out of the cloud (eth or wlan ap)
 * and also to pass them along within the cloud.
 */
int sendum(byte *message, int msg_len, device_t *device)
{
    struct sockaddr_ll send_arg;
    int result;

    // noncloud_message_count++;

    if (db[25].d || db[26].d) { short_print_io_stats(eprintf, stderr); }

    check_msg_count();

    if (db[15].d) {
        // DEBUG_SPARSE_PRINT(
            ddprintf("sending payload message ");
            fn_print_message(eprintf, stderr,
                    (unsigned char *) message, msg_len);
        // )
    }

    if (db[14].d && is_wlan(device)) {
        ddprintf("sendum..\n");
    }
    if (use_pipes) {
        result = pio_write(&device->out_pio, message, msg_len);
        if (db[14].d && is_wlan(device)) {
            ddprintf("sendum; ");
            pio_print(stderr, &device->out_pio);
        }
    } else {
        memset(&send_arg, 0, sizeof(send_arg));
        send_arg.sll_family = AF_PACKET;
        send_arg.sll_halen = 6;
        memset(send_arg.sll_addr, 0, sizeof(send_arg.sll_addr));

        send_arg.sll_ifindex = device->if_index;

        if (db[1].d) { ddprintf("do the sendto..\n"); }
        block_timer_interrupts(SIG_BLOCK);
        result = sendto(device->fd, message, msg_len, 0,
                (struct sockaddr *) &send_arg, sizeof(send_arg));
        block_timer_interrupts(SIG_UNBLOCK);
        if (db[1].d) { ddprintf("sendto result:  %d\n", result); }
    }
    if (db[1].d) {
        DEBUG_SPARSE_PRINT(
            ddprintf("sending to ");
            print_device(eprintf, stderr, device);
        )
    }
    if (result == -1) {
        ddprintf("sendum; sendto error:  %s", strerror(errno));
        if (errno == EMSGSIZE) {
            ddprintf("; message size %d", msg_len);
        }
        ddprintf("\n");
        io_stat[device->stat_index].noncloud_send_error++;
    }

    io_stat[device->stat_index].noncloud_send++;

    return result;

} /* sendum */

/* find a device of type device_type, and send the messsage through
 * that device.
 * this routine is only used to send message bodies outside the cloud.
 */
int send_to_interface(byte *message, int msg_len, device_type_t device_type)
{
    int i;
    int found = 0;
    int result;

    for (i = 0; i < device_list_count; i++) {
        if (device_list[i].device_type == device_type) {
            found = 1;
            break;
        }
    } 
    if (!found) {
        ddprintf("send_to_interface:  device_type %d not found\n",
                device_type);
        return -1;
    }

    result = sendum(message, msg_len, &device_list[i]);

    return result;

} /* send_to_interface */

/* change the promiscuity status of the named device (eth0 etc.) */
void promiscuous(char *device_name, bool_t on)
{
    int i;
    char found;
    struct packet_mreq mreq;
    int result;

    if (use_pipes) { return; }

    found = 0;
    for (i = 0; i < device_list_count; i++) {
        if (strcmp(device_list[i].device_name, device_name) == 0) {
            found = 1;
            break;
        }
    }

    if (!found) {
        ddprintf("cloud.promiscuous:  could not find device %s\n",
                device_name);
        return;
    }

    memset(&mreq, 0, sizeof(mreq));
    mreq.mr_ifindex = device_list[i].if_index;
    mreq.mr_type = PACKET_MR_PROMISC;
    if (on) {
        result = setsockopt(device_list[i].fd, SOL_PACKET,
                PACKET_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
    } else {
        result = setsockopt(device_list[i].fd, SOL_PACKET,
                PACKET_DROP_MEMBERSHIP, &mreq, sizeof(mreq));
    }

    if (result == -1) {
        ddprintf("cloud.promiscuous; setsockopt failed:  %s\n",
                strerror(errno));
    }
} /* promiscuous */
