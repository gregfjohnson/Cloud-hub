/* ll_shell_ftp.c - link-level utility for remote shell access and file transfer
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: ll_shell_ftp.c,v 1.10 2012-02-22 18:55:25 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: ll_shell_ftp.c,v 1.10 2012-02-22 18:55:25 greg Exp $";

/* link-level utility to provide remote shell access and file transfer.
 * depending on command-line arguments, this program operates in one of
 * four modes:
 *   - server mode; it stays up indefinitely until told to quit,
 *         responding to requests.  starts a local interactive shell to
 *         operate like telnetd and respond to remote telnet requests.
 *
 *   - remote interactive client mode; like telnet - read shell commands
 *         from stdin, send them to remote server to be given to remote
 *         interactive shell, print shell outputs received from remote shell.
 *
 *   - batch file transfer "send file" mode:  send file to remote server to
 *         be saved on remote machine.  exit when file is sent.
 *
 *   - batch file transfer "receive file" mode:  request that remote server
 *         send a file over the wire.  exit when file is received.
 *         (as of 2005/12/03 receive file seems broken.)
 *
 * Typical calling sequence (become super user on machine_a and machine_b):
 *    Assume eth1 is the device to use on machine_a, with mac address
 *    00:10:5A:81:E4:D2, and eth2 is the device to use on machine_b.
 *
 *    start server version on machine_a:
 *    machine_a; ll_shell_ftp -s -e eth1
 *
 *    start interactive shell client on machine_b:
 *    machine_b; ll_shell_ftp -e eth2 -d 00:10:5A:81:E4:D2
 *      pwd
 *      /home/greg/hostap/bin
 *      uptime
 *      8:42am  up 32 days,  2:36,  8 users,  load average: 0.00, 0.00, 0.00
 *      (interrupt to exit)
 *
 *    send a file:
 *    machine_b; ll_shell_ftp -e eth2 -d 00:10:5A:81:E4:D2 -p foo
 *        (file ends up in /tmp/foo on server side)
 */
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <features.h>    /* for the glibc version number */
#include <asm/types.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>   /* The L2 protocols */
#include <sys/ioctl.h>
#include <net/if.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <ctype.h>
#include <limits.h>

#include "mac.h"
#include "util.h"
#include "pio.h"
#include "com_util.h"

#define DEBUG0 0

/* debug variables */
static char_str_t db[] = {
    {0, "dump eth device messages"},
    {0, "send_message"},
    {0, "do_read"},
    {0, "select"},
    {0, "setup"},
    {0, "receive_message"},
    {-1, NULL},
};

/* message format for messages.  move this to com_util.h! */
#define BUF_LEN 1490
typedef struct {
    struct ethhdr eth_header;
    msg_type_t msg_type;
    int msg_body_len;
    unsigned char msg_body[BUF_LEN];
} message_t;

static struct ifreq get_index;
static int packet_socket;
static message_t buf;
static struct sockaddr_ll send_arg;
static char *dev_name = "eth0";

static int eth_if_index;

static char server = 0;
static char shell_client = 1;
static char *get_file_name = 0;
static char receiving = 0;
static char cloud_db = 0;
static int fd;
static int recv_sequence = 1;

#define MAX_FILES 10

static char *put_file_names[MAX_FILES];
static int put_file_count = 0;

static bool_t passive_receive_client = false;

static mac_address_t my_mac_addr = {0x00, 0x0C, 0x41, 0x76, 0x6C, 0x28 };
static mac_address_t dest_mac_addr = {0,0,0,0,0,0,};

static char *cooked_in_file = NULL;
static char *cooked_out_file = NULL;

static int in_fd, out_fd;
static pio_t in_pio, out_pio;
static char have_in_pio = 0, have_out_pio = 0;

static int test_len = -1;
static int protocol = 0x0806;

void bp1(void)
{
    fprintf(stderr, "hi from bp1.\n");
}

/* kill interactive shell if we had one, exit the process */
static void exit_program(int arg)
{
    if (server) {
        if (com_util_exit() == -1) {
            perror("exit_program kill failed");
            exit(1);
        }
    }
    exit(0);
}

