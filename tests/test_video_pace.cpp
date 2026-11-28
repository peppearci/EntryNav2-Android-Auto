// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// The [Video] line's totals: bytes, gaps between frames, slowest hand-off, lost frames, lateness, reset on take and
// restart.
#include <stdio.h>
#include <stdlib.h>

#include "../platform/video_pace.hpp"

#define CHECK(x)                                                \
    do {                                                        \
        if (!(x)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
            exit(1);                                            \
        }                                                       \
    } while (0)

int main() {
    evo::VideoPace pace;
    pace.frame(1000, 2048, 3, false, 0);  // first frame: no gap yet
    pace.frame(1040, 1024, 25, true, 0);
    pace.frame(1300, 512, 1, false, 0);
    evo::VideoPaceTotals t = pace.take();
    CHECK(t.frames == 3 && t.kbytes == 4 && t.gapMaxMs == 260 && t.feedMaxMs == 25 && t.lost == 1);
    t = pace.take();
    CHECK(t.frames == 0 && t.kbytes == 0 && t.gapMaxMs == 0 && t.feedMaxMs == 0 && t.lost == 0);
    pace.frame(5000, 100, 0, false, 0);  // a gap across a take, within one session
    CHECK(pace.take().gapMaxMs == 3700);
    pace.frame(6000, 100, 7, true, 0);
    pace.restart();  // a new session: nothing left over, and its first frame is not a gap
    pace.frame(90000, 100, 0, false, 0);
    t = pace.take();
    CHECK(t.frames == 1 && t.gapMaxMs == 0 && t.feedMaxMs == 0 && t.lost == 0);
    // Lateness: the quickest frame of the session is 0, the others against it; 32-bit wrap of the car's clock.
    evo::VideoPace late;
    late.frame(1000, 1, 0, false, 500000);    // offset 500: the base
    late.frame(1033, 1, 0, false, 533000);    // on time
    late.frame(5100, 1, 100, false, 566000);  // arrived at 5000: 3934 ms late, the hand-off not counted
    t = late.take();
    CHECK(t.lateMinMs == 0 && t.lateMaxMs == 3934);
    late.frame(5200, 1, 0, false, 4900000);  // quicker than all before: the new base
    late.frame(5300, 1, 0, false, 4900000);
    t = late.take();
    CHECK(t.lateMinMs == 0 && t.lateMaxMs == 100);
    CHECK(late.take().lateMinMs == 0);  // no frame: 0, not the sentinel
    late.restart();
    late.frame(0xfffffff0u, 1, 0, false, 1000000);
    late.frame(0x00000010u, 1, 0, false, 1000000);  // the clock wrapped: 32 ms later
    t = late.take();
    CHECK(t.lateMinMs == 0 && t.lateMaxMs == 32);
    printf("PASS video pace: bytes, gaps, slowest hand-off, lost frames, lateness, reset on take and restart\n");
    return 0;
}
