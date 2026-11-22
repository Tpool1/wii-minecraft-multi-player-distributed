/* Exercise the production command handlers with a disposable one-cell world and
 * recorded RPC sinks. */
#include "../../source/network/server_lan.c"
#include <stdarg.h>
#include <stdio.h>

static struct block_data cell;
static unsigned changes, private_updates[2], corrections[2], remote_poses[2];
static int accepted = -1;
static unsigned load_failures;
static bool wall;
static unsigned block_updates[2];
static void received(unsigned recipient, const struct client_rpc* m) {
	if(m->type == CRPC_HOST_LOAD_FAILED) ++load_failures;
	if(m->type == CRPC_SET_BLOCK)
		++block_updates[recipient];
	if(m->type == CRPC_INVENTORY_SLOT)
		++private_updates[recipient];
	if(m->type == CRPC_PLAYER_POS)
		++corrections[recipient];
	if(m->type == CRPC_REMOTE_PLAYER)
		++remote_poses[recipient];
}
void clin_rpc_send(struct client_rpc* m) {
	received(0, m);
}
bool lan_host_send(const struct client_rpc* m) {
	received(1, m);
	return true;
}
bool lan_host_enabled(void) {
	return true;
}
bool session_trace_enabled(void) { return false; }
void session_trace(const char* fmt, ...) {
	if(strncmp(fmt, "EDIT ", 5))
		return;
	va_list args;
	va_start(args, fmt);
	char line[200];
	vsnprintf(line, sizeof(line), fmt, args);
	va_end(args);
	accepted = strstr(line, "accepted=1") != NULL;
}
bool server_world_get_block(struct server_world* w, int x, int y, int z,
							struct block_data* b) {
	(void)w;
	if(x != 9 || z != 10 || (y != 64 && y != 65))
		return false;
	*b = y == 64 ? (struct block_data) {.type = BLOCK_STONE} : cell;
	return true;
}
bool server_world_set_block(struct server_local* s, int x, int y, int z,
							struct block_data b) {
	if(x != 9 || y != 65 || z != 10)
		return false;
	cell = b;
	++changes;
	server_local_publish_block(s, x, y, z, b);
	return true;
}
void blocks_side_offset(enum side side, int* x, int* y, int* z) {
	*x = 0;
	*z = 0;
	*y = side == SIDE_TOP ? 1 : -1;
}
bool entity_aabb_intersection(struct entity* e, struct AABB* a) {
	(void)e;
	(void)a;
	return wall;
}
void aabb_setsize_centered(struct AABB* a, float x, float y, float z) {
	*a = (struct AABB) {-x / 2, -y / 2, -z / 2, x / 2, y / 2, z / 2};
}
void aabb_translate(struct AABB* a, float x, float y, float z) {
	a->x1 += x;
	a->x2 += x;
	a->y1 += y;
	a->y2 += y;
	a->z1 += z;
	a->z2 += z;
}

