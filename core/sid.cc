#include "resid/sid.h"
extern "C" {
#include "sid.h"
#include "opl2.h"
#include "vice_clk.h"
#include "sidq.h"
}
#include <string.h>

static reSID::SID chips[K4510_SIDS];
static double cpu_hz = 40500000.0; static int rate = 48000;
static bool active[K4510_SIDS];                  /* written since reset: clocked and mixed */
static uint8_t shadow[K4510_SIDS][32];           /* every register as last written */
static int  muted;                               /* the OPL2 has the sound (they are mutually exclusive) */
static double out_acc = 0;                       /* the muted path counts SAMPLES, not cycles */
static double clk_frac = 0;                      /* the microsecond clock's fraction; see clk_advance_us */
static short carry[K4510_SIDS][8]; static int ncarry[K4510_SIDS];   /* the phase surplus; see sid_render */
static int  sid_max = K4510_SIDS;                /* the menu caps how many chips are clocked: on the Pi four sounding
                                                    chips are ~3.4 ms of a 16.7 ms frame, so dropping to one buys it back */
extern "C" void sid_set_max(int n) { sid_max = n < 1 ? 1 : n > K4510_SIDS ? K4510_SIDS : n; }
/* The SIDs run at SID_HZ like the real chip, whatever the CPU clock: the
 * frequency/ADSR registers keep their C64 meaning (f = reg * SID_HZ / 2^24)
 * and reSID does 1/40th of the work. sid_render receives CPU cycles. */
static double SID_HZ = 1000000.0;
static int sid_rate_saved = 48000;
static double sid_acc = 0;

extern "C" void sid_set_clock(int sel)          /* 0 = 1 MHz, 1 = PAL C64 (985248), 2 = NTSC (1022730) */
{
    SID_HZ = sel == 1 ? 985248.0 : sel == 2 ? 1022730.0 : 1000000.0;
    for (int i = 0; i < 4; i++) chips[i].set_sampling_parameters(SID_HZ, reSID::SAMPLE_FAST, sid_rate_saved);
}

extern "C" void sid_init(double hz, int sample_rate)
{
    sid_rate_saved = sample_rate;
    cpu_hz = hz; rate = sample_rate;
    for (int i = 0; i < K4510_SIDS; i++) {
        chips[i].set_chip_model(reSID::MOS6581);
        chips[i].set_sampling_parameters(SID_HZ, reSID::SAMPLE_FAST, rate);
        chips[i].reset();
    }
    opl2_init(rate);
}
extern "C" void sid_set_cpu_hz(double hz) { cpu_hz = hz; }   /* the CPU clock changed: same SID clock, different ratio */
extern "C" void sid_reset(void)
{
    sid_acc = 0; out_acc = 0;
    memset(shadow, 0, sizeof shadow);
    for (int i = 0; i < K4510_SIDS; i++) { chips[i].reset(); active[i] = false; ncarry[i] = 0; }
    opl2_reset(); vice_clk_reset(); clk_frac = 0; sidq_reset();
}
/* The write, once it is the rendering side's turn to perform it. */
static void sid_apply(int c, uint8_t r, uint8_t v)
{
    if (c == K4510_SIDS) { opl2_apply(r, v); return; }   /* the OPL2 rides the same queue */
    active[c] = true;
    chips[c].write(r & 0x1F, v);
}
extern "C" void sid_write(int c, uint8_t r, uint8_t v)
{
    if (c < 0 || c >= K4510_SIDS) return;
    shadow[c][r & 0x1F] = v;
    /* Another core has the sound: the write is queued with the moment it
     * happened and performed there.  If the queue is full that core has
     * stopped, so it is written through instead -- wrong sound beats none. */
    if (sidq_owner() != SIDQ_OWNER_CPU && sidq_push((uint8_t)c, (uint8_t)(r & 0x1F), v)) return;
    sid_apply(c, (uint8_t)(r & 0x1F), v);
}
/* The rendering core, before each block: perform everything due by now. */
extern "C" void sid_drain_to(uint32_t us) { sidq_drain(us, sid_apply); }
extern "C" uint8_t sid_read(int c, uint8_t r)
{
    if (c < 0 || c >= K4510_SIDS) return 0xFF;
    return chips[c].read(r & 0x1F);
}
extern "C" void sid_set_model(int c, int m8580)
{
    if (c < 0 || c >= K4510_SIDS) return;
    chips[c].set_chip_model(m8580 ? reSID::MOS8580 : reSID::MOS6581);
}
extern "C" void sid_set_mute(int m) { muted = m ? 1 : 0; }
/* Samples a chip produced that its neighbours have not caught up with yet.
 * A call advances every clocked chip by the same number of SID cycles, but
 * each chip carries its OWN resampling phase, and the phases do not agree:
 * a chip is only clocked once the machine has written to it, so one that
 * starts sounding later than another starts from a different point inside
 * the sample period and stays there.  Ask four chips for 34 cycles and three
 * hand back two samples while the fourth hands back one -- 4.8% of calls,
 * measured on SID12.
 *
 * That difference used to be thrown away: the mix ran to the LONGEST chip's
 * count and read the short chip's buffer past what it had written, which is
 * whatever that chip produced on an earlier call.  One stale sample per
 * lagging chip on 4.8% of calls, or about 2,400 corrupted samples a second
 * at 48 kHz, mixed into the sound of every program that sounds more than one
 * chip.  SID12 sounds four, so it was the loudest sufferer.
 *
 * The fix is to mix only as far as the SHORTEST chip and keep the rest: the
 * surplus goes here and is prepended to that chip's next call.  All four run
 * at one average rate, so a carry never holds more than a sample or two. */
