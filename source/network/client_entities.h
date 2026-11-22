/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CLIENT_ENTITIES_H
#define CLIENT_ENTITIES_H
#include "../entity/entity.h"
#include <stdlib.h>

static inline bool clin_move_known_entity(dict_entity_t entities, uint32_t id,
										  vec3 pos) {
	struct entity** entry = dict_entity_get(entities, id);
	if(!entry || !*entry)
		return false;
	glm_vec3_copy(pos, (*entry)->network_pos);
	return true;
}
static inline bool clin_remove_known_entity(dict_entity_t entities, uint32_t id,
											struct entity* local) {
	struct entity** entry = dict_entity_get(entities, id);
	if(!entry || !*entry || *entry == local)
		return false;
	free(*entry);
	dict_entity_erase(entities, id);
	return true;
}
#endif
