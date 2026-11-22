/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "lan_rpc.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* One schema for both directions. A wire record contains no ABI structs,
 * pointers or padding. */
struct cursor {
	const uint8_t* in;
	uint8_t* out;
	size_t pos, cap;
	bool ok;
};
static void bytes(struct cursor* c, void* p, size_t n) {
	if(!c->ok || n > c->cap - c->pos) {
		c->ok = false;
		return;
	}
	if(c->in)
		memcpy(p, c->in + c->pos, n);
	else
		memcpy(c->out + c->pos, p, n);
	c->pos += n;
}
static uint8_t u8(struct cursor* c, uint8_t v) {
	bytes(c, &v, 1);
	return v;
}
static uint16_t u16(struct cursor* c, uint16_t v) {
	uint16_t hi = u8(c, v >> 8);
	return (hi << 8) | u8(c, v);
}
static uint32_t u32(struct cursor* c, uint32_t v) {
	uint32_t hi = u16(c, v >> 16);
	return (hi << 16) | u16(c, v);
}
static uint64_t u64(struct cursor* c, uint64_t v) {
	uint64_t hi = u32(c, v >> 32);
	return (hi << 32) | u32(c, v);
}
static float f32(struct cursor* c, float v) {
	uint32_t bits;
	memcpy(&bits, &v, 4);
	bits = u32(c, bits);
	memcpy(&v, &bits, 4);
	if(!isfinite(v))
		c->ok = false;
	return v;
}
static double f64(struct cursor* c, double v) {
	uint64_t bits;
	memcpy(&bits, &v, 8);
	bits = u64(c, bits);
	memcpy(&v, &bits, 8);
	if(!isfinite(v))
		c->ok = false;
	return v;
}
static bool boolean(struct cursor* c, bool v) {
	uint8_t n = u8(c, v);
	if(n > 1)
		c->ok = false;
	return n == 1;
}
static void coords(struct cursor* c, w_coord_t* x, w_coord_t* y, w_coord_t* z) {
	*x = (int32_t)u32(c, (uint32_t)*x);
	*y = (int32_t)u32(c, (uint32_t)*y);
	*z = (int32_t)u32(c, (uint32_t)*z);
	if(*x < -30000000 || *x > 30000000 || *z < -30000000 || *z > 30000000
	   || *y < 0 || *y >= WORLD_HEIGHT)
		c->ok = false;
}
static enum side face(struct cursor* c, enum side s) {
	uint8_t v = u8(c, (uint8_t)s);
	if(v >= SIDE_MAX)
		c->ok = false;
	return (enum side)v;
}
static void vector(struct cursor* c, float* v, size_t n) {
	for(size_t i = 0; i < n; i++)
		v[i] = f32(c, v[i]);
}
static void item(struct cursor* c, struct item_data* it) {
	it->id = u16(c, it->id);
	it->durability = u8(c, it->durability);
	it->count = u8(c, it->count);
}
static void command(struct cursor* c, struct server_rpc* r) {
	r->type = (enum server_rpc_type)u8(c, (uint8_t)r->type);
	r->action_seq = u32(c, r->action_seq);
	switch(r->type) {
		case SRPC_PLAYER_POS:
			r->payload.player_pos.x = f64(c, r->payload.player_pos.x);
			r->payload.player_pos.y = f64(c, r->payload.player_pos.y);
			r->payload.player_pos.z = f64(c, r->payload.player_pos.z);
			r->payload.player_pos.rx = f32(c, r->payload.player_pos.rx);
			r->payload.player_pos.ry = f32(c, r->payload.player_pos.ry);
			r->payload.player_pos.vel_y = f32(c, r->payload.player_pos.vel_y);
			if(fabs(r->payload.player_pos.x) > 30000000
			   || fabs(r->payload.player_pos.z) > 30000000
			   || fabs(r->payload.player_pos.y) > 32768)
				c->ok = false;
			break;
		case SRPC_BLOCK_PLACE:
		case SRPC_BLOCK_DIG:
			r->has_expected_block = boolean(c, r->has_expected_block);
			r->expected_block = u8(c, r->expected_block);
			if(!r->has_expected_block)
				c->ok = false;
			if(r->type == SRPC_BLOCK_PLACE) {
				coords(c, &r->payload.block_place.x, &r->payload.block_place.y,
					   &r->payload.block_place.z);
				r->payload.block_place.side
					= face(c, r->payload.block_place.side);
			} else {
				coords(c, &r->payload.block_dig.x, &r->payload.block_dig.y,
					   &r->payload.block_dig.z);
				r->payload.block_dig.side = face(c, r->payload.block_dig.side);
				r->payload.block_dig.finished
					= boolean(c, r->payload.block_dig.finished);
			}
			break;
		case SRPC_HOTBAR_SLOT:
			if(!c->in && r->payload.hotbar_slot.slot >= 9)
				c->ok = false;
			r->payload.hotbar_slot.slot = u8(c, r->payload.hotbar_slot.slot);
			if(r->payload.hotbar_slot.slot >= 9)
				c->ok = false;
			break;
		case SRPC_ENTITY_ATTACK:
			r->payload.entity_attack.entity_id
				= u32(c, r->payload.entity_attack.entity_id);
			break;
		case SRPC_WINDOW_CLOSE:
			r->payload.window_close.window
				= u8(c, r->payload.window_close.window);
			break;
		case SRPC_WINDOW_CLICK:
			r->payload.window_click.window
				= u8(c, r->payload.window_click.window);
			r->payload.window_click.slot = u8(c, r->payload.window_click.slot);
			r->payload.window_click.right_click
				= boolean(c, r->payload.window_click.right_click);
			r->payload.window_click.action_id
				= u16(c, r->payload.window_click.action_id);
			break;
		default:
			c->ok = false; /* Only the host controls world loading and pause. */
	}
}
static uint32_t checksum(const uint8_t* p, size_t n) {
	uint32_t v = 2166136261u;
	while(n--)
		v = (v ^ *p++) * 16777619u;
	return v;
}
void lan_free_chunk(struct client_rpc* r) {
	if(r->type != CRPC_CHUNK)
		return;
	free(r->payload.chunk.ids);
	free(r->payload.chunk.metadata);
	free(r->payload.chunk.lighting_sky);
	free(r->payload.chunk.lighting_torch);
	r->payload.chunk.ids = r->payload.chunk.metadata = NULL;
	r->payload.chunk.lighting_sky = r->payload.chunk.lighting_torch = NULL;
}
static void chunk(struct cursor* c, struct client_rpc* r) {
	coords(c, &r->payload.chunk.x, &r->payload.chunk.y, &r->payload.chunk.z);
	r->payload.chunk.sx = (int32_t)u32(c, r->payload.chunk.sx);
	r->payload.chunk.sy = (int32_t)u32(c, r->payload.chunk.sy);
	r->payload.chunk.sz = (int32_t)u32(c, r->payload.chunk.sz);
	int sx = r->payload.chunk.sx, sy = r->payload.chunk.sy,
		sz = r->payload.chunk.sz;
	if(!c->ok || sx < 1 || sx > 16 || sy < 1
	   || sy > WORLD_HEIGHT - r->payload.chunk.y || sz < 1 || sz > 16) {
		c->ok = false;
		return;
	}
	size_t volume = (size_t)sx * sy * sz, half = (volume + 1) / 2,
		   body = volume + 3 * half;
	if(c->cap - c->pos < body + 4 || (c->in && c->cap - c->pos != body + 4)) {
		c->ok = false;
		return;
	}
	if(c->in) {
		const uint8_t* tail = c->in + c->pos + body;
		uint32_t want = ((uint32_t)tail[0] << 24) | ((uint32_t)tail[1] << 16)
			| ((uint32_t)tail[2] << 8) | tail[3];
		if(checksum(c->in + c->pos, body) != want) {
			c->ok = false;
			return;
		}
	}
	uint8_t** planes[]
		= {&r->payload.chunk.ids, &r->payload.chunk.metadata,
		   &r->payload.chunk.lighting_sky, &r->payload.chunk.lighting_torch};
	size_t start = c->pos;
	for(size_t i = 0; i < 4; i++) {
		size_t count = i ? half : volume;
		if(c->in)
			*planes[i] = malloc(count);
		if(!*planes[i]) {
			c->ok = false;
			return;
		}
		bytes(c, *planes[i], count);
	}
	u32(c, c->in ? 0 : checksum(c->out + start, body));
}
static void state(struct cursor* c, struct client_rpc* r) {
	r->type = (enum client_rpc_type)u8(c, (uint8_t)r->type);
	switch(r->type) {
		case CRPC_CHUNK: chunk(c, r); break;
		case CRPC_UNLOAD_CHUNK:
			r->payload.unload_chunk.x
				= (int32_t)u32(c, r->payload.unload_chunk.x);
			r->payload.unload_chunk.z
				= (int32_t)u32(c, r->payload.unload_chunk.z);
			break;
		case CRPC_INVENTORY_SLOT:
			r->payload.inventory_slot.window
				= u8(c, r->payload.inventory_slot.window);
			r->payload.inventory_slot.slot
				= u8(c, r->payload.inventory_slot.slot);
			item(c, &r->payload.inventory_slot.item);
			break;
		case CRPC_PLAYER_POS:
			vector(c, r->payload.player_pos.position, 3);
			vector(c, r->payload.player_pos.rotation, 2);
			break;
		case CRPC_TIME_SET:
			r->payload.time_set = u64(c, r->payload.time_set);
			break;
		case CRPC_WORLD_RESET:
			r->payload.world_reset.dimension
				= (int32_t)u32(c, r->payload.world_reset.dimension);
			if(r->payload.world_reset.dimension != WORLD_DIM_OVERWORLD
			   && r->payload.world_reset.dimension != WORLD_DIM_NETHER)
				c->ok = false;
			r->payload.world_reset.local_entity
				= u32(c, r->payload.world_reset.local_entity);
			break;
		case CRPC_SET_BLOCK:
			coords(c, &r->payload.set_block.x, &r->payload.set_block.y,
				   &r->payload.set_block.z);
			r->payload.set_block.block.type
				= u8(c, r->payload.set_block.block.type);
			{
				uint8_t packed = u8(c,
									(r->payload.set_block.block.sky_light << 4)
										| r->payload.set_block.block.metadata);
				r->payload.set_block.block.metadata = packed & 15;
				r->payload.set_block.block.sky_light = packed >> 4;
				uint8_t torch = u8(c, r->payload.set_block.block.torch_light);
				if(torch > 15)
					c->ok = false;
				r->payload.set_block.block.torch_light = torch;
			}
			break;
		case CRPC_WINDOW_TRANSACTION:
			r->payload.window_transaction.window
				= u8(c, r->payload.window_transaction.window);
			r->payload.window_transaction.action_id
				= u16(c, r->payload.window_transaction.action_id);
			r->payload.window_transaction.accepted
				= boolean(c, r->payload.window_transaction.accepted);
			break;
		case CRPC_SPAWN_ITEM:
			r->payload.spawn_item.entity_id
				= u32(c, r->payload.spawn_item.entity_id);
			item(c, &r->payload.spawn_item.item);
			vector(c, r->payload.spawn_item.pos, 3);
			vector(c, r->payload.spawn_item.vel, 3);
			break;
		case CRPC_PICKUP_ITEM:
			r->payload.pickup_item.entity_id
				= u32(c, r->payload.pickup_item.entity_id);
			r->payload.pickup_item.collector_id
				= u32(c, r->payload.pickup_item.collector_id);
			break;
		case CRPC_ENTITY_DESTROY:
			r->payload.entity_destroy.entity_id
				= u32(c, r->payload.entity_destroy.entity_id);
			break;
		case CRPC_ENTITY_MOVE:
			r->payload.entity_move.entity_id
				= u32(c, r->payload.entity_move.entity_id);
			vector(c, r->payload.entity_move.pos, 3);
			break;
		case CRPC_OPEN_WINDOW:
			r->payload.window_open.window
				= u8(c, r->payload.window_open.window);
			r->payload.window_open.type
				= (enum window_type)u8(c, r->payload.window_open.type);
			r->payload.window_open.slot_count
				= u8(c, r->payload.window_open.slot_count);
			break;
		case CRPC_PLAYER_SET_HEALTH:
			r->payload.player_set_health.health
				= (int16_t)u16(c, r->payload.player_set_health.health);
			break;
		case CRPC_SPAWN_MONSTER:
			r->payload.spawn_monster.entity_id
				= u32(c, r->payload.spawn_monster.entity_id);
			r->payload.spawn_monster.monster_id
				= (int32_t)u32(c, r->payload.spawn_monster.monster_id);
			vector(c, r->payload.spawn_monster.pos, 3);
			break;
		case CRPC_SPAWN_MINECART:
			r->payload.spawn_minecart.entity_id
				= u32(c, r->payload.spawn_minecart.entity_id);
			vector(c, r->payload.spawn_minecart.pos, 3);
			break;
		case CRPC_REMOTE_PLAYER:
			r->payload.remote_player.entity_id
				= u32(c, r->payload.remote_player.entity_id);
			r->payload.remote_player.present
				= boolean(c, r->payload.remote_player.present);
			vector(c, r->payload.remote_player.position, 3);
			vector(c, r->payload.remote_player.rotation, 2);
			break;
		default: c->ok = false;
	}
}
size_t lan_encode_command(uint8_t* dst, size_t cap,
						  const struct server_rpc* r) {
	struct cursor c = {.out = dst, .cap = cap, .ok = true};
	struct server_rpc copy = *r;
	command(&c, &copy);
	return c.ok ? c.pos : 0;
}
bool lan_decode_command(const uint8_t* src, size_t n, struct server_rpc* out) {
	struct cursor c = {.in = src, .cap = n, .ok = true};
	struct server_rpc r = {0};
	command(&c, &r);
	if(!c.ok || c.pos != n)
		return false;
	*out = r;
	return true;
}
size_t lan_encode_state(uint8_t* dst, size_t cap, const struct client_rpc* r) {
	struct cursor c = {.out = dst, .cap = cap, .ok = true};
	struct client_rpc copy = *r;
	state(&c, &copy);
	return c.ok ? c.pos : 0;
}
bool lan_decode_state(const uint8_t* src, size_t n, struct client_rpc* out) {
	struct cursor c = {.in = src, .cap = n, .ok = true};
	struct client_rpc r = {0};
	state(&c, &r);
	if(!c.ok || c.pos != n) {
		lan_free_chunk(&r);
		return false;
	}
	*out = r;
	return true;
}
