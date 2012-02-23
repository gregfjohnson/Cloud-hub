/* com_util.c - link-level utilities to support a remote shell and file transfer
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: com_util.c,v 1.9 2012-02-22 18:55:24 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: com_util.c,v 1.9 2012-02-22 18:55:24 greg Exp $";

/* link-level utility to support a remote shell and file transfer */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>

#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <limits.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <netinet/in.h>
#include <net/if.h>

#include <dirent.h>

#include "pio.h"
#include "util.h"
#include "com_util.h"
#include "mac.h"

static char_str_t db[] = {
    /*  0 */ {0, "dump eth device messages"},
    /*  1 */ {0, "send_message"},
    /*  2 */ {0, "read_and_fwd_shell_output"},
    /*  3 */ {0, "select"},
    /*  4 */ {0, "setup"},
    /*  5 */ {0, "error messages to stderr"},
    /*  6 */ {0, "com_util_process_message"},
    {-1, NULL},
};

#define BUF_LEN 1490

typedef enum {
    state_nominal,
    state_receiving_file,
    state_sending_file,
    state_receiving_shell_output,
} state_t;

static int state = state_nominal;

typedef struct {
    struct ethhdr eth_header;
    msg_type_t msg_type;
    int msg_body_len;
    unsigned char msg_body[BUF_LEN];
} message_t;

static int send_part_of_file(int fd);
static void end_sending_file();

static int new_stdin[2];
static int new_stdout[2];
static int new_stderr[2];

static pid_t shell_pid;

static pid_t ll_shell_ftp_server_pid = -1;

static struct ifreq get_index;
static int packet_socket;
// static message_t buf;
static struct sockaddr_ll send_arg;
// static char *dev_name = "eth0";

static int eth_if_index;

static char server = 0;
static char shell_client = 1;
// static char *get_file_name = 0;
static char *put_file_name = 0;
static char receiving = 0;
static int fd;
static int recv_sequence;
static mac_address_t my_mac_addr = {0x00, 0x0C, 0x41, 0x76, 0x6C, 0x28 };
static mac_address_t dest_mac_addr = {0,0,0,0,0,0,};
static mac_address_t zero_mac_addr = {0,0,0,0,0,0,};

static char *cooked_in_file = NULL;
static char *cooked_out_file = NULL;

static int in_fd, out_fd;
static pio_t in_pio, *out_pio;
static char have_in_pio = 0, have_out_pio = 0;

static int temp_file_suffix = 0;

static int com_errors = 0;

static int recv_seq_message(int msg_len, int msg_seq);
static int verified_send_message(unsigned char *buf, int buflen,
        msg_type_t msg_type);
static void send_message(unsigned char *buf, int buflen, msg_type_t msg_type);

/* set up link-level communication from eth_mac_addr (my end) to
 * client_mac_addr (his end).  interface has already been opened,
 * with raw socket "socket" and interface index if_index.
 * if pio is non-null, use pipe-based communication in simulation environment.
 */
void com_util_init(int socket, int if_index, pio_t *pio,
        mac_address_t eth_mac_addr, mac_address_t client_mac_addr)
{
    if (pio != NULL) {
        out_pio = pio;
        have_out_pio = 1;
    }

    packet_socket = socket;
    eth_if_index = if_index;

    mac_copy(my_mac_addr, eth_mac_addr);
    mac_copy(dest_mac_addr, client_mac_addr);
}

/* shut down the connection; kill the interactive shell on the client side. */
int com_util_exit()
{
    int result = kill(shell_pid, SIGHUP);
    if (result == -1) {
        perror("com_util_exit kill failed");
    }
    return result;
}

/* clean up (kill interactive shell if this is server-side)
 * and exit the executable.
 */
static void exit_program(int arg)
{
    if (server) {
        com_util_exit();
    }
    exit(0);
}

/* send a message to the server telling it to start an interactive shell
 * so that we can send shell commands and get them executed remotely.
 */
