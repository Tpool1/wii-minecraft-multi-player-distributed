/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Two tiny in-memory registries stand in for blocks_init/items_init in the
 * native tests. */
#include "lan_rpc.h"
struct block* blocks[256];
struct item* items[ITEMS_MAX];
static enum block_material stone(struct block_info* info) {
	(void)info;
	return MATERIAL_STONE;
}
static enum block_material wood(struct block_info* info) {
	(void)info;
	return MATERIAL_WOOD;
}
void wire_fixture_registry(bool alternate_material, bool alternate_texture) {
	static struct block tile = {.name = "Fixture", .opacity = 15};
	static struct item tool = {.name = "Fixture tool", .max_stack = 1};
	tile.getMaterial = alternate_material ? wood : stone;
	tool.render_data.item.texture_x = alternate_texture ? 9 : 2;
	blocks[1] = &tile;
	items[257] = &tool;
}
