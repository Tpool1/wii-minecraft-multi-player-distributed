/* Exercise the real serializer and cNBT clone/free paths at each allocation
 * failure. */
#include "../../source/network/level_archive.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned calls, fail_at, live;
static void* watched;
static bool released;
static void* allocate(size_t size) {
	if(++calls == fail_at)
		return NULL;
	void* p = malloc(size);
	if(p)
		++live;
	return p;
}
static void release(void* p) {
	if(p) {
		if(p == watched)
			released = true;
		--live;
	}
	free(p);
}
#define malloc allocate
#define free release
#include "inventory-write.inc"
#include <cNBT/nbt_treeops.c>
#undef malloc
#undef free

bool level_archive_read(struct level_archive* la, struct level_archive_tag tag,
						void* out, size_t size) {
	assert(tag.type == TAG_LIST && !size);
	*(nbt_node**)out = la->data;
	return true;
}
#define CHECK(x)                                                               \
	do {                                                                       \
		if(!(x)) {                                                             \
			fprintf(stderr,                                                    \
					"inventory checkpoint failure at allocation %u, line %d: " \
					"%s\n",                                                    \
					fail_at, __LINE__, #x);                                    \
			return 1;                                                          \
		}                                                                      \
	} while(0)
static int first_count(nbt_node* list) {
	struct nbt_list* first = list_entry(list->payload.tag_list->entry.flink,
										struct nbt_list, entry);
	nbt_node* count = nbt_find_by_path(first->data, ".Count");
	return count ? count->payload.tag_byte : -1;
}
int main(void) {
	struct nbt_list* empty = allocate(sizeof *empty);
	empty->data = allocate(sizeof *empty->data);
	empty->data->type = TAG_COMPOUND;
	INIT_LIST_HEAD(&empty->entry);
	nbt_node root = {.type = TAG_LIST, .payload.tag_list = empty};
	struct level_archive la = {.data = &root};
	struct item_data items[INVENTORY_SIZE] = {0};
	struct inventory bag = {.items = items, .capacity = INVENTORY_SIZE};
	items[INVENTORY_SLOT_HOTBAR] = (struct item_data) {.id = 4, .count = 3};
	CHECK(level_archive_write_inventory(&la, &bag));
	CHECK(first_count(&root) == 3);
	watched = root.payload.tag_list;
	unsigned baseline = live;
	items[INVENTORY_SLOT_HOTBAR].count = 9;
	items[INVENTORY_SLOT_HOTBAR + 1] = (struct item_data) {.id = 5, .count = 1};
	for(fail_at = 1; fail_at <= 200; ++fail_at) {
		calls = 0;
		released = false;
		la.modified = false;
		if(level_archive_write_inventory(&la, &bag)) {
			CHECK(calls < fail_at && released && la.modified
				  && first_count(&root) == 9);
			nbt_free_list(root.payload.tag_list);
			CHECK(!live);
			printf("PASS inventory save retry across %u allocation failures\n",
				   fail_at - 1);
			return 0;
		}
		CHECK(!released && root.payload.tag_list == watched && !la.modified);
		CHECK(live == baseline && first_count(&root) == 3);
	}
	return 1;
}