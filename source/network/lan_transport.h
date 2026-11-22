/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LAN_TRANSPORT_H
#define LAN_TRANSPORT_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
struct server_rpc;
struct client_rpc;
enum lan_role { LAN_OFFLINE, LAN_HOST, LAN_PEER };
bool lan_configure(enum lan_role role, const char* bind_ipv4,
				   const char* host_ipv4, uint16_t port);
enum lan_role lan_current_role(void);
const char* lan_status(void);
bool lan_host_enabled(void);
void lan_host_disconnect(void);
bool lan_host_connected(void);
size_t lan_host_queued_bytes(void);
bool lan_host_can_send_chunk(void);
/* Host calls belong to its server thread. Outgoing RPC storage stays with the
 * caller. */
void lan_host_pump(void (*link)(bool, void*),
				   void (*command)(struct server_rpc*, void*), void* user);
bool lan_host_send(const struct client_rpc* call);
/* The peer's render thread applies decoded messages; its normal client handler
 * owns the four chunk planes. */
void lan_peer_pump(void (*state)(struct client_rpc*), void (*lost)(void));
bool lan_peer_send(const struct server_rpc* call);
#endif
