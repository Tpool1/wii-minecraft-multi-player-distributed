/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_entities.h"
#include <stdio.h>
int main(void) {
	dict_entity_t entities;
	dict_entity_init(entities);
	struct entity local = {.id = 1};
	*dict_entity_safe_get(entities, 1) = &local;
	vec3 later = {7, 8, 9};
	/* A late join can receive a movement or removal before seeing an old spawn.
	 */
	if(clin_move_known_entity(entities, 42, later)
	   || clin_remove_known_entity(entities, 42, &local))
		return 1;
	struct entity* item = calloc(1, sizeof *item);
	if(!item)
		return 2;
	*dict_entity_safe_get(entities, 42) = item;
	if(!clin_move_known_entity(entities, 42, later)
	   || item->network_pos[2] != 9)
		return 3;
	if(!clin_remove_known_entity(entities, 42, &local)
	   || clin_remove_known_entity(entities, 42, &local))
		return 4;
	if(clin_remove_known_entity(entities, 1, &local)
	   || *dict_entity_get(entities, 1) != &local)
		return 5;
	dict_entity_clear(entities);
	puts("PASS late-join missing move/removal, later spawn, duplicate removal, "
		 "local actor retained");
	return 0;
}
