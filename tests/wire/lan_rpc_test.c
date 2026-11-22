/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "lan_rpc.h"
#include "lan_frame.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                               \
	do {                                                                       \
		if(!(x)) {                                                             \
			fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);               \
			return 1;                                                          \
		}                                                                      \
	} while(0)
static uint8_t bytes[LAN_RECORD_MAX], ids[32768], md[16384], sky[16384],
	torch[16384];
static int golden(const uint8_t* data, size_t n, const char* text) {
	const char* digits = "0123456789abcdef";
	if(strlen(text) != 2 * n)
		return 0;
	for(size_t i = 0; i < n; i++)
		if(text[i * 2] != digits[data[i] >> 4]
		   || text[i * 2 + 1] != digits[data[i] & 15])
			return 0;
	return 1;
}
static int golden_fields(void) {
	static const struct {
		struct client_rpc call;
		const char* hex;
	} states[] = {
		{{.type = CRPC_UNLOAD_CHUNK,
		  .payload.unload_chunk = {.x = -5, .z = 257}},
		 "01fffffffb00000101"},
		{{.type = CRPC_INVENTORY_SLOT,
		  .payload.inventory_slot
		  = {.window = 4,
			 .slot = 3,
			 .item = {.id = 513, .durability = 7, .count = 11}}},
		 "0204030201070b"},
		{{.type = CRPC_PLAYER_POS,
		  .payload.player_pos
		  = {.position = {1.5, -2.5, 3.75}, .rotation = {-45, 90}}},
		 "033fc00000c020000040700000c234000042b40000"},
		{{.type = CRPC_TIME_SET,
		  .payload.time_set = UINT64_C(0x23456789abcdef01)},
		 "0423456789abcdef01"},
		{{.type = CRPC_WORLD_RESET,
		  .payload.world_reset
		  = {.dimension = WORLD_DIM_NETHER, .local_entity = 0x12345678}},
		 "05ffffffff12345678"},
		{{.type = CRPC_SET_BLOCK,
		  .payload.set_block = {.x = -5,
								.y = 12,
								.z = 257,
								.block = {.type = 17,
										  .metadata = 3,
										  .sky_light = 14,
										  .torch_light = 12}}},
		 "06fffffffb0000000c0000010111e30c"},
		{{.type = CRPC_WINDOW_TRANSACTION,
		  .payload.window_transaction
		  = {.window = 4, .action_id = 0x6789, .accepted = true}},
		 "0704678901"},
		{{.type = CRPC_SPAWN_ITEM,
		  .payload.spawn_item
		  = {.entity_id = 0x12345678,
			 .item = {.id = 513, .durability = 7, .count = 11},
			 .pos = {1.5, -2.5, 3.75},
			 .vel = {-1.5, 2.5, -3.75}}},
		 "08123456780201070b3fc00000c020000040700000bfc0000040200000c0700000"},
		{{.type = CRPC_PICKUP_ITEM,
		  .payload.pickup_item
		  = {.entity_id = 0x12345678, .collector_id = 0x0102abef}},
		 "09123456780102abef"},
		{{.type = CRPC_ENTITY_DESTROY,
		  .payload.entity_destroy = {.entity_id = 0x12345678}},
		 "0a12345678"},
		{{.type = CRPC_ENTITY_MOVE,
		  .payload.entity_move
		  = {.entity_id = 0x12345678, .pos = {1.5, -2.5, 3.75}}},
		 "0b123456783fc00000c020000040700000"},
		{{.type = CRPC_OPEN_WINDOW,
		  .payload.window_open
		  = {.window = 4, .type = WINDOW_TYPE_SIGN, .slot_count = 36}},
		 "0c040524"},
		{{.type = CRPC_PLAYER_SET_HEALTH,
		  .payload.player_set_health = {.health = -127}},
		 "0dff81"},
		{{.type = CRPC_SPAWN_MONSTER,
		  .payload.spawn_monster = {.entity_id = 0x12345678,
									.monster_id = 7,
									.pos = {1.5, -2.5, 3.75}}},
		 "0e12345678000000073fc00000c020000040700000"},
		{{.type = CRPC_SPAWN_MINECART,
		  .payload.spawn_minecart
		  = {.entity_id = 0x12345678, .pos = {1.5, -2.5, 3.75}}},
		 "0f123456783fc00000c020000040700000"},
		{{.type = CRPC_REMOTE_PLAYER,
		  .payload.remote_player = {.entity_id = 0x12345678,
									.present = true,
									.position = {1.5, -2.5, 3.75},
									.rotation = {-45, 90}}},
		 "1012345678013fc00000c020000040700000c234000042b40000"}};
	static const struct {
		struct server_rpc call;
		const char* hex;
	} commands[] = {
		{{.type = SRPC_PLAYER_POS,
		  .action_seq = 0x75234901,
		  .has_expected_block = true,
		  .expected_block = 0x44,
		  .payload.player_pos = {.x = -5.5,
								 .y = 6.25,
								 .z = 7.75,
								 .rx = -45,
								 .ry = 90,
								 .vel_y = -3.75}},
		 "0075234901c0160000000000004019000000000000401f000000000000c234000042b"
		 "40000c0700000"},
		{{.type = SRPC_ENTITY_ATTACK,
		  .action_seq = 0x75234901,
		  .has_expected_block = true,
		  .expected_block = 0x44,
		  .payload.entity_attack = {.entity_id = 0x12345678}},
		 "027523490112345678"},
		{{.type = SRPC_HOTBAR_SLOT,
		  .action_seq = 0x75234901,
		  .has_expected_block = true,
		  .expected_block = 0x44,
		  .payload.hotbar_slot = {.slot = 7}},
		 "047523490107"},
		{{.type = SRPC_BLOCK_PLACE,
		  .action_seq = 0x75234901,
		  .has_expected_block = true,
		  .expected_block = 0x44,
		  .payload.block_place
		  = {.x = -5, .y = 12, .z = 257, .side = SIDE_BACK}},
		 "05752349010144fffffffb0000000c0000010105"},
		{{.type = SRPC_BLOCK_DIG,
		  .action_seq = 0x75234901,
		  .has_expected_block = true,
		  .expected_block = 0x44,
		  .payload.block_dig
		  = {.x = -5, .y = 12, .z = 257, .side = SIDE_FRONT, .finished = true}},
		 "06752349010144fffffffb0000000c000001010401"},
		{{.type = SRPC_WINDOW_CLICK,
		  .action_seq = 0x75234901,
		  .has_expected_block = true,
		  .expected_block = 0x44,
		  .payload.window_click = {.window = 2,
								   .slot = 43,
								   .right_click = true,
								   .action_id = 0x3301}},
		 "0775234901022b013301"},
		{{.type = SRPC_WINDOW_CLOSE,
		  .action_seq = 0x75234901,
		  .has_expected_block = true,
		  .expected_block = 0x44,
		  .payload.window_close = {.window = 4}},
		 "087523490104"}};
	for(size_t i = 0; i < sizeof states / sizeof *states; i++) {
		struct client_rpc out = {0};
		size_t n = lan_encode_state(bytes, sizeof bytes, &states[i].call);
		CHECK(golden(bytes, n, states[i].hex)
			  && lan_decode_state(bytes, n, &out));
		n = lan_encode_state(bytes, sizeof bytes, &out);
		CHECK(golden(bytes, n, states[i].hex));
		CHECK(!lan_decode_state(bytes, n + 1, &out));
	}
	for(size_t i = 0; i < sizeof commands / sizeof *commands; i++) {
		struct server_rpc out = {0};
		size_t n = lan_encode_command(bytes, sizeof bytes, &commands[i].call);
		CHECK(golden(bytes, n, commands[i].hex)
			  && lan_decode_command(bytes, n, &out));
		n = lan_encode_command(bytes, sizeof bytes, &out);
		CHECK(golden(bytes, n, commands[i].hex));
		CHECK(!lan_decode_command(bytes, n - 1, &out));
	}
	puts("PASS nonzero golden wire bytes for 16 scalar states and all seven "
		 "allowed commands");
	return 0;
}

