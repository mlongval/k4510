/* K4510 sound: the OPL2 at $D480 (core/opl2.c), rendered at the device's
 * rate.  This is the seam the frontends and tests use; they never see the
 * chip.  Two ways through it, one owner at a time (core/sndq.h):
 *
 *   OWNER_CPU    the thread running the machine renders, a scanline at a
 *                time: audio_render(cycles) hands in CPU cycles and gets
 *                back samples.  The headless harness, the tests, a host
 *                with no sound, and the desktop while its device is closed.
 *   OWNER_OTHER  the host's audio thread renders: audio_pull(n) is called
 *                from its callback for exactly the samples the card wants,
 *                and the machine's register writes reach the chip through
 *                the stamped queue.  The desktop while its device is open
 *                (2026-10-09; sndq.h has the why).
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
void audio_drain_to(uint32_t us);     /* the rendering side: perform every queued write due by then (core/sndq.h) */
/* ---- OWNER_CPU --------------------------------------------------------- */
/* Advance the sound by `cycles` CPU cycles and write the samples that fall
 * in them to out[] (mono, 16-bit); returns how many (<= max).  Whatever the
 * clock, samples come out at the device's rate. */
int  audio_render(int cycles, int16_t *out, int max);
/* ---- OWNER_OTHER ------------------------------------------------------- */
/* Hand the sound to the audio thread (SNDQ_OWNER_OTHER) or take it back
 * (SNDQ_OWNER_CPU).  The machine's thread, where the other side is provably
 * stopped: see sndq_take.  Taking it back performs every queued write at
 * once, so the chip is exactly where the program left it. */
void audio_take(int owner);
/* The host's microsecond (32 bits, wrapping), lent by the frontend so the
 * mark below can place a frame between two callbacks; before audio_take. */
void audio_set_clock(uint32_t (*us)(void));
/* The machine's thread, at the start of every frame: the mark that ties
 * the machine's clock to the card's (core/audio.c says how). */
void audio_frame_mark(void);
uint32_t audio_due(void);            /* the card sample a write made now is due at: what the queue is stamped with */
/* The audio thread: n samples of the machine's sound, the queued writes
 * performed each at its own sample.  Returns 1 if any sample was not zero.
 * *behind, if given, is how many of the n played on past a write that was
 * already due -- the machine that far behind the card, its frames late;
 * the frontend sums it at $D52A. */
int  audio_pull(int16_t *out, int n, int *behind);
/* How far ahead of the renderer a write made now would land, in
 * microseconds: between zero and a callback on a machine keeping up,
 * negative on one behind (its writes land the moment they arrive).
 * Diagnostics (K4510_RINGLOG); the machine's thread. */
int32_t audio_lead_us(void);
#ifdef __cplusplus
}
#endif
#endif
