/* cloud_box.h - utility routines to manage lists of cloud boxes
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: cloud_box.h,v 1.8 2012-02-22 19:27:22 greg Exp $
 */
#ifndef CLOUD_BOX_H
#define CLOUD_BOX_H

#include "mac.h"
#include "util.h"

/* max length of received, saved message from another box */
#define CLOUD_BUF_LEN 1600

/* a description of a box.  used for nbr_device_list and stp_list. */
typedef struct {

    /* this is the wlan0 mac address of the box and what we consider to be
     * the "name" of the box.
     */
    mac_address_t name;

    /* the mac address of the ethernet card on the box if we see one. */
    mac_address_t eth_mac_addr;
    char has_eth_mac_addr;

    int signal_strength;

    /* used for flow control; instances of this type in stp_list */
    byte send_sequence;
    byte recv_sequence;
    byte recv_sequence_error;
    int send_error, recv_error;
    bool_t received_duplicate;

    /* if we get too many of these, we delete the arc. */
    byte unroutable_count; 

    /* I sent a sequence packet and a message; I'm expecting an ack. */
    bool_t awaiting_ack;

    /* I received a sequence packet and a message; I owe an ack.  Will send
     * it as soon as I get all the ack's I'm waiting for.
     */
    bool_t pending_ack;

    /* I expect a sequence packet (versus a message) */
    bool_t expect_seq;

    /* last message I sent; saving it in case I time out waiting for an ack
     * and need to resend it.
     */
    byte message[CLOUD_BUF_LEN];
    int message_len;

    /* the message length claimed by the last sequence message we got */
    int recv_message_len;

    int perm_io_stat_index;

} cloud_box_t;

typedef enum {
    node_type_cloud_box
} node_type_t;

typedef struct {
    long sec, usec;
    node_type_t type;
    union {
        cloud_box_t box;
    };
} node_t;

typedef char (*node_predicate_t)(node_t *);
typedef void (*node_finalizer_t)(node_t *);

extern void cloud_box_print(ddprintf_t *fn, FILE *f, cloud_box_t *cloud_box,
        int indent);
extern void node_list_print(ddprintf_t *fn, FILE *f, char *title,
        node_t *list, int list_count);
extern void print_cloud_list(cloud_box_t *list, int list_len, char *title);
extern void trim_list(node_t *list, int *list_count, node_predicate_t predicate,
        node_finalizer_t finalizer);

extern mac_address_ptr_t delete_me;
extern char delete_node(node_t *node);

#endif
