/* The K4510's OPL2 -- a Yamaha YM3812 at $D480, nine FM voices.
 *
 * The design map has had "OPL2, DigiMAX" written against $D480 since the
 * machine was drawn; this is the OPL2 half arriving.  The chip is the AdLib's,
 * and it is wired the AdLib's way: an address port, a data port, and a status
 * register you poll.  Every AdLib instrument table and every OPL2 register
 * list ever written therefore means what it says on this machine.
 *
 *   $D480  W  ADDR    the register to write next
 *          R  STATUS  bit7 IRQ, bit6 timer 1 expired, bit5 timer 2 expired
 *   $D481  W  DATA    write it
 *          R          the last value written to the addressed register
 *   $D482  R  ID      $02 = an OPL2 is fitted, $00 = it is not
 *
 * It is the machine's only sound chip.  Four SIDs shared this page until
 * 2026-09-05 (muted since 2026-09-01) and were removed on Doc's instruction.
 *
 * MAME's fmopl.c does the synthesis, by way of VICE (core/opl2/, unaltered).
 * What it wants around it is an allocator and two alarms for its timers, and
 * that is what the top of this file is.
 */
#include "opl2.h"
#include "sndq.h"
#include "audio.h"
#include "vice_clk.h"
#include <string.h>
#include "opl2/fmopl.h"

/* ---- the two alarms fmopl.c sets its timers with ----------------------- *
 * VICE has a general alarm queue; the OPL2 uses exactly two of them, so this
 * is two slots and a poll rather than the machinery.  vice_clk_advance()
 * calls alarm_poll as the microsecond clock moves, which is what makes the
 * status register's overflow flags appear at the right time for a player
 * that waits on them. */
#define MAX_ALARMS 4
static alarm_t alarms[MAX_ALARMS];
static int nalarms;
alarm_context_t *maincpu_alarm_context;          /* VICE names one; we need none */

alarm_t *alarm_new(alarm_context_t *ctx, const char *name, alarm_callback_t cb, void *data)
{
    (void)ctx; (void)name;
    if (nalarms >= MAX_ALARMS) return 0;
    { alarm_t *a = &alarms[nalarms++]; a->cb = cb; a->data = data; a->set = 0; a->when = 0; return a; }
}
void alarm_destroy(alarm_t *a) { if (a) a->set = 0; }
void alarm_set(alarm_t *a, uint32_t when) { if (a) { a->when = when; a->set = 1; } }
void alarm_unset(alarm_t *a) { if (a) a->set = 0; }
void alarm_poll(uint32_t now)
{
    for (int i = 0; i < nalarms; i++) {
        alarm_t *a = &alarms[i];
        /* Unsigned wrap: due when it is not more than half the clock's range
         * in the future.  The clock is 32 bits of microseconds -- 71 minutes
         * -- and a machine left on for longer must not have its timers stop. */
        if (a->set && (uint32_t)(now - a->when) < 0x80000000u) {
            a->set = 0;
            a->cb((uint32_t)(now - a->when), a->data);   /* VICE passes the overshoot */
        }
    }
}

/* ---- the chip ---------------------------------------------------------- */
#define OPL2_HZ 3579545.0            /* the AdLib's crystal, which every register list assumes */
static FM_OPL *opl;
static int     opl_rate = 48000;
static uint8_t opl_addr;             /* the address port's latch */
static uint8_t opl_shadow[256];      /* what was last written where, so DATA reads back */
/* Asleep (2026-10-06): half a second of exact silence and no write since, and
 * the chip is not rendered -- its output would be zeros, and rendering them
 * was half a millisecond of every frame on the Dell, sound or none.  Any
 * write wakes it.  (Its timers are the alarms', not the render's: they run
 * on.  What does not advance while it sleeps is the LFO's phase, which no
 * silence can show.)  Both are the rendering side's: with the audio thread
 * rendering, a write wakes the chip there, when the queue delivers it. */
static unsigned opl_quiet;           /* samples of exact silence in a row */
static int      opl_asleep;
static int      opl_written;         /* a write since last asked (opl2_touched): the CPU's side, for
                                      * the frontend to know there may be something to hear */
#define OPL_WAKE() (opl_quiet = 0, opl_asleep = 0)

void opl2_init(int rate)
{
    opl_rate = rate;
    if (opl) { ym3812_shutdown(opl); opl = 0; }
    nalarms = 0;
    fmopl_set_machine_parameter(K4510_VICE_CLK_HZ);   /* the timers count microseconds */
    opl = ym3812_init((uint32_t)OPL2_HZ, (uint32_t)rate);
    memset(opl_shadow, 0, sizeof opl_shadow);
    opl_addr = 0; OPL_WAKE();
}
/* The chip itself, put back to power-on: on the rendering side, so with the
 * audio thread rendering it is queued as SNDQ_EV_RESET and performed in its
 * turn there (opl2_apply) -- a program's writes before the reset still land,
 * the ones after it start from a clean chip, and no thread touches the chip
 * while another renders from it.  The microsecond clock the timers run on
 * is put back with it: the two belong together (a timer set before the reset
 * must not stay due after). */
