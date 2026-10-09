/* See sndq.h.  Single producer (the CPU's thread), single consumer (whichever
 * thread is rendering), no lock -- the ring's two indices are the only shared
 * mutable state and each is written by one side only. */
#include "sndq.h"

/* GCC's builtins rather than <stdatomic.h>: written that way for the
 * bare-metal Pi kernel, whose newlib had no <stdatomic.h>.  The Pi port is
 * gone (2026-09-07); the builtins are as good on Linux, so they stay. */
#define A_LOAD(p)        __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define A_STORE(p, v)    __atomic_store_n((p), (v), __ATOMIC_RELEASE)
#define A_ADD(p, v)      __atomic_fetch_add((p), (v), __ATOMIC_RELEASE)
#define A_LOAD_RLX(p)    __atomic_load_n((p), __ATOMIC_RELAXED)

/* ~340 ms of the busiest tune at a write a scanline (was 4096, 85 ms, when
 * the consumer was a core of its own that never slept; SDL's audio thread
 * can be held off that long by a host that is swapping, and the writes it
 * then finds are what keep the chip's registers right). */
#define QN 16384
typedef struct { uint32_t at; uint8_t port, val, pad[2]; } ev_t;
static ev_t q[QN];
static volatile unsigned q_w, q_r;
static volatile unsigned dropped;

static volatile uint32_t now_us;
static volatile int owner = SNDQ_OWNER_CPU;
static volatile int want  = SNDQ_OWNER_CPU;

void     sndq_tick(uint32_t us) { A_ADD(&now_us, us); }
uint32_t sndq_now(void)         { return A_LOAD(&now_us); }
int      sndq_owner(void)       { return A_LOAD(&owner); }
int      sndq_pending(void)     { return A_LOAD(&want) != A_LOAD(&owner); }
void     sndq_accept(void)      { A_STORE(&owner, A_LOAD(&want)); }
void     sndq_take(int o)       { A_STORE(&want, o); A_STORE(&owner, o); }
unsigned sndq_dropped(void)     { return A_LOAD(&dropped); }

void sndq_reset(void)
{
    A_STORE(&q_w, 0u); A_STORE(&q_r, 0u);
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

int sndq_push(uint32_t at, uint8_t port, uint8_t val)
{
    unsigned w = A_LOAD_RLX(&q_w);
    unsigned n = (w + 1) & (QN - 1);
    if (n == A_LOAD(&q_r)) { A_ADD(&dropped, 1u); return 0; }                /* full: dropped, see sndq.h */
    q[w].at = at; q[w].port = port; q[w].val = val;
    A_STORE(&q_w, n);
    return 1;
}

int sndq_next(uint32_t *at)
{
    unsigned r = A_LOAD_RLX(&q_r);
    if (r == A_LOAD(&q_w)) return 0;
    *at = q[r].at;
    return 1;
}

void sndq_drain(uint32_t at, void (*apply)(uint8_t port, uint8_t val))
{
    unsigned r = A_LOAD_RLX(&q_r);
    for (;;) {
        unsigned w = A_LOAD(&q_w);
        if (r == w) break;
        /* Unsigned wrap-safe "is it due yet": the moment is 32 bits that
         * roll over -- a day of samples at 48 kHz. */
        if ((uint32_t)(at - q[r].at) >= 0x80000000u) break;                   /* still in the future */
        apply(q[r].port, q[r].val);
        r = (r + 1) & (QN - 1);
        A_STORE(&q_r, r);
    }
}
