/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../../source/game/session_metrics.h"
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
    session_metrics_enable(false);
    session_metrics_tick(at(0), at(4));
    assert(!fopen("metrics00/tick.csv", "rb"));
    session_metrics_enable(true);
    int time = 0;
    for(int i = 0; i <= 100; i++) {
        session_metrics_frame(at(time));
        session_metrics_tick(at(i * 60), at(i * 60 + i));
        time += i + 1;
    }
    assert(contains("metrics00/frame.csv", "frame_start_ms,100,95,99,100"));
    assert(contains("metrics00/tick.csv", "tick_work_ms,100,94,98,99,49"));
    assert(contains("metrics00/tick.csv", "tick_period_ms,100,60,60,60,100"));
    puts("PASS disabled metrics, 100-sample quantiles and separate tick work/cadence.");
    return 0;
}