/* The machine's microsecond clock (core/vice_clk.h) moves with the sound, so
 * it moves at the same rate whichever chip is rendering and whether the
 * SIDs are the ones being heard.  Fractions are kept, or the OPL2's timers
 * would run slow by however much is thrown away each call. */
static void clk_advance_us(double us)
{
    clk_frac += us;
    uint32_t whole = (uint32_t)clk_frac;
    if (whole) { clk_frac -= whole; vice_clk_advance(whole); }
}
static void clk_advance_cycles(int sid_cycles) { clk_advance_us((double)sid_cycles * K4510_VICE_CLK_HZ / SID_HZ); }
static void clk_advance_samples(int n)         { clk_advance_us((double)n * K4510_VICE_CLK_HZ / rate); }

extern "C" int sid_render(int cycles, int16_t *out, int max)
{
    static short tmp[K4510_SIDS][4096];
    int n = 0, nact = 0, got[K4510_SIDS];
    /* Muted means the SIDs are not the ones sounding -- which since
     * 2026-09-01 is the machine's normal state, the OPL2 being the default.
     * This path counts OUTPUT SAMPLES rather than SID cycles, on its own
     * accumulator: none of the phase reconciliation below applies, because
     * whatever renders here hands back exactly the count it was asked for.
     * If nothing at all is selected the ring is still fed silence at the
     * device's rate, or the frontend would top it up for ever. */
    if (muted) {
        out_acc += (double)cycles * rate / cpu_hz;
        int want = (int)out_acc; out_acc -= want;
        if (want <= 0) return 0;
        if (want > max) { out_acc += want - max; want = max; }
        clk_advance_samples(want);
        if (opl2_enabled()) return opl2_render(want, out, max);
        for (int i = 0; i < want; i++) out[i] = 0;
        return want;
    }
    sid_acc += (double)cycles * SID_HZ / cpu_hz;
    int sid_cycles = (int)sid_acc; sid_acc -= sid_cycles;
    if (sid_cycles <= 0) return 0;
    /* Never ask for more cycles than `max` samples can hold.  reSID's clock()
     * fills the buffer and leaves the rest of the cycles in its delta_t, which
     * this discards -- so an over-long call used to lose sound silently.  The
     * frontend asks for two samples at a time and never came near it; the
     * test suite did.  The surplus goes back on the accumulator, so nothing
     * is lost and nothing is invented. */
    { int cap = (int)((double)(max < 4096 ? max : 4096) * SID_HZ / rate);
      if (cap < 1) cap = 1;
      if (sid_cycles > cap) { sid_acc += sid_cycles - cap; sid_cycles = cap; } }
    clk_advance_cycles(sid_cycles);
    /* Only chips the machine has written since reset are clocked. reSID costs
     * the same for silence as for sound, and at the prompt -- or in SIDPLAY,
     * which uses one chip -- three of the four are silent: on a Pi 3B+ that
     * was 2.5 ms of every 16.7 ms frame. The mix and its headroom are
     * unchanged, so a tune sounds exactly as it did. */
    for (int c = 0; c < K4510_SIDS; c++) {
        if (c >= sid_max || !active[c]) { got[c] = 0; continue; }
        int cap = (max < 4096 ? max : 4096) - ncarry[c];
        reSID::cycle_count dt = sid_cycles;
        for (int i = 0; i < ncarry[c]; i++) tmp[c][i] = carry[c][i];
        got[c] = ncarry[c] + (cap > 0 ? chips[c].clock(dt, tmp[c] + ncarry[c], cap) : 0);
        if (!nact++ || got[c] < n) n = got[c];             /* the shortest chip sets the pace */
    }
    if (!nact) {                                  /* nothing active: silence at the rate the chips would have given */
        n = (int)((double)sid_cycles * rate / SID_HZ); if (n > max) n = max;
        for (int i = 0; i < n; i++) out[i] = 0;
        return n;
    }
    for (int i = 0; i < n; i++) {
        int v = 0;
        for (int c = 0; c < K4510_SIDS; c++) if (c < sid_max && active[c]) v += tmp[c][i];
        v /= 2;                                   /* 4 chips: headroom */
        out[i] = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
    }
    for (int c = 0; c < K4510_SIDS; c++) {        /* what the faster chips ran ahead by, kept for next time */
        int left = got[c] - n;
        if (left > 8) left = 8;                   /* cannot happen while the chips share one clock; not a reason to smash the array */
        for (int i = 0; i < left; i++) carry[c][i] = tmp[c][n + i];
        ncarry[c] = left > 0 ? left : 0;
    }
    return n;
}
