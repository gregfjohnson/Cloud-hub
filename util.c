/* util.c - generic utility data types ("bool_t" etc.) and functions
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: util.c,v 1.14 2012-02-22 19:27:23 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: util.c,v 1.14 2012-02-22 19:27:23 greg Exp $";

/* a set of generic utility routines and typedefs.
 *
 * this is an API that is usable generically and is not specific to the
 * cloud_hub project.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <netinet/in.h>

#include "util.h"

int util_db_vec[UTIL_DB_VEC_MAX] = { -1, };

static bool_t debug_print = true;

/* set debug printing for the util.[hc] routines.  if on, debug messages
 * go to stderr.
 */
bool_t util_debug_print(bool_t b)
{
    bool_t prev = debug_print;
    debug_print = b;
    return prev;
}

/* we use a function pointer scheme for printing debugging messages.
 * sometimes we want to send debugging messages over the wire from the
 * embedded box to a development box.  sometimes we want them to be
 * printed locally to a file.
 * this routine is a function that can be passed to a function pointer
 * printing routine when in fact we want to print to a local file.
 */
int fprint(FILE *f, const char *msg, ...)
{
    va_list args;
    va_start(args, msg);
    vfprintf(f, msg, args);
    va_end(args);

    return 0;
}

/* return a static string representing the value of the bool, for printing */
char *bool_string(bool_t bool)
{
    char *p;
    switch (bool) {
    default : p = "invalid_bool_t"; break;
    case false : p = "false"; break;
    case true : p = "true"; break;
    }
    return p;
}

void print_space(ddprintf_t *fn, FILE *f, int space)
{
    int i;
    for (i = 0; i < space; i++) { fn(f, " "); }
}

/* print hex values for the values in cbuf, then print char values
 * (use "." for unprintable characters.)
 * expect a max of 16 chars.
 * use function-based printing, so that we can send over the wire to
 * development host if desired.
 */
static void print_chars(ddprintf_t *fn, FILE *f, char *cbuf, int cbuf_len)
{
    int j;
    for (j = cbuf_len; j < 16; j++) {
        fn(f, "  ");
        if (j % 2 == 1) { fn(f, " "); }
    }
    fn(f, "       |");

    for (j = 0; j < cbuf_len; j++) {
        if (isprint((unsigned int) cbuf[j])) {
            fn(f, "%c", cbuf[j]);
        } else {
            fn(f, ".");
        }
    }
    fn(f, "|\n");
}

/* print an ethernet message using function pointers so that the info
 * can be sent over the wire from embedded box to development box if
 * desired.
 * the first line is 14 bytes, and contains source and dest mac address
 * and packet type.
 * subsequent part of message is just 16 bytes per line, printed both
 * in hex and as chars.
 */
void fn_print_message(ddprintf_t *fn, FILE *f, unsigned char *buf, int msg_len)
{
    int i;
    char cbuf[16];
    int this_byte = 0;
    bool_t line_start;
    int len;

    fn(f, "message length:  %d\n", msg_len);
    if (msg_len < 0) {
        fprintf(stderr, "hmmm.  negative message length.\n");
    }
    if (msg_len <= 0) { return; }

    fn(f, "%4d:  ", this_byte);

    len = (14 < msg_len) ? 14 : msg_len;

    for (i = 0; i < len; i++) {
        fn(f, "%02x", 0xff & buf[i]);
        if (i % 2 == 1) { fn(f, " "); }
        cbuf[i] = (char) buf[i];
    }
    print_chars(fn, f, cbuf, len);

    if (msg_len <= 14) { return; }

    msg_len -= 14;
    buf += 14;
    this_byte += 14;
    line_start = true;

    for (i = 0; i < msg_len; i++) {
        if (line_start) {
            fn(f, "%4d:  ", this_byte);
            line_start = false;
            this_byte += 16;
        }
        fn(f, "%02x", 0xff & buf[i]);
        if (i % 2 == 1) { fn(f, " "); }

        cbuf[i % 16] = (char) buf[i];
        if ((i + 1) % 16 == 0) {
            print_chars(fn, f, cbuf, 16);
            line_start = true;
        }
    }

    /* print block of "|...|" text for last (partial) line */
    if (msg_len % 16 != 0) {
        print_chars(fn, f, cbuf, msg_len % 16);
    }
}