int com_util_start_client_shell(bool_t passive)
{
    byte buf[16];
    if (passive) {
        send_message(buf, 1, start_shell_msg);
        fprintf(stderr, "com_util_start_client_shell;\n"
                "sent passive start_shell_msg; retry manually if necessary.\n");
    } else {
        if (0 != verified_send_message(buf, 1, start_shell_msg)) {
            fprintf(stderr, "com_util_start_client_shell; "
                    "could not send start_shell_msg\n");
            return -1;
        }
    }
    return 0;
}

/* start server-side interactive shell.  we set up pipes to the shell
 * for stdin, stdout, and stderr, and then fork and exec the shell.
 * client can then send us messages containing shell commands, which we
 * can give to the shell, and then send messages back with the shell's
 * output.
 */
int com_util_start_shell()
{
    server = 1;
    shell_client = 0;

    if (-1 == (pipe(new_stdin))) {
        perror("start_shell error trying to create stdin pipe:");
        return(1);
    }

    if (-1 == (pipe(new_stdout))) {
        perror("start_shell error trying to create stdout pipe:");
        return(1);
    }

    if (-1 == (pipe(new_stderr))) {
        perror("start_shell error trying to create stderr pipe:");
        return(1);
    }

    if (-1 == (shell_pid = fork())) {
        perror("start_shell error trying to fork:");
        return(1);
    }

    if (shell_pid != 0) { return 0; }

    if (-1 == dup2(new_stdin[0], 0)) {
        perror("start_shell error trying to dup2 stdin pipe");
        exit(1);
    }

    #if 0
    if (-1 == close(new_stdin[0])) {
        perror("start_shell error trying to close stdin pipe");
        return(1);
    }
    #endif

    if (-1 == dup2(new_stdout[1], 1)) {
        perror("start_shell error trying to dup2 stdin pipe:");
        exit(1);
    }

    #if 0
    if (-1 == close(new_stdout[0])) {
        perror("start_shell error trying to close stdout pipe");
        exit(1);
    }
    #endif

    if (-1 == dup2(new_stderr[1], 2)) {
        perror("start_shell error trying to dup2 stderr pipe:");
        exit(1);
    }

    #if 0
    if (-1 == close(new_stderr[0])) {
        perror("start_shell error trying to close stderr pipe");
        exit(1);
    }
    #endif

    if (-1 == execl("/bin/sh", "/bin/sh", "-i", NULL)) {
        perror("start_shell error trying to execl /bin/sh:");
        exit(1);
    }

    return 0;
}

static struct sockaddr_ll send_arg;

/* send buf to the other side.  if we are using a link-level raw socket,
 * set up the "sendto" arguments to shove
 * the bits through the raw socket interface.
 * if an IP connection, do the "write()".
 * if a pipe, send the bytes through the pipe.
 *
 * if the buffer is too
 * long to fit in one sendto message, break the
 * buffer up into multiple chunks and send them separately.
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
            result = pio_write(out_pio, (void *) &msg,
                    (char *) &(msg.msg_body[next_gulp]) - (char *) &msg);
        } else {
            result = write(out_fd, (void *) &msg,
                    (char *) &(msg.msg_body[next_gulp]) - (char *) &msg);
        }

        if (result == -1) {
            com_errors++;
            if (db[5].d) {
                fprintf(stderr, "sendto/write errno %d (%s)\n", errno,
                        strerror(errno));
            }
        } else {
            #if DEBUG0
                printf("sendto result %d\n", result);
            #endif
        }
    }
}

static int seq = 1;

/* send the message and then wait for an ack from the receiver.
 * retry 3 times before giving up.
 * the caller is expected to leave 4 available bytes at the beginning
 * of the buffer he gives us, so we can put a message sequence number there.
 */
