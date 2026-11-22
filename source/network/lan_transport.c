/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "lan_transport.h"
#include "lan_socket.inc"
#include "lan_frame.h"
#include "lan_rpc.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { TX_CAP = 256 * 1024, IO_BATCH = 8, IO_SIZE = 8192 };
struct buffers {
	struct lan_reader reader;
	uint8_t tx[TX_CAP], record[LAN_RECORD_MAX];
};
struct callbacks {
	void (*link)(bool, void*);
	void (*command)(struct server_rpc*, void*);
	void (*state)(struct client_rpc*);
	void* user;
};
static struct buffers* buf;
static enum lan_role role;
static uint32_t local_ip, remote_ip, epoch, next_epoch, sent_seq, recv_seq;
static uint16_t port;
static game_socket listener = BAD_SOCKET, peer = BAD_SOCKET;
static size_t tx_start, tx_end;
static bool connected, connecting, lost, retrying;
static uint32_t last_rx, last_ping, last_retry;
enum pump_phase {
	P_TOTAL,
	P_ESTABLISH,
	P_TX,
	P_RX,
	P_APPLY,
	P_ENCODE,
	P_COUNT
};
#ifdef PLATFORM_WII
#include "../game/session_trace.h"
/* Only the host authority thread records these counters. Encoding may occur
 * outside the pump, and inside an apply callback, so its time is inclusive. */
static bool profile_enabled;
static unsigned profile_n, profile_windows;
static uint32_t phase_us[P_COUNT], samples_us[P_COUNT][100];
static unsigned tx_calls, rx_calls, tx_bytes, rx_bytes;
static uint32_t profile_mark(void) {
	return profile_enabled ? (uint32_t)ticks_to_microsecs(gettime()) : 0;
}
static void profile_add(enum pump_phase which, uint32_t begin, int bytes) {
	if(!profile_enabled)
		return;
	phase_us[which] += (uint32_t)ticks_to_microsecs(gettime()) - begin;
	if(which == P_TX) {
		++tx_calls;
		if(bytes > 0)
			tx_bytes += (unsigned)bytes;
	}
	if(which == P_RX) {
		++rx_calls;
		if(bytes > 0)
			rx_bytes += (unsigned)bytes;
	}
}
static int compare_sample(const void* a, const void* b) {
	uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b;
	return (x > y) - (x < y);
}
static void profile_finish(uint32_t start) {
	if(!profile_enabled)
		return;
	profile_add(P_TOTAL, start, 0);
	for(unsigned i = 0; i < P_COUNT; i++) {
		samples_us[i][profile_n] = phase_us[i];
		phase_us[i] = 0;
	}
	if(++profile_n < 100)
		return;
	for(unsigned i = 0; i < P_COUNT; i++)
		qsort(samples_us[i], 100, sizeof(uint32_t), compare_sample);
	++profile_windows;
	session_trace("NETPUMP w=%u n=100 p95us=%lu open=%lu send=%lu recv=%lu "
				  "apply=%lu encode=%lu max=%lu",
				  profile_windows, (unsigned long)samples_us[P_TOTAL][94],
				  (unsigned long)samples_us[P_ESTABLISH][94],
				  (unsigned long)samples_us[P_TX][94],
				  (unsigned long)samples_us[P_RX][94],
				  (unsigned long)samples_us[P_APPLY][94],
				  (unsigned long)samples_us[P_ENCODE][94],
				  (unsigned long)samples_us[P_TOTAL][99]);
	session_trace("NETIO w=%u send_calls=%u sent_B=%u recv_calls=%u recv_B=%u",
				  profile_windows, tx_calls, tx_bytes, rx_calls, rx_bytes);
	profile_n = tx_calls = rx_calls = tx_bytes = rx_bytes = 0;
}
#else
#define profile_mark() 0u
#define profile_add(which, begin, n) ((void)(begin))
#define profile_finish(begin) ((void)(begin))
#endif

/* The protocol build tag covers behavior that cannot be inferred from table
 * data. Update it for an incompatible change to simulation or serialization
 * semantics. */
