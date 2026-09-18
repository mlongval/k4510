/* The clock governor's decision, apart from the frontend so it can be tested
 * without a window (test/govtest.c).  The frontend measures a window -- three
 * seconds of the machine's own half of the frame, and the audio gaps in it --
 * and asks here what to do with the clock: down a step, up a step, or nothing.
 *
 * Down is the old rule: over GOV_LATE_MS of the 16.67, or the sound starving.
 *
 * Up is Doc's, 2026-09-18, after a Dell that measures 40.5 MHz was found at 10:
 * the governor had walked it down the whole ladder in its first 83 seconds and
 * had no way back, so one bad minute was a slow machine for ever.  ("go ahead
 * and make the governor step back up".)  What keeps up and down from chasing
 * each other:
 *
 *   - up only after GOV_UP_WINDOWS quiet windows in a row, with no gap in any;
 *   - quiet means the cost PROJECTED at the next step is under GOV_UP_MS, and
 *     the projection scales the whole frame by the clock ratio, fixed work and
 *     all -- wrong in the safe direction.  10 against the 14 that sends it
 *     down is the hysteresis;
 *   - never above the ceiling: SETUP's measurement where this host has one,
 *     the compiled-in default where it has not.  The governor recovers what
 *     was lost; it does not go looking for more.  That is SETUP's business;
 *   - a step it has just had to leave is closed for GOV_BACKOFF_MS, doubling
 *     each time that same step fails again, to GOV_BACKOFF_MAX_MS.  An idle
 *     shell is cheap at any clock and would otherwise vote the clock up every
 *     thirty seconds for a program to vote it down again.  The cost of being
 *     wrong is one late window per backoff, and it gets rarer.
 */
#ifndef K4510_GOVERNOR_H
#define K4510_GOVERNOR_H
#include <stdint.h>

#define GOV_LATE_MS        14.0
#define GOV_UP_MS          10.0
#define GOV_UP_WINDOWS     10            /* thirty seconds */
#define GOV_BACKOFF_MS     (5u * 60u * 1000u)
#define GOV_BACKOFF_MAX_MS (80u * 60u * 1000u)

typedef struct {
    unsigned quiet;                      /* quiet windows in a row */
    unsigned bad_hz;                     /* the step last stepped down FROM; 0 = none */
    uint32_t bad_until, backoff;         /* closed until then (ms); how long it was closed for */
} gov_state;

/* One finished window.  ms: the machine's share of a frame; gaps: audio gaps
 * in the window; cur/up/down: this clock and its neighbours on the ladder in
 * Hz (0 = there is none); ceiling: see above; now: a millisecond clock.
 * Returns -1 step down, +1 step up, 0 stay. */
static inline int gov_decide(gov_state *g, double ms, unsigned gaps, unsigned cur, unsigned up, unsigned down,
                             unsigned ceiling, uint32_t now)
{
    if (ms > GOV_LATE_MS || gaps >= 3) {
        g->quiet = 0;
        if (!down) return 0;
        g->backoff = (g->bad_hz == cur && g->backoff) ? (g->backoff * 2 > GOV_BACKOFF_MAX_MS ? GOV_BACKOFF_MAX_MS : g->backoff * 2)
                                                      : GOV_BACKOFF_MS;
        g->bad_hz = cur; g->bad_until = now + g->backoff;
        return -1;
    }
    if (!up || up > ceiling || gaps || ms * (double)up / (double)cur >= GOV_UP_MS) { g->quiet = 0; return 0; }
    if (g->bad_hz && up >= g->bad_hz && (int32_t)(now - g->bad_until) < 0) { g->quiet = 0; return 0; }   /* closed; the difference, signed, so the clock may wrap */
    if (++g->quiet < GOV_UP_WINDOWS) return 0;
    g->quiet = 0;
    return 1;
}
/* the clock changed under us (the menu, SETUP): what was learnt about the old one is not about this one */
static inline void gov_restart(gov_state *g) { g->quiet = 0; }
#endif