#define REQUIRE(test)                                                          \
	do {                                                                       \
		if(!(test)) {                                                          \
			fprintf(stderr, "line %d: %s\n", __LINE__, #test);                 \
			return 1;                                                          \
		}                                                                      \
	} while(0)
int main(void) {
	struct server_local s = {0};
	/* Low-level path failure must stop before any world or peer is exposed. */
	struct server_rpc invalid = {.type=SRPC_LOAD_WORLD};
	char longpath[700];
	memset(longpath,'a',sizeof longpath - 1);
	longpath[sizeof longpath-1]='\0';
	string_init_set_str(invalid.payload.load_world.name,longpath);
	REQUIRE(server_lan_local_command(&s,&invalid));
	REQUIRE(load_failures==1 && !s.player.has_pos && !s.peer_joined);
	struct item_data bag[2][INVENTORY_SIZE] = {0};
	s.peer_joined = true;
	for(unsigned i = 0; i < 2; ++i) {
		struct server_player* p = actor(&s, i);
		p->x = i ? 10.5 : 8.5;
		p->y = 65.62;
		p->z = 8.5;
		p->finished_loading = true;
		p->inventory.items = bag[i];
		p->inventory.capacity = INVENTORY_SIZE;
		bag[i][INVENTORY_SLOT_HOTBAR]
			= (struct item_data) {.id = i ? 5 : 4, .count = 2};
		s.views[i].count = 1;
	}
	struct server_rpc put = {.type = SRPC_BLOCK_PLACE,
							 .action_seq = 1,
							 .has_expected_block = true,
							 .expected_block = BLOCK_AIR,
							 .payload.block_place = {9, 64, 10, SIDE_TOP}};
	command(&s, 0, &put);
	REQUIRE(accepted && changes == 1 && cell.type == 4
			&& bag[0][INVENTORY_SLOT_HOTBAR].count == 1);
	peer_command(&put, &s);
	REQUIRE(!accepted && changes == 1
			&& bag[1][INVENTORY_SLOT_HOTBAR].count == 2);
	REQUIRE(private_updates[0] == 1 && private_updates[1] == 0);
	cell.type = BLOCK_AIR;
	command(&s, 0, &put);
	peer_command(&put, &s);
	REQUIRE(!accepted && !cell.type && changes == 1);
	put.action_seq = 2;
	peer_command(&put, &s);
	REQUIRE(accepted && cell.type == 5 && private_updates[0] == 1
			&& private_updates[1] == 1);
	struct server_rpc dig
		= {.type = SRPC_BLOCK_DIG,
		   .action_seq = 3,
		   .has_expected_block = true,
		   .expected_block = 5,
		   .payload.block_dig = {.finished = true, .x = 9, .y = 65, .z = 10}};
	for(int i = INVENTORY_SLOT_MAIN; i < INVENTORY_SIZE; ++i)
		bag[1][i] = (struct item_data) {.id = 5, .count = 64};
	peer_command(&dig, &s);
	REQUIRE(!accepted && cell.type == 5 && changes == 2);
	bag[1][INVENTORY_SLOT_MAIN].count = 63;
	dig.action_seq = 4;
	peer_command(&dig, &s);
	REQUIRE(accepted && !cell.type && bag[1][INVENTORY_SLOT_MAIN].count == 64
			&& private_updates[1] == 2 && private_updates[0] == 1);
	struct server_rpc move = {.type = SRPC_PLAYER_POS,
							  .payload.player_pos = {10.5, 65.62, 9, 0, 0, 0}};
	peer_command(&move, &s);
	REQUIRE(s.peer.z == 9 && remote_poses[0] == 1 && remote_poses[1] == 0);
	move.payload.player_pos.z = 1000;
	peer_command(&move, &s);
	REQUIRE(s.peer.z == 9 && corrections[1] == 1);
	move.payload.player_pos.x = NAN;
	peer_command(&move, &s);
	REQUIRE(s.peer.x == 10.5 && corrections[1] == 2 && corrections[0] == 0);
	move.payload.player_pos.x = 10.5;
	move.payload.player_pos.z = 9.5;
	wall = true;
	peer_command(&move, &s);
	REQUIRE(s.peer.z == 9 && corrections[1] == 3);
	wall = false;
	move.payload.player_pos.z = 11.0;
	peer_command(&move, &s);
	REQUIRE(s.peer.z == 11);
	move.payload.player_pos.z = 13.0;
	peer_command(&move, &s);
	REQUIRE(s.peer.z == 11 && corrections[1] == 4);
	++s.world_time;
	peer_command(&move, &s);
	REQUIRE(s.peer.z == 13);
	s.views[1].columns[0].x = 12;
	unsigned a = block_updates[0], b = block_updates[1];
	server_local_publish_block(&s, 8, 65, 8, cell);
	REQUIRE(block_updates[0] == a + 1 && block_updates[1] == b);
	server_local_publish_block(&s, 12 * 16, 65, 8, cell);
	REQUIRE(block_updates[0] == a + 1 && block_updates[1] == b + 1);
	return 0;
}

/* Unused I/O paths must fail the test if any command reaches them. */
bool server_world_save_chunk_obj(struct server_world* w, bool e, int x, int z,
								 struct server_chunk* c) {
	abort();
}
bool peer_record_save(struct server_local* s) {
	abort();
}
bool peer_record_load(struct server_local* s) {
	abort();
}
void lan_host_pump(void (*l)(bool, void*), void (*c)(struct server_rpc*, void*),
				   void* u) {
	abort();
}
bool lan_host_connected(void) {
	return true;
}
void lan_host_disconnect(void) {
	abort();
}
bool server_world_furthest_chunk(struct server_world* w, int d, int x, int z,
								 int* cx, int* cz) {
	abort();
}
bool server_world_save_chunk(struct server_world* w, bool e, int x, int z) {
	abort();
}
void server_world_random_tick(struct server_world* w, struct random_gen* g,
							  struct server_local* s, int x, int z, int d) {
	abort();
}
void server_world_tick(struct server_world* w, struct server_local* s) {
	abort();
}
bool lan_host_can_send_chunk(void) {
	abort();
}
bool server_world_disk_has_chunk(struct server_world* w, int x, int z) {
	abort();
}
bool server_world_load_chunk(struct server_world* w, int x, int z,
							 struct server_chunk** c) {
	abort();
}
bool level_archive_read_inventory(struct level_archive* l,
								  struct inventory* i) {
	abort();
}
struct item* item_get(struct item_data* i) {
	abort();
}

bool level_archive_write_player(struct level_archive* a, vec3 p, vec2 r, vec3 v,
								enum world_dim d) {
	abort();
}
bool level_archive_write_inventory(struct level_archive* a,
								   struct inventory* i) {
	abort();
}
bool level_archive_write(struct level_archive* a, struct level_archive_tag t,
						 void* d) {
	abort();
}
bool level_archive_checkpoint(struct level_archive* a) {
	abort();
}
