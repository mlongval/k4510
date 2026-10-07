/* See audio.h. */
#include "audio.h"
#include "opl2.h"
#include "vice_clk.h"
#include "sndq.h"

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
    opl2_reset(); vice_clk_reset(); sndq_reset();
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
