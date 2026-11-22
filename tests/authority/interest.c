/* Run the production eviction selector against two small interest areas. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef int32_t w_coord_t;
struct column { int64_t key; };
struct columns { struct column *values; unsigned count; };
struct cursor { struct columns data; unsigned index; };
typedef struct cursor dict_server_chunks_it_t[1];
#define dict_server_chunks_it(it, cols) (*(it) = (struct cursor){ cols, 0 })
#define dict_server_chunks_end_p(it) ((it)->index >= (it)->data.count)
#define dict_server_chunks_ref(it) (&(it)->data.values[(it)->index])
#define dict_server_chunks_next(it) (++(it)->index)
#define S_CHUNK_X(id) ((int32_t)(id))
#define S_CHUNK_Z(id) ((int32_t)((uint64_t)(id) >> 32))
#define CHUNK_DIST2(x1,x2,z1,z2) (((x1)-(x2))*((x1)-(x2))+((z1)-(z2))*((z1)-(z2)))
#define assert(expr) ((void)0)
static int abs(int value) { return value < 0 ? -value : value; }
struct server_world {
    struct columns chunks;
    bool peer_interest;
    w_coord_t peer_cx, peer_cz;
};
#include "eviction-body.inc"
static int checks(void) {
    struct column cols[] = {{0}, {12}, {7}};
    struct server_world w = {{cols, 3}, true, 12, 0};
    w_coord_t x, z;
    /* 12 is required by B. Seven is the only unclaimed column. */
    if (!server_world_furthest_chunk(&w, 2, 0, 0, &x, &z) || x != 7 || z != 0) return 21;
    w.chunks.count = 2;
    if (server_world_furthest_chunk(&w, 2, 0, 0, &x, &z)) return 22;
    w.peer_interest = false;
    if (!server_world_furthest_chunk(&w, 2, 0, 0, &x, &z) || x != 12) return 23;
    return 0;
}
int main(void) { return checks(); }
