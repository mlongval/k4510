/* The sound decoupled from the frames (2026-10-09, core/audio.c audio_pull):
 * a pretend main loop runs the machine's frames at uneven intervals -- some
 * of them 30-50 ms late, as the Dell's are under KMSDRM -- queueing the
 * sequencer's register writes, while a pretend audio callback pulls 1024
 * samples every 21.3 ms of the card's time.  A held note must never break
 * however late the frames come; a note must sound within a callback and a
 * half of its write; two notes a known distance apart in the machine's time
 * must land that distance apart in samples; and the chip must survive being
 * handed back and forth (the device closing at idle) and a queued reset.
 *
 * Time here is the card's: a sample is the unit, 48,000 of them a second.
 * The machine's frame k starts at card time T_k and runs in a burst, as the
 * real one does (the CPU runs its 16.67 ms of machine time in about ten of
 * the host's, then waits for vblank). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/mem.h"
#include "../core/io.h"
#include "../core/audio.h"
#include "../core/sndq.h"

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)
#define RATE 48000
#define BLOCK 1024
#define LINES 480
#define LINE_US (1000000.0 / (60.0 * LINES))                /* 34.72 us of machine time a scanline */
#define FRAME_SAMPLES (RATE / 60.0)                          /* 800 */

/* ---- the card: every sample ever played, in order ---------------------- */
#define OUT_MAX (RATE * 24)
static int16_t out[OUT_MAX];
static long out_n;                                           /* card time now, in samples */
static long behind_total;
static double sim_now;                                       /* the host's clock, in the card's samples: where the pretend loop is */
static uint32_t sim_us(void) { return (uint32_t)(sim_now * 1000000.0 / RATE); }
static void callback(void)
{
    int behind = 0;
    if (out_n + BLOCK > OUT_MAX) { fprintf(stderr, "sndtest: out of room\n"); exit(2); }
    sim_now = (double) out_n;                                /* the card asks as the block before this one starts to play */
    audio_pull(out + out_n, BLOCK, &behind);
    behind_total += behind;
    out_n += BLOCK;
}

/* ---- the machine: a frame in a burst, writes at given scanlines --------- */
static double tick_acc;
static double burst_ms;                                      /* how long the host takes to run a frame: 0 = no time at all */
static int    chatter;                                       /* a program that writes the chip every frame, at scanline 400 (a harmless register) */
static long   next_cb_at;
static void   callback(void);
typedef struct { int line, ch, amp, pitch, dur; } ev_t;      /* a sequencer note at a scanline (ch < 0: none) */
static void note(int ch, int amp, int pitch, int dur)
{
    io_write(0xD5E0, (uint8_t)ch); io_write(0xD5E1, (uint8_t)amp); io_write(0xD5E2, (uint8_t)pitch); io_write(0xD5E3, (uint8_t)dur);
}
static void frame(const ev_t *ev, int nev)
{
    double start = sim_now;
    audio_frame_mark();                                      /* as the frontend's line_begin at scanline 0 */
    for (int y = 0; y < LINES; y++) {
        sim_now = start + burst_ms * RATE / 1000.0 * y / LINES;   /* the host's clock moves on as the frame runs... */
        while (next_cb_at <= sim_now) { double keep = sim_now; callback(); next_cb_at += BLOCK; sim_now = keep; }   /* ...and the card keeps asking */
        for (int i = 0; i < nev; i++) if (ev[i].line == y) note(ev[i].ch, ev[i].amp, ev[i].pitch, ev[i].dur);
        if (chatter && y == 400) { io_write(0xD480, 0x01); io_write(0xD481, 0x20); }
        tick_acc += LINE_US;
        { unsigned us = (unsigned) tick_acc; tick_acc -= us; sndq_tick(us); }
    }
    io_frame_tick();
}

