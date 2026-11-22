/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "session_metrics.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif
#ifdef PLATFORM_WII
#include <malloc.h>
#include <ogc/irq.h>
#include <ogc/system.h>
#endif

enum { WINDOW = 100, MAX_WINDOWS = 10000 };
struct samples {
    uint32_t values[WINDOW];
    unsigned count, windows;
};
static bool enabled, have_frame, have_tick;
static ptime_t last_frame, last_tick;
static struct samples frame, tick, period;
static char directory[16];

void session_metrics_enable(bool on) {
    if(!on || enabled) return;
    for(unsigned i = 0; i < 100; ++i) {
        snprintf(directory, sizeof(directory), "metrics%02u", i);
#ifdef _WIN32
        int result = _mkdir(directory);
#else
        int result = mkdir(directory, 0777);
#endif
        if(result == 0) {
            enabled = true;
            return;
        }
        if(errno != EEXIST) return;
    }
}

/* Each file has just one producer. Reopen only at window boundaries, keeping
 * completed windows available on a cleanly unmounted SD. A full disk must not
 * interrupt play. A new boot never truncates a previous run. */
static FILE *open_log(const char *name, const char *header) {
    char path[48];
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    FILE *file = fopen(path, "ab");
    if(!file) return NULL;
    if(fseek(file, 0, SEEK_END) || (ftell(file) == 0 && fputs(header, file) < 0)) {
        fclose(file);
        return NULL;
    }
    return file;
}

static int compare(const void *left, const void *right) {
    uint32_t a = *(const uint32_t *)left, b = *(const uint32_t *)right;
    return (a > b) - (a < b);
}

static bool add(struct samples *s, uint32_t value, const char *filename,
                const char *name, uint32_t deadline) {
    if(s->windows >= MAX_WINDOWS) return false;
    s->values[s->count++] = value;
    if(s->count < WINDOW) return false;
    qsort(s->values, WINDOW, sizeof(s->values[0]), compare);
    unsigned late = 0;
    for(unsigned i = 0; i < WINDOW; ++i) late += s->values[i] > deadline;
    FILE *f = open_log(filename, "series,n,p95,p99,max,over_limit,limit,window\n");
    if(f) {
        fprintf(f, "%s,%u,%lu,%lu,%lu,%u,%lu,%u\n", name, WINDOW,
                (unsigned long)s->values[94], (unsigned long)s->values[98],
                (unsigned long)s->values[99], late, (unsigned long)deadline,
                s->windows + 1);
        fclose(f);
    }
    ++s->windows;
    s->count = 0;
    return true;
}

static uint32_t elapsed(ptime_t start, ptime_t end) {
    int32_t ms = time_diff_ms(start, end);
    return ms > 0 ? (uint32_t)ms : 0;
}

#ifdef PLATFORM_WII
/* Bank owners are tagged from the actual cached or uncached pointer. */
static uint32_t xfb_bytes[3], fifo_bytes[3];
static unsigned bank(const void *pointer) {
    uintptr_t address = (uintptr_t)pointer & 0x3FFFFFFFu;
    if(address < 0x01800000u && pointer) return 1;
    if(address >= 0x10000000u && address < 0x14000000u) return 2;
    return 0;
}
void session_metrics_buffers(void *const xfb[3], uint32_t xfb_size,
                             const void *fifo, uint32_t fifo_size) {
    for(unsigned i = 0; i < 3; ++i) xfb_bytes[bank(xfb[i])] += xfb_size;
    fifo_bytes[bank(fifo)] = fifo_size;
}

enum { VI_RING = 64 };
static volatile uint32_t vi_delta[VI_RING], vi_write, vi_read, vi_dropped;
static bool have_retrace;
static uint32_t last_retrace;
static struct samples vi;
/* Pre-retrace callback. Only arithmetic and a bounded store run in the ISR. */
void session_metrics_present(uint32_t retrace) {
    if(!enabled) return;
    if(have_retrace) {
        uint32_t next = (vi_write + 1) % VI_RING;
        if(next == vi_read) ++vi_dropped;
        else {
            vi_delta[vi_write] = retrace - last_retrace;
            vi_write = next;
        }
    }
    last_retrace = retrace;
    have_retrace = true;
}
static void collect_vi(void) {
    uint32_t local[VI_RING], n = 0;
    uint32_t level = IRQ_Disable();
    while(vi_read != vi_write) {
        local[n++] = vi_delta[vi_read];
        vi_read = (vi_read + 1) % VI_RING;
    }
    IRQ_Restore(level);
    for(uint32_t i = 0; i < n; i++) add(&vi, local[i], "frame.csv", "vi_swap_retraces", 2);
}

static uint32_t min1 = UINT32_MAX, min2 = UINT32_MAX;
static void memory_sample(bool publish) {
    uint32_t a1 = SYS_GetArena1Size(), a2 = SYS_GetArena2Size();
    if(a1 < min1) min1 = a1;
    if(a2 < min2) min2 = a2;
    if(!publish) return;
    struct mallinfo info = mallinfo();
    FILE *f = open_log("memory.csv", "frame_window,arena1_unreserved_min_B,arena2_unreserved_min_B,"
        "newlib_reserved_B,newlib_inuse_B,newlib_reusable_B,xfb_MEM1_B,xfb_MEM2_B,"
        "fifo_MEM1_B,fifo_MEM2_B,vi_dropped_total\n");
    if(f) {
        fprintf(f, "%u,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n", frame.windows,
            (unsigned long)min1, (unsigned long)min2, (unsigned long)info.arena,
            (unsigned long)info.uordblks, (unsigned long)info.fordblks,
            (unsigned long)xfb_bytes[1], (unsigned long)xfb_bytes[2],
            (unsigned long)fifo_bytes[1], (unsigned long)fifo_bytes[2],
            (unsigned long)vi_dropped);
        fclose(f);
    }
    min1 = min2 = UINT32_MAX;
}
#endif

void session_metrics_frame(ptime_t start) {
    if(!enabled || frame.windows >= MAX_WINDOWS) return;
    bool publish = false;
    if(have_frame) publish = add(&frame, elapsed(last_frame, start), "frame.csv", "frame_start_ms", 50);
    last_frame = start;
    have_frame = true;
#ifdef PLATFORM_WII
    collect_vi();
    memory_sample(publish);
#else
    (void)publish;
#endif
}

void session_metrics_tick(ptime_t start, ptime_t finish) {
    if(!enabled || period.windows >= MAX_WINDOWS) return;
    add(&tick, elapsed(start, finish), "tick.csv", "tick_work_ms", 50);
    if(have_tick) add(&period, elapsed(last_tick, start), "tick.csv", "tick_period_ms", 50);
    last_tick = start;
    have_tick = true;
}
