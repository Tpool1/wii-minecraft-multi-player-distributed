/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Faults exist only between the native test transport and its private loopback
 * socket. */
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lan_frame.h"
#include "lan_rpc.h"
#include "lan_transport.h"

void wire_fixture_registry(bool alternate_material, bool alternate_texture);
enum { COUNT = 200, LAST_ROUND = 11 };
static bool host, failed, completed, rejection, got_hello, saw_eof,
	manual_clock, cut_link;
static const char* mode;
static unsigned round_id, events, acks, closed_links, delayed_sends, chunks;
static uint32_t offset_ms, simulated_ms, due, hold_until, started;
static bool counting_delay, partition_next;
static size_t peak;
static unsigned jitter30, jitter70;
#define REQUIRE(v)                                                             \
	do {                                                                       \
		if(!(v)) {                                                             \
			fprintf(stderr, "FAIL %s:%d %s\n", mode, __LINE__, #v);            \
			failed = true;                                                     \
			return;                                                            \
		}                                                                      \
	} while(0)
uint32_t lan_test_clock(void) {
	return manual_clock ? simulated_ms : GetTickCount() + offset_ms;
}
static int pending_io(void) {
	WSASetLastError(WSAEWOULDBLOCK);
	return SOCKET_ERROR;
}
static int delay_ms(void) {
	if(round_id == 4)
		return 50;
	if(round_id == 5)
		return 100;
	if(round_id == 6)
		return (delayed_sends & 1) ? 30 : 70;
	return 0;
}
int lan_test_send(SOCKET socket, const char* data, size_t n, int flags) {
	uint8_t amended[128];
	if(rejection && n >= LAN_HEADER + 4 && data[5] == LAN_HELLO) {
		if(n > sizeof amended) {
			failed = true;
			return SOCKET_ERROR;
		}
		memcpy(amended, data, n);
		if(!strcmp(mode, "bad-protocol"))
			amended[4] ^= 1;
		if(!strcmp(mode, "bad-build"))
			amended[LAN_HEADER + 4] ^= 1;
		data = (const char*)amended;
		got_hello = true;
	}
	if(cut_link || (hold_until && (int32_t)(hold_until - GetTickCount()) > 0))
		return pending_io();
	if(delay_ms() > 0) {
		if(!counting_delay) {
			int delay = delay_ms();
			if(round_id == 6 && delay == 30)
				++jitter30;
			if(round_id == 6 && delay == 70)
				++jitter70;
			due = GetTickCount() + delay;
			counting_delay = true;
			++delayed_sends;
		}
		if((int32_t)(due - GetTickCount()) > 0)
			return pending_io();
		counting_delay = false;
	}
	if(round_id == 6 && n > 2048)
		n = 2048;
	return send(socket, data, (int)n, flags);
}
int lan_test_recv(SOCKET socket, char* data, size_t n, int flags) {
	if(cut_link)
		return pending_io();
	int got = recv(socket, data, (int)n, flags);
	if(rejection && got_hello) {
		if(got == 0 || (got < 0 && WSAGetLastError() == WSAECONNRESET))
			saw_eof = true;
		if(got > 0) {
			fprintf(stderr, "FAIL rejected client received %d bytes\n", got);
			failed = true;
		}
	}
	return got;
}
static uint8_t ids[32768], md[16384], sk[16384], to[16384];
static struct client_rpc column = {.type = CRPC_CHUNK,
								   .payload.chunk = {.sx = 16,
													 .sy = 128,
													 .sz = 16,
													 .ids = ids,
													 .metadata = md,
													 .lighting_sky = sk,
													 .lighting_torch = to}};
static void link_change(bool up, void* unused) {
	(void)unused;
	if(!up) {
		++closed_links;
		return;
	}
	++round_id;
	started = GetTickCount();
	acks = 0;
	counting_delay = false;
	if(round_id == 1) {
		hold_until = GetTickCount() + 2000;
		REQUIRE(lan_host_send(&column) && lan_host_send(&column));
		peak = lan_host_queued_bytes();
		REQUIRE(peak > 2 * 81920 && peak < 256 * 1024
				&& !lan_host_can_send_chunk());
	}
	for(unsigned k = 1; k <= COUNT; k++) {
		struct client_rpc rpc
			= {.type = CRPC_TIME_SET,
			   .payload.time_set = ((uint64_t)round_id << 32) | k};
		REQUIRE(lan_host_send(&rpc));
	}
}
static void on_command(struct server_rpc* rpc, void* unused) {
	(void)unused;
	REQUIRE(rpc->type == SRPC_ENTITY_ATTACK && rpc->action_seq == acks + 1);
	++acks;
	REQUIRE(rpc->payload.entity_attack.entity_id == (round_id << 16 | acks));
	if(acks != COUNT)
		return;
	unsigned elapsed = GetTickCount() - started;
	printf("round %u, %u+%u messages, elapsed %u ms, egress delay %d ms\n",
		   round_id, COUNT, acks, elapsed, delay_ms());
	if(round_id == 1) {
		REQUIRE(elapsed >= 2000);
		REQUIRE(lan_host_send(&column) && lan_host_send(&column)
				&& lan_host_send(&column));
		peak = lan_host_queued_bytes();
		REQUIRE(peak < 256 * 1024 && !lan_host_send(&column)
				&& !lan_host_connected() && lan_host_queued_bytes() == 0);
		printf(
			"blocked 2 s; peak before cap %zu bytes; overflow ended session\n",
			peak);
	} else if(round_id == 3) {
		manual_clock = true;
		simulated_ms = GetTickCount() + offset_ms;
		cut_link = true;
		partition_next = true;
	} else if(round_id < LAST_ROUND)
		lan_host_disconnect();
	else
		completed = true;
}
static void on_state(struct client_rpc* rpc) {
	REQUIRE(!rejection);
	if(rpc->type == CRPC_CHUNK) {
		++chunks;
		lan_free_chunk(rpc);
		return;
	}
	REQUIRE(rpc->type == CRPC_TIME_SET);
	unsigned which = (unsigned)(rpc->payload.time_set >> 32),
			 seq = (uint32_t)rpc->payload.time_set;
	if(which != round_id) {
		REQUIRE(which == round_id + 1 && seq == 1);
		round_id = which;
		events = 0;
		counting_delay = false;
	}
	REQUIRE(seq == ++events);
	struct server_rpc ack
		= {.type = SRPC_ENTITY_ATTACK,
		   .action_seq = seq,
		   .payload.entity_attack.entity_id = which << 16 | seq};
	REQUIRE(lan_peer_send(&ack));
}
static void client_lost(void) {
	++closed_links;
	if(round_id == LAST_ROUND && events == COUNT)
		completed = true;
}
static void partition_check(void) {
	partition_next = false;
	simulated_ms += 4999;
	lan_host_pump(link_change, on_command, NULL);
	REQUIRE(lan_host_connected());
	++simulated_ms;
	lan_host_pump(link_change, on_command, NULL);
	REQUIRE(!lan_host_connected());
	cut_link = false;
	manual_clock = false;
	offset_ms += 5000;
	puts(
		"PASS stream partition: connected at 4999 ms, disconnected at 5000 ms");
}
int main(int argc, char** argv) {
	if(argc != 3)
		return 2;
	mode = argv[1];
	host = !strcmp(mode, "host");
	rejection = !strncmp(mode, "bad-", 4);
	wire_fixture_registry(!strcmp(mode, "bad-content"), !host);
	if(!lan_configure(host ? LAN_HOST : LAN_PEER, "127.0.0.1", "127.0.0.1",
					  (uint16_t)atoi(argv[2])))
		return 3;
	uint32_t begin = GetTickCount();
	while(!failed && !completed && !saw_eof
		  && GetTickCount() - begin < (rejection ? 3000u : 30000u)) {
		if(host) {
			if(partition_next)
				partition_check();
			else
				lan_host_pump(link_change, on_command, NULL);
		} else
			lan_peer_pump(on_state, client_lost);
		Sleep(1);
	}
	if(rejection && !failed && got_hello && saw_eof && !events && !chunks) {
		printf("PASS %s rejected before snapshot or READY\n", mode);
		return 0;
	}
	if(!rejection && completed && !failed && round_id == LAST_ROUND
	   && closed_links >= 10 && (host || chunks == 2) && jitter30 && jitter70) {
		printf("jitter sends: 30 ms x%u, 70 ms x%u\n", jitter30, jitter70);
		printf("PASS %s: 11 sessions/10 rejoins, %u numbered records in each "
			   "direction, %u scheduled egress delays\n",
			   mode, LAST_ROUND * COUNT, delayed_sends);
		return 0;
	}
	fprintf(stderr, "FAIL %s rounds=%u events=%u closed=%u hello=%d eof=%d\n",
			mode, round_id, events, closed_links, got_hello, saw_eof);
	return 1;
}
