/* ll_dump.c - tcp_dump-like program, but very small, for embedded applications
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: ll_dump.c,v 1.10 2012-02-22 18:55:25 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: ll_dump.c,v 1.10 2012-02-22 18:55:25 greg Exp $";

/* a tiny program to monitor activity on an interface.
 * sorta like tcpdump or tethereal, but very small for embedded applications.
 *
 * this program is usable generically and is not specific to the
 * cloud_hub project.
 *
 * can be run in batch mode or interactive mode, and can monitor up to two
 * interfaces.
 *
 * example batch invocation that prints all packets on eth0:
 *     ll_dump -e eth0 -p
 *
 * command line arguments:
 *
 *  -i:  interactive
 *  -p:  print packets (by default, just print counts of packets)
 *  -a:  use promiscuous mode even if only looking for one protocol type
 *       (without this, only see packets with our mac address as destination)
 *  -l:  limit length of packets read (normally, read and print whole packet)
 *  -X:  exclude a protocol type.  can have multiple instances of this arg.
 *  -P:  only print packets of this protocol type.
 *  -e:  first interface to read
 *  -E:  second interface to read
 *
 * in interactive mode, commands:
 *  y:  start reading packets
 *  n:  stop reading packets
 *  g N:  read N packets
 *  p:  toggle packet printing (just print packet count if off)
 *  z:  zero packet count
 *  q:  quit
 */

#include <sys/socket.h>
#include <sys/time.h>
#include <features.h>    /* for the glibc version number */
#include <asm/types.h>
#include <sys/types.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>   /* The L2 protocols */
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "util.h"

#define MAX_DEBUG 10
static bool_t debug[MAX_DEBUG];

static struct ifreq get_index;
static int dev1_socket, dev2_socket = -1, max_socket;
#define BUF_LEN 10000
static char buf[BUF_LEN];
static struct ethhdr *eth_header = (struct ethhdr *) buf;
static int read_len = BUF_LEN;

static char *dev1_name = NULL;
static char *dev2_name = NULL;

static int dev1_ifindex;
static int dev2_ifindex;

/* by default, look at all message types
 * if this gets changed via command-line argument, we will only see
 * messages of the specified type.
 */
static int protocol = ETH_P_ALL;

/* we we want to exclude any messages based on message type? */
#define MAX_EXCL 10
static int exclude_protocols[MAX_EXCL];
static int exclude_protocols_count = 0;
static bool_t promiscuous = false;

/* open the interface "dev_name" (i.e., eth0 etc.) for message monitoring.
 */
static void setup_interface(char *dev_name, int *dev_socket, int *dev_ifindex,
    int protocol)
{
    int result;
    struct sockaddr_ll bind_arg;
    struct packet_mreq mreq;

    fprintf(stderr, "setup_interface(%s)..\n", dev_name);
    *dev_socket = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    fprintf(stderr, "socket result %d\n", *dev_socket);

    sprintf(get_index.ifr_name, dev_name);
    result  = ioctl(*dev_socket, SIOCGIFINDEX, &get_index);
    fprintf(stderr, "ioctl result %d, ifindex %d\n", result,
            get_index.ifr_ifindex);
    *dev_ifindex = get_index.ifr_ifindex;

    memset(&bind_arg, 0, sizeof(bind_arg));
    bind_arg.sll_family = AF_PACKET;
    bind_arg.sll_ifindex = get_index.ifr_ifindex;
    bind_arg.sll_protocol = htons(protocol);

    result = bind(*dev_socket, (struct sockaddr *) &bind_arg, sizeof(bind_arg));
    fprintf(stderr, "bind result %d; errno %d\n", result, errno);
    perror("bind xxx ");

    if (protocol == ETH_P_ALL || promiscuous) {
        memset(&mreq, 0, sizeof(mreq));
        mreq.mr_ifindex = *dev_ifindex;
        mreq.mr_type = PACKET_MR_PROMISC;
        result = setsockopt(dev1_socket, SOL_PACKET, PACKET_ADD_MEMBERSHIP,
                &mreq, sizeof(mreq));
        fprintf(stderr, "promiscuous setsockopt result %d\n", result);
    }
}

