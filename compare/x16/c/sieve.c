/* The Byte Sieve (Gilbreath, BYTE Sept 1981) in C, 10 iterations
 * of 8191 flags, as in rprouse/8bit-benchmarks ByteSieve.c and
 * the K4510's own /LANG/C/SIEVE.C.  Same source for both
 * machines: see bench.h. */
#include "bench.h"
#define SIZE 8190
static char flags[SIZE + 1];

void main(void)
{
    unsigned i, prime, k, count = 0, iter; uint16_t t0, t;
    out("byte sieve in c, 10 iterations, "); out(MACHINE); outc('\n');
    t0 = ticks();
    for (iter = 1; iter <= 10; iter++) {
        count = 0;
        for (i = 0; i <= SIZE; i++) flags[i] = 1;
        for (i = 0; i <= SIZE; i++) {
            if (flags[i]) {
                prime = i + i + 3;
                k = i + prime;
                while (k <= SIZE) { flags[k] = 0; k += prime; }
                count++;
            }
        }
    }
    t = ticks() - t0;
    dec(count); out(" primes\n");
    report("csieve", t);
    out("done\n");
    finish();
}
