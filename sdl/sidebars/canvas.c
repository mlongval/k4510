/* sdl/sidebars/canvas.c -- the one table the scenes share: a sine
 * (Bhaskara's approximation, no libm), a full turn in 1024 steps. */
#include "canvas.h"

int sb_sintab[1024];                          /* 256 sin, a full turn in 1024 */
void sb_sin_init(void)
{
    static int done; if (done) return; done = 1;
    for (int a = 0; a < 1024; a++) {
        int hh = a % 512; long long p = (long long) hh * (512 - hh);
        int r = (int)(4096LL * p / (5LL * 512 * 512 - 4 * p));
        sb_sintab[a] = a < 512 ? r : -r;
    }
}