static struct sockaddr_ll send_arg;

#define MAX_BODY 10000

typedef struct {
    struct ethhdr eth_header;
    byte msg_body[MAX_BODY];
} test_message_t;

/* just zap some bits out the interface.  handy for debugging network
 * connectivity; do an ll_dump/tcpdump/tethereal on the other side and
 * see if we got anything.
 */
static void test_send(int buflen)
{
    int i, j, result;
    test_message_t msg;

    printf("hi from test_send..  sending %d bytes\n", buflen);

    if (buflen < sizeof(msg.eth_header) || buflen > sizeof(msg)) {
        fprintf(stderr, "invalid test length %d\n", buflen);
        exit(1);
    }

    if (cooked_out_file == NULL) {
        memset(&send_arg, 0, sizeof(send_arg));
        send_arg.sll_family = AF_PACKET;
        send_arg.sll_ifindex = eth_if_index;
        send_arg.sll_halen = 6;

        mac_copy(send_arg.sll_addr, dest_mac_addr);
    }

    // msg.eth_header.h_proto = htons(0x0806);
    msg.eth_header.h_proto = htons(protocol);

    mac_copy(msg.eth_header.h_dest, dest_mac_addr);
    mac_copy(msg.eth_header.h_source, my_mac_addr);

    for (i = 0; i < buflen - sizeof(msg.eth_header); i++) {
        msg.msg_body[i] = '.';
    }

    for (i = 0; i < buflen - sizeof(msg.eth_header); i += 16) {
        char buf[17];
        if (i <= buflen - sizeof(msg.eth_header) - 16) {
            sprintf(buf, "%16d", i + 30);
            for (j = 0; j < 16; j++) {
                if (buf[j] != ' ') {
                    msg.msg_body[i + j] = buf[j];
                }
            }
        }
    }

    {
        if (cooked_out_file == NULL) {
            result = sendto(packet_socket, (void *) &msg, buflen, 0,
                    (struct sockaddr *) &send_arg, sizeof(send_arg));
        } else if (have_out_pio) {
            result = pio_write(&out_pio, (void *) &msg, buflen);
        } else {
            result = write(out_fd, (void *) &msg, buflen);
        }

        if (result == -1) {
            fprintf(stderr, "sendto/write errno %d (%s)", errno,
                    strerror(errno));
            if (errno == EMSGSIZE) {
                fprintf(stderr, "; message size %d", buflen);
            }
            fprintf(stderr, "\n");
        } else {
            printf("sendto result %d\n", result);
        }
    }
}

/* send a message; if buf is too long, break it into chunks and send them
 * separately.  label the messsage as having ll_shell_ftp protocol type
 * "msg_type".
 */
static void send_message(unsigned char *buf, int buflen, msg_type_t msg_type)
{
    int result;
    message_t msg;

    if (db[1].d) {
        printf("hi from send_message..  sending %d bytes\n", buflen);
    }

    if (cooked_out_file == NULL) {
        memset(&send_arg, 0, sizeof(send_arg));
        send_arg.sll_family = AF_PACKET;
        send_arg.sll_ifindex = eth_if_index;
        send_arg.sll_halen = 6;

        mac_copy(send_arg.sll_addr, dest_mac_addr);
    }

    msg.eth_header.h_proto = htons(0x2985);

    mac_copy(msg.eth_header.h_dest, dest_mac_addr);
    mac_copy(msg.eth_header.h_source, my_mac_addr);

    while (buflen > 0) {
        int next_gulp;

        if (buflen > BUF_LEN) {
            next_gulp = BUF_LEN;
        } else {
            next_gulp = buflen;
        }
        memcpy((void *) msg.msg_body, buf, next_gulp);
        msg.msg_body_len = next_gulp;

        msg.msg_type = msg_type;

        buf += next_gulp;
        buflen -= next_gulp;

        if (db[1].d) {
            fprintf(stderr, "sending %ld bytes..\n",
                    (unsigned long) &(msg.msg_body[next_gulp])
                    - (unsigned long) &msg);
        }

        if (cooked_out_file == NULL) {
            result = sendto(packet_socket, (void *) &msg,
                    (char *) &(msg.msg_body[next_gulp]) - (char *) &msg, 0,
                    (struct sockaddr *) &send_arg, sizeof(send_arg));
        } else if (have_out_pio) {
            result = pio_write(&out_pio, (void *) &msg,
                    (char *) &(msg.msg_body[next_gulp]) - (char *) &msg);
        } else {
            result = write(out_fd, (void *) &msg,
                    (char *) &(msg.msg_body[next_gulp]) - (char *) &msg);
        }

        if (result == -1) {
            fprintf(stderr, "sendto/write errno %d (%s)\n", errno,
                    strerror(errno));
        } else {
            #if DEBUG0
                printf("sendto result %d\n", result);
            #endif
        }
    }
}

