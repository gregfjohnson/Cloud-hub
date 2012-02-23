/* print.c - printing routines to files and to remote hosts for debugging
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: print.c,v 1.10 2012-02-22 19:27:23 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: print.c,v 1.10 2012-02-22 19:27:23 greg Exp $";

#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

#include "util.h"
#include "cloud.h"
#include "print.h"
#include "com_util.h"

FILE *perm_log_file = NULL;
FILE *temp_log_file = NULL;

// char *perm_log_fname = "/tmp/merge_cloud.perm_log";

bool_t eth_print_cmd = false;
bool_t temp_print_cmd = false;
bool_t stdout_print_cmd = false;

/* close, clear, and re-open the permanent log file */
static void setup_perm_log_file()
{
    if (db[55].d) {
        if (perm_log_file != NULL) {
            /* sorta really hope these work, because we don't really
             * have anywhere to log the messages at the moment.
             */
            if (!fclose(perm_log_file)) {
                ddprintf("setup_perm_log_file; fclose failed:  %s.\n",
                        strerror(errno));
            }
            perm_log_file = NULL;

            if (!unlink(perm_log_fname)) {
                ddprintf("setup_perm_log_file; unlink failed:  %s.\n",
                        strerror(errno));
            }
        }

        db[55].d = false;
    }
    if (perm_log_file == NULL) {
        perm_log_file = fopen(perm_log_fname, "w");
        if (perm_log_file == NULL) {
            ddprintf("setup_perm_log_file; fopen failed:  %s.\n",
                    strerror(errno));
        }
    }
}

/* a debug printing routine that sends messages to each of the following
 * places depending on configuration:
 *
 *     - the stream f (usually stderr)
 *     - the permanent log file
 *     - the temporary log file
 *     - over the wire via link-level communication to a development box
 */
int eprintf(FILE *f, const char *msg, ...)
{
    char msg_buf[256];
    va_list args;

    va_start(args, msg);
    vsnprintf(msg_buf, 256, msg, args);
    va_end(args);

    if (!db[27].d || stdout_print_cmd) {
        fprintf(f, "%s", msg_buf);
    }

    if (db[35].d) {
        setup_perm_log_file();

        if (perm_log_file != NULL) {
            fprintf(perm_log_file, "%s", msg_buf);
            fflush(perm_log_file);
        }
    }

    if (temp_print_cmd) {
        fprintf(temp_log_file, "%s", msg_buf);
        fflush(temp_log_file);
    }

    if ((db[18].d || eth_print_cmd) && do_ll_shell && did_com_util_init) {
        com_util_printf((byte *) msg_buf, strlen(msg_buf));
    }
    
    return 0;
}

/* a debug printing routine that sends messages to each of the following
 * places depending on configuration:
 *
 *     - stderr
 *     - the permanent log file
 *     - the temporary log file
 *     - over the wire via link-level communication to a development box
 */
void ddprintf(const char *msg, ...)
{
    char msg_buf[256];
    va_list args;

    va_start(args, msg);
    vsnprintf(msg_buf, 256, msg, args);
    va_end(args);

    if (!db[27].d || stdout_print_cmd) {
        fprintf(stderr, "%s", msg_buf);
    }

    if (db[35].d) {
        setup_perm_log_file();

        if (perm_log_file != NULL) {
            fprintf(perm_log_file, "%s", msg_buf);
            fflush(perm_log_file);
        }
    }

    if (temp_print_cmd) {
        fprintf(temp_log_file, "%s", msg_buf);
        fflush(temp_log_file);
    }

    if ((db[18].d || eth_print_cmd) && do_ll_shell && did_com_util_init) {
        com_util_printf((byte *) msg_buf, strlen(msg_buf));
    }
}
