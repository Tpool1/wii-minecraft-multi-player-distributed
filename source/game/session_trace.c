/*-------------------------------------------------------------

sys_report.c -- Support for printing to Dolphin debug UART

Copyright (C) 2023
Dave Murphy (WinterMute) <davem@devkitpro.org>

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any
damages arising from the use of this software.

Permission is granted to anyone to use this software for any
purpose, including commercial applications, and to alter it and
redistribute it freely, subject to the following restrictions:

1.	The origin of this software must not be misrepresented; you
must not claim that you wrote the original software. If you use
this software in a product, an acknowledgment in the product
documentation would be appreciated but is not required.

2.	Altered source versions must be plainly marked as such, and
must not be misrepresented as being the original software.

3.	This notice may not be removed or altered from any source
distribution.

-------------------------------------------------------------*/
/* Modified from devkitPro/libogc sys_report.c @ ef718e2d7df5e5b1750a9e1a67c17350bdd30d3e.
 * Formats opt-in game event records and preserves the Dolphin-only hardware guard. */

#include "session_trace.h"
#include "../config.h"
#include "../world.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#ifdef PLATFORM_WII
#include <ogc/exi.h>
#endif

static bool enabled;
void session_trace_enable(bool on) { enabled = on; }

void session_trace(const char *format, ...) {
	if(!enabled) return;
	char buf[192] = "[FCX] ";
	va_list args;
	va_start(args, format);
	int count = vsnprintf(buf + 6, sizeof(buf) - 7, format, args);
	va_end(args);
	if(count < 0) return;
	size_t len = strlen(buf);
	buf[len++] = '\n';
	buf[len] = '\0';
#ifdef PLATFORM_WII
    if (((*(volatile u32 *)0xCD006024) & 0xFF) != 0x01) return;
    for (size_t i = 0; i < len; ++i) {
        if (buf[i] == '\n') buf[i] = '\r';
    }
    if (!EXI_Lock(EXI_CHANNEL_0, EXI_DEVICE_1, NULL)) return;
    if (EXI_Select(EXI_CHANNEL_0, EXI_DEVICE_1, EXI_SPEED8MHZ)) {
        u32 cmd = 0xa0010000;
        if (EXI_Imm(EXI_CHANNEL_0, &cmd, 4, EXI_WRITE, NULL) &&
            EXI_Sync(EXI_CHANNEL_0)) {
            EXI_ImmEx(EXI_CHANNEL_0, buf, len, EXI_WRITE);
        }
        EXI_Deselect(EXI_CHANNEL_0);
    }
    EXI_Unlock(EXI_CHANNEL_0);

#else
	fputs(buf, stdout);
#endif
}

static int probes[4 * 3];
static size_t probe_count;

void session_trace_probes(struct config *config) {
	size_t count = sizeof probes / sizeof *probes;
	if(enabled && config_read_int_array(config, "network.probes", probes, &count))
		probe_count = count / 3;
}

void session_trace_column(struct world *world, int x, int y, int z, int sx, int sy, int sz) {
	for(size_t i = 0; i < probe_count; i++) {
		int *p = &probes[i * 3];
		if(p[0] >= x && p[0] < x + sx && p[1] >= y && p[1] < y + sy && p[2] >= z && p[2] < z + sz) {
			struct block_data block = world_get_block(world, p[0], p[1], p[2]);
			session_trace("READ_AT x=%d y=%d z=%d type=%u", p[0], p[1], p[2], (unsigned)block.type);
		}
	}
}

void session_trace_pose(bool remote, int slot, float x, float y, float z) {
	if(!enabled) return;
	static struct sample { bool seen; float x, y, z; } samples[2];
	struct sample *p = &samples[remote ? 1 : 0];
	if(p->seen && fabsf(x-p->x) < 0.05F && fabsf(y-p->y) < 0.05F && fabsf(z-p->z) < 0.05F)
		return;
	*p = (struct sample){ true, x, y, z };
	session_trace("%s slot=%d x=%.3f y=%.3f z=%.3f", remote ? "REMOTE" : "LOCAL", slot, x, y, z);
}