/* read a client interactive shell command from stdin and send it to
 * the remote server.
 *
 * if we intend to talk to (non-interactive) merge_cloud in the embedded
 * box, we jigger up a shell command that puts our command into
 * /tmp/cloud.db on the other side.  (merge_cloud periodically sees if
 * there is anything in that file.)
 * (either start this client with '-c' on the command line, or preface
 * the typed in text with "!".)
 */
static void do_read(int fd, msg_type_t msg_type)
{
    char buf[1024];
    int result = read(fd, buf, 1024);
    if (result == -1) {
        perror("read of stdin failed");
    } else {
        if ((cloud_db || (result >= 1 && buf[0] == '!'))
            && msg_type == shell_cmd_msg)
        {
            char buf2[256];
            int start, i;
            buf[result] = '\0';
            if (result >= 1 && buf[0] == '!') {
                start = 1;
            } else {
                start = 0;
            }
            for (i = 0; i < 1024; i++) {
                if (buf[i] == '\0') { break; }
                if (buf[i] == '\n') { buf[i] = '\0'; break; }
            }
            snprintf(buf2, 256, "echo '%s' > /tmp/cloud.db\n", &buf[start]);
            strcpy(buf, buf2);
            result = strlen(buf);
        }
        if (db[2].d) {
            fprintf(stderr, "do_read message from fd %d:  '%s'\n", fd, buf);
            print_message(stderr, (unsigned char *) &buf, result);
        }
        send_message((byte *) buf, result, msg_type);
    }
}

/* read a message from the other side.
 * ignore it if the message type doesn't indicate it's an ll_shell_ftp
 * message, or if the dest mac address indicates it's for someone other
 * than us.
 */
static int receive_message(message_t *msg)
{
    struct sockaddr_ll recv_arg;
    socklen_t recv_arg_len;
    int result;

    if (cooked_in_file == NULL) {
        memset(&recv_arg, 0, sizeof(recv_arg));
        recv_arg.sll_family = AF_PACKET;
        recv_arg.sll_ifindex = get_index.ifr_ifindex;
        recv_arg.sll_protocol = htons(ETH_P_ALL);

        recv_arg_len = sizeof(recv_arg);
        result = recvfrom(packet_socket, (void *) msg, sizeof(*msg),
                MSG_TRUNC, (struct sockaddr *) &recv_arg, &recv_arg_len);
    } else if (have_in_pio) {
        result = pio_read(&in_pio, (void *) msg, sizeof(*msg));
    } else {
        result = read(in_fd, (void *) msg, sizeof(*msg));
    }

    if (result == -1) {
        if (db[5].d) {
            fprintf(stderr, "recvfrom errno %d (%s)\n", errno, strerror(errno));
        }
        result = -1;
        goto done;
    }

    if (((unsigned char *) msg)[0] == 0x41) {
        if (db[5].d) {
            fprintf(stderr, "receive_message; 0x41\n");
        }
        result = 0;
        goto done;
    }

    if (msg->eth_header.h_proto != htons(0x2985)) {
        if (db[5].d) {
            fprintf(stderr, "receive_message; 0x41\n");
        }
        result = 0;
        goto done;
    }

    if (0 != mac_cmp(my_mac_addr, msg->eth_header.h_dest)) {
        if (db[5].d) {
            fprintf(stderr, "rejecting msg not for me.\n");
        }
        result = 0;
        goto done;
    }

    if (!server || receiving) {
        if (0 != mac_cmp(dest_mac_addr, msg->eth_header.h_source)) {
            if (db[5].d) {
                fprintf(stderr, "rejecting msg not for me.\n");
            }
            result = 0;
            goto done;
        }
    }

    done:
    return result;
}

