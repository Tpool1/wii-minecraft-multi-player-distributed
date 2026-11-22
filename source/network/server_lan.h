/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SERVER_LAN_H
#define SERVER_LAN_H
#include "server_interface.h"
#include "server_local.h"
void server_lan_pump(struct server_local* s);
bool server_lan_local_command(struct server_local* s, struct server_rpc* call);
void server_lan_stream(struct server_local* s);
bool server_lan_save(struct server_local* s);
#endif
