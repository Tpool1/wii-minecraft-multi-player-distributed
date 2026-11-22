/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LAN_PACING_H
#define LAN_PACING_H
#include <stdint.h>

/* Start every host tick with a new deadline. Even an overrun yields for a
 * millisecond so a burst of late ticks cannot starve the render worker. */
static inline unsigned lan_tick_wait_ms(int32_t work_ms) {
    if(work_ms <= 0) return 50;
    return work_ms < 50 ? 50u - (unsigned)work_ms : 1u;
}
#endif
