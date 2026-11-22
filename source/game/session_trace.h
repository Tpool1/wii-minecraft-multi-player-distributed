/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SESSION_TRACE_H
#define SESSION_TRACE_H
#include <stdbool.h>

struct config;
struct world;
void session_trace_probes(struct config *config);
void session_trace_column(struct world *world, int x, int y, int z, int sx, int sy, int sz);
void session_trace_enable(bool enabled);
void session_trace(const char *format, ...);
void session_trace_pose(bool remote, int slot, float x, float y, float z);
#endif
