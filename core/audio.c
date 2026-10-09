/* See audio.h. */
#include "audio.h"
#include "opl2.h"
#include "vice_clk.h"
#include "sndq.h"

#define A_LOAD(p)        __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define A_STORE(p, v)    __atomic_store_n((p), (v), __ATOMIC_RELEASE)

static double cpu_hz = 40500000.0;
static int    rate = 48000;
static double out_acc;                           /* fractional samples owed */
static double clk_frac;                          /* the microsecond clock's fraction; see clk_advance_us */

void audio_init(double hz, int sample_rate)
{
    cpu_hz = hz; rate = sample_rate;
    opl2_init(rate);
}
void audio_set_cpu_hz(double hz) { cpu_hz = hz; }
void audio_reset(void)
{
    out_acc = 0; clk_frac = 0;
    opl2_reset();                                /* the chip and its clock: here, or queued for the audio thread */
    if (sndq_owner() == SNDQ_OWNER_CPU) sndq_reset();
}
void audio_drain_to(uint32_t us) { sndq_drain(us, opl2_apply); }

/* The machine's microsecond clock (core/vice_clk.h) moves with the sound.
 * Fractions are kept, or the OPL2's timers would run slow by however much
 * is thrown away each call. */
static void clk_advance_us(double us)
{
    clk_frac += us;
    uint32_t whole = (uint32_t)clk_frac;
    if (whole) { clk_frac -= whole; vice_clk_advance(whole); }
}

int audio_render(int cycles, int16_t *out, int max)
{
    out_acc += (double)cycles * rate / cpu_hz;
    int want = (int)out_acc; out_acc -= want;
    if (want <= 0) return 0;
    if (want > max) { out_acc += want - max; want = max; }
    clk_advance_us((double)want * K4510_VICE_CLK_HZ / rate);
    return opl2_render(want, out, max);
}

/* ---- the audio thread's side ------------------------------------------ *
 * Two clocks, and a mark that ties them.  The machine has its own audio
 * microsecond (sndq_now: a scanline's worth per scanline, so a frame is
 * 16.67 ms of it however long the host took to run that frame); the card
 * has its sample counter, `played`, the samples rendered so far.  The two
 * have no fixed relation -- a machine at 54 frames a second makes 16.67 ms
 * of its time per 18.5 ms of the card's; a frame that stalls 50 ms (the
 * Dell's KMSDRM, 2026-10-09) makes none for 50 ms and then a frame in a
 * burst; the frontend's loop then runs the frames it owes back to back --
 * so no rate converts one into the other.  What is true is this: WITHIN a
 * frame the machine's time is the truth (a program that writes the chip at
 * scanline 100 and again at 300 means 6.9 ms between them), and BETWEEN
 * frames the card's is (the frames came when they came).
 *
 * So at the start of every frame the machine's thread takes a MARK: its
 * clock now, and the card's position now (audio_frame_mark).  A write made
 * at machine time S in that frame is due at the card's
 *
 *     P_mark + LEAD + (S - M_mark) * rate
 *
 * -- that frame's writes land at their own sample, the frame's distance
 * from the one before is whatever the card measured, and nothing is ever
 * reordered: the queue is drained in the order the writes were made, so a
 * frame run late lands late and bunched, after the frame before it, the
 * tone under it never breaking because the chip is clocked by the card
 * regardless.  LEAD is a fixed margin, one callback: the frame takes real
 * time to run (14 ms of a 16.67 on the Dell at 40 MHz) while the renderer
 * goes on, and the margin keeps the frame's early writes ahead of it.
 *
 * "The card's position now" is not the counter, which moves a callback at
 * a time -- marked off that, frames would sit 21 ms apart or not at all.
 * The callback notes the counter and the host's microsecond as it starts
 * (one 64-bit store, never torn), and the mark carries the counter on from
 * there by the host's clock: the card's time between callbacks, drawn
 * straight.  The frontend lends the clock (audio_set_clock); without one
 * the counter alone serves, which a harness can live with.
 *
 * Nothing in here can make a gap; a gap now means the card starved, and
 * the frontend counts those ($D524). */
static uint32_t (*host_us)(void);                /* the host's microsecond, wrapping; the frontend's */
static volatile uint64_t cb_mark;                /* the callback's note: the counter when it started << 32 | the host's microsecond then */
static volatile uint32_t played;                 /* the card's counter: samples rendered so far (the audio thread writes it) */
static uint32_t mark_us, mark_at;                /* the mark: the machine's clock, and the card's position, at the frame's start (the machine's thread's) */
#define LEAD_SAMPLES 1024                        /* the margin: one callback, 21 ms */

void audio_set_clock(uint32_t (*us)(void)) { host_us = us; }
static uint32_t card_now(void)                   /* the card's position now, in samples (the machine's thread) */
{
    uint64_t m = A_LOAD(&cb_mark);
    uint32_t at = (uint32_t)(m >> 32), since;
    if (!host_us) return A_LOAD(&played);
    since = (uint32_t)(host_us() - (uint32_t) m);
    if (since > 3 * LEAD_SAMPLES * (uint32_t)(K4510_VICE_CLK_HZ / rate)) since = 3 * LEAD_SAMPLES * (uint32_t)(K4510_VICE_CLK_HZ / rate);   /* the callbacks have stopped (the device opening, a host asleep): not for ever */
    return at + (uint32_t)((double) since * rate / K4510_VICE_CLK_HZ);
}
static uint32_t due_now(void)                    /* the card sample a write made now is due at (the machine's thread) */
{
    return mark_at + LEAD_SAMPLES + (uint32_t)((double)(uint32_t)(sndq_now() - mark_us) * rate / K4510_VICE_CLK_HZ);
}
void audio_frame_mark(void)
{
    mark_us = sndq_now(); mark_at = card_now();
}
void audio_take(int o)
{
    if (o != SNDQ_OWNER_CPU) {
        audio_frame_mark();
        sndq_take(o);
    } else {
        uint32_t d;
        sndq_take(o);
        while (sndq_next(&d)) sndq_drain(d, opl2_apply);   /* all of them, whenever they were due: the chip is where the program left it */
    }
}
uint32_t audio_due(void)     { return due_now(); }
int32_t  audio_lead_us(void) { return (int32_t)(due_now() - A_LOAD(&played)) * (int32_t)(K4510_VICE_CLK_HZ / rate); }

int audio_pull(int16_t *out, int n, int *behind)
{
    uint32_t head = A_LOAD(&played), next;
    int done = 0, loud = 0;
    if (host_us) A_STORE(&cb_mark, (uint64_t) head << 32 | host_us());
    /* the samples this block plays on past a write that was due already:
     * how far the oldest waiting write is overdue, the machine that far
     * behind the card.  The frontend sums it at $D52A. */
    if (behind) { int32_t over = sndq_next(&next) ? (int32_t)(head - next) : 0; *behind = over <= 0 ? 0 : over >= n ? n : over; }
    while (done < n) {
        int run = n - done;
        sndq_drain(head, opl2_apply);
        if (sndq_next(&next)) {                                 /* the next write is in the future: render up to its sample */
            int32_t to = (int32_t)(next - head);
            if (to < run) run = to < 1 ? 1 : to;
        }
        opl2_render(run, out + done, run);
        head += (uint32_t) run;
        clk_advance_us((double)run * K4510_VICE_CLK_HZ / rate); /* the timers keep the card's time */
        done += run;
    }
    A_STORE(&played, head);
    for (int i = 0; i < n; i++) if (out[i]) { loud = 1; break; }
    return loud;
}
