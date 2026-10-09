/* jimfuzz -- random escape sequences into JIM, both screens, under ASan and
 * UBSan (`make jimfuzz`; not part of `make test`, it runs for a while).
 *   test/asan/jimfuzz [seconds] [seed]
 * Each input runs under a 5 s alarm: a hang writes hang.bin and fails, a
 * sanitizer report writes crash.bin.  From the JIM review's harness of
 * 2026-10-09, which found the Kitty placement overflow and the number parse
 * overflow (core/jimgfx.c) in its first minute. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include "../core/xemu/emutools_basicdefs.h"
#include "../core/xemu/cpu65.h"
#include "../core/mem.h"
#include "../core/io.h"
#include "../core/term.h"
#define W(r, v) io_write(IO_TERM + (r), (uint8_t)(v))
void __sanitizer_set_death_callback(void (*)(void));
static uint8_t in[4096]; static size_t inn; static unsigned long iter;
static void dump(const char *name) { FILE *f = fopen(name, "wb"); if (f) { fwrite(in, 1, inn, f); fclose(f); } }
static void on_death(void) { dump("crash.bin"); }
static void on_alarm(int s) { (void)s; dump("hang.bin"); fprintf(stderr, "jimfuzz: HANG at input %lu (hang.bin)\n", iter); _exit(3); }
static const char *tok[] = { "\033", "\033[", "\033]", "\033_G", "\033\\", "\033P", "\033(", "\033)", "\033#", "\033%", "\033%G", "\033%@",
  ";", ":", "?", ">", "<", "=", "!", " ", "$", "\007", "\030", "\032", "\r", "\n", "\t", "\b", "\016", "\017",
  "\xE2\x94", "\x80", "\xF0\x9F\x98", "\xC3", "4510;", "note;", "kos", "term", "a=T,", "a=t,", "a=p,", "a=q,", "a=d,", "f=24,", "f=32,", "f=100,",
  "s=", "v=", "i=", "c=", "r=", "x=", "y=", "w=", "h=", "m=1,", "m=0,", "o=z,", "C=1,", "AAAA", "/w==", "iVBORw0KGgo=",
  "m", "H", "J", "K", "r", "h", "l", "@", "P", "X", "L", "M", "S", "T", "Z", "g", "n", "c", "t", "q", "u", "s", "d", "G", "A", "B", "C", "D", "E", "F", "p", "b", "I" };
static void gen(void)
{
    inn = 0; int n = 1 + rand() % 200;
    for (int i = 0; i < n && inn < sizeof in - 16; i++) {
        int k = rand() % 10;
        if (k < 6) { const char *t = tok[rand() % (sizeof tok / sizeof *tok)]; size_t l = strlen(t); memcpy(in + inn, t, l); inn += l; }
        else if (k < 8) { int d = rand() % 4 == 0 ? rand() % 100000 : rand() % 300; inn += (size_t)sprintf((char *)in + inn, "%d", d); }
        else in[inn++] = (uint8_t)rand();
    }
}
int main(int argc, char **argv)
{
    int secs = argc > 1 ? atoi(argv[1]) : 120; unsigned seed = argc > 2 ? (unsigned)atoi(argv[2]) : (unsigned)time(NULL);
    srand(seed); signal(SIGALRM, on_alarm); __sanitizer_set_death_callback(on_death);
    mem_init(); io_reset(); W(5, 80); W(6, 24); W(0x0D, 80); W(0x0E, 1); term2_open();
    time_t end = time(NULL) + secs;
    for (iter = 0; time(NULL) < end; iter++) {
        gen(); alarm(5);
        int which = rand() % 3;
        if (which == 2) term2_feed(in, inn);
        else for (size_t i = 0; i < inn; i++) W(0, in[i]);
        if (rand() % 50 == 0) { int r = 5 + rand() % 9; W(r, rand() % 256); }       /* geometry and colour registers */
        if (rand() % 50 == 0) W(5, 1 + rand() % 120);
        if (rand() % 200 == 0) W(4, 1 + rand() % 2);
        if (rand() % 20 == 0) { uint8_t o[256]; term2_replies(o, sizeof o); while (io_read(IO_TERM + 1) & 0x80) io_read(IO_TERM + 2); }
        if (rand() % 100 == 0) term_tick();
        alarm(0);
    }
    printf("fuzz: %lu inputs, seed %u, no crash\n", iter, seed);
    return 0;
}
