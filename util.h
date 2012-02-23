/* util.h - generic utility data types ("bool_t" etc.) and functions
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: util.h,v 1.12 2012-02-22 19:27:23 greg Exp $
 */
#ifndef UTIL_H
#define UTIL_H

#include <math.h>
#include <stdio.h>
#include <sys/types.h>

/* used with tweak_db_vec - see comment there. */
#define UTIL_DB_VEC_MAX 16
extern int util_db_vec[UTIL_DB_VEC_MAX];

typedef unsigned char byte;

/* define boolean type and values */
typedef byte bool_t;

typedef enum {
    false = 0,
    true = 1,
} bool_value_t;

/* debugging array entry; the value of the debug entry, and a descriptive name
 * for the entry.
 */
typedef struct {
    char d;
    char *str;
} char_str_t;

/* we use a function-pointer scheme for printing messages.  sometimes
 * we just want to print the message to an open stream, other times we
 * want to send the message over the wire from an embedded box to a
 * development box.
 */
typedef int ddprintf_t(FILE *f, const char *msg, ...);

extern int fprint(FILE *f, const char *msg, ...)
        __attribute__((format(printf,2,3)));
extern void print_message(FILE *f, unsigned char *buf, int msg_len);
extern void print_select(FILE *f, char *title, fd_set *set, int select_result,
        int max_fd);
extern void fn_print_message(ddprintf_t *fn, FILE *f, unsigned char *buf,
        int msg_len);
extern int repeat_open(char *fname, int flags);
extern int tweak_db(char *arg, char_str_t *db);
extern void tweak_db_vec(char *arg, char_str_t *db);
extern void tweak_db_int(int arg, char_str_t *db);
extern bool_t file_exists(char *fname);
extern char *bool_string(bool_t bool);
extern long long timeval_diff(struct timeval *tv1, struct timeval *tv2);
extern char checked_gettimeofday(struct timeval *tv);
extern long long usec_diff(long sec1, long usec1, long sec2, long usec2);
extern void usec_add_msecs(long *sec, long *usec, int msec);
extern void update_time(long *out_sec, long *out_usec, long sec, long usec);
extern void util_print_time(FILE *f, struct timeval *tv);
extern void util_fn_print_time(ddprintf_t *fn, FILE *f, struct timeval *tv);
extern void read_all_available(int fd);
extern bool_t util_debug_print(bool_t b);
extern int skip_comment_line(FILE *f);
extern unsigned int hash_bytes(byte *bytes, int len);
extern void print_space(ddprintf_t *fn, FILE *f, int space);
extern void copy_string(char *dest, char *src, int len);

#endif
