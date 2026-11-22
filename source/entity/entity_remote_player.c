/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "entity.h"
#include "../graphics/render_model.h"

static void remote_render(struct entity *e, mat4 view, float delta) {
	vec3 eye;
	mat4 model, mv;
	glm_vec3_lerp(e->pos_old, e->pos, delta, eye);
	glm_translate_make(model, eye);
	glm_rotate_y(model, glm_rad(180.0F - e->orient[0]), model);
	// The existing inventory model is 32 pixels tall, with the origin at eye level.
	glm_scale_uni(model, 1.8F / 32.0F);
	glm_mat4_mul(view, model, mv);
	render_model_player(mv, e->orient[1], 0, 0, 0, NULL, NULL, NULL, NULL, NULL);
}

static size_t remote_bounds(const struct entity *e, struct AABB *box) {
	(void)e;
	(void)box;
	// Other players are visible but cannot be mined, attacked, or used as a block target.
	return 0;
}

void entity_remote_player(uint32_t id, struct entity *e, struct world *world) {
	*e = (struct entity){ .id = id, .type = ENTITY_REMOTE_PLAYER, .name = "Player",
		.tick_client = entity_default_client_tick, .render = remote_render,
		.teleport = entity_default_teleport, .getBoundingBox = remote_bounds };
	entity_default_init(e, false, world);
}