static int verified_send_message(unsigned char *buf, int buflen,
        msg_type_t msg_type)
{
    int errors = 0, tries, got_it;
    int seq_buf = htonl(seq);
    memcpy(buf, (char *) &seq_buf, 4);

    /* might be nicer to have a state-driven conversation, and just
     * one receive.
     */
    got_it = 0;
    for (tries = 0; tries < 3; tries++) {

        send_message(buf, buflen + 4, msg_type);

        if (recv_seq_message(buflen, seq) == 0) {
            got_it = 1;
            errors = 0;
            break;
        }
        fprintf(stderr, "com_util trying to resend seq %d\n", seq);
    }

    if (!got_it) {
        if (++errors > 10) {
            fprintf(stderr, "transfer_file:  transmission failed\n");
            return(-1);
        }
    }

    seq++;
    return 0;
}

/* send buf as a sequence of sequence-numbered messages, and verify
 * reception of each message.  (resend if necessary.)
 */
void com_util_printf(unsigned char *buf, int buflen)
{
    int i = 0;
    byte my_buf[1028];

    if (state != state_receiving_shell_output) {
        state = state_receiving_shell_output;
        seq = 1;
    }

    while (i < buflen) {
        int len = ((1024 < (buflen - i)) ? 1024 : buflen - i);
        memcpy(&my_buf[4], &buf[i], len);
        verified_send_message(my_buf, len, shell_response_msg);
        i += len;
    }
}

/* see if there is any output available on fd using select, and if so
 * read it and send it as a verified message to the other side.
 */
static void read_and_fwd_shell_output(int fd)
{
    while (true) {
        byte buf[1024];
        fd_set read_set;
        struct timeval timer = {0, 0};
        int result;

        FD_ZERO(&read_set);
        FD_SET(fd, &read_set);
        result = select(fd + 1, &read_set, 0, 0, &timer);
        if (result == -1) {
            fprintf(stderr, "read_and_fwd_shell_output; select error %s\n",
                    strerror(errno));
        }

        if (result == 0) { break; }

        result = read(fd, (char *) &(buf[4]), 1020);

        if (db[2].d) {
            fprintf(stderr, "read_and_fwd_shell_output message from fd %d:\n",
                    fd);
            print_message(stderr, (unsigned char *) &buf, result);
        }

        if (result == 0) { break; }

        if (result == -1) {
            perror("read of stdin failed");
        } else if (!mac_equal(dest_mac_addr, zero_mac_addr)) {
            if (state != state_receiving_shell_output) {
                state = state_receiving_shell_output;
                seq = 1;
            }
            verified_send_message(buf, result, shell_response_msg);
        }
    }
}

/* there is input available from the other side.  read the input
 * (from link-level raw socket, or ip connection, or pipe depending on
 * how we were initialized.)
 * make sure the message is an ll_shell_ftp message (reject it and
 * return 0 otherwise).
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
            fprintf(stderr,
                    "recvfrom errno %d (%s)\n", errno, strerror(errno));
        }
        result = -1;
        goto done;
    }

    if (((unsigned char *) msg)[0] == 0x41) {
        result = 0;
        goto done;
    }

    if (msg->eth_header.h_proto != htons(0x2985)) {
        result = 0;
        goto done;
    }

    if (0 != mac_cmp(my_mac_addr, msg->eth_header.h_dest)) {
        // fprintf(stderr, "rejecting msg not for me.\n");
        result = 0;
        goto done;
    }

    if (!server || receiving) {
        if (0 != mac_cmp(dest_mac_addr, msg->eth_header.h_source)) {
            // fprintf(stderr, "rejecting msg not for me.\n");
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
                if (db[5].d) {
                    fprintf(stderr, "timeout waiting for response.\n");
                }
                return 1;
            }
        } else {
            int result;
            struct timeval timer = {0, 100000};
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
                if (db[5].d) {
                    fprintf(stderr, "timeout waiting for response.\n");
                }
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

static int fd;

/* this funky stuff is because ll_shell_ftp on the linksys is out of
 * sync with a changed buf_len.  maybe change this next time we update
 * linksys firmware.
 */