/* wait up to a tenth of a second to receive a sequence number back from the
 * other side.  we are expecting the sequence number to equal msg_seq.
 * and, we are sent back a message length, which we expect to be equal to
 * msg_len.
 * return 0 iff everything is happy.
 * this is duplicated in com_util.c.  combine them for code cleanup at
 *  some point.
 */
static int recv_seq_message(int msg_len, int msg_seq)
{
    int len, seq;
    message_t msg;
    int result;

    while (1) {
        if (have_in_pio) {
            result = pio_timed_read_ok(&in_pio, 1);
            if (result != 1) {
                fprintf(stderr, "timeout waiting for response.\n");
                return 1;
            }
        } else {
            int result;
            struct timeval timer = {1, 0};
            fd_set read_set;
            int fd;
            FD_ZERO(&read_set);
            if (cooked_in_file == NULL) {
                fd = packet_socket;
            } else {
                fd = in_fd;
            }
            FD_SET(fd, &read_set);
            result = select(fd + 1, &read_set, 0, 0, &timer);
            if (result == -1) {
                perror("select problem");
                return 1;
            }
            if (result != 1) {
                fprintf(stderr, "timeout waiting for response.\n");
                return 1;
            }
            if (db[3].d) {
                fprintf(stderr, "recv_seq_message select returned %d:  ",
                        result);
                {
                    int i;
                    for (i = 0; i < fd + 1; i++) {
                        if (FD_ISSET(i, &read_set)) {
                            fprintf(stderr, "%d ", i);
                        }
                    }
                    fprintf(stderr, "\n");
                }
            }
        }

        result = receive_message(&msg);

        if (result == 0) { continue; }

        if (result == -1) { return -1; }

        if (0 != mac_cmp(my_mac_addr, msg.eth_header.h_dest)) {
            // fprintf(stderr, "rejecting msg not for me.\n");
            continue;
        }

        if (msg.msg_type != recv_ok_msg && msg.msg_type != recv_err_msg
            && msg.msg_type != recv_err_stop_msg)
        {
            continue;
        }

        if (msg.msg_body_len != 8) {
            return -1;
        }

        len = ntohl(* (unsigned long int *) &msg.msg_body[0]);
        seq = ntohl(* (unsigned long int *) &msg.msg_body[4]);

        if (len != msg_len || seq != msg_seq) {
            return -1;
        }

        if (msg.msg_type == recv_err_msg) {
            return -1;
        }

        if (msg.msg_type == recv_err_stop_msg) {
            fprintf(stderr, "got recv_err_stop_msg from server!\n");
            return(-1);
        }

        return 0;
    }
}

/* send a file to the other side as multiple chunks if necessary.
 * expect a message from
 * the other side after each chunk with a sequence number, and
 * resend each chunk up to three times if there is a problem with the
 * sequence number.
 * after three attempts, give up.
 */
