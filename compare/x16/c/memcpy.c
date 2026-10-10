/* Memory copy: 256 memcpy()s of 4 KB (one megabyte moved by the
 * C library's routine, the same generic code in both targets'
 * libraries), then 64 copies of the same 4 KB by a plain byte
 * loop (256 KB).  The K4510 has DMA that would do this in a
 * blink; this measures the CPU, like for like.  See bench.h. */
#include "bench.h"
#include <string.h>
static char src[4096], dst[4096];

void main(void)
{
    unsigned i, n; uint16_t t0, t1, t2;
    out("memory copy in c, "); out(MACHINE); outc('\n');
    for (i = 0; i < 4096; i++) src[i] = (char) i;
    t0 = ticks();
    for (n = 0; n < 256; n++) memcpy(dst, src, 4096);
    t1 = ticks() - t0;
    t0 = ticks();
    for (n = 0; n < 64; n++)
        for (i = 0; i < 4096; i++) dst[i] = src[i];
    t2 = ticks() - t0;
    out("check "); dec((uint8_t) dst[4095]); outc('\n');
    report("cmemcpy", t1);
    report("cbytecopy", t2);
    out("done\n");
    finish();
}
