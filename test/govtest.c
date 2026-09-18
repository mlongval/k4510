/* The clock governor's rules (core/governor.h), without a window: windows are
 * fed in by hand, three seconds apart.  What must hold: it steps down as it
 * always did; it comes back up after thirty quiet seconds, one step at a time,
 * and never past the ceiling; a projected cost too near the edge keeps it
 * where it is; one gap spoils a quiet run; and a step that failed stays closed
 * for five minutes, then ten -- so an idle shell cannot vote the clock up
 * every thirty seconds for a program to vote it down again. */
#include <stdio.h>
#include "../core/governor.h"

static const unsigned L[] = { 10000000u, 15000000u, 20000000u, 30000000u, 40500000u, 60000000u, 81000000u };
#define NL (int)(sizeof L / sizeof L[0])
static gov_state g; static int at; static uint32_t now;
static int window(double ms, unsigned gaps, unsigned ceiling)
{
    int w = gov_decide(&g, ms, gaps, L[at], at + 1 < NL ? L[at + 1] : 0, at > 0 ? L[at - 1] : 0, ceiling, now);
    now += 3000; at += w;
    return w;
}
static int fails;
#define CHECK(c, what) do { if (!(c)) { printf("govtest: FAILED: %s (line %d; at %.1f MHz)\n", what, __LINE__, L[at] / 1e6); fails++; } } while (0)

int main(void)
{
    int i, ups;
    /* 1. the Dell, 2026-09-18: walked to the floor by four late windows, and no further */
    at = 4; now = 4000000000u;                       /* near the wrap of a 32-bit millisecond clock, on purpose */
    for (i = 0; i < 4; i++) CHECK(window(20.0, 0, 40500000u) == -1, "a late window did not step down");
    CHECK(at == 0 && window(20.0, 0, 40500000u) == 0 && at == 0, "it went below the bottom of the ladder");

    /* 2. quiet now, 2 ms a frame -- but it has just left 15 MHz, and that step and all above it are
     *    closed for five minutes.  Then thirty quiet seconds, and up; and thirty more for each step after. */
    { uint32_t t0 = now; int n = 0;
      while (at == 0 && n < 400) { window(2.0, 0, 40500000u); n++; }
      CHECK(at == 1, "never came up off the floor");
      CHECK(now - t0 == 294000u + 30000u, "off the floor, but not thirty seconds after the five minutes were up"); }   /* two windows of the five minutes had gone before t0 */
    for (i = 0; i < 9; i++) CHECK(window(2.0, 0, 40500000u) == 0, "stepped up again before thirty seconds");
    CHECK(window(2.0, 0, 40500000u) == 1 && at == 2, "did not step up after ten quiet windows");
    for (ups = 0, i = 0; i < 20; i++) ups += window(2.0, 0, 40500000u);
    CHECK(ups == 2 && at == 4, "two more steps in the next sixty seconds, to where it fell from");

    /* 3. the ceiling: quiet for an hour, and no higher */
    for (i = 0; i < 1200; i++) window(2.0, 0, 40500000u);
    CHECK(at == 4, "stepped above the ceiling");

    /* 4. a program that is too much for 40.5: down, closed five minutes, back up ... */
    CHECK(window(20.0, 0, 40500000u) == -1 && at == 3, "did not step down from the ceiling");
    for (i = 0; i < 90; i++) window(2.0, 0, 40500000u);       /* four and a half minutes */
    CHECK(at == 3, "went back into a step that had just failed");
    for (i = 0; i < 25; i++) window(2.0, 0, 40500000u);
    CHECK(at == 4, "the closed step never opened again");
    /* 5. ... and when the SAME step fails again, closed for ten */
    CHECK(window(20.0, 0, 40500000u) == -1 && at == 3, "did not step down the second time");
    for (i = 0; i < 150; i++) window(2.0, 0, 40500000u);      /* seven and a half minutes: over five, under ten */
    CHECK(at == 3, "the second failure was not closed for longer than the first");
    for (i = 0; i < 70; i++) window(2.0, 0, 40500000u);
    CHECK(at == 4, "and it never opened");

    /* 6. near the edge: 8 ms at 30 projects to 10.8 at 40.5 -- stay; 7 ms projects to 9.45 -- go */
    g = (gov_state){ 0, 0, 0, 0 }; at = 3;
    for (i = 0; i < 30; i++) window(8.0, 0, 81000000u);
    CHECK(at == 3, "stepped up into a frame it had projected as too dear");
    for (i = 0; i < 10; i++) window(7.0, 0, 81000000u);
    CHECK(at == 4, "did not step up with room to spare");

    /* 7. one gap in a quiet run starts the count again; three gaps is starving, whatever the frame time */
    g = (gov_state){ 0, 0, 0, 0 }; at = 2;
    for (i = 0; i < 9; i++) window(2.0, 0, 81000000u);
    window(2.0, 1, 81000000u);
    CHECK(at == 2, "a window with a gap in it counted as quiet");
    for (i = 0; i < 9; i++) window(2.0, 0, 81000000u);
    CHECK(at == 2, "the gap did not start the count again");
    CHECK(window(2.0, 0, 81000000u) == 1, "ten clean windows after the gap");
    CHECK(window(2.0, 3, 81000000u) == -1, "the sound starving did not step down");

    if (!fails) printf("govtest: OK (down as before; up after thirty quiet seconds, a step at a time, not past the ceiling, not into a frame too dear, not after a gap; a failed step closed five minutes, then ten; the millisecond clock wrapping)\n");
    return fails ? 1 : 0;
}
