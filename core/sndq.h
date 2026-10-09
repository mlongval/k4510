/* Handing the sound to another thread.
 *
 * This began on the Pi: the emulator had core 1 and the Tube had core 3,
 * asleep on a `wfe` until the ROM ran BBC or CPM -- which on most sessions
 * was never -- and the OPL2's synthesis, a measurable slice of core 1's
 * 16.7 ms frame, was free over there.  Doc's: "move the sound to core 3
 * when the tube is not active".  The Pi port is gone (2026-09-07); the idea
 * is not, because the desktop turned out to have the same shape of problem
 * (2026-10-09): the sound was made in lockstep with the video frames, and
 * on the Dell the frames came late and uneven, 18.5 ms apiece, so the host's
 * audio callback -- 21 ms, on SDL's own thread -- found the ring dry a few
 * times a second and the notes crackled.  Now the OPL2 is rendered ON that
 * thread, pulled at the device's rate, and the emulator's thread only queues
 * its register writes.  A late frame makes its notes a little late; the tone
 * under them never stops, because the chip is clocked by the card, not by
 * the frame.
 *
 * What makes it awkward is that a register write happens on the CPU's
 * thread, at the instant of the store, and the render then happens somewhere
 * else.  So the writes are QUEUED, each with the moment it is due, and the
 * render applies them as it passes that moment.  The moment is a sample of
 * the card's -- core/audio.c works it out from the machine's own audio
 * microsecond, advanced once per scanline by the thread running the CPU
 * (34.7 us, exactly the granularity the writes had when the render was per
 * scanline, so nothing is lost by moving) and a mark it takes each frame.
 *
 * Ownership never overlaps.  One side owns the chip, and the handover
 * happens where the other side is provably still: SDL's audio device is
 * closed or not yet unpaused (sndq_take), or -- the Pi's way -- at a
 * rendezvous the asking side waits on (sndq_request / sndq_accept).  There
 * is no lock on the audio path itself, because there are never two owners.
 *
 * With no audio device (the headless harness, the tests, a host without
 * sound) the owner is OWNER_CPU and the emulator's thread renders, as it
 * always has: audio_render per scanline, straight through to the chip.
 */
#ifndef K4510_SNDQ_H
#define K4510_SNDQ_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* ---- the machine's audio clock ---------------------------------------- */
void     sndq_tick(uint32_t us);      /* the CPU's thread, once a scanline */
uint32_t sndq_now(void);

/* ---- ownership -------------------------------------------------------- */
#define SNDQ_OWNER_CPU   0            /* the thread running the emulator renders, as it always has */
#define SNDQ_OWNER_OTHER 1            /* another thread does (SDL's audio thread); writes are queued for it */
int  sndq_owner(void);
/* Set the owner outright.  Only where the other side is provably stopped:
 * the audio device closed, or opened and not yet unpaused -- SDL joins its
 * thread on close, so after SDL_CloseAudioDevice returns nobody else can be
 * in the chip.  The frontend goes through audio_take(), which also does the
 * bookkeeping either side needs (core/audio.h). */
void sndq_take(int owner);
/* The Pi's way: ask the other core to take the sound, or to give it back,
 * and WAIT for it to say it has.  Called from the CPU's core at a quiescent
 * point only.  Returns 1 if the handover happened, 0 if it timed out (and
 * nothing moved).  Nothing on the desktop calls it; kept for a core that
 * cannot be joined. */
int  sndq_request(int owner);
int  sndq_pending(void);              /* the other core: has a handover been asked for? */
void sndq_accept(void);               /* ...take it, or let it go */

/* ---- the queue -------------------------------------------------------- */
/* An OPL2 port write (port 0 = ADDR, 1 = DATA) due at `at` -- a moment on
 * the consumer's clock, 32 bits that wrap (core/audio.c: a sample of the
 * card's); or SNDQ_EV_RESET, the chip and its clock put back to power-on,
 * in its place in the stream so the writes before it still land and the
 * ones after it start clean.  Returns 0 if the queue is full -- the
 * rendering thread has stopped consuming for a third of a second at a
 * write a scanline -- and the write is DROPPED and counted (sndq_dropped):
 * writing through would have two threads in the chip, and a thread that
 * far behind is not making sound anybody can hear in time anyway. */
#define SNDQ_EV_RESET 2
int  sndq_push(uint32_t at, uint8_t port, uint8_t val);
unsigned sndq_dropped(void);
/* Apply every queued write due by `at`, in the order they were made -- an
 * older write that is due later holds the ones behind it, so an ADDR/DATA
 * pair can never swap.  The rendering thread calls this before each run of
 * samples it renders. */
void sndq_drain(uint32_t at, void (*apply)(uint8_t port, uint8_t val));
/* When the oldest write still queued is due: 1 and *at, or 0 if none.  The
 * renderer uses it to stop a block at the write's own sample. */
int  sndq_next(uint32_t *at);
/* Empty the queue.  The owner's side only, and only when it is OWNER_CPU
 * (a reset with the audio thread rendering is queued instead, see
 * opl2_reset); the machine's clock is left alone -- it is a clock, and
 * nobody depends on its zero. */
void sndq_reset(void);

#ifdef __cplusplus
}
#endif
#endif
