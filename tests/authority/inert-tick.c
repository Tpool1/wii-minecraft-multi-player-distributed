/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../../source/network/server_local.h"
#include "../../source/game/session_metrics.h"
#include "../../source/block/blocks.h"
#include "../../source/daytime.h"
#include "../../source/lighting.h"
#include "../../source/util.h"
#include <assert.h>
#include <stdio.h>

enum { CELLS = CHUNK_SIZE * CHUNK_SIZE * WORLD_HEIGHT };
struct block* blocks[256];
static struct server_local host;
static uint8_t ids[2][CELLS];
static uint8_t meta[2][CELLS/2], sky[2][CELLS/2], torch[2][CELLS/2];
static uint8_t height[2][CHUNK_SIZE * CHUNK_SIZE];
static unsigned id_reads, random_calls, random_draws, publications;
static w_coord_t first, second;
static struct { w_coord_t x, y; } seen[8];
static unsigned count;
static bool cascade;

/* The runner counts only the production world-tick raw-ID gate. Neighbour
 * reads by the real setter and random tick do not contribute to this count. */
#include "inert-body.inc"

ptime_t time_get(void) { return (ptime_t){0}; }
float daytime_get_time(void) { return 0.0f; }
void session_metrics_world_scan(ptime_t start, ptime_t finish) {
    (void)start; (void)finish;
}
void server_local_publish_block(struct server_local* s, w_coord_t x,
        w_coord_t y, w_coord_t z, struct block_data blk) {
    (void)s; (void)x; (void)y; (void)z; (void)blk;
    ++publications;
}
void lighting_heightmap_update(uint8_t* h, c_coord_t x, w_coord_t y,
        c_coord_t z, uint8_t type, bool (*get)(void*, c_coord_t, w_coord_t,
        c_coord_t, struct block_data*), void* user) {
    (void)h; (void)x; (void)y; (void)z; (void)type; (void)get; (void)user;
}
void lighting_update_at_block(struct world_modification_entry entry,
        bool ignore_sky, bool (*get)(void*, w_coord_t, w_coord_t, w_coord_t,
        struct block_data*, uint8_t*), void (*set)(void*, w_coord_t,
        w_coord_t, w_coord_t, uint8_t), void* user) {
    (void)entry; (void)ignore_sky; (void)get; (void)set; (void)user;
}
int rand_gen_range(struct random_gen* g, int min, int max) {
    assert(g && max > min);
    ++random_draws;
    return min;
}
static void write_cell(w_coord_t x, w_coord_t y, uint8_t id) {
    assert(server_world_set_block(&host, x, y, 0, (struct block_data){.type=id}));
}
static void world_callback(struct server_local* s, struct block_info* cell) {
    assert(s == &host && count < sizeof seen / sizeof *seen);
    seen[count].x = cell->x;
    seen[count++].y = cell->y;
    if(cascade && cell->x == first && cell->y == 10)
        write_cell(second, 7, BLOCK_DIRT);
    if(cascade && cell->x == second && cell->y == 7)
        write_cell(first, 3, BLOCK_DIRT);
}
static void random_callback(struct server_local* s, struct block_info* cell) {
    assert(s == &host && cell->block->type == BLOCK_STONE);
    ++random_calls;
    if(random_calls == 1)
        write_cell(cell->x, 12, BLOCK_DIRT);
}
/* Replace archive I/O only. server_world_load_chunk still owns zeroing the
 * resident object, copying it into the dictionary, and returning the entry. */