/* ---- the loop: frames at T_k, callbacks every BLOCK, in card order ------ */
static unsigned rng = 0x2545F491u;
static unsigned rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static double next_frame_at;                                 /* card time the next frame starts */
static double deadline;                                      /* ...and when it was due: the real loop runs a frame it owes at once */
static double pace_ms = 1000.0 / 60;                         /* the host's frame: 16.67 on a 60 Hz desktop, 18.5 on the Dell */
static int    stall_ms, stall_every;                         /* a frame in stall_every takes up to stall_ms longer than it should */
/* Run the machine `frames` frames; the events fire in frame `at` of the run
 * (0-based), at their scanlines, and *t_at gets that frame's card time. */
static void run(int frames, int at, const ev_t *ev, int nev, long *t_at)
{
    for (int k = 0; k < frames; k++) {
        while (next_cb_at <= next_frame_at) { callback(); next_cb_at += BLOCK; }
        sim_now = next_frame_at;
        if (k == at) { frame(ev, nev); if (t_at) *t_at = (long) next_frame_at; }
        else frame(NULL, 0);
        double took = burst_ms;
        if (stall_every && rnd() % (unsigned) stall_every == 0) took = stall_ms / 2.0 + (double)(rnd() % (unsigned)(stall_ms * 500)) / 1000.0;
        deadline += pace_ms * RATE / 1000.0;
        if (next_frame_at > deadline + 4 * pace_ms * RATE / 1000.0) deadline = next_frame_at;   /* as the frontend: four frames behind and it stops owing */
        next_frame_at = next_frame_at + took * RATE / 1000.0;
        if (next_frame_at < deadline) next_frame_at = deadline;
    }
}
static void run_quiet(int frames) { run(frames, -1, NULL, 0, NULL); }

/* ---- what came out --------------------------------------------------- */
static long first_sound(long from)                           /* the first sample at or after `from` that is not zero */
{
    for (long i = from; i < out_n; i++) if (out[i]) return i;
    return -1;
}
static long longest_silence(long from, long to)              /* the longest run of exact zeros in [from, to) */
{
    long best = 0, run = 0;
    for (long i = from; i < to && i < out_n; i++) { if (out[i]) run = 0; else if (++run > best) best = run; }
    return best;
}
static double energy(long from, long to)
{
    double e = 0; long n = 0;
    for (long i = from; i < to && i < out_n; i++, n++) e += out[i] < 0 ? -out[i] : out[i];
    return n ? e / n : 0;
}
static void fresh(void)                                      /* the machine and the card from power-on, the audio thread owning */
{
    io_reset();
    audio_take(SNDQ_OWNER_CPU); audio_take(SNDQ_OWNER_OTHER);
    out_n = 0; behind_total = 0; tick_acc = 0; next_frame_at = 0; deadline = 0; next_cb_at = 0;
    pace_ms = 1000.0 / 60; stall_ms = 0; stall_every = 0; burst_ms = 0; chatter = 0;
    run_quiet(60);                                           /* a second of nothing */
}

