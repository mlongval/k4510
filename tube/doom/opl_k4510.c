/* opl_k4510.c -- DOOM's music on MELODY, the machine's own OPL2.
 *
 * Doc, 2026-09-17: "do fixes and add music please", having chosen music only:
 * DOOM's effects are 11 kHz samples and the machine has no DAC, but its music
 * was written for exactly the chip this computer has.  MELODY is a YM3812.
 * So nothing here synthesises anything.  Chocolate Doom's OPL music driver
 * decides which registers to write and when; this hands those writes to the
 * emulator, which performs them on the real emulated chip through
 * opl2_write_reg() -- the same door the machine's own sound sequencer uses.
 *
 * Two things make this driver unlike the others upstream ships:
 *
 * THE WRITES ARE ONE-WAY.  They go into a ring in the shared segment and the
 * emulator drains it from its scanline hook, ~262 times a frame.  So a read
 * can never be the answer to a write: by the time anything could be read back
 * the write may not have been performed yet.
 *
 * WHICH MEANS DETECTION IS THEATRE.  OPL_Detect() writes the AdLib timer
 * registers and expects the status byte to change in the way a real chip's
 * would -- 0x00 before timer 1 is started, 0xc0 once it has expired -- and
 * I_OPL_InitMusic refuses to start the music if it does not see that ("Dude.
 * The Adlib isn't responding.").  read_port_func below therefore answers from
 * a small state machine of its own rather than from the chip.  It is not
 * lying about whether an OPL2 is fitted -- one is, and the writes reach it --
 * only about the handshake, which cannot cross an asynchronous ring.  It also
 * has to be cheap: OPL_WriteRegister reads the port THIRTY times per register
 * written, for timing.
 *
 * The callbacks are the other half.  The music driver schedules itself with
 * OPL_SetCallback(us, fn, data) and expects to be called back at that time;
 * upstream's SDL backend runs the queue from the audio mixer callback.  This
 * has no audio device of its own, so it runs a thread.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#include "opl/opl.h"
#include "opl/opl_internal.h"
#include "opl/opl_queue.h"

/* doomgeneric_k4510.c owns the shared segment; this is the only thing the
 * music side needs from it. */
void k4510_opl_write(uint8_t reg, uint8_t val);

static opl_callback_queue_t *queue;
static pthread_t timer_thread;
static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t callback_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile int timer_running, timer_paused;
static uint64_t start_us;
static uint64_t paused_at;             /* under queue_mutex: when the pause began */

/* The register latch, as the chip has one: a write to port 0 says which
 * register, a write to port 1 gives its value, and only then is there
 * something to send. */
/* PER THREAD, because two threads write registers: the timer thread from its
 * callbacks, and DOOM's own from I_OPL_SetMusicVolume and I_OPL_PauseSong,
 * which upstream calls without OPL_Lock.  A register write is two port
 * writes, and with one shared latch the other thread's address could land
 * between them -- a volume sent to a frequency register.  Each thread keeping
 * its own latch makes every pair whole, with nothing in the vendored driver
 * touched.  (Review, 2026-09-17.) */
static __thread unsigned int latched_reg;

/* What read_port_func answers.  The detection sequence is:
 *   write TIMER_CTRL 0x60   (reset both timers)
 *   write TIMER_CTRL 0x80   (enable interrupts)   -> status must read & 0xe0 == 0x00
 *   write TIMER1    0xff
 *   write TIMER_CTRL 0x21   (start timer 1)
 *   ...wait...                                    -> status must read & 0xe0 == 0xc0
 * and finally port 0 non-zero says OPL2 rather than OPL3. */
static int timer1_started;

static uint64_t now_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t) t.tv_sec * 1000000u + (uint64_t) t.tv_nsec / 1000u;
}

/* The queue's time, which stands still while the music is paused.  Call with
 * queue_mutex held. */
static uint64_t queue_now(void)
{
    return (timer_paused ? paused_at : now_us()) - start_us;
}

static void *TimerThread(void *unused)
{
    (void) unused;
    while (timer_running) {
        opl_callback_t callback;
        void *data;
        int ran = 0;

        pthread_mutex_lock(&queue_mutex);
        if (timer_paused) pthread_mutex_unlock(&queue_mutex);
        else {
            while (!timer_paused && !OPL_Queue_IsEmpty(queue)
                   && OPL_Queue_Peek(queue) <= queue_now()) {
                if (!OPL_Queue_Pop(queue, &callback, &data)) break;
                pthread_mutex_unlock(&queue_mutex);
                /* the callback may schedule more, so the queue must be free */
                pthread_mutex_lock(&callback_mutex);
                callback(data);
                pthread_mutex_unlock(&callback_mutex);
                pthread_mutex_lock(&queue_mutex);
                ran = 1;
            }
            pthread_mutex_unlock(&queue_mutex);
        }
        if (!ran) {
            struct timespec t = { 0, 500000L };   /* half a millisecond */
            nanosleep(&t, NULL);
        }
    }
    return NULL;
}