static int transfer_file(int fd, char *put_file_name)
{
    int errors = 0, tries, got_it;
    int seq = 1;

    /* this funky stuff is because ll_shell_ftp on the linksys is out of
     * sync with a changed buf_len.  maybe change this next time we update
     * linksys firmware.
     */
    #define TBUF_LEN 1000
    unsigned char buf[TBUF_LEN];

    while (1) {
        int result = read(fd, &(buf[4]), TBUF_LEN - 4);
        int seq_buf = htonl(seq);
        memcpy(buf, (char *) &seq_buf, 4);

        if (result == 0) { break; }

        if (result == -1) {
            perror("transfer_file:  read failed");
            fprintf(stderr, "error reading file %s\n", put_file_name);
            return(-1);
        }

        /* might be nicer to have a state-driven conversation, and just
         * one receive.
         */
        got_it = 0;
        for (tries = 0; tries < 3; tries++) {

            send_message(buf, result  + 4, transfer_cont_msg);

            if (recv_seq_message(result, seq) == 0) {
                got_it = 1;
                errors = 0;
                break;
            }
            fprintf(stderr, "ll_shell_ftp trying to resend seq %d\n", seq);
        }

        if (!got_it) {
            if (++errors > 10) {
                fprintf(stderr, "transfer_file:  transmission failed\n");
                return(-1);
            }
        }

        seq++;
    }
    send_message(buf, 1, transfer_end_msg);

    return(0);
}

/* open the local file "fname", and send it to the other side in chunks, using
 * sequence numbers to validate reception of each chunk.
 */
static void put_file(char *fname)
{
    int fd = open(fname, O_RDONLY);

    fprintf(stderr, "putting %s\n", fname);
    if (fd == -1) {
        perror("put_file:  open failed");
        fprintf(stderr, "file that could not be opened:  %s\n", fname);
        exit(1);
    }

    send_message((byte *) fname, strlen(fname) + 1, put_name_msg);

    transfer_file(fd, fname);

    close(fd);
}

/* set up to receive a file from the other side. */
static void start_get_file()
{
    fd = open(get_file_name, O_WRONLY | O_CREAT | O_TRUNC,
            S_IRWXU | S_IRGRP | S_IROTH);

    if (fd == -1) {
        perror("start_get_file:  open failed");
        fprintf(stderr, "file that could not be opened:  %s\n", get_file_name);
        exit(1);
    }

    send_message((byte *) get_file_name, strlen(get_file_name) + 1,
            get_name_msg);
    receiving = 1;
    recv_sequence = 1;
}

static void usage()
{
    fprintf(stderr, "usage:\nll_shell_ftp [-e ethN] [-f beacon_file] \\\n"
            "    [-D debug_index] \\\n"
            "    [-d dest_mac_address] \\\n"
            "    [-i cooked_input_pseudo-device] \\\n"
            "    [-o cooked_output_pseudo-device] \\\n"
            "    [-r | -s | -p put_file | -g get_file]\n");
    exit(1);
}

static void process_args(int argc, char **argv)
{
    char got_my_mac_addr = 0;
    int c;

    while ((c = getopt(argc, argv, "ci:o:g:p:e:E:n:t:P:rscb:d:D:")) != -1) {
        switch (c) {

        case 'i' :
            cooked_in_file = strdup(optarg);
            break;

        case 'o' :
            cooked_out_file = strdup(optarg);
            break;

        case 'e' :
            dev_name = strdup(optarg);
            break;

        case 'E' :
            if (1 != mac_sscanf(my_mac_addr, optarg)) {
                fprintf(stderr, "invalid eth mac address '%s'\n", optarg);
                exit(1);
            }
            got_my_mac_addr = 1;
            break;

        case 'P':
            if (sscanf(optarg, "%x", &protocol) != 1) {
                fprintf(stderr, "invalid protocol '%s'\n", optarg);
                exit(1);
            }
            break;

        case 't':
            if (sscanf(optarg, "%d", &test_len) != 1) {
                fprintf(stderr, "invalid test length '%s'\n", optarg);
                exit(1);
            }
            break;

        case 'D' :
            tweak_db(optarg, db);
            break;


        case 'd' :
            if (1 != mac_sscanf(dest_mac_addr, optarg)) {
                fprintf(stderr, "invalid dest mac address '%s'\n", optarg);
                exit(1);
            }
            break;

        case 'r' :
            passive_receive_client = 1;
            break;

        case 'c' :
            cloud_db = 1;
            break;

        case 's' :
            server = 1;
            shell_client = 0;
            break;

        case 'g' :
            get_file_name = strdup(optarg);
            shell_client = 0;
            break;

        case 'p' : {
            if (put_file_count >= MAX_FILES) {
                fprintf(stderr, "too many '-p' files\n");
                exit(1);
            }
            put_file_names[put_file_count++] = strdup(optarg);
            shell_client = 0;
            break;
        }

        default :
            usage();
        }
    }

    if (!got_my_mac_addr) {
        mac_get(my_mac_addr, dev_name);
    }
}

