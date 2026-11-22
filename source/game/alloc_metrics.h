/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef FCX_ALLOC_METRICS_H
#define FCX_ALLOC_METRICS_H
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

enum alloc_work { AM_CHUNK, AM_MESH, AM_NETWORK, AM_ARCHIVE, AM_AUTHORITY, AM_ALL, AM_ROWS };
enum alloc_bank { AM_OTHER, AM_MEM1, AM_MEM2, AM_BANKS };
struct allocation_sample {
    uint32_t live[AM_BANKS], peak[AM_BANKS];
    uint32_t request_max, failed_max, failed, dropped;
};

#if defined(PLATFORM_WII) || defined(FCX_ALLOC_TEST)
bool alloc_metrics_begin(void);
bool alloc_metrics_snapshot(struct allocation_sample rows[AM_ROWS]);
enum alloc_bank alloc_bank(uintptr_t address);
void *alloc_payload(enum alloc_work work, size_t size);
void *alloc_zero(enum alloc_work work, size_t size);
bool alloc_resize(enum alloc_work work, void **owner, size_t size);
void alloc_adopt(enum alloc_work work, void *p, size_t size);
void alloc_free(void *p);
#else
#define alloc_payload(work, size) malloc(size)
#define alloc_zero(work, size) calloc(1, size)
#define alloc_adopt(work, p, size) ((void)0)
#define alloc_free(p) free(p)
#endif
#endif
