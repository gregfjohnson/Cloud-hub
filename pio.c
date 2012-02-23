/* pio.c - use pipes to simulate wireless connections; for simulation mode.
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: pio.c,v 1.7 2012-02-22 18:55:25 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: pio.c,v 1.7 2012-02-22 18:55:25 greg Exp $";

/* Use pipes to simulate wireless connections.  This is for simulation
 * testing of the mesh algorithms.  Multiple "cloud boxes" can be
 * simulated on a single development host.
 *
 * This is old out-of-date code that is probably in a state of bit rot.
 * But, having development machine-based simulation infrastructure seems
 * like a very good idea, so let's keep this stuff around until someone
 * gets around to updating it.
 */

#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include "pio.h"

/* check that fd is the file descriptor for a named pipe, and then
 * initialize "pio" to start communicating using that named pipe.
 */
int pio_init_from_fd(pio_t *pio, char *fname, int fd)
{
    int result;
    struct stat stat_buf;

    result = fstat(fd, &stat_buf);

    /* if there's a problem with the file descriptor, error exit. */
    if (result == -1) {
        return(-1);
    }

    if (!S_ISFIFO(stat_buf.st_mode)) {
        fprintf(stderr, "pipe; file %s is apparently not a fifo.\n", fname);
        return(-1);
    }

    pio->bytes_in_pipe = -1;
    pio->fd = fd;
    pio->buf_len = 0;
    pio->seq = 0;
    strncpy(pio->pipe_name, fname, PATH_MAX);

    return 0;
}

/* if there is no file name "fname", create a named pipe of that name.
 * if there is already a named pipe named "fname", or if we had to create
 * it, open it read/write.  initialize pio to be associated with the
 * named pipe.
 */
int pio_open(pio_t *pio, char *fname)
{
    int fd;
    int result;
    struct stat stat_buf;

    result = stat(fname, &stat_buf);

    /* if there's a problem with the file other than that it is missing,
     * error exit. */
    if (result == -1 && errno != ENOENT) {
        return(-1);
    }

    /* if the file is missing, create it. */
    if (result == -1 && errno == ENOENT) {
        result = mkfifo(fname,
                S_IRUSR|S_IWUSR | S_IRGRP|S_IWGRP | S_IROTH|S_IWOTH);
        if (result == -1) {
            perror("pipe; mkfifo failed");
            return(-1);
        }
    } else if (!S_ISFIFO(stat_buf.st_mode)) {
        fprintf(stderr, "pipe; file %s is apparently not a fifo.\n", fname);
        return(-1);
    }

    /* open in read/write so we don't block */
    fd = open(fname, O_RDWR);
    if (fd == -1) {
        perror("pipe; mkfifo failed");
        return(-1);
    }

    return pio_init_from_fd(pio, fname, fd);
}

/* debug-print the state of the pio */
void pio_print(FILE *f, pio_t *pio)
{
    int i;

    fprintf(f, "pipe; %s; fd %d; bytes_in_pipe %d; seq %d; "
            "buf_len %d; message <len seq> ",
            pio->pipe_name, pio->fd, pio->bytes_in_pipe, pio->seq,
            pio->buf_len);

    i = 0;
    while (i < pio->buf_len) {
        int len = ntohs(*(short *) &pio->buf[i]);
        fprintf(stderr, "<%d %d> ", len, (int) pio->buf[i + 2]);
        i += len + 3;
    }
    fprintf(stderr, "\n");
}

/* see if there is any input available to be read from the named pipe,
 * with a blocking select that will time out after "seconds".
 */
static int pio_read_select(pio_t *pio, int seconds)
{
    int return_value = 0;
    int result;
    struct timeval timeout = {0, 0};
    fd_set read_set;

    timeout.tv_sec = seconds;

    FD_ZERO(&read_set);
    FD_SET(pio->fd, &read_set);

    result = select(pio->fd + 1, &read_set, 0, 0, &timeout);

    if (result == -1) {
        return_value = -1;
        goto done;
    }

    if (FD_ISSET(pio->fd, &read_set)) {
        return_value = 1;
        goto done;
    }

    done:
    return return_value;
}

/* do a non-blocking query of the pio to see if there's anything in it
 * available to be read.
 */
int pio_read_ok(pio_t *pio)
{
    if (pio->buf_len > 0) {
        return 1;
    }

    return pio_read_select(pio, 0);
}

/* see if there is any input available to be read from the pio
 * (either its internal buffer or its associated named pipe),
 * with a blocking select that will time out after "seconds".
 */