#define TBUF_LEN 1000
unsigned char byte_buf[TBUF_LEN];

/* ll_shell_ftp server uses this to send a file from wrt54g to host, when
 * host requests it.
 *
 * the message contains the name of the file to send.
 *
 * find file /tmp/(file to send) on wrt54g and send that file.
 *
 * set state to "state_sending_file".
 */
static void start_sending_file(message_t *msg)
{
    unsigned char buf[512];
    char fname[PATH_MAX];
    unsigned char *p;

    p = &msg->msg_body[msg->msg_body_len - 1];

    while (p > &msg->msg_body[0] && *(p-1) != '/') { p--; }
    snprintf(fname, PATH_MAX, "/tmp/%s", p);

    fd = open(fname, O_RDONLY);

    if (fd == -1) {
        perror("send_file:  open failed");
        fprintf(stderr, "file that could not be opened:  %s\n", fname);
        send_message(buf, 1, recv_err_stop_msg);
        return;
    }

    seq = 1;
    state = state_sending_file;
    send_part_of_file(fd);
}

/* we are in the middle of sending a file.  read the next chunk
 * of the file and send it, using verification.
 */
static int send_part_of_file(int fd)
{
    // int errors = 0, tries, got_it;

    int result = read(fd, &(byte_buf[4]), TBUF_LEN - 4);
    // int seq_buf = htonl(seq);
    // memcpy(byte_buf, (char *) &seq_buf, 4);

    if (result == 0) {
        end_sending_file();
        return 0;
    }

    if (result == -1) {
        perror("transfer_file:  read failed");
        fprintf(stderr, "error reading file %s\n", put_file_name);
        return(-1);
    }

    if (0 != verified_send_message(byte_buf, result, transfer_cont_msg)) {
        return(-1);
    }

    return(0);
}

/* we have gotten to the end of the local file we are sending;
 * close the file descriptor on that file, and send a message to the
 * other side indicating that the whole file has been sent.
 */
static void end_sending_file()
{
    send_message(byte_buf, 1, transfer_end_msg);
    state = state_nominal;
    close(fd);
}

/* read more of the local file and send another chunk to the other side.
 */
static int continue_sending_file(message_t *msg_buf)
{
    // int errors = 0, tries, got_it;
    // int seq = 1;

    /* this funky stuff is because ll_shell_ftp on the linksys is out of
     * sync with a changed buf_len.  maybe change this next time we update
     * linksys firmware.
     */
    #define TBUF_LEN 1000
    unsigned char buf[TBUF_LEN];

    if (state != state_sending_file) { return 0; }

    while (1) {
        int result = read(fd, &(buf[4]), TBUF_LEN - 4);
        // int seq_buf = htonl(seq);
        // memcpy(buf, (char *) &seq_buf, 4);

        if (result == 0) { break; }

        if (result == -1) {
            perror("transfer_file:  read failed");
            fprintf(stderr, "error reading file %s\n", put_file_name);
            return(-1);
        }

        if (0 != verified_send_message(buf, result, transfer_cont_msg)) {
            return(-1);
        }
    }
    send_message(buf, 1, transfer_end_msg);

    return(0);
}

/* we have a message from the other side that it is trying to send us a
 * file.  the message indicates the name the file should have on our side.
 * put it in /tmp.
 * use sequence numbers to make sure sende and receiver stay in sync;
 * start receive sequence at 1 at start of receiving each file.
 */
