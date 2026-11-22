/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LAN_FRAME_H
#define LAN_FRAME_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { LAN_HEADER = 24, LAN_FRAME_DATA = 8192, LAN_RECORD_MAX = 82000 };
enum lan_kind { LAN_HELLO = 1, LAN_WELCOME, LAN_COMMAND, LAN_STATE, LAN_PING };
struct lan_header {
	uint8_t kind;
	uint32_t epoch, seq;
};
struct lan_reader {
	uint8_t header[LAN_HEADER], record[LAN_RECORD_MAX];
	size_t header_used, record_used, remain;
	uint16_t flags;
	bool assembling;
	struct lan_header message;
};
typedef bool (*lan_record_fn)(void*, const struct lan_header*, const uint8_t*,
							  size_t);
size_t lan_frame_pack(uint8_t* out, size_t cap, uint8_t kind, uint32_t epoch,
					  uint32_t seq, const uint8_t* data, size_t n);
bool lan_frame_feed(struct lan_reader* r, const uint8_t* data, size_t n,
					lan_record_fn on_record, void* arg);
#endif
