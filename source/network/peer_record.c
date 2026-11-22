/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "peer_record.h"
#include "server_interface.h"
#include "server_local.h"
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Alternating records leave the previous player checkpoint readable after a
 * short write. */
enum {
	INVENTORY_OFFSET = 32,
	RECORD_BYTES = INVENTORY_OFFSET + 4 * INVENTORY_SIZE + 4
};
static uint32_t read32(const uint8_t* p) {
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8
		| p[3];
}
static void write32(uint8_t* p, uint32_t v) {
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = v;
}
static uint32_t sum(const uint8_t* p) {
	uint32_t n = UINT32_C(2166136261);
	for(size_t i = 0; i < RECORD_BYTES - 4; ++i)
		n = (n ^ p[i]) * UINT32_C(16777619);
	return n;
}
static void store_fixed(uint8_t* dest, double n) {
	write32(dest, (uint32_t)(int32_t)llround(n * 256.0));
}
static double load_fixed(const uint8_t* src) {
	return (int32_t)read32(src) / 256.0;
}

static FILE* open_record(struct server_local* s, int slot, const char* mode) {
	string_t path;
	string_init_printf(path, "%s/peer-b-%d.fcx", string_get_cstr(s->level_name),
					   slot);
	FILE* f = fopen(string_get_cstr(path), mode);
	int error = errno;
	string_clear(path);
	errno = error;
	return f;
}

bool peer_record_save(struct server_local* s) {
	struct server_player* p = &s->peer;
	uint8_t buf[RECORD_BYTES] = {0};
	uint32_t gen = s->peer_generation + 1;
	if(!gen)
		return false;
	memcpy(buf, "FCB1", 4);
	write32(buf + 4, gen);
	store_fixed(buf + 8, p->x);
	store_fixed(buf + 12, p->y);
	store_fixed(buf + 16, p->z);
	store_fixed(buf + 20, p->rx);
	store_fixed(buf + 24, p->ry);
	buf[28] = p->inventory.hotbar_slot;
	buf[29] = p->health;
	buf[30] = (uint8_t)(p->dimension == WORLD_DIM_NETHER);
	for(size_t i = 0; i < INVENTORY_SIZE; ++i) {
		size_t j = INVENTORY_OFFSET + 4 * i;
		struct item_data item = p->inventory.items[i];
		buf[j] = item.id >> 8;
		buf[j + 1] = item.id;
		buf[j + 2] = item.durability;
		buf[j + 3] = item.count;
	}
	write32(buf + RECORD_BYTES - 4, sum(buf));
	FILE* f = open_record(s, gen & 1, "wb");
	if(!f)
		return false;
	bool ok = fwrite(buf, 1, sizeof(buf), f) == sizeof(buf) && fflush(f) == 0;
	if(fclose(f) != 0)
		ok = false;
	if(ok) {
		uint8_t check[RECORD_BYTES];
		f = open_record(s, gen & 1, "rb");
		if(!f)
			return false;
		ok = fread(check, 1, sizeof(check), f) == sizeof(check)
			&& fgetc(f) == EOF && !ferror(f)
			&& memcmp(check, buf, sizeof(buf)) == 0;
		if(fclose(f) != 0)
			ok = false;
	}
	if(ok)
		s->peer_generation = gen;
	return ok;
}

bool peer_record_load(struct server_local* s) {
	uint8_t best[RECORD_BYTES];
	bool exists = false;
	s->peer_generation = 0;
	for(int k = 0; k < 2; ++k) {
		FILE* f = open_record(s, k, "rb");
		if(!f) {
			if(errno != ENOENT)
				return false;
			continue;
		}
		exists = true;
		uint8_t buf[RECORD_BYTES];
		bool complete = fread(buf, 1, sizeof(buf), f) == sizeof(buf)
			&& fgetc(f) == EOF && !ferror(f);
		if(fclose(f) != 0)
			complete = false;
		if(!complete || memcmp(buf, "FCB1", 4) != 0
		   || read32(buf + RECORD_BYTES - 4) != sum(buf))
			continue;
		uint32_t gen = read32(buf + 4);
		if(!gen || (gen & 1) != (unsigned)k || buf[28] >= INVENTORY_SIZE_HOTBAR
		   || fabs(load_fixed(buf + 8)) > 1000000
		   || fabs(load_fixed(buf + 16)) > 1000000 || load_fixed(buf + 12) < -2
		   || load_fixed(buf + 12) > WORLD_HEIGHT + 4
		   || buf[29] > MAX_PLAYER_HEALTH
		   || buf[30] != (s->world.dimension == WORLD_DIM_NETHER))
			continue;
		if(gen > s->peer_generation) {
			s->peer_generation = gen;
			memcpy(best, buf, sizeof(buf));
		}
	}
	if(!s->peer_generation)
		return !exists;
	struct server_player* p = &s->peer;
	p->x = load_fixed(best + 8);
	p->y = load_fixed(best + 12);
	p->z = load_fixed(best + 16);
	p->rx = load_fixed(best + 20);
	p->ry = load_fixed(best + 24);
	p->inventory.hotbar_slot = best[28];
	p->health = best[29];
	for(size_t i = 0; i < INVENTORY_SIZE; ++i) {
		size_t j = INVENTORY_OFFSET + 4 * i;
		p->inventory.items[i] = (struct item_data) {
			.id = (uint16_t)((best[j] << 8) | best[j + 1]),
			.durability = best[j + 2],
			.count = best[j + 3]};
	}
	return true;
}
