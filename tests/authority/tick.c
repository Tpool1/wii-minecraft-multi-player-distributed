/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../../source/network/server_local.h"
#include "../../source/block/blocks.h"
#include "../../source/util.h"
#include "../../source/daytime.h"
#include "../../source/game/session_metrics.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>

struct block* blocks[256];
static struct server_local host;
static unsigned observed, columns, day_calls, night_calls;
static int64_t order[50];
static float world_time;
extern uint64_t nibble_reads;

ptime_t time_get(void) { return (ptime_t){0}; }
void session_metrics_world_scan(ptime_t start, ptime_t finish) {
    (void)start;
    (void)finish;
}
float daytime_get_time(void) {
	return world_time;
}

/* These are the production functions extracted by tick.ps1, including the
 * storage index, the real m-lib dictionary, block reads and neighbour offsets. */
#include "tick-body.inc"

static void on_tick(struct server_local* s, struct block_info* cell) {
	unsigned phase = observed % 3;
	int64_t key = order[observed / 3];
	int x = S_CHUNK_X(key) * CHUNK_SIZE, z = S_CHUNK_Z(key) * CHUNK_SIZE;
	const struct block_data* b = cell->block;
	assert(s == &host && observed < columns * 3);
	assert(b->metadata == 3 && b->sky_light == 11 && b->torch_light == 7);
	if(phase < 2) {
		assert(cell->x == x + 2 && cell->z == z + 1 && !cell->neighbours);
		assert(cell->y == (phase ? 6 : 5));
		assert(b->type == (phase ? BLOCK_GLASS : BLOCK_DIRT));
		if(!phase) {
			struct server_chunk* c = dict_server_chunks_get(s->world.chunks, key);
			/* A previously passed voxel waits until the next tick. A later
			 * voxel becomes active immediately, and another loses its tick. */
			c->ids[S_CHUNK_IDX(2, 3, 1)] = BLOCK_GLASS;
			c->ids[S_CHUNK_IDX(2, 6, 1)] = BLOCK_GLASS;
			c->ids[S_CHUNK_IDX(2, 9, 1)] = BLOCK_STONE;
		}
	} else {
		assert(cell->x == x + 15 && cell->z == z + 15 && cell->y == 127);
		assert(b->type == BLOCK_TNT && cell->neighbours);
		assert(cell->neighbours[SIDE_TOP].type == BLOCK_AIR);
		assert(cell->neighbours[SIDE_TOP].sky_light == 0);
		assert(cell->neighbours[SIDE_BOTTOM].type == BLOCK_STONE);
		assert(cell->neighbours[SIDE_BOTTOM].sky_light == 11);
	}
	++observed;
}
static void on_day(struct server_local* s, struct block_info* b) {
	assert(s == &host && b && observed == day_calls + 1);
	++day_calls;
}
static void on_night(struct server_local* s, struct block_info* b) {
	assert(s == &host && b && observed == night_calls + 1);
	++night_calls;
}
static void reset_cells(void) {
	dict_server_chunks_it_t it;
	dict_server_chunks_it(it, host.world.chunks);
	unsigned i = 0;
	while(!dict_server_chunks_end_p(it)) {
		order[i++] = dict_server_chunks_cref(it)->key;
		uint8_t* ids = dict_server_chunks_ref(it)->value.ids;
		ids[S_CHUNK_IDX(2, 3, 1)] = BLOCK_STONE;
		ids[S_CHUNK_IDX(2, 5, 1)] = BLOCK_DIRT;
		ids[S_CHUNK_IDX(2, 6, 1)] = BLOCK_STONE;
		ids[S_CHUNK_IDX(2, 9, 1)] = BLOCK_DIRT;
		ids[S_CHUNK_IDX(15, 127, 15)] = BLOCK_TNT;
		dict_server_chunks_next(it);
	}
	assert(i == columns);
}
static bool exercise(unsigned count) {
	enum { CELLS = CHUNK_SIZE * CHUNK_SIZE * WORLD_HEIGHT, ROUNDS = 24 };
	columns = count;
	dict_server_chunks_init(host.world.chunks);
	for(unsigned i = 0; i < count; ++i) {
		unsigned j = i % 25;
		int x = (i < 25 ? -10 : 18) + (int)(j % 5);
		int z = -2 + (int)(j / 5);
		struct server_chunk c = {0};
		c.ids = malloc(CELLS);
		c.metadata = malloc(CELLS/2);
		c.lighting_sky = malloc(CELLS/2);
		c.lighting_torch = malloc(CELLS/2);
		assert(c.ids && c.metadata && c.lighting_sky && c.lighting_torch);
		memset(c.ids, BLOCK_STONE, CELLS);
		c.ids[S_CHUNK_IDX(2, 15, 1)] = 255; /* unregistered ID */
		memset(c.metadata, 0x33, CELLS/2);
		memset(c.lighting_sky, 0xbb, CELLS/2);
		memset(c.lighting_torch, 0x77, CELLS/2);
		dict_server_chunks_set_at(host.world.chunks, S_CHUNK_ID(x,z), c);
	}
	nibble_reads = 0;
	clock_t ticks = 0;
	for(unsigned i = 0; i < ROUNDS; ++i) {
		reset_cells();
		observed = day_calls = night_calls = 0;
		world_time = i % 2 ? 13000.0f : 0.0f;
		clock_t start = clock();
		server_world_tick(&host.world, &host);
		ticks += clock() - start;
		assert(observed == count * 3);
		assert(day_calls == (i % 2 ? 0 : observed));
		assert(night_calls == (i % 2 ? observed : 0));
	}
	uint64_t decodes = nibble_reads / (3 * ROUNDS);
	printf("columns=%u cells=%u ticks=%u callbacks/tick=%u decoded/tick=%llu "
		   "native_ms/tick=%.3f\n", count, count*CELLS, ROUNDS, count*3,
		   (unsigned long long)decodes, 1000.0*ticks/(CLOCKS_PER_SEC*ROUNDS));
	dict_server_chunks_it_t it;
	for(dict_server_chunks_it(it, host.world.chunks); !dict_server_chunks_end_p(it); dict_server_chunks_next(it)) {
		struct server_chunk* c = &dict_server_chunks_ref(it)->value;
		free(c->ids); free(c->metadata); free(c->lighting_sky); free(c->lighting_torch);
	}
	dict_server_chunks_clear(host.world.chunks);
	/* 3 callbacks and at most 6 valid neighbour reads at the one TNT. */
	return decodes <= count * 9;
}
int main(void) {
	static struct block inert, active;
	active.onWorldTick = on_tick;
	active.onDay = on_day;
	active.onNight = on_night;
	blocks[BLOCK_STONE] = &inert;
	blocks[BLOCK_DIRT] = blocks[BLOCK_GLASS] = blocks[BLOCK_TNT] = &active;
	bool near = exercise(25);
	bool apart = exercise(50);
	if(!near || !apart) {
		puts("FAIL inactive voxels decoded metadata and lighting");
		return 1;
	}
	puts("PASS ordered live cell edits, all callbacks/fields, neighbours, separated interests and bounded decoding");
	return 0;
}
