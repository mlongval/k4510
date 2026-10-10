/* A tight integer loop: 200 x 1000 rounds of 16-bit arithmetic
 * (an add, a shift, an exclusive or, a mask), nothing in memory
 * but the counters.  What cc65's code costs per turn on each
 * CPU.  Same source for both machines: see bench.h. */
#include "bench.h"

void main(void)
{
    unsigned i, j, acc = 0; uint16_t t0, t;
    out("integer loop in c, 200000 turns, "); out(MACHINE); outc('\n');
    t0 = ticks();
    for (j = 0; j < 200; j++)
        for (i = 0; i < 1000; i++)
            acc += (i ^ (acc >> 3)) & 0xFF;
    t = ticks() - t0;
    out("check "); dec(acc); outc('\n');
    report("cloop", t);
    out("done\n");
    finish();
}
