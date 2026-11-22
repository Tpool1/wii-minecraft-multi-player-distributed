/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Two independent processes; no game renderer, storage or global emulator
 * settings. */
#include "lan_rpc.h"
#include "lan_transport.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
static bool is_host, sent, state_ok, done;
static unsigned connections, snapshots, drops;
static void lost_host(void) {
	++drops;
}
static uint8_t a[32768], b[16384], c[16384], d[16384];
static void link(bool up, void* user) {
	(void)user;
	if(!up) {
		++drops;
		return;
	}
	++connections;
	memset(a, 0x2a, sizeof a);
	memset(b, 0xb1, sizeof b);
	memset(c, 0xc2, sizeof c);
	memset(d, 0xd3, sizeof d);
	struct client_rpc rpc = {.type = CRPC_CHUNK,
							 .payload.chunk = {.x = -16,
											   .sx = 16,
											   .sy = 128,
											   .sz = 16,
											   .ids = a,
											   .metadata = b,
											   .lighting_sky = c,
											   .lighting_torch = d}};
	sent = lan_host_send(&rpc);
	memset(a, 0, sizeof a);
	memset(b, 0, sizeof b);
	memset(c, 0, sizeof c);
	memset(d, 0, sizeof d);
}
static void command(struct server_rpc* rpc, void* user) {
	(void)user;
	if(rpc->type == SRPC_BLOCK_DIG && rpc->action_seq == 42
	   && rpc->has_expected_block && rpc->expected_block == 0x2a) {
		state_ok = true;
		struct client_rpc reply
			= {.type = CRPC_TIME_SET,
			   .payload.time_set = UINT64_C(0x123456789abcdef0)};
		lan_host_send(&reply);
	}
	if(rpc->type == SRPC_HOTBAR_SLOT && rpc->payload.hotbar_slot.slot == 8) {
		if(connections == 1)
			lan_host_disconnect();
		else
			done = true;
	}
}
static void state(struct client_rpc* rpc) {
	if(rpc->type == CRPC_CHUNK) {
		++snapshots;
		state_ok = (snapshots == 1 || (snapshots == 2 && drops == 1))
			&& rpc->payload.chunk.x == -16
			&& rpc->payload.chunk.ids[32767] == 0x2a
			&& rpc->payload.chunk.metadata[16383] == 0xb1
			&& rpc->payload.chunk.lighting_sky[16383] == 0xc2
			&& rpc->payload.chunk.lighting_torch[16383] == 0xd3;
		lan_free_chunk(rpc);
		struct server_rpc dig
			= {.type = SRPC_BLOCK_DIG,
			   .action_seq = 42,
			   .has_expected_block = true,
			   .expected_block = 0x2a,
			   .payload.block_dig
			   = {.x = -1, .y = 2, .z = 3, .side = SIDE_TOP, .finished = true}};
		if(state_ok)
			sent = lan_peer_send(&dig);
	} else if(rpc->type == CRPC_TIME_SET
			  && rpc->payload.time_set == UINT64_C(0x123456789abcdef0)) {
		struct server_rpc ack = {.type = SRPC_HOTBAR_SLOT,
								 .action_seq = 43,
								 .payload.hotbar_slot.slot = 8};
		done = lan_peer_send(&ack) && snapshots == 2;
	}
}
int main(int argc, char** argv) {
	if(argc != 3)
		return 2;
	is_host = strcmp(argv[1], "host") == 0;
	if(lan_configure(LAN_HOST, "0.0.0.0", NULL, 45841))
		return 3;
	if(!lan_configure(is_host ? LAN_HOST : LAN_PEER, "127.0.0.1", "127.0.0.1",
					  (uint16_t)atoi(argv[2])))
		return 4;
	DWORD start = GetTickCount();
	while(GetTickCount() - start < 10000 && !done) {
		if(is_host)
			lan_host_pump(link, command, NULL);
		else
			lan_peer_pump(state, lost_host);
		Sleep(1);
	}
	if(!is_host && done)
		for(int i = 0; i < 20; i++) {
			lan_peer_pump(state, lost_host);
			Sleep(1);
		}
	if(!done || !sent || !state_ok || drops < 1) {
		fprintf(stderr, "FAIL %s accepted=%d received=%d done=%d status=%s\n",
				argv[1], sent, state_ok, done, lan_status());
		return 1;
	}
	printf("PASS %s: full owned 80-KiB column, dig precondition, 64-bit "
		   "result, command acknowledgment, reconnect + resnapshot\n",
		   argv[1]);
	return 0;
}
