/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "alloc_metrics.h"
#if defined(PLATFORM_WII) || defined(FCX_ALLOC_TEST)
#include <string.h>
#ifdef PLATFORM_WII
#include <ogc/mutex.h>
static mutex_t lock;
static void enter(void) { LWP_MutexLock(lock); }
static void leave(void) { LWP_MutexUnlock(lock); }
#elif defined(_WIN32)
#include <windows.h>
static SRWLOCK lock = SRWLOCK_INIT;
static void enter(void) { AcquireSRWLockExclusive(&lock); }
static void leave(void) { ReleaseSRWLockExclusive(&lock); }
#else
#include <pthread.h>
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static void enter(void) { pthread_mutex_lock(&lock); }
static void leave(void) { pthread_mutex_unlock(&lock); }
#endif

/* Eight probes maximum. Full buckets explicitly undercount live payload. */
enum { ALLOC_BUCKETS = 512, ALLOC_WAYS = 8 };
struct allocation { uintptr_t address; uint32_t bytes; enum alloc_work work; };
struct registry {
    struct allocation buckets[ALLOC_BUCKETS][ALLOC_WAYS];
    struct allocation_sample rows[AM_ROWS];
};
static struct registry *tracking;

bool alloc_metrics_begin(void) {
    if(tracking) return true;
    struct registry *r = calloc(1, sizeof *r);
    if(!r) return false;
#ifdef PLATFORM_WII
    if(LWP_MutexInit(&lock, false) < 0) { free(r); return false; }
#endif
    /* Called by main before starting either worker. Never disabled mid-run. */
    tracking = r;
    return true;
}

enum alloc_bank alloc_bank(uintptr_t a) {
    if(a > UINT32_MAX) return AM_OTHER;
    uint32_t addr = (uint32_t)a & 0xbfffffffu; /* normalize uncached Wii aliases */
    if(addr >= 0x80000000u && addr < 0x81800000u) return AM_MEM1;
    if(addr >= 0x90000000u && addr < 0x94000000u) return AM_MEM2;
    return AM_OTHER;
}

static struct allocation *bucket(uintptr_t key) {
    uint32_t h = (uint32_t)(key >> 3);
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    return tracking->buckets[h % ALLOC_BUCKETS];
}

static void add_live(enum alloc_work work, unsigned bank, uint32_t n) {
    struct allocation_sample *s = &tracking->rows[work];
    s->live[bank] += n;
    if(s->live[bank] > s->peak[bank]) s->peak[bank] = s->live[bank];
}

static void admit(enum alloc_work work, uintptr_t key, uint32_t n) {
    struct allocation *b = bucket(key), *empty = NULL;
    for(unsigned i=0; i<ALLOC_WAYS; i++) {
        if(b[i].address == key) return;
        if(!b[i].address && !empty) empty = &b[i];
    }
    if(!empty) {
        ++tracking->rows[work].dropped;
        ++tracking->rows[AM_ALL].dropped;
        return;
    }
    *empty = (struct allocation){key, n, work};
    unsigned bank = alloc_bank(key);
    add_live(work, bank, n);
    add_live(AM_ALL, bank, n);
}

static void retire(uintptr_t key) {
    if(!key) return;
    struct allocation *b = bucket(key);
    for(unsigned i=0; i<ALLOC_WAYS; i++) {
        if(b[i].address != key) continue;
        unsigned bank = alloc_bank(key);
        tracking->rows[b[i].work].live[bank] -= b[i].bytes;
        tracking->rows[AM_ALL].live[bank] -= b[i].bytes;
        b[i].address = 0;
        return;
    }
}

static void request(enum alloc_work work, uint32_t n, bool failed) {
    struct allocation_sample *s = &tracking->rows[work];
    if(n > s->request_max) s->request_max = n;
    if(failed) {
        ++s->failed;
        if(n > s->failed_max) s->failed_max = n;
    }
}

static void record(enum alloc_work work, size_t n, void *p) {
    request(work, (uint32_t)n, !p && n);
    request(AM_ALL, (uint32_t)n, !p && n);
    if(p) admit(work, (uintptr_t)p, (uint32_t)n);
}

void *alloc_payload(enum alloc_work work, size_t n) {
    void *p = malloc(n);
    if(tracking) { enter(); record(work,n,p); leave(); }
    return p;
}

void *alloc_zero(enum alloc_work work, size_t n) {
    void *p = calloc(1,n);
    if(tracking) { enter(); record(work,n,p); leave(); }
    return p;
}

void alloc_adopt(enum alloc_work work, void *p, size_t n) {
    /* Attribute pre-existing NBT planes only after ownership transfers. */
    if(tracking && p) { enter(); admit(work,(uintptr_t)p,(uint32_t)n); leave(); }
}

void alloc_free(void *p) {
    if(tracking && p) { enter(); retire((uintptr_t)p); leave(); }
    free(p);
}

bool alloc_resize(enum alloc_work work, void **owner, size_t n) {
    if(!n) { alloc_free(*owner); *owner = NULL; return true; }
    /* Serialize realloc and pointer replacement: the allocator may recycle the
       old address on another thread as soon as realloc moves the payload. */
    if(tracking) enter();
    uintptr_t old = (uintptr_t)*owner;
    void *next = realloc(*owner,n);
    if(tracking) {
        if(next) retire(old);
        record(work,n,next);
        leave();
    }
    if(next) *owner = next;
    return next != NULL;
}

bool alloc_metrics_snapshot(struct allocation_sample rows[AM_ROWS]) {
    if(!tracking) return false;
    enter();
    memcpy(rows, tracking->rows, sizeof tracking->rows);
    for(unsigned i=0; i<AM_ROWS; i++) {
        tracking->rows[i].request_max = 0;
        tracking->rows[i].failed_max = 0;
    }
    leave();
    return true;
}
#endif