static uint8_t identity[16];
static uint32_t hash_field(uint32_t h, uint32_t v) {
	for(int shift = 24; shift >= 0; shift -= 8)
		h = (h ^ ((v >> shift) & 255u)) * 16777619u;
	return h;
}
static void identity_word(size_t at, uint32_t v) {
	identity[at] = (uint8_t)(v >> 24);
	identity[at + 1] = (uint8_t)(v >> 16);
	identity[at + 2] = (uint8_t)(v >> 8);
	identity[at + 3] = (uint8_t)v;
}
static void make_identity(void) {
	uint32_t h = 2166136261u;
	for(unsigned id = 0; id < 256; id++) {
		struct block* b = blocks[id];
		if(!b)
			continue;
		h = hash_field(h, id);
		for(size_t j = 0; j < sizeof b->name && b->name[j]; j++)
			h = hash_field(h, (uint8_t)b->name[j]);
		struct block_data data = {.type = id}, neighbours[6] = {0};
		struct block_info at = {.block = &data, .neighbours = neighbours};
		for(unsigned md = 0; md < 16; md++) {
			data.metadata = md;
			h = hash_field(
				h, b->getMaterial ? (uint32_t)b->getMaterial(&at) : UINT32_MAX);
		}
		h = hash_field(h,
					   b->luminance | (b->opacity << 4) | (b->flammable << 8)
						   | (b->place_ignore << 9));
		h = hash_field(h, b->digging.hardness);
		h = hash_field(h, b->digging.tool);
		h = hash_field(h, b->digging.min);
		h = hash_field(h, b->digging.best);
	}
	for(unsigned id = 0; id < ITEMS_MAX; id++) {
		struct item* it = items[id];
		if(!it)
			continue;
		h = hash_field(h, 0x10000u + id);
		for(size_t j = 0; j < sizeof it->name && it->name[j]; j++)
			h = hash_field(h, (uint8_t)it->name[j]);
		h = hash_field(h, it->max_stack);
		h = hash_field(h, it->max_damage);
		h = hash_field(h, it->fuel);
		h = hash_field(h, it->has_damage);
		h = hash_field(h, it->tool.type);
		h = hash_field(h, it->tool.tier);
		h = hash_field(h, it->armor.is_armor);
		h = hash_field(h, it->armor.type);
		h = hash_field(h, it->armor.tier);
	}
	identity_word(0, 1);		   /* game message schema */
	identity_word(4, 0x7023e0e0u); /* pinned game base */
	identity_word(8, 1);		   /* multiplayer rules revision */
	identity_word(12, h);		   /* loaded gameplay table fingerprint */
}

