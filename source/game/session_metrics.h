/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SESSION_METRICS_H
#define SESSION_METRICS_H
#include <stdbool.h>
#include <stdint.h>
#include "../platform/time.h"

/* Configure once at startup, before launching the authority thread. */
void session_metrics_enable(bool on);
void session_metrics_frame(ptime_t start);
void session_metrics_tick(ptime_t start, ptime_t finish);
#ifdef PLATFORM_WII
void session_metrics_buffers(void *const xfb[3], uint32_t xfb_size,
                             const void *fifo, uint32_t fifo_size);
void session_metrics_present(uint32_t retrace);
#endif
#endif
