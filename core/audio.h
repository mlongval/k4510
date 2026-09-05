/* K4510 sound: the OPL2 at $D480 (core/opl2.c), rendered into the host's
 * ring at the device's rate.  This is the seam the frontends and tests use:
 * they hand in CPU cycles and get back samples, and never see the chip.
 *
 * Until 2026-09-05 four SIDs sat here too, muted since 2026-09-01 and then
 * removed on Doc's instruction ("nuke anything having to do with SIDs").
 * git has them; the machine is an OPL2 machine. */
#ifndef K4510_AUDIO_H
#define K4510_AUDIO_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void audio_init(double cpu_hz, int sample_rate);
void audio_reset(void);
void audio_set_cpu_hz(double hz);     /* the CPU clock changed; the sample rate did not */
void audio_drain_to(uint32_t us);     /* the rendering core: perform every queued write due by then (core/sndq.h) */
/* Advance the sound by `cycles` CPU cycles and write the samples that fall
 * in them to out[] (mono, 16-bit); returns how many (<= max).  Whatever the
 * clock, samples come out at the device's rate, so the ring is always fed. */
int  audio_render(int cycles, int16_t *out, int max);
#ifdef __cplusplus
}
#endif
#endif