static void start_receiving_file(message_t *buf)
{
    char fname[PATH_MAX];
    unsigned char *p;

    p = &buf->msg_body[buf->msg_body_len - 1];

    while (p > &buf->msg_body[0] && *(p-1) != '/') { p--; }
    snprintf(fname, PATH_MAX, "/tmp/%s", p);

    /* this is needed so that we can transfer merge_cloud itself.
     * (if we rm it first, the executable continues to run, with its link
     * the the old executable file.  if we don't rm it, we start writing
     * on our own executable file, ruining the file the operating system
     * has a link to.)
     */
    if (file_exists(fname)) {
        int result = unlink(fname);
        if (result == -1) {
            fprintf(stderr, "start_receiving_file:  unlink failed.\n");
            goto done;
        }
    }

    fd = open(fname, O_WRONLY | O_CREAT | O_TRUNC,
            S_IRWXU | S_IRGRP | S_IROTH);

    if (fd == -1) {
        perror("start_receiving_file:  open failed");
        fprintf(stderr, "file that could not be opened:  %s\n", fname);
        goto done;
    }

    state = state_receiving_file;
    receiving = 1;
    recv_sequence = 1;

    done : ;
}

/* gulp the next chunk of the file from the other side.
 * send the other side back the length of the message we received, and
 * our sequence number.  he will re-send if he notices a problem.
 */
static void continue_receiving_file(message_t *buf)
{
    int int_buf;
    unsigned char recv_buf[8];
    int result;
    msg_type_t msg_type;
    int len;
    int seq;

    if (!receiving) {
        fprintf(stderr, "continue_receiving_file:  "
                "got transfer_cont while not receiving.\n");
        return;
    }

    seq = ntohl(*(unsigned long int *) &(buf->msg_body[0]));
    if (seq != recv_sequence) {
        msg_type = recv_err_msg;
        len = 1;
        goto done;
    }
    result = write(fd, &(buf->msg_body[4]), buf->msg_body_len - 4);
    if (result == -1) {
        perror("continue_receiving_file:  error on write");
        msg_type = recv_err_stop_msg;
        len = 1;
    } else if (result != buf->msg_body_len - 4) {
        fprintf(stderr, "continue_receiving_file:  "
                "did not write entire buffer.\n");
        msg_type = recv_err_msg;
        len = 1;
    } else {
        int_buf = htonl(buf->msg_body_len - 4);
        memcpy(&recv_buf[0], (char *) &int_buf, 4);
        int_buf = htonl(recv_sequence);
        memcpy(&recv_buf[4], (char *) &int_buf, 4);
        msg_type = recv_ok_msg;
        len = 8;

        recv_sequence++;
    }

    done:
    send_message(recv_buf, len, msg_type);
}

/* we've gotten a message from the other side saying that he's sent the
 * last of the file being transfered.  close the local file descriptor.
 */
static void end_receiving_file(message_t *buf)
{
    if (!receiving) {
        fprintf(stderr, "end_receiving_file:  "
                "got transfer_end_msg while not receiving.\n");
        return;
    }

    close(fd);
    receiving = 0;
    fprintf(stderr, "end_receiving_file..\n");

    if (!server) {
        exit_program(0);
    }

    state = state_nominal;
}

#if 0
static void start_get_file()
{
    fd = open(get_file_name, O_WRONLY | O_CREAT | O_TRUNC,
            S_IRWXU | S_IRGRP | S_IROTH);

    if (fd == -1) {
        perror("start_get_file:  open failed");
        fprintf(stderr, "file that could not be opened:  %s\n", get_file_name);
        exit(1);
    }

    send_message(get_file_name, strlen(get_file_name) + 1, get_name_msg);
    receiving = 1;
    recv_sequence = 1;
}
#endif

/* translate from integer protocol message number to text string describing
 * the protocol message, for debugging printouts.
 */
