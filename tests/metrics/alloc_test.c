/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
static void *next_resize;
static void *mock_realloc(void *old, size_t bytes) { (void)old; (void)bytes; return next_resize; }
#define FCX_ALLOC_TEST
#define realloc mock_realloc
#include "../../source/game/alloc_metrics.c"
#undef realloc

#ifdef _WIN32
static DWORD WINAPI exercise_worker(void *unused) {
    (void)unused;
    for(unsigned i=0; i<2000; i++) {
        void *p = alloc_payload(AM_NETWORK, 1u + i%64u);
        assert(p);
        alloc_free(p);
    }
    return 0;
}

static void concurrent_allocations(void) {
    HANDLE workers[4];
    for(unsigned i=0; i<4; i++) {
        workers[i] = CreateThread(NULL, 0, exercise_worker, NULL, 0, NULL);
        assert(workers[i]);
    }
    struct allocation_sample s[AM_ROWS];
    for(unsigned i=0; i<100; i++) {
        assert(alloc_metrics_snapshot(s));
        for(unsigned b=0; b<AM_BANKS; b++) assert(s[AM_ALL].live[b] <= s[AM_ALL].peak[b]);
    }
    assert(WaitForMultipleObjects(4, workers, TRUE, 10000) == WAIT_OBJECT_0);
    for(unsigned i=0; i<4; i++) CloseHandle(workers[i]);
    assert(alloc_metrics_snapshot(s));
    for(unsigned b=0; b<AM_BANKS; b++) assert(s[AM_ALL].live[b] == 0);
}
#endif
int main(void) {
    struct allocation_sample s[AM_ROWS];
    assert(!alloc_metrics_snapshot(s));
    assert(alloc_metrics_begin());
    assert(alloc_bank(0x817fffffu) == AM_MEM1 && alloc_bank(0xc0000040u) == AM_MEM1);
    assert(alloc_bank(0x90000040u) == AM_MEM2 && alloc_bank(0xd3ffffffu) == AM_MEM2);
    assert(alloc_bank(0x94000000u) == AM_OTHER && alloc_bank(0x10000040u) == AM_OTHER);
    void *p = (void *)(uintptr_t)0x80001000u;
    alloc_adopt(AM_MESH, p, 64);
    next_resize = NULL;
    assert(!alloc_resize(AM_MESH, &p, 100));
    assert((uintptr_t)p == 0x80001000u);
    assert(alloc_metrics_snapshot(s) && s[AM_MESH].live[AM_MEM1] == 64);
    assert(s[AM_MESH].failed == 1 && s[AM_MESH].failed_max == 100 && s[AM_MESH].request_max == 100);
    next_resize = (void *)(uintptr_t)0xd0100010u;
    assert(alloc_resize(AM_MESH, &p, 120) && p == next_resize);
    assert(alloc_metrics_snapshot(s));
    assert(s[AM_MESH].live[AM_MEM1] == 0 && s[AM_MESH].peak[AM_MEM1] == 64);
    assert(s[AM_ALL].live[AM_MEM2] == 120 && s[AM_MESH].peak[AM_MEM2] == 120);
    assert(s[AM_MESH].failed == 1 && s[AM_MESH].failed_max == 0 && s[AM_MESH].request_max == 120);
    retire((uintptr_t)p);
    const unsigned offered = 2 * ALLOC_BUCKETS * ALLOC_WAYS;
    for(unsigned i=0;i<offered;i++)
        alloc_adopt(AM_CHUNK, (void *)(uintptr_t)(0x81001000u+i*32u), 4);
    assert(alloc_metrics_snapshot(s));
    assert(s[AM_CHUNK].dropped > 0);
    assert(s[AM_CHUNK].live[AM_MEM1]/4 + s[AM_CHUNK].dropped == offered);
    for(unsigned i=0;i<offered;i++) retire(0x81001000u+i*32u);
    assert(alloc_metrics_snapshot(s) && !s[AM_ALL].live[AM_MEM1] && !s[AM_ALL].live[AM_MEM2]);
    void *other = alloc_zero(AM_NETWORK, 48);
    assert(other && ((unsigned char*)other)[47] == 0);
    alloc_free(other);
    assert(alloc_metrics_snapshot(s) && s[AM_ALL].request_max == 48);
    assert(s[AM_ALL].live[AM_OTHER] == 0);
#ifdef _WIN32
    concurrent_allocations();
#endif
    puts("PASS allocation bank attribution, failed resize, cross-bank peaks, window reset, dropped records release and concurrent snapshots");
}