int main(void)
{
    io_set_opts(SYSOPT_NOBOOT);
    if (mem_init()) return 1; io_reset(); audio_init(40500000.0, RATE); audio_set_clock(sim_us);
    static const ev_t on_ch1  = { 100, 1, -15, 100, 255 };   /* channel 1, held for ever, at scanline 100 */
    static const ev_t off_ch1 = { 300, 0x11, 0, 100, 255 }; /* amplitude 0 keys it off, at scanline 300 (bit 4: now, not queued behind the held note) */
    static const ev_t on_ch2  = { 300, 2, -15, 120, 255 };

    /* 1. a silent machine makes silence, and nothing was played past its clock */
    fresh();
    CHECK(first_sound(0) < 0, "silent machine made sound at %ld", first_sound(0));
    CHECK(behind_total == 0, "an idle machine at 60 fps was behind the card: %ld samples", behind_total);

    /* 2. latency: a note is rendered the margin after its write -- one
     *    callback -- give or take the OPL2's attack */
    { long t0 = 0, t1;
      run(10, 2, &on_ch1, 1, &t0);
      t1 = first_sound(t0);
      long ahead = t1 - (t0 + (long)(on_ch1.line * LINE_US * RATE / 1000000.0));
      CHECK(t1 >= t0, "a note sounded before its frame ran: %ld < %ld", t1, t0);
      CHECK(ahead >= BLOCK - 8 && ahead <= BLOCK + 64, "a note's latency: %ld samples (%.1f ms) after its write; wanted the margin, one callback", ahead, ahead * 1000.0 / RATE);
      printf("  latency: %.1f ms from the write to the first sample rendered\n", ahead * 1000.0 / RATE); }

    /* 3. continuity under late frames: the held note never breaks.  The
     *    Dell: 18.5 ms frames, and one in five stalls 25-50 ms on top */
    { long from = out_n;
      pace_ms = 18.5; stall_ms = 50; stall_every = 5;
      run(300, -1, NULL, 0, NULL);                           /* six seconds of it */
      long gap = longest_silence(from, out_n);
      CHECK(gap < 48, "the held note broke under late frames: %ld zero samples in a row (%.1f ms)", gap, gap * 1000.0 / RATE);
      CHECK(energy(from, out_n) > 500, "the held note faded under late frames: energy %.0f", energy(from, out_n));
      printf("  300 Dell frames (18.5 ms, one in five stalled up to 50 ms): longest silence %ld samples, %ld samples played past the machine's clock, lead now %.1f ms\n",
             gap, behind_total, audio_lead_us() / 1000.0);
      /* and a machine at 30 frames a second keeps its tone too */
      from = out_n; pace_ms = 33.3; stall_ms = 0; stall_every = 0;
      run(100, -1, NULL, 0, NULL);
      gap = longest_silence(from, out_n);
      CHECK(gap < 48, "the held note broke at 30 fps: %ld zero samples in a row", gap);
      pace_ms = 1000.0 / 60; run_quiet(120);
      CHECK(audio_lead_us() > -1000 && audio_lead_us() < 45000, "the lead is off once the frames are steady again: %.1f ms", audio_lead_us() / 1000.0);
      printf("  100 frames at 30 fps: longest silence %ld samples; lead after 120 steady frames %.1f ms\n", gap, audio_lead_us() / 1000.0);
      /* and a host that takes 60 ms to run each frame -- the writes late in
       * the frame are due before they are even made, and land as they come,
       * counted at $D52A; the tone holds */
      from = out_n; behind_total = 0; burst_ms = 60; pace_ms = 1000.0 / 60; chatter = 1;
      run(60, -1, NULL, 0, NULL);
      gap = longest_silence(from, out_n);
      CHECK(gap < 48, "the held note broke under 60 ms frames: %ld zero samples in a row", gap);
      CHECK(behind_total > 0, "60 ms frames and nothing counted as played past the machine");
      printf("  60 frames of 60 ms each, a write a frame: longest silence %ld samples, %ld samples played past the machine's writes\n", gap, behind_total);
      burst_ms = 0; chatter = 0; }

    /* 4. key-off lands too: silence within a callback and the lead, plus the release */
    { long t0 = 0;
      run(10, 1, &off_ch1, 1, &t0);
      long tail = t0 + (long)(off_ch1.line * LINE_US * RATE / 1000000.0) + BLOCK + 512 + RATE / 20;   /* the OPL2's fastest release is a few ms */
      CHECK(energy(tail, tail + 4000) < 2, "the key-off did not land: energy %.1f after it", energy(tail, tail + 4000)); }

    /* 5. placement: two notes a known distance apart in the machine's time
     *    are that distance apart in samples (frames of 60 Hz, no jitter) */
    { fresh();
      long a = 0, b = 0;
      run(10, 2, &on_ch1, 1, &a);  long on1 = first_sound(a);
      run(10, 1, &off_ch1, 1, NULL); run_quiet(10);
      run(20, 7, &on_ch2, 1, &b);  long on2 = first_sound(b);
      double want = ((7 + 10 + 10 + 8) * LINES + (on_ch2.line - on_ch1.line)) * LINE_US * RATE / 1000000.0;
      CHECK(on1 > 0 && on2 > 0 && labs((long)(on2 - on1 - want)) <= 4, "two notes %.1f samples apart in the machine's time came %ld apart on the card", want, on2 - on1);
      printf("  placement: %.1f samples wanted, %ld got\n", want, on2 - on1); }

    /* 5b. and on the Dell's 18.5 ms frames: the same two notes, 35 frames
     *     apart as the card measures them -- 18.5 ms each, not 16.67, since
     *     that is when the frames came -- plus their 200 scanlines in the
     *     machine's time */
    { fresh(); pace_ms = 18.5; run_quiet(30);
      long a = 0, b = 0;
      run(10, 2, &on_ch1, 1, &a);  long on1 = first_sound(a);
      run(10, 1, &off_ch1, 1, NULL); run_quiet(10);
      run(20, 7, &on_ch2, 1, &b);  long on2 = first_sound(b);
      double want = (7 + 10 + 10 + 8) * 18.5 * RATE / 1000.0 + (on_ch2.line - on_ch1.line) * LINE_US * RATE / 1000000.0;
      CHECK(on1 > 0 && on2 > 0 && labs((long)(on2 - on1 - want)) <= 4, "on 18.5 ms frames two notes due %.1f samples apart came %ld apart on the card", want, on2 - on1);
      CHECK(behind_total == 0, "on steady 18.5 ms frames the card ran past the machine: %ld samples", behind_total);
      printf("  placement on 18.5 ms frames: %.1f samples wanted, %ld got; lead %.1f ms\n", want, on2 - on1, audio_lead_us() / 1000.0); }

    /* 6. the hand-over: the device closing at idle takes the chip back with
     *    every queued write performed; opening hands it over whole */
    { fresh();
      long a = 0;
      run(3, 2, &on_ch1, 1, &a);                          /* keyed on in the last frame: its writes may still be queued */
      audio_take(SNDQ_OWNER_CPU);                            /* the device closed */
      { uint32_t us; CHECK(!sndq_next(&us), "writes left in the queue after the chip was taken back"); }
      long e = 0; for (int y = 0; y < LINES * 3; y++) { int16_t t[256]; int n = audio_render(40500000 / 60 / LINES, t, 256); for (int i = 0; i < n; i++) e += t[i] < 0 ? -t[i] : t[i]; }
      CHECK(e > 100000, "the machine's thread, given the chip back, did not find the note on it: energy %ld", e);
      audio_take(SNDQ_OWNER_OTHER);                          /* opened again */
      long from = out_n; run_quiet(10);
      CHECK(energy(from + BLOCK, out_n) > 500, "the audio thread, given the chip again, did not find the note on it: energy %.0f", energy(from + BLOCK, out_n)); }

    /* 7. a reset with the audio thread rendering is queued, and silences */
    { fresh();
      run(5, 1, &on_ch1, 1, NULL);
      io_reset();                                            /* audio_reset -> opl2_reset -> SNDQ_EV_RESET queued */
      long from = out_n; run_quiet(10);
      CHECK(energy(from + 2 * BLOCK, out_n) < 1, "a queued reset did not silence the chip: energy %.1f", energy(from + 2 * BLOCK, out_n));
      run(5, 1, &on_ch2, 1, NULL); from = out_n; run_quiet(10);
      CHECK(energy(from, out_n) > 500, "the chip did not sound again after a queued reset: energy %.0f", energy(from, out_n)); }

    /* 8. a queue nobody drains fills, drops, and recovers */
    { fresh();
      unsigned before = sndq_dropped();
      for (int i = 0; i < 20000; i++) io_write(0xD480, 0x20);   /* 20,000 writes, no callback between */
      CHECK(sndq_dropped() > before, "a full queue did not drop");
      run_quiet(5);                                          /* the callbacks come back and drain it */
      run(5, 1, &on_ch1, 1, NULL); long from = out_n; run_quiet(10);
      CHECK(energy(from, out_n) > 500, "no sound after the queue overflowed: energy %.0f", energy(from, out_n)); }

    audio_take(SNDQ_OWNER_CPU);
    printf("%s: the sound is the audio thread's -- late frames do not break a note, writes land at their sample, the chip survives the hand-over\n", fails ? "FAIL" : "OK");
    return fails ? 1 : 0;
}