static char *com_util_message_string(int msg_type)
{
    char *c;

    switch (msg_type) {
    case shell_cmd_msg:  c = "shell_cmd_msg"; break;
    case shell_response_msg:  c = "shell_response_msg"; break;
    case put_name_msg:  c = "put_name_msg"; break;
    case get_name_msg:  c = "get_name_msg"; break;
    case transfer_cont_msg:  c = "transfer_cont_msg"; break;
    case transfer_end_msg:  c = "transfer_end_msg"; break;
    case recv_ok_msg:  c = "recv_ok_msg"; break;
    case recv_err_msg:  c = "recv_err_msg"; break;
    case recv_err_stop_msg:  c = "recv_err_stop_msg"; break;
    case start_shell_msg:  c = "start_shell_msg"; break;
    case no_op_msg_msg:  c = "no_op_msg_msg"; break;
    default : c = "unknown message"; break;
    }

    return c;
}

/* we've received a message from the other side.  look at the protocol
 * byte in the message and decide what to do with it based on that.
 *
 * if it's a shell command, write the command to our local interactive
 * shell.
 *
 * if its part of the protocol to send or receive a file, do that.
 */
int com_util_process_message(unsigned char *buf)
{
    message_t *message = (message_t *) buf;

    mac_copy(dest_mac_addr,
            *(mac_address_t *) message->eth_header.h_source);

    if (db[6].d) {
        fprintf(stderr, "processing message type %s; set dest_mac_addr to ",
                com_util_message_string(message->msg_type));
        mac_print(stderr, dest_mac_addr);
    }

    if (message->msg_type == shell_cmd_msg) {
        if (-1 == write(new_stdin[1], message->msg_body,
                message->msg_body_len))
        {
            perror("write to stdout of shell failed");
            return -1;
        }
    } else if (message->msg_type == start_shell_msg) {
        byte recv_buf[8];
        int int_buf;
        seq = 1;
        int_buf = htonl(message->msg_body_len - 4);
        memcpy(&recv_buf[0], (char *) &int_buf, 4);
        int_buf = htonl(seq);
        memcpy(&recv_buf[4], (char *) &int_buf, 4);
        send_message(recv_buf, 8, recv_ok_msg);

    } else if (message->msg_type == put_name_msg) {
        start_receiving_file(message);

    } else if (message->msg_type == transfer_cont_msg) {
        continue_receiving_file(message);

    } else if (message->msg_type == transfer_end_msg) {
        end_receiving_file(message);

    } else if (message->msg_type == get_name_msg) {
        start_sending_file(message);

    } else if (message->msg_type == recv_ok_msg
            || message->msg_type == recv_err_msg
            || message->msg_type == recv_err_stop_msg)
    {
        continue_sending_file(message);
    }

    return 0;
}

/* caller wants us to add our local interactive shell's stdout and stderr
 * file descriptors to a select bit set, so his select can notice if our
 * shell has something for us to read.
 */
void com_util_set_select(int *max_fd, fd_set *read_set)
{
    FD_SET(new_stdout[0], read_set);
    if (new_stdout[0] > *max_fd) { *max_fd = new_stdout[0]; }

    FD_SET(new_stderr[0], read_set);
    if (new_stderr[0] > *max_fd) { *max_fd = new_stderr[0]; }
}

/* create a FILE in /tmp named "fname.NNN", where NNN is bumped each time.
 * it is an error to do two of these at the same time; this one should
 * be closed before another one is opened.
 */
FILE *com_util_open_temp_file(char *fname)
{
    char buf[256];
    FILE *f;
    fprintf(stderr, "com_util_open_temp_file..\n");
    sprintf(buf, "%s.%d", fname, temp_file_suffix);
    f = fopen(buf, "w");
    if (f == NULL) {
        fprintf(stderr, "com_util_clear_temp_file; could not fopen '%s':  %s\n",
                fname, strerror(errno));
        return NULL;
    }
    return f;
}

/* send SIGUSR1 to the local ll_shell_ftp_server process.
 * the idea is that a program (i.e., merge_cloud) writes stuff into a
 * file in /tmp, and then signals the ll_shell_ftp process that is running
 * on the internet interface (not the one that merge_cloud is listening on)
 * to pick up that file and send it off the box.
 */