/* print an ethernet message to stream f.
 * the first line is 14 bytes, and contains source and dest mac address
 * and packet type.
 * subsequent part of message is just 16 bytes per line, printed both
 * in hex and as chars.
 */
void print_message(FILE *f, unsigned char *buf, int msg_len)
{
    fn_print_message(fprint, f, buf, msg_len);
}

/* print the result of a "select" call to stream f for debugging */
void print_select(FILE *f, char *title, fd_set *set, int select_result,
        int max_fd)
{
    int i;

    fprintf(stderr, "select returned %d:  ", select_result);
    for (i = 0; i < max_fd + 1; i++) {
        if (FD_ISSET(i, set)) {
            fprintf(stderr, "%d ", i);
        }
    }
    fprintf(stderr, "\n ");
}

/* by-god open the file fname.  (if we can't open fname, then the box is
 * severely broken and there's no point in trying to come up.)
 * hard-loop repeatedly trying to open the file, with "open" flags "flags".
 */
int repeat_open(char *fname, int flags)
{
    int i;
    int fd;
    int errno_2;

    for (i = 0; i < 10; i++) {
        fd = open(fname, flags);
        errno_2 = errno;
        fprintf(stderr, "fd:  %d\n", fd);
        if (fd != -1) {
            break;
        } else {
            fprintf(stderr, "open '%s' failed:  %s\n", fname,
                    strerror(errno_2));
        }
    }

    return fd;
}

/* change an element of debug array db[] based on arg.
 * arg contains both the index of db[] to change, and what to change
 * it to.  arg % 1000 give the index of the value to change.
 * 1xxx means set to false, 2xxx means set to true.
 *
 * this routine is typically used when a box wants to tell all the other
 * boxes to change a debug value.
 */
void tweak_db_int(int arg, char_str_t *db)
{
    int i, i_max;
    bool_t new_state;

    if (arg / 1000 == 1) {
        new_state = false;
    } else if (arg / 1000 == 2) {
        new_state = true;
    } else {
        fprintf(stderr, "tweak_db_int; ignoring '%d'\n", arg);
        goto done;
    }
    i = arg % 1000;
    for (i_max = 0; db[i_max].d != -1; i_max++) {
        if (db[i_max].d == -1) { break; }
    }
    if (i >= 0 && i < i_max) {
        db[i].d = new_state;
        if (debug_print) {
            fprintf(stderr, "changed db[%d] to %d\n", i, db[i].d);
        }
    } else {
        fprintf(stderr, "debug index %d out of range\n", i);
        goto done;
    }

    done:;
}

/* return the last integer arg from *db if it is a legal index for db,
 * -1 otherwise.
 * when have the guts, replace body of this routine with call to tweak_db_vec()
 * and return last element of util_db_vec[].
 */
int tweak_db(char *arg, char_str_t *db)
{
    int return_value = -1;
    int i, ind, i_max;
    while (true) {
        int result, len;
        while (*arg != '\0' && !isdigit(*arg)) { arg++; }
        if (*arg == '\0') { break; }
        result = sscanf(arg, "%d%n", &i, &len);
        if (result != 1) {
            fprintf(stderr, "could not find int in '%s'\n", arg);
            goto done;
        }
        arg += len;
        for (i_max = 0; db[i_max].d != -1; i_max++) {
            if (db[i_max].d == -1) { break; }
        }
        ind = i % 1000;
        if (ind >= 0 && ind < i_max) {
            if (i >= 2000) {
                db[ind].d = true;
            } else if (i >= 1000) {
                db[ind].d = false;
            } else {
                db[ind].d = 1 - db[ind].d;
            }
            if (debug_print) {
                fprintf(stderr, "changed db[%d] to %d\n", ind, db[ind].d);
            }
            return_value = ind;
        } else {
            fprintf(stderr, "debug index %d out of range\n", ind);
            goto done;
        }
    }

    done:
    return return_value;
}

/* parse "arg" and set a value in the debug array db[] accordingly.
 * arg is a sequence of debug array indices.  toggle each indexed element.
 * put indices of values toggled into util_db_vec, terminated by -1.
 */
