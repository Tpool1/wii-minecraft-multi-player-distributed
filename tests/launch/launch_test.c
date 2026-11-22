/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include "../../source/game/session_launch.h"

static int failures;
static void check(const char *name, const char *role, const char *port, const char *world,
				  enum lan_role want_role, enum launch_screen want_screen, uint16_t want_port) {
	struct session_launch plan = session_launch_plan(role, port, world);
	if(plan.role != want_role || plan.screen != want_screen || plan.port != want_port) {
		fprintf(stderr, "FAIL %s: role=%d screen=%d port=%u\n", name,
				(int)plan.role, (int)plan.screen, (unsigned)plan.port);
		failures++;
	}
}
int main(void) {
	check("unconfigured single player", NULL, NULL, NULL, LAN_OFFLINE, LAUNCH_SELECT_WORLD, 45838);
	check("private fixture can autostart", "offline", NULL, "saves/Fixture", LAN_OFFLINE, LAUNCH_LOAD_WORLD, 45838);
	check("host chooses a save", "host", "45838", NULL, LAN_HOST, LAUNCH_SELECT_WORLD, 45838);
	check("host loads configured save", "host", "65535", "saves/Fixture", LAN_HOST, LAUNCH_LOAD_WORLD, 65535);
	check("peer ignores any local save", "peer", "1", "saves/Fixture", LAN_PEER, LAUNCH_JOIN, 1);
	check("invalid role", "other", NULL, NULL, LAN_OFFLINE, LAUNCH_ERROR, 0);
	const char *bad[] = {"", "0", "65536", "4294967297", "-1", "+2", " 7", "5x"};
	for(size_t i=0; i < sizeof bad / sizeof *bad; i++)
		check("invalid port", "host", bad[i], NULL, LAN_HOST, LAUNCH_ERROR, 0);
	if(!failures) puts("PASS launch role, authority, world selection and port validation");
	return failures != 0;
}
