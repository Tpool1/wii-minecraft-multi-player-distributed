/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "lan_frame.h"
#include <string.h>

enum { FIRST = 1, LAST = 2 };

static uint32_t get32(const uint8_t* p) {
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8
		| p[3];
}
static void put32(uint8_t* p, uint32_t v) {
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = v;
}
size_t lan_frame_pack(uint8_t* out, size_t cap, uint8_t kind, uint32_t epoch,
					  uint32_t seq, const uint8_t* data, size_t n) {
	if(!n || n > LAN_RECORD_MAX || kind < LAN_HELLO || kind > LAN_PING)
		return 0;
	size_t start = 0, written = 0,
		   frames = (n + LAN_FRAME_DATA - 1) / LAN_FRAME_DATA;
	if(n + frames * LAN_HEADER > cap)
		return 0;
	while(start < n) {
		size_t len = n - start;
		if(len > LAN_FRAME_DATA)
			len = LAN_FRAME_DATA;
		uint8_t* h = out + written;
		memcpy(h, "FCVX", 4);
		h[4] = 1;
		h[5] = kind;
		h[6] = 0;
		h[7] = (start == 0 ? FIRST : 0) | (start + len == n ? LAST : 0);
		put32(h + 8, len);
		put32(h + 12, epoch);
		put32(h + 16, seq);
		put32(h + 20, 0);
		memcpy(h + LAN_HEADER, data + start, len);
		start += len;
		written += LAN_HEADER + len;
	}
	return written;
}

static bool start_frame(struct lan_reader* r) {
	const uint8_t* h = r->header;
	if(memcmp(h, "FCVX", 4) || h[4] != 1 || h[5] < LAN_HELLO || h[5] > LAN_PING
	   || h[6] || (h[7] & ~(FIRST | LAST)) || get32(h + 20) != 0)
		return false;
	size_t len = get32(h + 8);
	if(!len || len > LAN_FRAME_DATA || len > LAN_RECORD_MAX - r->record_used)
		return false;
	if(h[7] & FIRST) {
		if(r->assembling)
			return false;
		r->message = (struct lan_header) {h[5], get32(h + 12), get32(h + 16)};
		r->assembling = true;
	} else if(!r->assembling || r->message.kind != h[5]
			  || r->message.epoch != get32(h + 12)
			  || r->message.seq != get32(h + 16))
		return false;
	r->flags = h[7];
	r->remain = len;
	return true;
}

bool lan_frame_feed(struct lan_reader* r, const uint8_t* data, size_t n,
					lan_record_fn cb, void* arg) {
	while(n) {
		if(r->header_used < LAN_HEADER) {
			size_t take = LAN_HEADER - r->header_used;
			if(take > n)
				take = n;
			memcpy(r->header + r->header_used, data, take);
			r->header_used += take;
			data += take;
			n -= take;
			if(r->header_used != LAN_HEADER)
				continue;
			if(!start_frame(r))
				return false;
		}
		size_t take = r->remain < n ? r->remain : n;
		memcpy(r->record + r->record_used, data, take);
		r->record_used += take;
		r->remain -= take;
		data += take;
		n -= take;
		if(r->remain)
			continue;
		r->header_used = 0;
		if(r->flags & LAST) {
			if(!cb(arg, &r->message, r->record, r->record_used))
				return false;
			r->record_used = 0;
			r->assembling = false;
		}
	}
	return true;
}
