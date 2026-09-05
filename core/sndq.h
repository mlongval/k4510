/* Handing the sound to another core.
 *
 * On the Pi the emulator has core 1 and the Tube has core 3, and core 3 is
 * asleep on a `wfe` from power-on until the ROM runs BBC or CPM -- which on
 * most sessions is never.  The OPL2's synthesis is a measurable slice of
 * core 1's 16.7 ms frame; moved across, it is free.  That is the whole idea,
 * and Doc's: "move the sound to core 3 when the tube is not active".
 *
 * What makes it awkward is that a register write happens on the CPU's core,
 * at the instant of the store, and the render then happens somewhere else.
 * So the writes are QUEUED with a timestamp and the render applies them as
 * it passes their moment.  The timestamp is the machine's audio microsecond,
 * advanced once per scanline by the core running the CPU -- 34.7 us, which
 * is exactly the granularity the writes already had when the render was per
 * scanline, so nothing is lost by moving.
 *
 * Ownership never overlaps.  One side owns the chip and the ring, and the
 * handover is a rendezvous at a point where the machine is already still:
 * a menu close, or the ROM starting the Tube (which the CPU's core asks for
 * and waits on before the co-processor is allowed to run).  There is no lock
 * on the audio path itself, because there are never two owners.
 *
 * On the desktop this is all compiled to nothing: OWNER_CPU, always.
 */
#ifndef K4510_SNDQ_H
#define K4510_SNDQ_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* ---- the audio clock the writes are stamped with ---------------------- */
void     sndq_tick(uint32_t us);      /* the CPU's core, once a scanline */
uint32_t sndq_now(void);

/* ---- ownership -------------------------------------------------------- */
#define SNDQ_OWNER_CPU   0            /* the core running the emulator renders, as it always has */
#define SNDQ_OWNER_OTHER 1            /* another core does; writes are queued for it */
int  sndq_owner(void);
/* Ask for the other core to take the sound, or to give it back, and WAIT for
 * it to say it has.  Called from the CPU's core at a quiescent point only.
 * Returns 1 if the handover happened, 0 if it timed out (and nothing moved). */
int  sndq_request(int owner);
/* The other core: has a handover been asked for?  Take it, or let it go. */
int  sndq_pending(void);
void sndq_accept(void);

/* ---- the queue -------------------------------------------------------- */
/* An OPL2 port write (port 0 = ADDR, 1 = DATA), stamped.  Returns 0 if the
 * queue is full, which can only happen if the rendering core has stopped --
 * the caller then writes through, so sound is wrong rather than absent. */
int  sndq_push(uint8_t port, uint8_t val);
/* Apply every queued write up to and including `us`.  The rendering core
 * calls this before each block it renders. */
void sndq_drain(uint32_t us, void (*apply)(uint8_t port, uint8_t val));
void sndq_reset(void);

#ifdef __cplusplus
}
#endif
#endif
