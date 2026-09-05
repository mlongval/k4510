/* See sndq.h.  Single producer (the CPU's core), single consumer (whichever
 * core is rendering), no lock -- the ring's two indices are the only shared
 * mutable state and each is written by one side only. */
#include "sndq.h"

/* GCC's builtins rather than <stdatomic.h>, for the same reason core/tube_cp.c
 * gives: the Pi kernel compiles freestanding against Circle's newlib, which
 * has no <stdatomic.h>, and the desktop and the Pi share this file.  (Written
 * with stdatomic first, and the Pi build said so at once.) */
#define A_LOAD(p)        __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define A_STORE(p, v)    __atomic_store_n((p), (v), __ATOMIC_RELEASE)
#define A_ADD(p, v)      __atomic_fetch_add((p), (v), __ATOMIC_RELEASE)
#define A_LOAD_RLX(p)    __atomic_load_n((p), __ATOMIC_RELAXED)

#define QN 4096                                  /* ~85 ms of the busiest tune at a write a scanline */
typedef struct { uint32_t us; uint8_t port, val, pad[2]; } ev_t;
static ev_t q[QN];
static volatile unsigned q_w, q_r;

static volatile uint32_t now_us;
static volatile int owner = SNDQ_OWNER_CPU;
static volatile int want  = SNDQ_OWNER_CPU;

void     sndq_tick(uint32_t us) { A_ADD(&now_us, us); }
uint32_t sndq_now(void)         { return A_LOAD(&now_us); }
int      sndq_owner(void)       { return A_LOAD(&owner); }
int      sndq_pending(void)     { return A_LOAD(&want) != A_LOAD(&owner); }
void     sndq_accept(void)      { A_STORE(&owner, A_LOAD(&want)); }

void sndq_reset(void)
{
    A_STORE(&q_w, 0u); A_STORE(&q_r, 0u);
    A_STORE(&now_us, 0u);
}

/* The rendezvous.  The asking side spins -- this is called at a point where
 * the machine is already stopped (a menu close, or the ROM starting the Tube),
 * so a spin of a few milliseconds costs nothing and a lock would cost more.
 * The bound matters: if the other core has died or was never started, the
 * machine must not hang, it must keep the sound itself. */
int sndq_request(int o)
{
    A_STORE(&want, o);
    if (A_LOAD(&owner) == o) return 1;
    for (long i = 0; i < 20000000L; i++) {                    /* tens of milliseconds: a pump loop is far shorter */
        if (A_LOAD(&owner) == o) return 1;
#if defined(__aarch64__)
        __asm__ volatile("yield" ::: "memory");
#endif
    }
    A_STORE(&want, A_LOAD(&owner));
    return 0;                                                  /* nobody answered: nothing moved */
}

int sndq_push(uint8_t port, uint8_t val)
{
    unsigned w = A_LOAD_RLX(&q_w);
    unsigned n = (w + 1) & (QN - 1);
    if (n == A_LOAD(&q_r)) return 0;                                         /* full */
    q[w].us = sndq_now(); q[w].port = port; q[w].val = val;
    A_STORE(&q_w, n);
    return 1;
}

void sndq_drain(uint32_t us, void (*apply)(uint8_t port, uint8_t val))
{
    unsigned r = A_LOAD_RLX(&q_r);
    for (;;) {
        unsigned w = A_LOAD(&q_w);
        if (r == w) break;
        /* Unsigned wrap-safe "is it due yet": the stamp is 32 bits of
         * microseconds and rolls over every 71 minutes. */
        if ((uint32_t)(us - q[r].us) >= 0x80000000u) break;                   /* still in the future */
        apply(q[r].port, q[r].val);
        r = (r + 1) & (QN - 1);
        A_STORE(&q_r, r);
    }
}