int main(void) {
	CHECK(golden_fields() == 0);
	struct server_rpc c = {.type = SRPC_BLOCK_DIG,
						   .action_seq = 0x12345678,
						   .has_expected_block = true,
						   .expected_block = 4,
						   .payload.block_dig = {.finished = true,
												 .x = -100,
												 .y = 6,
												 .z = 1024,
												 .side = SIDE_TOP}},
					  next = {0};
	size_t n = lan_encode_command(bytes, sizeof bytes, &c);
	CHECK(n > 0 && lan_decode_command(bytes, n, &next));
	CHECK(next.action_seq == c.action_seq && next.expected_block == 4
		  && next.has_expected_block && next.payload.block_dig.x == -100
		  && next.payload.block_dig.z == 1024
		  && next.payload.block_dig.finished);
	CHECK(!lan_decode_command(bytes, n - 1, &next));
	c.type = SRPC_LOAD_WORLD;
	CHECK(!lan_encode_command(bytes, sizeof bytes, &c));
	puts("PASS edit preconditions, action sequence, coordinate, truncated and "
		 "host-only command");
	struct client_rpc original = {.type = CRPC_CHUNK,
								  .payload.chunk = {.x = -64,
													.y = 0,
													.z = 128,
													.sx = 16,
													.sy = 128,
													.sz = 16,
													.ids = ids,
													.metadata = md,
													.lighting_sky = sky,
													.lighting_torch = torch}},
					  clone = {0};
	for(size_t i = 0; i < sizeof ids; ++i)
		ids[i] = (uint8_t)(i * 17);
	memset(md, 0x17, sizeof md);
	memset(sky, 0xa2, sizeof sky);
	memset(torch, 0xcf, sizeof torch);
	n = lan_encode_state(bytes, sizeof bytes, &original);
	CHECK(n > 81920);
	ids[1] = 0;
	md[1] = 0;
	sky[1] = 0;
	torch[1] = 0;
	CHECK(lan_decode_state(bytes, n, &clone));
	CHECK(clone.payload.chunk.ids != ids && clone.payload.chunk.ids[1] == 17
		  && clone.payload.chunk.metadata[1] == 0x17
		  && clone.payload.chunk.lighting_sky[1] == 0xa2
		  && clone.payload.chunk.lighting_torch[1] == 0xcf);
	memset(bytes + 27, 0, n - 27);
	CHECK(clone.payload.chunk.ids[1] == 17
		  && clone.payload.chunk.lighting_torch[16383] == 0xcf);
	lan_free_chunk(&clone);
	CHECK(!lan_decode_state(bytes, n - 1, &clone));
	CHECK(!lan_decode_state(bytes, n, &clone));
	original.payload.chunk.sy = 129;
	CHECK(!lan_encode_state(bytes, sizeof bytes, &original));
	puts("PASS complete 80 KiB owned snapshot, four planes, mutation "
		 "isolation, checksum and shape limits");
	c = (struct server_rpc) {.type = SRPC_PLAYER_POS,
							 .payload.player_pos = {.x = -334455.125,
													.y = 64.875,
													.z = 19,
													.rx = 12.5f,
													.ry = -15.25f,
													.vel_y = -1}};
	n = lan_encode_command(bytes, sizeof bytes, &c);
	CHECK(n && lan_decode_command(bytes, n, &next)
		  && next.payload.player_pos.x == c.payload.player_pos.x
		  && next.payload.player_pos.ry == -15.25f);
	puts("PASS all outbound state kinds and IEEE754 peer position");
	return 0;
}