int pio_timed_read_ok(pio_t *pio, int seconds)
{
    if (pio->buf_len > 0) {
        return 1;
    }

    return pio_read_select(pio, seconds);
}

/* see if the pio can accept a message of length n without blocking. */
int pio_write_ok(pio_t *pio, int n)
{
    int return_value = 0;
    int result;
    struct timeval timeout = {0, 0};
    fd_set write_set;

    /* for the length header */
    n += 3;

    if (n > PIPE_CAPACITY) {
        return_value = -1;
        goto done;
    }

    if (pio->bytes_in_pipe >= 0 && (PIPE_CAPACITY - pio->bytes_in_pipe >= n)) {
        return_value = 1;
    }

    /* for debugging */
    if (/*!return_value*/ 1) {
        FD_ZERO(&write_set);
        FD_SET(pio->fd, &write_set);

        result = select(pio->fd + 1, 0, &write_set, 0, &timeout);

        if (result == -1) {
            return_value = -1;
            goto done;
        }

        if (FD_ISSET(pio->fd, &write_set)) {
            return_value = 1;
            pio->bytes_in_pipe = 0;
            goto done;
        }
    }

    done:
    return return_value;
}

/* array bounds checking; make sure that p is between start and end */
static char check_range(void *start, void *end, void *p)
{
    return (p >= start && p <= end);
}

/* array bounds checking; verify that bytes
 * [src_start .. src_start + cpy_len - 1]
 * are inside src_buf, and
 * [dest_start .. dest_start + cpy_len - 1]
 * are inside dest_buf.
 *
 * if not, print text and the arguments, and error-exit.
 */
static void check_ranges(void *dest_buf, int dest_buf_len,
        void *src_buf, int src_buf_len,
        void *dest_start, void *src_start, int cpy_len,
        char *text)
{
    char error = 0;
    char *problem;

    if (cpy_len < 0) {
        error = 1;
        problem = "negative copy length";
        goto done;
    }

    if (!check_range(dest_buf, dest_buf + dest_buf_len - 1, dest_start)) {
        error = 1;
        problem = "dest_start out of range";
        goto done;
    }

    if (!check_range(dest_buf, dest_buf + dest_buf_len - 1,
            dest_start + cpy_len - 1))
    {
        error = 1;
        problem = "dest_end out of range";
        goto done;
    }

    if (!check_range(src_buf, src_buf + src_buf_len - 1, src_start)) {
        error = 1;
        problem = "src_start out of range";
        goto done;
    }

    if (!check_range(src_buf, src_buf + src_buf_len - 1,
            src_start + cpy_len - 1))
    {
        error = 1;
        problem = "src_end out of range";
        goto done;
    }

    done:
    if (error) {
        fprintf(stderr, text);
        fprintf(stderr, "dest buf %lx, dest len %d\n", (unsigned long) dest_buf,
                dest_buf_len);
        fprintf(stderr, "src buf %lx, src len %d\n", (unsigned long) src_buf,
                src_buf_len);
        fprintf(stderr, "dest start %lx, src start %lx, copy len %d\n",
                (unsigned long) dest_start, (unsigned long) src_start, cpy_len);
        exit(1);
    }
}

static unsigned char read_write_buf[PIPE_CAPACITY];

/* non-blocking read; if there is nothing in the pio, error-return.
 * otherwise, read and return one message.
 * throw messages away if we are getting clogged, i.e., the writer
 * is producing data faster than we can consume it.
 */