static void signal_temp_file()
{
    if (ll_shell_ftp_server_pid != -1) {
        if (kill(ll_shell_ftp_server_pid, SIGUSR1) != 0) {
            fprintf(stderr, "signal_temp_file; kill problem:  %s\n",
                    strerror(errno));
        }
    }
}

/* hard-loop waiting to find a file containing a process id in /tmp/fname.
 * the other program does a write to a temp file and an atomic move, so
 * as soon as we see the file we can safely read it.
 */
void com_util_get_server_pid(char *fname)
{
    if (ll_shell_ftp_server_pid == -1) {
        while (true) {
            FILE *f = fopen(fname, "r");
            if (f == NULL) {
                sleep(1);
                continue;
            }
            if (fscanf(f, "%d", &ll_shell_ftp_server_pid) != 1) {
                sleep(1);
                continue;
            }

            break;
        }
    }
}

/* caller is done writing to /tmp/fname.NNN.  fclose **f and set it to null.
 * move /tmp/fname.NNN to /tmp/fname.NNN_done.
 */
void com_util_close_temp_file(char *fname, FILE **f)
{
    bool_t got_something = false;
    char buf[256];
    FILE *done;

    if (*f != NULL) {

        sprintf(buf, "%s.%d", fname, temp_file_suffix);
        fflush(*f);
        if (fclose(*f) == EOF) {
            fprintf(stderr, "com_util_close_temp_file; fclose(%s) failed:"
                    "  %s\n", buf, strerror(errno));
        }
        *f = NULL;

        sprintf(buf, "%s.%d_done", fname, temp_file_suffix++);
        if ((done = fopen(buf, "w")) == NULL) {
            fprintf(stderr, "com_util_close_temp_file; fopen(%s) failed:"
                    "  %s\n", buf, strerror(errno));
        }

        if (fclose(done) == EOF) {
            fprintf(stderr, "com_util_close_temp_file; fclose(%s) failed:"
                    "  %s\n", buf, strerror(errno));
        }

        got_something = true;
    }

    if (got_something) {
        signal_temp_file();
    }
}

/* send and then delete every file /tmp/fname.NNN_done that we find.
 */
void com_util_send_temp_files(char *fname)
{
    DIR *dir = opendir("/tmp");
    struct dirent *next_dir;
    char *p;
    int f;

    // fprintf(stderr, "com_util_send_temp_files..\n");
    if (dir == NULL) {
        fprintf(stderr, "com_util_send_temp_files; opendir(%s) failed:  %s\n",
                fname, strerror(errno));
        return;
    }

    while ((next_dir = readdir(dir)) != NULL) {
        char buf2[256];
        sprintf(buf2, "/tmp/%s", next_dir->d_name);
        if (strstr(buf2, fname) && strstr(buf2, "_done")) {
            if (unlink(buf2) == -1) {
                fprintf(stderr, "com_util_send_temp_files; unlink(%s) failed:"
                        "  %s\n", buf2, strerror(errno));
            }

            p = strstr(buf2, "_done");
            *p = '\0';
            f = open(buf2, O_RDONLY);
            if (f == -1) {
                fprintf(stderr, "com_util_send_temp_files; open(%s) failed:"
                        "  %s\n", buf2, strerror(errno));
                continue;
            }
            read_and_fwd_shell_output(f);
            close(f);

            if (unlink(buf2) == -1) {
                fprintf(stderr, "com_util_send_temp_files; unlink(%s) failed:"
                        "  %s\n", buf2, strerror(errno));
            }
        }
    }

    closedir(dir);
}

/* see if read_set indicates that our interactive shell has some available
 * output on either its stdout or its stderr.
 */
void com_util_shell_input(fd_set *read_set)
{
    if (FD_ISSET(new_stdout[0], read_set)) {
        read_and_fwd_shell_output(new_stdout[0]);
    }

    if (FD_ISSET(new_stderr[0], read_set)) {
        read_and_fwd_shell_output(new_stderr[0]);
    }
}
