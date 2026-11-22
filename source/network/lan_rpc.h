/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LAN_RPC_H
#define LAN_RPC_H
#include "client_interface.h"
#include "server_interface.h"
size_t lan_encode_command(uint8_t* dst, size_t cap,
						  const struct server_rpc* call);
bool lan_decode_command(const uint8_t* src, size_t size,
						struct server_rpc* out);
size_t lan_encode_state(uint8_t* dst, size_t cap,
						const struct client_rpc* call);
bool lan_decode_state(const uint8_t* src, size_t size, struct client_rpc* out);
void lan_free_chunk(struct client_rpc* call);
#endif
