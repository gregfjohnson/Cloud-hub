/* pio.h - use pipes to simulate wireless connections; for simulation mode.
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: pio.h,v 1.5 2012-02-22 18:55:25 greg Exp $
 */
#ifndef PIO_H
#define PIO_H

#include <limits.h>

#define PIPE_BUF_LEN 8192
#define PIPE_CAPACITY 4096

typedef struct {
    unsigned char seq;
    int fd;
    int bytes_in_pipe;
    char pipe_name[PATH_MAX];
    unsigned char buf[PIPE_BUF_LEN];
    short buf_len;
} pio_t;

int pio_open(pio_t *pio, char *fname);
int pio_init_from_fd(pio_t *pio, char *fname, int fd);
int pio_read_ok(pio_t *pio);
int pio_timed_read_ok(pio_t *pio, int seconds);
int pio_write_ok(pio_t *pio, int n);
int pio_read(pio_t *pio, unsigned char *buf, int buf_len);
int pio_write(pio_t *pio, void *buf, int buf_len);
void pio_print(FILE *f, pio_t *pio);

#endif
