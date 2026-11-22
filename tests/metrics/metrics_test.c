/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../../source/game/session_metrics.h"
#include "../../source/network/lan_pacing.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static ptime_t at(int ms) {
    return (ptime_t){ms / 1000, (ms % 1000) * 1000000L};
}
int32_t time_diff_ms(ptime_t start, ptime_t end) {
    return (int32_t)((end.tv_sec - start.tv_sec) * 1000 +
                     (end.tv_nsec - start.tv_nsec) / 1000000);
}
static int contains(const char *path, const char *text) {
    FILE *f = fopen(path, "rb");
    assert(f);
    char line[256];
    int found = 0;
    while(fgets(line, sizeof(line), f)) found |= strstr(line, text) != NULL;
    fclose(f);
    return found;
}
int main(void) {
    assert(lan_tick_wait_ms(0) == 50 && lan_tick_wait_ms(8) == 42);
    assert(lan_tick_wait_ms(49) == 1 && lan_tick_wait_ms(50) == 1 && lan_tick_wait_ms(600) == 1);
    session_metrics_enable(false);
    session_metrics_tick(at(0), at(4), TICK_IDLE);
    assert(!fopen("metrics00/tick.csv", "rb"));
    session_metrics_enable(true);
    int time = 0;
    for(int i = 0; i <= 100; i++) {
        session_metrics_frame(at(time));
        session_metrics_tick(at(i * 60), at(i * 60 + i), i < 3 ? TICK_LOADING : TICK_PAIRED);
        time += i + 1;
    }
    assert(contains("metrics00/frame.csv", "frame_start_ms,100,95,99,100"));
    assert(contains("metrics00/tick.csv", "tick_work_ms,100,94,98,99,49,50,1,10"));
    assert(contains("metrics00/tick.csv", "tick_period_ms,100,60,60,60,100"));
    puts("PASS disabled metrics, 100-sample quantiles and separate tick work/cadence.");
    return 0;
}