static int K4510_Init(unsigned int port_base)
{
    (void) port_base;                  /* 0x388 on a PC; meaningless here */
    queue = OPL_Queue_Create();
    if (queue == NULL) return 0;
    start_us = now_us();
    timer1_started = 0;
    timer_running = 1;
    timer_paused = 0;
    if (pthread_create(&timer_thread, NULL, TimerThread, NULL) != 0) {
        OPL_Queue_Destroy(queue);
        queue = NULL;
        return 0;
    }
    return 1;
}

static void K4510_Shutdown(void)
{
    if (!timer_running) return;
    timer_running = 0;
    pthread_join(timer_thread, NULL);
    if (queue != NULL) { OPL_Queue_Destroy(queue); queue = NULL; }
}

static unsigned int K4510_ReadPort(opl_port_t port)
{
    if (port == OPL_REGISTER_PORT) {
        /* Before timer 1 runs, the status bits are clear; once it has been
         * started they read as "both timers expired", which is what the
         * detection wants to see.  A real chip would need the timer actually
         * to elapse; here the write itself is the event, because the write is
         * the only thing we can be sure happened. */
        return timer1_started ? 0xc0 : 0x00;
    }
    /* Port 2 is the OPL3 register port.  Returning non-zero from port 0 at the
     * end of detection is what says OPL2 rather than OPL3, and OPL2 is what
     * MELODY is. */
    return 0xff;
}

static void K4510_WritePort(opl_port_t port, unsigned int value)
{
    switch (port) {
    case OPL_REGISTER_PORT:
        latched_reg = value & 0xff;
        if (latched_reg == OPL_REG_TIMER_CTRL) { /* nothing yet: the value decides */ }
        break;
    case OPL_REGISTER_PORT_OPL3:
        latched_reg = value & 0xff;             /* no OPL3 here; keep the latch sane */
        break;
    case OPL_DATA_PORT:
        if (latched_reg == OPL_REG_TIMER_CTRL) {
            /* 0x21 starts timer 1; 0x60/0x80 reset and unmask it */
            if ((value & 0x01) != 0) timer1_started = 1;
            else if ((value & 0x60) == 0x60) timer1_started = 0;
        }
        k4510_opl_write((uint8_t) latched_reg, (uint8_t) value);
        break;
    }
}

static void K4510_SetCallback(uint64_t us, opl_callback_t callback, void *data)
{
    pthread_mutex_lock(&queue_mutex);
    OPL_Queue_Push(queue, callback, data, queue_now() + us);
    pthread_mutex_unlock(&queue_mutex);
}

static void K4510_ClearCallbacks(void)
{
    pthread_mutex_lock(&queue_mutex);
    OPL_Queue_Clear(queue);
    pthread_mutex_unlock(&queue_mutex);
}

/* OPL_Lock is held while a callback runs, so that the music driver can stop
 * callbacks happening under it.  The timer thread takes the same mutex. */
static void K4510_Lock(void)   { pthread_mutex_lock(&callback_mutex); }
static void K4510_Unlock(void) { pthread_mutex_unlock(&callback_mutex); }

/* The queue's clock stops while the music is paused.  Without this the time
 * spent paused still counted, so every track's next event was overdue at the
 * resume and they all fired at once; upstream's SDL driver keeps a
 * pause_offset for the same reason.  Moving start_us forward by the length of
 * the pause is that offset, applied once. */
static void K4510_SetPaused(int paused)
{
    pthread_mutex_lock(&queue_mutex);
    if (paused && !timer_paused) paused_at = now_us();
    else if (!paused && timer_paused) start_us += now_us() - paused_at;
    timer_paused = paused;
    pthread_mutex_unlock(&queue_mutex);
}

static void K4510_AdjustCallbacks(float value)
{
    pthread_mutex_lock(&queue_mutex);
    OPL_Queue_AdjustCallbacks(queue, queue_now(), value);
    pthread_mutex_unlock(&queue_mutex);
}

opl_driver_t opl_k4510_driver =
{
    "K4510",
    K4510_Init,
    K4510_Shutdown,
    K4510_ReadPort,
    K4510_WritePort,
    K4510_SetCallback,
    K4510_ClearCallbacks,
    K4510_Lock,
    K4510_Unlock,
    K4510_SetPaused,
    K4510_AdjustCallbacks,
};