void tweak_db_vec(char *arg, char_str_t *db)
{
    int i, ind, i_max;
    int next_util_db_vec = 0;

    while (true) {
        int result, len;

        /* skip to next digit if any.  (done if no next digit.) */
        while (*arg != '\0' && !isdigit(*arg)) { arg++; }
        if (*arg == '\0') { break; }

        /* read next number, and advance 'arg' over it.  done if read fails. */
        result = sscanf(arg, "%d%n", &i, &len);
        if (result != 1) {
            fprintf(stderr, "could not find int in '%s'\n", arg);
            goto done;
        }
        arg += len;

        /* set i_max to index in db[] of -1, signifying end of db[]. */
        for (i_max = 0; db[i_max].d != -1; i_max++) {
            if (db[i_max].d == -1) { break; }
        }

        /* if 'ind' is in range, change db[ind].d, and append 'ind'
         * to util_db_vec.
         */
        ind = i % 1000;
        if (ind >= 0 && ind < i_max) {
            if (next_util_db_vec >= UTIL_DB_VEC_MAX - 1) {
                fprintf(stderr, "too many debug variables\n");
                goto done;
            }

            if (i >= 2000) {
                db[ind].d = true;
            } else if (i >= 1000) {
                db[ind].d = false;
            } else {
                db[ind].d = 1 - db[ind].d;
            }

            util_db_vec[next_util_db_vec++] = ind;
            util_db_vec[next_util_db_vec] = -1;

            if (debug_print) {
                fprintf(stderr, "changed db[%d] to %d\n", ind, db[ind].d);
            }
        } else {
            fprintf(stderr, "debug index %d out of range\n", ind);
            goto done;
        }
    }

    done:;
}

/* check to see if a file exists.  it's convenient to set up the call to
 * "stat()" here.  and, if stat() fails for a weird reason, print the
 * problem to stderr.
 */
bool_t file_exists(char *fname)
{
    bool_t return_value;
    int result;

    struct stat stat_buf;

    result = stat(fname, &stat_buf);

    if (result == -1 && errno == ENOENT) {
        return_value = false;

    } else if (result == -1) {
        fprintf(stderr, "file_exists; stat(%s) failed:  %s\n",
                fname, strerror(errno));
        return_value = false;
    } else {
        return_value = true;
    }

    return return_value;
}

/* try gettimeofday, and print error to stderr if it doesn't work.
 * can't imagine how gettimeofday would ever fail, but just in case..
 */
char checked_gettimeofday(struct timeval *tv)
{
    static struct timezone tz;
    if (gettimeofday(tv, &tz)) {
        fprintf(stderr, "gettimeofday failed\n");
        return 0;
    } else {
        return 1;
    }
}

/* return <sec1, usec1> - <sec2, usec2>.  if the first is later (larger),
 * the result will be positive.
 */
long long timeval_diff(struct timeval *tv1, struct timeval *tv2)
{
    long long result;
    result = ((long long) (tv1->tv_sec - tv2->tv_sec)) * 1000000;
    return result + tv1->tv_usec - tv2->tv_usec;
}

/* compute "<sec1, usec1> - <sec2, usec2>" and return the result in usecs */
long long usec_diff(long sec1, long usec1, long sec2, long usec2)
{
    long long result;
    result = ((long long) (sec1 - sec2)) * 1000000;
    return result + usec1 - usec2;
}

/* add msec milliseconds to <sec, usec> */
void usec_add_msecs(long *sec, long *usec, int msec)
{
    long long ll_usec = ((long long) *usec) + 1000 * ((long long) msec);

    *sec += ((long) (ll_usec / 1000000));

    *usec = (long) (ll_usec % 1000000);
}

/* if sec is not the flag value -1, and if <sec, usec> is later than
 * <out_sec, out_usec>, assign <sec, usec> to <out_sec, out_usec>
 */
void update_time(long *out_sec, long *out_usec, long sec, long usec)
{
    long long diff;

    if (sec == -1) { return; }

    if (*out_sec == -1) {
        *out_sec = sec;
        *out_usec = usec;
        return;
    }

    diff = usec_diff(*out_sec, *out_usec, sec, usec);

    if (diff > 0) {
        *out_sec = sec;
        *out_usec = usec;
    }
}

