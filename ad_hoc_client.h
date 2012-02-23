/* ad_hoc_client.h - manage ad-hoc devices that are not part of the cloud
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: ad_hoc_client.h,v 1.11 2012-02-22 18:55:24 greg Exp $
 */
#ifndef AD_HOC_CLIENT_H
#define AD_HOC_CLIENT_H

#include "cloud.h"
#include "util.h"
#include "mac.h"

/* status of ad-hoc clients */
#define AD_HOC_CLIENT_UNKNOWN 0
#define AD_HOC_CLIENT_MINE 1
#define AD_HOC_CLIENT_OTHER 2

/* we allow vanilla client boxes to participate as leaves in the spanning
 * tree.  (they require no special cloud mesh software, but must be able
 * to operate in ad-hoc mode.)
 */
typedef struct {
    mac_address_t client;       /* mac address of the vanilla box */
    mac_address_t server;       /* mac address of cloud box it attaches to */
    byte my_client;             /* does the current cloud box own it? */
    int sig_strength;           /* my signal strength to the client box */
    int owner_sig_strength;     /* strength of current owner to client box */
} ad_hoc_client_t;


extern ad_hoc_client_t ad_hoc_clients[AD_HOC_CLIENTS_ACROSS_CLOUD];
extern int ad_hoc_client_count;

extern void trim_ad_hoc_client(mac_address_t mac_address);
extern void print_ad_hoc_clients();
extern void update_sig_strength(ad_hoc_client_t *c);
extern void check_ad_hoc_client_improvement();
extern void process_ad_hoc_bcast_block_msg(message_t *message, int d);
extern void process_ad_hoc_bcast_unblock_msg(message_t *message, int d);
extern void ad_hoc_clients_unserved(stp_recv_beacon_t *b);
extern void ad_hoc_client_add(mac_address_t client);
extern void ad_hoc_client_update_my_beacon(int *count);
extern void ad_hoc_client_process_stp_beacon(message_t *message);
extern void ad_hoc_client_forward_msg(message_t *message, int msg_len, int d);
extern bool_t ignore_ad_hoc_bcast(message_t *msg, int msg_len);

#endif