int pio_read(pio_t *pio, unsigned char *buf, int buf_len)
{
    int msg_len;
    unsigned char *p;
    char did_read = 0;
    char from_pio_buf;
    int bytes_read;

    if (!pio_read_ok(pio)) {
        // fprintf(stderr, "!pio_read_ok\n");
        return(-1);
    }

    /* is there anything available in the pipe we are attached to? */
    if (pio_read_select(pio, 0)) {

        /* read the entire contents of the pipe. */
        bytes_read = read(pio->fd, read_write_buf, PIPE_CAPACITY);
        if (bytes_read == -1) {
            fprintf(stderr, "pio_read; read -1:  %s\n", strerror(errno));
            return -1;
        }

        did_read = 1;

        /* drop messages out of our pio buffer if necessary so that after
         * we return the newly read message and the remaining buffered
         * messages will fit in the pio's buffer.
         */
        while (1) {
            int msg_len;

            /* how many bytes will be left after we return a message? */
            int total = bytes_read + pio->buf_len;
            if (pio->buf_len > 0) {
                total -= ntohs(*(short *) &pio->buf[0]) + 3;
            } else {
                total -= ntohs(*(short *) &read_write_buf[0]) + 3;
            }

            if (total <= PIPE_BUF_LEN) { break; }

            /* we have to delete a message; slide the other stuff in the
             * pio's buffer to the left over it.
             */
            msg_len = ntohs(*(short *) &pio->buf[0]);
            p = pio->buf + msg_len + 3;

            check_ranges(pio->buf, PIPE_BUF_LEN,
                    pio->buf, PIPE_BUF_LEN,
                    pio->buf, p, pio->buf_len - (msg_len + 3),
                    "pio_read 1");

            memmove(pio->buf, p, pio->buf_len - (msg_len + 3));
            pio->buf_len -= msg_len + 3;
        }
    }

    /* get the message to return from the pio buf if there's anything there,
     * from the recently read device buffer otherwise.
     */
    if (pio->buf_len > 0) {
        from_pio_buf = 1;
        msg_len = ntohs(*(short *) pio->buf);
        p = &pio->buf[3];
    } else {
        from_pio_buf = 0;
        msg_len = ntohs(*(short *) &read_write_buf[0]);
        p = &read_write_buf[3];
    }

    /* (we always consume a message.  maybe we shouldn't consume a message
     * if the caller didn't give us a big enough buffer.)
     */
    /* if the message will fit in the caller's buffer, put it in there. */
    if (msg_len <= buf_len) {

        check_ranges(buf, buf_len,
                (pio->buf_len > 0) ? pio->buf : read_write_buf,
                (pio->buf_len > 0) ? PIPE_BUF_LEN : PIPE_CAPACITY,
                buf, p, msg_len,
                "pio_read to caller");

        memmove(buf, p, msg_len);
        pio->seq = *(p - 1);
    }

    /* if we got the message from our buffer, delete it by sliding stuff
     * to the left over it.
     */
    if (from_pio_buf) {
        if (pio->buf_len - (msg_len + 3) > 0) {
            p = pio->buf + msg_len + 3;

            check_ranges(pio->buf, PIPE_BUF_LEN,
                    pio->buf, PIPE_BUF_LEN,
                    pio->buf, p, pio->buf_len - (msg_len + 3),
                    "pio_read delete buffer msg");

            memmove(pio->buf, p, pio->buf_len - (msg_len + 3));
        }
        pio->buf_len -= (msg_len + 3);
    }

    /* if we did a read off the pipe, save anything we need to from it. */
    if (did_read) {
        int len;

        /* if we are returning a message from the pio buffer, save the entire
         * buffer just read from the pipe.  otherwise, we are returning a
         * message just read from the pipe.  in that case, save the rest
         * of the buffer just read from the pipe if any.
         */
        if (from_pio_buf) {
            p = read_write_buf;
            len = bytes_read;
        } else {
            p = &read_write_buf[3 + ntohs(*(short *) &read_write_buf[0])];
            len = bytes_read - (p - read_write_buf);
        }
        if (len > 0) {
            unsigned char *q = &pio->buf[pio->buf_len];

            check_ranges(pio->buf, PIPE_BUF_LEN,
                    read_write_buf, PIPE_CAPACITY,
                    q, p, len,
                    "pio_read; save rest of pipe read buffer");

            memmove(q, p, len);
            pio->buf_len += len;
        }
    }

    #if 0
    if (msg_len > buf_len) {
        fprintf(stderr, "pio_read:  msg_len %d > buf_len %d\n",
                msg_len, buf_len);
    }
    #endif

    return (msg_len <= buf_len) ? msg_len : -1;
}

/* write the message into the pio, prefixed by a two-byte length field
 * and a one-byte sequence number.
 *
 * don't block; if the write would block, error-return.
 */
int pio_write(pio_t *pio, void *buf, int buf_len)
{
    int result;
    short len = htons((short) buf_len);

    if (pio_write_ok(pio, buf_len) != 1) {
        return -1;
    }

    check_ranges(read_write_buf, PIPE_CAPACITY,
            buf, buf_len,
            &read_write_buf[3], buf, buf_len,
            "pio_write");

    memcpy(read_write_buf, &len, 2);
    read_write_buf[2] = pio->seq++;
    memcpy(&read_write_buf[3], buf, buf_len);

    result = write(pio->fd, read_write_buf, buf_len + 3);

    pio->bytes_in_pipe += buf_len + 3;

    return result - 3;
}