/* print time since midnight as hours:minutes:seconds:milliseconds:seq
 * where seq is 0..99, to distinguish if there's more than one call of
 * this routine per millisecond.
 *
 * print to stream f.
 */
void util_print_time(FILE *f, struct timeval *tv)
{
    util_fn_print_time(fprint, f, tv);
}

static int last_msec = -1;
static long last_sec_since_midnight_gmt = -1;
static int sequence;

/* print time since midnight as hours:minutes:seconds:milliseconds:seq
 * where seq is 0..99, to distinguish if there's more than one call of
 * this routine per millisecond.
 *
 * print using function pointers in case we want to send the message
 * over the wire from embedded box to development box.
 */
void util_fn_print_time(ddprintf_t *fn, FILE *f, struct timeval *tv)
{
    char buf[256];
    int hour, min, sec, sec_since_midnight_gmt;

    sec_since_midnight_gmt = tv->tv_sec % 86400;
    hour = sec_since_midnight_gmt / 3600 - 8;
    if (hour < 0) { hour += 24; }
    min = (sec_since_midnight_gmt / 60) % 60;
    sec = sec_since_midnight_gmt % 60;

    if (tv->tv_usec / 1000 != last_msec
            || sec_since_midnight_gmt != last_sec_since_midnight_gmt)
    {
        last_sec_since_midnight_gmt = sec_since_midnight_gmt;
        last_msec = tv->tv_usec / 1000;
        sequence = 0;
    } else {
        sequence++;
    }
    if (sequence >= 100) {
        sequence = 99;
    }

    sprintf(buf, "%02d:%02d:%02d.%03ld.%02d", hour, min, sec,
            tv->tv_usec / 1000,
            sequence);
    fn(f, "%s", buf);
}

/* read and discard all available input from fd. */
void read_all_available(int fd)
{
    /* clear everything out of the interrupt pipe */
    while (1) {
        byte buf[256];
        int result;
        fd_set read_set;
        struct timeval timer = {0, 0};

        FD_ZERO(&read_set);
        FD_SET(fd, &read_set);
        result = select(fd + 1, &read_set, 0, 0, &timer);
        if (result == -1 || !FD_ISSET(fd, &read_set)) {
            break;
        }
        result = read(fd, buf, 256);
        if (result == 0) { break; }
        if (result == -1) {
            fprintf(stderr, "read_all_available; read from pipe had an error:"
                    "  %s\n", strerror(errno));
            break;
        }
    }
}

/* do fgetc() and ungetc() to read and discard
 * "blank*" "#" "not_linefeed*" "linefeed"
 * return 0 if it went ok, -1 otherwise.
 */
int skip_comment_line(FILE *f)
{
    while (1) {
       int c = fgetc(f);
       if (c == EOF) { return 0; }
       if (!isspace(c)) {
           if (c == '#') {
               do {
                   c = fgetc(f);
               } while (c != EOF && c != '\n');
           } else {
               if (!ungetc(c, f)) { return -1; }
               break;
           }
       }
    }

    return 0;
}

/* grab the bytes in 'bytes' four at a time, ntohl() them so that we get
 * the same integer result everywhere, and add them up.  for last few
 * bytes (less than 4 at the end), put them into the high part of an int
 * and add that in too.
 */
unsigned int hash_bytes(byte *bytes, int len)
{
    unsigned int result = 0, rem_len;
    unsigned int data;
    int i;

    for (i = 0; i < len / 4; i++) {
        data = *((int *) &bytes[i * 4]);
        result += ntohl(data);
    }

    rem_len = len % 4;
    if (rem_len > 0) {
        data = 0;
        memcpy(&data, &bytes[len - rem_len], rem_len);
        result += ntohl(data);
    }

    return result;
}

/* copy at most len-1 chars of src string to dest string.
 * terminate on null char, eoln char (not included in output string)
 * or len-1 chars copied.
 * make sure dest string is null-terminated, unlike strncpy.
 */
void copy_string(char *dest, char *src, int len)
{
    int i;

    dest[len - 1] = '\0';
    for (i = 0; i < len - 1; i++) {
        *dest++ = *src;
        if (*src == '\0') { break; }
        if (*src == '\n') { *(dest-1) = '\0'; break; }
        src++;
    }
}
