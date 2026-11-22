/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "session_launch.h"
#include <string.h>

struct session_launch session_launch_plan(const char *role, const char *port, const char *world) {
	struct session_launch plan = { LAN_OFFLINE, LAUNCH_ERROR, 0 };
	if(role && strcmp(role, "offline")) {
		if(!strcmp(role, "host")) plan.role = LAN_HOST;
		else if(!strcmp(role, "peer")) plan.role = LAN_PEER;
		else return plan;
	}
	unsigned value = 45838;
	if(port) {
		value = 0;
		for(const char *p = port; *p; p++) {
			if(*p < '0' || *p > '9') return plan;
			value = value * 10u + (unsigned)(*p - '0');
			if(value > 65535u) return plan;
		}
		if(!value) return plan;
	}
	plan.port = (uint16_t)value;
	plan.screen = plan.role == LAN_PEER ? LAUNCH_JOIN
		: world && *world ? LAUNCH_LOAD_WORLD : LAUNCH_SELECT_WORLD;
	return plan;
}
