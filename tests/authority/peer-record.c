/* Exercise the production file implementation with an in-memory actor and a
 * disposable directory. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define SERVER_LOCAL
#define SERVER_INTERFACE_H
#define INVENTORY_SIZE 45
#define INVENTORY_SIZE_HOTBAR 9
#define WORLD_HEIGHT 128
#define WORLD_DIM_NETHER (-1)
#define MAX_PLAYER_HEALTH 160
typedef char string_t[512];
#define string_init_printf(p, ...) snprintf(p, sizeof(p), __VA_ARGS__)
#define string_get_cstr(p) (p)
#define string_clear(p) ((void)(p))
struct item_data {
	uint16_t id;
	uint8_t durability, count;
};
struct server_player {
	double x, y, z, rx, ry;
	int health, dimension;
	struct {
		int hotbar_slot;
		struct item_data items[INVENTORY_SIZE];
	} inventory;
};
struct server_local {
	struct server_player peer;
	uint32_t peer_generation;
	char level_name[512];
	struct {
		int dimension;
	} world;
};
#include "../../source/network/peer_record.c"

int main(void) {
	struct server_local s
		= {.level_name = ".",
		   .peer = {.x = 10.5, .y = 65.62, .z = 8.5, .health = 160}};
	s.peer.inventory.items[36] = (struct item_data) {.id = 3, .count = 16};
	if(!peer_record_load(&s) || s.peer_generation != 0 || !peer_record_save(&s))
		return 31;
	s.peer.x = -27.25;
	s.peer.inventory.items[36].count = 11;
	if(!peer_record_save(&s) || s.peer_generation != 2)
		return 32;
	s.peer.x = 0;
	s.peer.inventory.items[36].count = 0;
	if(!peer_record_load(&s) || s.peer.x != -27.25
	   || s.peer.inventory.items[36].count != 11)
		return 33;
	/* Simulate a short write in generation 2, then validate generation 1. */
	FILE* f = fopen("peer-b-0.fcx", "wb");
	if(!f || fwrite("FCB1", 1, 4, f) != 4 || fclose(f))
		return 34;
	if(!peer_record_load(&s) || s.peer_generation != 1 || s.peer.x != 10.5
	   || s.peer.inventory.items[36].count != 16)
		return 35;
	f = fopen("peer-b-1.fcx", "wb");
	if(!f || fwrite("broken", 1, 6, f) != 6 || fclose(f))
		return 36;
	if(peer_record_load(&s))
		return 37;
	strcpy(s.level_name, "missing-directory");
	if(peer_record_save(&s))
		return 38;
	return 0;
}