static int interrupt_pipe[2];

/* we got a SIGUSR1 interrupt, which caused this routine to get called.
 * if this program is doing a blocking select waiting for something to
 * happen, break it out of the select.
 * go find any files /tmp/merge_cloud.temp_log.NNN_done
 * and send them over the wire to the interactive client ll_shell_ftp
 * on the other side.
 */
static void read_temp_files_int(int arg)
{
    char c = 'x';
    int result = write(interrupt_pipe[1], &c, 1);
    if (result == -1) {
        fprintf(stderr, "send_interrupt_pipe_char; write failed:  %s\n",
                strerror(errno));
        return;
    }
}

int main(int argc, char **argv)
{
    struct sockaddr_ll bind_arg;
    int result;
    struct sigaction action;
    int i;

    process_args(argc, argv);

    if (server) {
        FILE *f;
        char buf[256];
        pid_t pid;
        if (com_util_start_shell() != 0) {
            fprintf(stderr, "com_util_start_shell failed.\n");
            exit(1);
        }

        if (pipe(interrupt_pipe) == -1) {
            fprintf(stderr, "main; pipe creation failed:  %s\n",
                strerror(errno));
            exit(1);
        }
        pid = getpid();
        sprintf(buf, "/tmp/ll_shell_ftp.pid.%d", pid);
        f = fopen(buf, "w");
        fprintf(f, "%d\n", pid);
        fclose(f);
        if (rename(buf, "/tmp/ll_shell_ftp.pid") == -1) {
            fprintf(stderr, "rename failed:  %s\n", strerror(errno));
            exit(1);
        }
    }

    signal(SIGINT, exit_program);
    signal(SIGQUIT, exit_program);
    signal(SIGHUP, exit_program);
    signal(SIGTERM, exit_program);

    memset(&action, 0, sizeof(action));
    action.sa_handler = read_temp_files_int;
    sigaction(SIGUSR1, &action, NULL);

    if (cooked_in_file == NULL || cooked_out_file == NULL) {
        packet_socket = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
        if (db[4].d) { fprintf(stderr, "socket result %d\n", packet_socket); }

        sprintf(get_index.ifr_name, dev_name);
        result  = ioctl(packet_socket, SIOCGIFINDEX, &get_index);
        if (db[4].d) {
            fprintf(stderr, "ioctl result %d, ifindex %d\n", result,
                    get_index.ifr_ifindex);
        }
        eth_if_index = get_index.ifr_ifindex;

        memset(&bind_arg, 0, sizeof(bind_arg));
        bind_arg.sll_family = AF_PACKET;
        bind_arg.sll_ifindex = get_index.ifr_ifindex;
        bind_arg.sll_protocol = htons(ETH_P_ALL);

        result = bind(packet_socket, (struct sockaddr *) &bind_arg,
                sizeof(bind_arg));
        if (db[4].d) {
            perror("bind perror");
            fprintf(stderr, "bind result %d; errno %d\n", result, errno);
        }
    }

    if (cooked_in_file != NULL) {
        in_fd = repeat_open(cooked_in_file, O_RDONLY);
        if (in_fd == -1) {
            fprintf(stderr, "could not open %s\n", cooked_in_file);
            exit_program(1);
        }
        if (pio_init_from_fd(&in_pio, cooked_in_file, in_fd)) {
            fprintf(stderr, "could not pio_init %s\n", cooked_in_file);
            exit_program(1);
        }
        have_in_pio = 1;
    }

    if (cooked_out_file != NULL) {
        out_fd = repeat_open(cooked_out_file, O_RDWR);
        if (out_fd == -1) {
            fprintf(stderr, "could not open %s\n", cooked_out_file);
            exit_program(1);
        }
        if (pio_init_from_fd(&out_pio, cooked_out_file, out_fd)) {
            fprintf(stderr, "could not pio_init %s\n", cooked_out_file);
            exit_program(1);
        }
        have_out_pio = 1;
    }

    com_util_init(packet_socket, eth_if_index,
            have_out_pio ? &out_pio : 0,
            my_mac_addr, dest_mac_addr);
            
    for (i = 0; i < put_file_count; i++) {
        put_file(put_file_names[i]);
    }


    if (put_file_count > 0) {
        exit_program(0);
    }

    if (test_len != -1) {
        test_send(test_len);
        exit_program(0);
    }

    if (get_file_name != 0) {
        start_get_file();
    }

    if (shell_client) {
        com_util_start_client_shell(passive_receive_client);
        fprintf(stderr, "connected\n");
    }

    while (1) {
        int read_fd;
        fd_set read_set;
        int max_fd = -1;

        FD_ZERO(&read_set);

        if (!have_in_pio || !pio_read_ok(&in_pio)) {

            if (cooked_in_file == NULL) {
                read_fd = packet_socket;
            } else if (have_in_pio) {
                read_fd = in_pio.fd;
            } else {
                read_fd = in_fd;
            }

            FD_SET(read_fd, &read_set);
            max_fd = (read_fd > max_fd) ? read_fd : max_fd;

            if (shell_client) {
                FD_SET(0, &read_set);
                max_fd = (0 > max_fd) ? 0 : max_fd;

            } else if (server) {
                com_util_set_select(&max_fd, &read_set);
                FD_SET(interrupt_pipe[0], &read_set);
                if (interrupt_pipe[0] > max_fd) { max_fd = interrupt_pipe[0]; }
            }

            if (db[3].d) {
                print_select(stderr, "before select", &read_set, 0, max_fd);
            }
            if (-1 == (result = select(max_fd + 1, &read_set, 0, 0, 0))) {
                perror("main:  select failed");
                continue;
            }

            if (db[3].d) {
                print_select(stderr, "after select", &read_set, result, max_fd);
            }
        }

        if ((have_in_pio && pio_read_ok(&in_pio))
            || FD_ISSET(read_fd, &read_set))
        {
            result = receive_message(&buf);

            if (result <= 0) { continue; }

            if (db[0].d) {
                fprintf(stderr, "message from eth device %d:\n", packet_socket);
                print_message(stderr, (unsigned char *) &buf, result);
            }

            if (shell_client) {
                int seq;
                byte recv_buf[8];
                int int_buf;
                seq = ntohl(*(unsigned long int *) &(buf.msg_body[0]));

                if (!passive_receive_client && seq != recv_sequence) {
                    // send_message(recv_buf, 1, recv_err_msg);
                } else {
                    if (-1 == write(1, 4 + (char *) buf.msg_body,
                        buf.msg_body_len - 4))
                    {
                        perror("write to stdout failed");
                        continue;
                    }

                    int_buf = htonl(buf.msg_body_len - 4);
                    memcpy(&recv_buf[0], (char *) &int_buf, 4);

                    if (passive_receive_client) {
                        int_buf = htonl(seq);
                        memcpy(&recv_buf[4], (char *) &int_buf, 4);
                    } else {
                        int_buf = htonl(recv_sequence);
                        memcpy(&recv_buf[4], (char *) &int_buf, 4);
                    }

                    send_message(recv_buf, 8, recv_ok_msg);

                    recv_sequence++;
                }

            } else if (server || receiving) {
                com_util_process_message((void *) &buf);
            }
        }

        if (shell_client && FD_ISSET(0, &read_set)) {
            do_read(0, shell_cmd_msg);
        }

        if (server) {
            com_util_shell_input(&read_set);
        }

        if (FD_ISSET(interrupt_pipe[0], &read_set)) {
            /* clear everything out of the interrupt pipe */
            read_all_available(interrupt_pipe[0]);
            // fprintf(stderr, "off to com_util_send_temp_files..\n");
            com_util_send_temp_files("/tmp/merge_cloud.temp_log");
        }
    }
}
