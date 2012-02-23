/* label.c - logging utility to read stdin and print it to a file of finite size
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: label.c,v 1.7 2012-02-22 18:55:25 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: label.c,v 1.7 2012-02-22 18:55:25 greg Exp $";

/* a logging utility that takes stdin and prints it to a specified file.
 *
 * this program is usable generically and is not specific to the
 * cloud_hub project.
 *
 * the file can optionally be specified to be trimmed, in which case
 * older log messages are deleted and the file never gets longer than
 * MAX_FILE_SIZE (currently 16K bytes).
 *
 * each line is labeled by date and time, and a hostname identifier string.
 *
 *  usage:  label [-h hostname] "[-o outfile | -t trim_outfile]
 */
#include <sys/time.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

// #define MAX_FILE_SIZE 4096
#define MAX_FILE_SIZE 16384

static int trim = 0;

static int last_msec = -1;
static long last_sec_since_midnight_gmt = -1;
static int sequence;

static char *out_fname;
static int out_fd;

#if 0
/* debugging breakpoint routine */
static void bp1() { }
#endif

/* print time since midnight to milliseconds, with sequence number appended
 * to distinguish multiple messages in the same millisecond.
 */
static void print_time(FILE *f)
{
    char buf[256];
    struct timeval tv;
    struct timezone tz;
    int hour, min, sec, sec_since_midnight_gmt;

    if (gettimeofday(&tv, &tz)) {
        perror("print_time:  gettimeofday failed");
        return;
    }
    sec_since_midnight_gmt = tv.tv_sec % 86400;
    hour = sec_since_midnight_gmt / 3600 - 8;
    if (hour < 0) { hour += 24; }
    min = (sec_since_midnight_gmt / 60) % 60;
    sec = sec_since_midnight_gmt % 60;

    if (tv.tv_usec / 1000 != last_msec
            || sec_since_midnight_gmt != last_sec_since_midnight_gmt)
    {
        last_sec_since_midnight_gmt = sec_since_midnight_gmt;
        last_msec = tv.tv_usec / 1000;
        sequence = 0;
    } else {
        sequence++;
    }
    if (sequence >= 100) {
        fprintf(stderr, "\n\noy.  sequence >= 100.\n\n");
        sequence = 99;
    }

    sprintf(buf, "%02d:%02d:%02d.%03ld.%02d", hour, min, sec,
            tv.tv_usec / 1000,
            sequence);
    write(out_fd, buf, strlen(buf));
}

static char *hostname = "";

/* print the line prefix; hostname char string and time */
static void print_prefix(FILE *f)
{
    static char c = ' ';
    static char *colon = ":  ";
    if (strlen(hostname) > 0) {
        write(out_fd, hostname, strlen(hostname));
        write(out_fd, &c, 1);
    }
    print_time(f);
    write(out_fd, colon, strlen(colon));
}

/* rewrite the output file to make it small if it's starting to get too big.
 * we grab the last half of the file from disk, then write that out.
 */
static void trim_output()
{
    char buf[MAX_FILE_SIZE / 2];
    struct stat stat_buf;
    int result = fstat(out_fd, &stat_buf);
    if (stat_buf.st_size < MAX_FILE_SIZE) { goto done; }

    result = lseek(out_fd, -MAX_FILE_SIZE / 2, SEEK_END);
    if (result == -1) {
        perror("lseek problem");
        return;
    }
    result = read(out_fd, buf, MAX_FILE_SIZE / 2);
    if (result == -1) {
        perror("read problem");
        return;
    }
    result = close(out_fd);
    if (result == -1) {
        perror("read problem");
        return;
    }
    out_fd = open(out_fname, O_RDWR|O_TRUNC);
    if (out_fd == -1) {
        perror("open problem");
        return;
    }
    result = write(out_fd, buf, MAX_FILE_SIZE / 2);
    if (result == -1) {
        perror("write problem");
        return;
    }

    done:;
}

static void usage()
{
    fprintf(stderr, "usage:  label [-h hostname] "
            "[-o outfile | -t trim_outfile]\n");
    exit(1);
}

static void process_args(int argc, char **argv)
{
    int c;
    int got_outfile = 0;

    while ((c = getopt(argc, argv, "o:t:h:")) != -1) {
        switch (c) {

        case 'o' :
            out_fname = strdup(optarg);
            got_outfile = 1;
            break;

        case 't' :
            trim = 1;
            out_fname = strdup(optarg);
            got_outfile = 1;
            break;

        case 'h' :
            hostname = strdup(optarg);
            break;

        default :
            usage();
        }
    }

    if (!got_outfile) {
        fprintf(stderr, "outfile required.\n");
        usage();
    } else {
        out_fd = open(out_fname, O_RDWR|O_CREAT|O_TRUNC,
                S_IRUSR|S_IWUSR | S_IRGRP | S_IROTH);
    }
}

int main(int argc, char **argv)
{
    int c;
    int new_line = 1;

    process_args(argc, argv);

    while (1) {
        c = getchar();
        if (c == EOF) { break; }
        if (new_line) {
            print_prefix(stdout);
            new_line = 0;
        }
        write(out_fd, &c, 1);
        if (c == '\n') {
            new_line = 1;
        }

        if (trim) { trim_output(); }
    }

    return 0;
}