static bool private_ip(uint32_t ip) {
	return (ip >> 24) == 127 || (ip >> 24) == 10 || (ip >> 20) == 0xac1
		|| (ip >> 16) == 0xc0a8;
}
static bool address(const char* s, uint32_t* value) {
	if(!s)
		return false;
	uint32_t ip = 0;
	for(int i = 0; i < 4; i++) {
		unsigned octet = 0, count = 0;
		while(*s >= '0' && *s <= '9') {
			if(++count > 3)
				return false;
			octet = octet * 10 + (unsigned)(*s++ - '0');
		}
		if(!count || octet > 255)
			return false;
		ip = (ip << 8) | octet;
		if(i < 3 && *s++ != '.')
			return false;
	}
	if(*s || !private_ip(ip) || (ip & 255u) == 255)
		return false;
	*value = ip;
	return true;
}
bool lan_configure(enum lan_role value, const char* bind_ip,
				   const char* host_ip, uint16_t server_port) {
	if(role != LAN_OFFLINE)
		return false;
	if(value == LAN_OFFLINE)
		return true;
	if((value != LAN_HOST && value != LAN_PEER) || !server_port
	   || !address(bind_ip, &local_ip))
		return false;
	if(value == LAN_PEER
	   && (!address(host_ip, &remote_ip)
		   || (local_ip >> 24 == 127) != (remote_ip >> 24 == 127)))
		return false;
	buf = calloc(1, sizeof *buf);
	if(!buf)
		return false;
#ifdef PLATFORM_WII
	profile_enabled = value == LAN_HOST && session_trace_enabled();
#endif
	make_identity();
	role = value;
	port = server_port;
	next_epoch = (uint32_t)time(NULL) ^ millis();
	return true;
}
enum lan_role lan_current_role(void) {
	return role;
}
bool lan_host_enabled(void) {
	return role == LAN_HOST;
}
bool lan_host_connected(void) {
	return role == LAN_HOST && connected;
}
size_t lan_host_queued_bytes(void) {
	return tx_end - tx_start;
}
bool lan_host_can_send_chunk(void) {
	return lan_host_connected() && lan_host_queued_bytes() < 96 * 1024;
}
const char* lan_status(void) {
	if(role == LAN_HOST)
		return "Hosting shared world";
	if(role == LAN_PEER)
		return connected ? "Receiving shared world" : "Connecting to host";
	return "Local world";
}
static void drop(void) {
	lost = lost || connected;
	if(peer != BAD_SOCKET)
		sock_close(peer);
	peer = BAD_SOCKET;
	connecting = connected = false;
	tx_start = tx_end = 0;
	epoch = sent_seq = recv_seq = 0;
	last_retry = millis();
	retrying = true;
}
void lan_host_disconnect(void) {
	if(role == LAN_HOST)
		drop();
}
static struct sockaddr_in endpoint(uint32_t ip, uint16_t p) {
	struct sockaddr_in a = {0};
#ifdef PLATFORM_WII
	a.sin_len = sizeof a;
#endif
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(ip);
	a.sin_port = htons(p);
	return a;
}
static game_socket open_bound(uint16_t p) {
	game_socket fd = sock_open(AF_INET, SOCK_STREAM, IPPROTO_IP);
#ifdef PLATFORM_WII
	if(fd < 0)
		return BAD_SOCKET;
#else
	if(fd == BAD_SOCKET)
		return BAD_SOCKET;
#endif
	struct sockaddr_in a = endpoint(local_ip, p);
	if(nonblock(fd) < 0 || sock_bind(fd, (struct sockaddr*)&a, sizeof a) < 0) {
		sock_close(fd);
		return BAD_SOCKET;
	}
	return fd;
}
static bool enqueue(uint8_t kind, const uint8_t* data, size_t n) {
	if(peer == BAD_SOCKET || !n)
		return false;
	size_t need = n + ((n + LAN_FRAME_DATA - 1) / LAN_FRAME_DATA) * LAN_HEADER;
	if(need > TX_CAP - (tx_end - tx_start)) {
		drop();
		return false;
	}
	if(need > TX_CAP - tx_end) {
		memmove(buf->tx, buf->tx + tx_start, tx_end - tx_start);
		tx_end -= tx_start;
		tx_start = 0;
	}
	size_t wrote = lan_frame_pack(buf->tx + tx_end, TX_CAP - tx_end, kind,
								  epoch, ++sent_seq, data, n);
	if(!wrote || !sent_seq) {
		drop();
		return false;
	}
	tx_end += wrote;
	return true;
}
bool lan_host_send(const struct client_rpc* call) {
	if(!lan_host_connected())
		return false;
	uint32_t t = profile_mark();
	size_t n = lan_encode_state(buf->record, sizeof buf->record, call);
	if(!n) {
		drop();
		return false;
	}
	bool queued = enqueue(LAN_STATE, buf->record, n);
	profile_add(P_ENCODE, t, 0);
	return queued;
}
bool lan_peer_send(const struct server_rpc* call) {
	if(role != LAN_PEER || !connected)
		return false;
	size_t n = lan_encode_command(buf->record, sizeof buf->record, call);
	return n && enqueue(LAN_COMMAND, buf->record, n);
}
static bool record(void* opaque, const struct lan_header* h, const uint8_t* p,
				   size_t n) {
	struct callbacks* cb = opaque;
	if(recv_seq == UINT32_MAX || h->seq != ++recv_seq)
		return false;
	if(!connected) {
		if(n != sizeof identity || memcmp(p, identity, n))
			return false;
		if(role == LAN_HOST) {
			if(h->kind != LAN_HELLO || h->epoch != 0)
				return false;
			if(++next_epoch == 0)
				++next_epoch;
			epoch = next_epoch;
			if(!enqueue(LAN_WELCOME, identity, sizeof identity))
				return false;
		} else {
			if(h->kind != LAN_WELCOME || !h->epoch)
				return false;
			epoch = h->epoch;
		}
		connected = true;
		if(cb->link)
			cb->link(true, cb->user);
		return peer != BAD_SOCKET;
	}
	if(h->epoch != epoch)
		return false;
	if(h->kind == LAN_PING)
		return n == 1 && p[0] == 0;
	if(role == LAN_HOST) {
		struct server_rpc rpc;
		if(h->kind != LAN_COMMAND || !cb->command
		   || !lan_decode_command(p, n, &rpc))
			return false;
		cb->command(&rpc, cb->user);
	} else {
		struct client_rpc rpc;
		if(h->kind != LAN_STATE || !cb->state || !lan_decode_state(p, n, &rpc))
			return false;
		cb->state(&rpc);
	}
	return peer != BAD_SOCKET;
}
static void opened(void) {
	memset(&buf->reader, 0, sizeof buf->reader);
	recv_seq = sent_seq = 0;
	last_rx = last_ping = millis();
	connecting = false;
	retrying = false;
}
static void establish(void) {
	if(!net_ready())
		return;
	if(role == LAN_HOST) {
		if(listener == BAD_SOCKET) {
			listener = open_bound(port);
			if(listener == BAD_SOCKET) {
				drop();
				return;
			}
			if(sock_listen(listener, 1) < 0) {
				sock_close(listener);
				listener = BAD_SOCKET;
				drop();
				return;
			}
		}
		struct sockaddr_in addr = {0};
		socklen_t length = sizeof addr;
		game_socket candidate
			= sock_accept(listener, (struct sockaddr*)&addr, &length);
#ifdef PLATFORM_WII
		if(candidate < 0)
			return;
#else
		if(candidate == BAD_SOCKET)
			return;
#endif
		uint32_t ip = ntohl(addr.sin_addr.s_addr);
		if(!private_ip(ip) || (local_ip >> 24 == 127) != (ip >> 24 == 127)
		   || nonblock(candidate) < 0) {
			sock_close(candidate);
			return;
		}
		peer = candidate;
		opened();
	} else {
		if(!connecting) {
			peer = open_bound(0);
			if(peer == BAD_SOCKET) {
				drop();
				return;
			}
			connecting = true;
			last_rx = millis();
		}
		struct sockaddr_in host = endpoint(remote_ip, port);
		int r = sock_connect(peer, &host);
		if(r < 0 && pending(r)) {
			if(millis() - last_rx > 5000)
				drop();
			return;
		}
		if(r < 0) {
			drop();
			return;
		}
		opened();
		enqueue(LAN_HELLO, identity, sizeof identity);
	}
}
static void pump(struct callbacks* cb, void (*peer_lost)(void)) {
	if(lost) {
		lost = false;
		if(cb->link)
			cb->link(false, cb->user);
		if(peer_lost)
			peer_lost();
	}
	if(retrying && millis() - last_retry < 500)
		return;
	if(peer == BAD_SOCKET || connecting) {
		uint32_t t = profile_mark();
		establish();
		profile_add(P_ESTABLISH, t, 0);
	}
	if(peer == BAD_SOCKET || connecting)
		return;
	uint32_t now = millis();
	if(now - last_rx >= 5000) {
		drop();
		return;
	}
	if(connected && now - last_ping >= 1000) {
		uint8_t zero = 0;
		if(!enqueue(LAN_PING, &zero, 1))
			return;
		last_ping = now;
	}
	for(unsigned i = 0; i < IO_BATCH && tx_start < tx_end; i++) {
		size_t len = tx_end - tx_start;
		if(len > IO_SIZE)
			len = IO_SIZE;
		uint32_t t = profile_mark();
		int n = sock_send(peer, (const char*)buf->tx + tx_start, len, 0);
		profile_add(P_TX, t, n);
		if(n < 0 && pending(n))
			break;
		if(n <= 0) {
			drop();
			return;
		}
		tx_start += (size_t)n;
	}
	if(tx_start == tx_end)
		tx_start = tx_end = 0;
	/* Allocate the libogc IOS read buffer outside the small server thread
	 * stack. */
	static uint8_t incoming[IO_SIZE] __attribute__((aligned(32)));
	for(unsigned i = 0; i < IO_BATCH; i++) {
		uint32_t t = profile_mark();
		int n = sock_recv(peer, (char*)incoming, sizeof incoming, 0);
		profile_add(P_RX, t, n);
		if(n < 0 && pending(n))
			break;
		t = profile_mark();
		if(n <= 0
		   || !lan_frame_feed(&buf->reader, incoming, (size_t)n, record, cb)) {
			drop();
			return;
		}
		profile_add(P_APPLY, t, 0);
		last_rx = millis();
	}
}
void lan_host_pump(void (*link)(bool, void*),
				   void (*command)(struct server_rpc*, void*), void* user) {
	if(role == LAN_HOST) {
		struct callbacks cb = {.link = link, .command = command, .user = user};
		uint32_t t = profile_mark();
		pump(&cb, NULL);
		profile_finish(t);
	}
}
void lan_peer_pump(void (*state)(struct client_rpc*), void (*lost_cb)(void)) {
	if(role == LAN_PEER) {
		struct callbacks cb = {.state = state};
		pump(&cb, lost_cb);
	}
}
