/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "lan_frame.h"
#include <stdio.h>
#include <string.h>

#define CHECK(p)                                                               \
	do {                                                                       \
		if(!(p)) {                                                             \
			fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #p);               \
			return 1;                                                          \
		}                                                                      \
	} while(0)
static unsigned received;
static bool capture(void* arg, const struct lan_header* h, const uint8_t* data,
					size_t size) {
	const uint8_t* expected = arg;
	if(h->kind != LAN_STATE || h->epoch != 0x01020304 || h->seq != received + 1
	   || size != LAN_FRAME_DATA + 3 || memcmp(expected, data, size) != 0)
		return false;
	++received;
	return true;
}
int main(void) {
	static uint8_t record[LAN_FRAME_DATA + 3],
		stream[2 * (sizeof(record) + 48)];
	for(size_t i = 0; i < sizeof(record); ++i)
		record[i] = (uint8_t)i;
	size_t n = lan_frame_pack(stream, sizeof(stream), LAN_STATE, 0x01020304, 1,
							  record, sizeof(record));
	CHECK(n == sizeof(record) + 48);
	const uint8_t golden[24]
		= {'F', 'C', 'V', 'X', 1, LAN_STATE, 0, 1, 0, 0, 0x20, 0,
		   1,	2,	 3,	  4,   0, 0,		 0, 1, 0, 0, 0,	   0};
	CHECK(memcmp(golden, stream, 24) == 0);
	CHECK(lan_frame_pack(stream + n, sizeof(stream) - n, LAN_STATE, 0x01020304,
						 2, record, sizeof(record))
		  == n);
	static struct lan_reader reader;
	for(size_t i = 0; i < 23; ++i)
		CHECK(lan_frame_feed(&reader, stream + i, 1, capture, record));
	CHECK(!received);
	CHECK(lan_frame_feed(&reader, stream + 23, 2 * n - 23, capture, record));
	CHECK(received == 2);
	puts("PASS golden header, split header and coalesced fragmented records");
	memset(&reader, 0, sizeof reader);
	stream[10] = 0xff;
	CHECK(!lan_frame_feed(&reader, stream, 24, capture, record));
	stream[10] = 0x20;
	stream[4] = 99;
	memset(&reader, 0, sizeof reader);
	CHECK(!lan_frame_feed(&reader, stream, 24, capture, record));
	stream[4] = 1;
	stream[7] = 0;
	memset(&reader, 0, sizeof reader);
	CHECK(!lan_frame_feed(&reader, stream, 24, capture, record));
	CHECK(lan_frame_pack(stream, sizeof stream, LAN_STATE, 1, 2, record,
						 LAN_RECORD_MAX + 1)
		  == 0);
	puts("PASS oversized length, version, missing start and logical record "
		 "bounds");
	return 0;
}
