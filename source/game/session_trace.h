/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SESSION_TRACE_H
#define SESSION_TRACE_H
#include <stdbool.h>

void session_trace_enable(bool enabled);
void session_trace(const char *format, ...);
void session_trace_pose(bool remote, int slot, float x, float y, float z);
#endif
