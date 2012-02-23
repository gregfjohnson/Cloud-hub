/* com_util.h - link-level utilities to support a remote shell and file transfer
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: com_util.h,v 1.6 2012-02-22 18:55:24 greg Exp $
 */
#ifndef COM_UTIL_H
#define COM_UTIL_H

#include <stdio.h>
#include "util.h"
#include "mac.h"
#include "pio.h"

/* the protocol message types for link-level interactive remote shell and
 * file transfer.
 */
typedef enum {
    shell_cmd_msg,
    shell_response_msg,
    put_name_msg,
    get_name_msg,
    transfer_cont_msg,
    transfer_end_msg,
    recv_ok_msg,
    recv_err_msg,
    recv_err_stop_msg,
    start_shell_msg,
    no_op_msg_msg,
} msg_type_t;

extern void com_util_init(int socket, int if_index, pio_t *pio,
        mac_address_t eth_mac_addr, mac_address_t client_mac_addr);
extern int com_util_exit();
extern int com_util_start_shell();
extern int com_util_process_message(unsigned char *message);
extern void com_util_printf(unsigned char *buf, int buflen);
extern void com_util_set_select(int *max_fd, fd_set *read_set);
extern int com_util_start_client_shell(bool_t passive);
extern void com_util_shell_input(fd_set *read_set);
extern void com_util_get_server_pid(char *fname);
extern FILE *com_util_open_temp_file(char *fname);
extern void com_util_close_temp_file(char *fname, FILE **f);
extern void com_util_send_temp_files(char *fname);

#endif