int main(int argc, char **argv)
{
    int i;
    // struct sockaddr_ll bind_arg;
    struct sockaddr_ll recv_arg;
    socklen_t recv_arg_len;
    int result;
    // struct packet_mreq mreq;
    char *device = NULL;
    bool_t interactive = false;
    bool_t readum = true;
    int count = 0;
    bool_t printum = false;
    int get_lines = 0;
    bool_t print_prompt = true;
    int excl;

    for (i = 0; i < MAX_DEBUG; i++) { debug[i] = false; }

    /* process command-line arguments */
    for (i = 1; i < argc; i++) {

        if (strcmp(argv[i], "-i") == 0) {
            interactive = true;
            readum = false;

        } else if (strcmp(argv[i], "-p") == 0) {
            printum = true;

        } else if (strcmp(argv[i], "-a") == 0) {
            promiscuous = true;

        } else if (strcmp(argv[i], "-l") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "-l needs a length argument\n");
                exit(1);
            }
            if (sscanf(argv[i], "%x", &read_len) != 1) {
                fprintf(stderr, "invalid read length '%s'\n", argv[i]);
                exit(1);
            }

        } else if (strcmp(argv[i], "-X") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "-X needs a protocol argument\n");
                exit(1);
            }
            if (sscanf(argv[i], "%x", &excl) != 1) {
                fprintf(stderr, "invalid protocol '%s'\n", argv[i]);
                exit(1);
            }
            if (exclude_protocols_count >= MAX_EXCL) {
                fprintf(stderr, "too many exclude protocols\n");
                exit(1);
            }
            exclude_protocols[exclude_protocols_count++] = excl;

        } else if (strcmp(argv[i], "-D") == 0) {
            int ind;
            if (++i >= argc) {
                fprintf(stderr, "-D needs a debug value\n");
                exit(1);
            }
            if (sscanf(argv[i], "%d", &ind) != 1
                || ind < 0 || ind >= MAX_DEBUG)
            {
                fprintf(stderr, "invalid debug value '%s'\n", argv[i]);
                exit(1);
            }
            debug[ind] = true;

        } else if (strcmp(argv[i], "-P") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "-P needs a protocol argument\n");
                exit(1);
            }
            if (sscanf(argv[i], "%x", &protocol) != 1) {
                fprintf(stderr, "invalid protocol '%s'\n", argv[i]);
                exit(1);
            }

        } else if (strcmp(argv[i], "-e") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "-e needs an interface argument\n");
                exit(1);
            }
            device = argv[i];

        } else if (strcmp(argv[i], "-E") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "-E needs an interface argument\n");
                exit(1);
            }
            dev2_name = strdup(argv[i]);
        }
    }

    if (device == NULL) {
        fprintf(stderr, "give me a device\n");
        return(1);
    }

    dev1_name = strdup(device);

    /* open the interface(s) for reading */
    setup_interface(dev1_name, &dev1_socket, &dev1_ifindex, protocol);

    if (dev2_name != NULL) {
        setup_interface(dev2_name, &dev2_socket, &dev2_ifindex, 0x1234);
    }

    max_socket = dev1_socket;
    if (dev2_socket > max_socket) { max_socket = dev2_socket; }

    while (1) {
        bool_t skip;
        int read_ifindex;
        int read_socket;
        fd_set read_set;

        FD_ZERO(&read_set);

        /* if we are supposed to read packets, set the select bits to do so */
        if (readum || (get_lines > 0)) {
            FD_SET(dev1_socket, &read_set);
            if (dev2_socket != -1) { FD_SET(dev2_socket, &read_set); }
        }

        /* if we are interactive, set the select bit to read stdin */
        if (interactive && !readum && get_lines == 0) {
            fprintf(stderr, "command:  ");
        }

        if (interactive) {
            FD_SET(0, &read_set);
        }

        print_prompt = false;

        result = select((readum || (get_lines > 0)) ? (max_socket + 1) : 1,
                &read_set, 0, 0, 0);

        /* if there is interactive input, parse it and do it. */
        if (FD_ISSET(0, &read_set)) {
            char c;
            char buf[64];
            fgets(buf, 64, stdin);
            c = buf[0];

            if (c == 'y') {
                readum = true;
                fprintf(stderr, "always read the device "
                        "('n' to get prompt back)..\n");

            } else if (c == 'p') {
                printum = !printum;

            } else if (c == 'z') {
                count = 0;

            } else if (c == 'X') {
                if (sscanf(&buf[1], "%x", &excl) != 1) {
                    fprintf(stderr, "invalid protocol '%s'\n", &buf[1]);

                } else if (exclude_protocols_count >= MAX_EXCL) {
                    fprintf(stderr, "too many exclude protocols\n");

                } else {
                    exclude_protocols[exclude_protocols_count++] = excl;
                }

            } else if (c == 'D') {
                int ind;
                if (sscanf(&buf[1], "%d", &ind) != 1
                    || ind < 0 || ind >= MAX_DEBUG)
                { fprintf(stderr, "invalid debug value '%s'\n", &buf[1]); }
                 else { debug[ind] = true; }

            } else if (c == 'd') {
                int ind;
                if (sscanf(&buf[1], "%d", &ind) != 1
                    || ind < 0 || ind >= MAX_DEBUG)
                { fprintf(stderr, "invalid debug value '%s'\n", &buf[1]); }
                 else { debug[ind] = false; }

            } else if (c == 'I') {
                for (i = 0; i < exclude_protocols_count; i++) {
                    fprintf(stderr, "%x ", exclude_protocols[i]);
                }
                fprintf(stderr, "\n");
                if (sscanf(&buf[1], "%x", &excl) != 1) {
                    fprintf(stderr, "invalid protocol '%s'\n", &buf[1]);

                } else {
                    int i;
                    for (i = 0; i < exclude_protocols_count; i++) {
                        if (exclude_protocols[i] == excl) { break; }
                    }
                    for (; i < exclude_protocols_count - 1; i++) {
                        exclude_protocols[i] = exclude_protocols[i + 1];
                        i++;
                    }
                    exclude_protocols_count--;
                }
                for (i = 0; i < exclude_protocols_count; i++) {
                    fprintf(stderr, "%x ", exclude_protocols[i]);
                }
                fprintf(stderr, "\n");

            } else if (c == 'n') {
                readum = false;
                fprintf(stderr, "don't always read the device..\n");

            } else if (c == 'g') {
                if (1 != sscanf(&buf[1], "%d", &get_lines)) {
                    fprintf(stderr, "invalid line count '%s'\n", &buf[1]);
                    get_lines = 0;
                } else {
                    fprintf(stderr, "read %d lines", get_lines);
                    if (get_lines != 0) {
                        fprintf(stderr, " ('g 0' to get the prompt back)..\n");
                    }
                    fprintf(stderr, "\n");
                }

            } else if (c == 'q') {
                fprintf(stderr, "bye..\n");
                exit(0);
            }
        }

        /* see if there is something on an interface being monitored */

        if (FD_ISSET(dev1_socket, &read_set)) {
            read_socket = dev1_socket;
            read_ifindex = dev1_ifindex;

        } else if (dev2_socket != -1 && FD_ISSET(dev2_socket, &read_set)) {
            read_socket = dev2_socket;
            read_ifindex = dev2_ifindex;

        } else {
            continue;
        }

        /* read the packet in from the interface */
        memset(&recv_arg, 0, sizeof(recv_arg));
        recv_arg.sll_family = AF_PACKET;
        recv_arg.sll_ifindex = read_ifindex;
        recv_arg.sll_protocol = htons(ETH_P_ALL);

        result = recvfrom(read_socket, buf, read_len, 0,
                (struct sockaddr *) &recv_arg, &recv_arg_len);
        if (result == -1) {
            fprintf(stderr, "recvfrom errno %d\n", errno);
            perror("recvfrom xxx ");
        }

        if (debug[0]) { fprintf(stderr, "proto %x\n", eth_header->h_proto); }

        /* is its protocol type one of the ones we are supposed to ignore? */
        skip = false;
        for (i = 0; i < exclude_protocols_count; i++) {
            if (debug[0]) {
                fprintf(stderr, "compare %x to %x\n", eth_header->h_proto,
                        htons(exclude_protocols[i]));
            }
            if (eth_header->h_proto == htons(exclude_protocols[i])) {
                if (debug[0]) { fprintf(stderr, "match.\n"); }

                skip = true;
            }
        }
        if (skip) {
            continue;
        }

        /* if we are reading just N lines, decrement the line count */
        count++;
        if (!readum && (get_lines > 0)) {
            get_lines--;
        }

        /* print output for this message */

        fprintf(stderr, "count %d\n", count);
        print_prompt = true;

        if (printum) {
            print_message(stderr, (byte *) buf, result);
        }
    }
}