static bool archived_active[2];
bool region_archive_contains(struct region_archive* ra, w_coord_t x,
        w_coord_t z, bool* exists) {
    assert(ra == &host.world.loaded_regions[0] && z == 0);
    *exists = x == -3 || x == 3;
    return true;
}
bool region_archive_get_blocks(struct region_archive* ra, w_coord_t x,
        w_coord_t z, struct server_chunk* column) {
    assert(ra == &host.world.loaded_regions[0] && z == 0);
    unsigned storage = x < 0 ? 0 : 1;
    memset(ids[storage], BLOCK_STONE, CELLS);
    if(archived_active[storage]) ids[storage][S_CHUNK_IDX(0, 6, 0)] = BLOCK_DIRT;
    column->ids = ids[storage];
    column->metadata = meta[storage];
    column->lighting_sky = sky[storage];
    column->lighting_torch = torch[storage];
    column->heightmap = height[storage];
    return true;
}
static void admit(w_coord_t cx) {
    struct server_chunk* column;
    assert(server_world_load_chunk(&host.world, cx, 0, &column));
    assert(column->ids == ids[cx < 0 ? 0 : 1]);
}
static unsigned tick(void) {
    id_reads = count = 0;
    server_world_tick(&host.world, &host);
    return id_reads;
}
static void expect(unsigned i, w_coord_t x, w_coord_t y) {
    assert(i < count && seen[i].x == x && seen[i].y == y);
}
int main(void) {
    static struct block stone, dirt;
    stone.onRandomTick = random_callback;
    dirt.onWorldTick = world_callback;
    blocks[BLOCK_STONE] = &stone;
    blocks[BLOCK_DIRT] = &dirt;

    dict_server_chunks_init(host.world.chunks);
    ilist_regions_init(host.world.loaded_regions_lru);
    ilist_regions_push_back(host.world.loaded_regions_lru, &host.world.loaded_regions[0]);
    admit(-3);
    admit(3);
    dict_server_chunks_it_t it;
    dict_server_chunks_it(it, host.world.chunks);
    first = S_CHUNK_X(dict_server_chunks_cref(it)->key) * CHUNK_SIZE;
    dict_server_chunks_next(it);
    second = S_CHUNK_X(dict_server_chunks_cref(it)->key) * CHUNK_SIZE;

    unsigned cold = tick();
    assert(count == 0);
    unsigned warm = tick();
    assert(count == 0);
    write_cell(first, 10, BLOCK_DIRT);
    cascade = true;
    tick();
    assert(count == 2);
    expect(0, first, 10);
    expect(1, second, 7);
    cascade = false;
    tick();
    assert(count == 3);
    expect(0, first, 3);
    expect(1, first, 10);
    expect(2, second, 7);
    write_cell(first, 3, BLOCK_STONE);
    write_cell(first, 10, BLOCK_STONE);
    write_cell(second, 7, BLOCK_STONE);
    tick(); /* Discover both have become inert again. */
    assert(count == 0);
    unsigned quiet_again = tick();

    /* Regular tick skipping must not suppress the independent random pass.
     * A random callback can rearm an inert column before the regular pass. */
    struct random_gen rng = {0};
    server_world_random_tick(&host.world, &rng, &host, first / CHUNK_SIZE, 0, 0);
    assert(random_calls == 80 && random_draws == 240);
    tick();
    assert(count == 1);
    expect(0, first, 12);
    host.world.peer_interest = true;
    host.world.peer_cx = second / CHUNK_SIZE;
    server_world_random_tick(&host.world, &rng, &host, first / CHUNK_SIZE, 0, 0);
    assert(random_calls == 240 && random_draws == 720);

    /* Readmit the same key with a different block in its archive data. */
    w_coord_t cx = second / CHUNK_SIZE;
    assert(dict_server_chunks_erase(host.world.chunks, S_CHUNK_ID(cx, 0)));
    archived_active[cx < 0 ? 0 : 1] = true;
    admit(cx);
    tick();
    assert(count == 2);
    assert((seen[0].x == second && seen[0].y == 6) ||
           (seen[1].x == second && seen[1].y == 6));
    assert(publications == 7);
    dict_server_chunks_clear(host.world.chunks);

    printf("world-tick raw ID reads: first=%u repeated=%u after removals=%u\n",
           cold, warm, quiet_again);
    if(cold != 2 * CELLS || warm != 0 || quiet_again != 0) {
        puts("FAIL repeated inert columns must incur no further world-tick ID scans");
        return 1;
    }
    puts("PASS warm inert columns, live setter order, random reactivation and reloaded column");
    return 0;
}
