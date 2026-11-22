/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SESSION_LAUNCH_H
#define SESSION_LAUNCH_H
#include "../network/lan_transport.h"

enum launch_screen { LAUNCH_SELECT_WORLD, LAUNCH_LOAD_WORLD, LAUNCH_JOIN, LAUNCH_ERROR };
struct session_launch {
	enum lan_role role;
	enum launch_screen screen;
	uint16_t port;
};
struct session_launch session_launch_plan(const char *role, const char *port, const char *world);
#endif