static void opl2_reset_chip(void)
{
    vice_clk_reset();
    OPL_WAKE();
    if (opl) ym3812_reset_chip(opl);
}
void opl2_reset(void)
{
    /* nalarms stays: the chip object survives a reset and still owns its two
     * alarms.  Zeroing it here left the timers dead after every power cycle
     * (review 2026-09-12, 4); opl2_init rebuilds both chip and alarms. */
    memset(opl_shadow, 0, sizeof opl_shadow);
    opl_addr = 0; opl_written = 1;
    if (sndq_owner() != SNDQ_OWNER_CPU) sndq_push(audio_due(), SNDQ_EV_RESET, 0);
    else opl2_reset_chip();
}

/* The write, once it is the rendering side's turn to perform it.  Port 0 and
 * port 1 writes are queued in the order they were made, so the chip's own
 * address latch is driven by the drained stream and stays in step. */
void opl2_apply(uint8_t reg, uint8_t v)
{
    if (!opl) opl2_init(opl_rate);
    if (!opl) return;
    if (reg == SNDQ_EV_RESET) { opl2_reset_chip(); return; }
    OPL_WAKE();
    if (reg < 2) ym3812_write(opl, reg, v);
}

void opl2_write(uint8_t reg, uint8_t v)
{
    /* The chip is part of the machine, so it answers whether or not the
     * frontend has got as far as opl2_init -- a bare harness (test/capture)
     * brings up memory and the CPU and nothing else, and a program that pokes
     * $D480 there should still find a chip. */
    if (!opl) opl2_init(opl_rate);
    if (!opl) return;
    if (reg > 1) return;
    /* The shadow and the address latch are this side's own bookkeeping: they
     * answer the readback at $D481 and must be right here, now, whoever is
     * doing the rendering. */
    if (reg == 0) opl_addr = v; else opl_shadow[opl_addr] = v;
    opl_written = 1;
    /* Another thread has the sound: hand the write over stamped, and it is
     * performed there as the render passes its moment (core/sndq.h).
     * Without this the chip's state was being mutated here while the other
     * thread rendered from it -- and the chip's sleep (opl_asleep) is that
     * side's too, so not even OPL_WAKE is done here.  A full queue drops the
     * write (counted; sndq.h says why writing through would be worse). */
    if (sndq_owner() != SNDQ_OWNER_CPU) { sndq_push(audio_due(), reg, v); return; }
    OPL_WAKE();
    ym3812_write(opl, reg, v);
}
int opl2_touched(void) { int t = opl_written; opl_written = 0; return t; }
/* One register, from the machine itself rather than a program: the sound
 * sequencer ($D5E0) plays its notes through here from the frame tick, which
 * can land between a program's ADDR write and its DATA write.  The latch is
 * put back afterwards so that pair still lands where the program meant. */
void opl2_write_reg(uint8_t reg, uint8_t v)
{
    uint8_t keep = opl_addr;
    opl2_write(0, reg); opl2_write(1, v);
    opl2_write(0, keep);
}
uint8_t opl2_read(uint8_t reg)
{
    switch (reg) {
    case 0: return opl ? ym3812_read(opl, 0) : 0x00;      /* STATUS */
    case 1: return opl_shadow[opl_addr];
    case 2: return 0x02;                                  /* an OPL2 is fitted: it is part of the machine */
    default: return 0xFF;
    }
}

/* n samples of FM, mixed down to the machine's one channel.  fmopl renders
 * into its own buffer and we scale: the OPL2 swings wide and would clip the
 * mix at full tilt. */
#define OPL2_BLOCK 1024
int opl2_render(int n, int16_t *out, int max)
{
    static OPLSAMPLE tmp[OPL2_BLOCK];
    int done = 0;
    if (n > max) n = max;
    if (n <= 0) return 0;
    if (!opl || opl_asleep) { for (int i = 0; i < n; i++) out[i] = 0; return n; }
    while (done < n) {
        int want = n - done, loud = 0;
        if (want > OPL2_BLOCK) want = OPL2_BLOCK;
        ym3812_update_one(opl, tmp, want);
        for (int i = 0; i < want; i++) {
            int v = tmp[i] / 2;                            /* headroom */
            loud |= tmp[i];
            out[done + i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
        opl_quiet = loud ? 0 : opl_quiet + (unsigned) want;
        done += want;
    }
    if (opl_quiet >= (unsigned) opl_rate / 2) opl_asleep = 1;   /* half a second of nothing: asleep until a write */
    return done;
}
