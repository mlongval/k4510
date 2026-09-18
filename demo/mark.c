/* K4510: MARK -- one benchmark, for setting this machine beside others.
 *
 *   MARK            every clock from 10 to 60 MHz, the MATH unit off and on
 *   MARK 40         one clock only: the step nearest 40 MHz
 *
 * Doc, 2026-09-18: "one program, not a bunch of little ones"; "must test the
 * k4510 at different MHz values between 10 and 60, and also redo those same
 * tests ... with and without using the MATH unit."  What it grew from: the 6502.org
 * benchmarking thread (viewtopic.php?t=6323) and Gordon Henderson's BASIC
 * Mandelbrot behind it, gfoot's mandelbrot6502 in assembly, and the two CPU
 * test suites (Klaus Dormann's, Tom Seddon's) -- which are not benchmarks but
 * are the reason for the first line of the report: a speed measured on a CPU
 * that adds wrongly is not a speed.
 *
 * Once, before anything is timed:
 *   BCD     40 000 decimal-mode ADC and SBC against the same sums in binary
 *   PICTURE the Mandelbrot both ways must sum to what gfoot's arithmetic gives
 *           on paper (tools: a Python model, 2026-09-18): 16897
 * Then at every clock:
 *   6502    a loop of exactly known NMOS cycles: "the work of a 6502 at N MHz"
 *   SIEVE   the Byte Sieve of 1981, in assembly: 8191 flags, ten passes, 1899
 *   COPY    2 MB through (zp),Y, a byte at a time
 *   MANDEL  gfoot's: 134 x 80, 34 iterations, 8.8 fixed point, multiplies by
 *           shift and add                                  -- the MATH unit off
 *   +MATH   the same, its multiplies by the unit at $D770   -- the MATH unit on
 * (The first three do no arithmetic the unit could take over: they are the
 * same number either way and are run once a clock.  MANDEL is the pair.)
 *
 * Every figure is MACHINE time: the frame counter at SYS+$0D, sixty to the
 * second, which advances with the emulated CPU's cycles and nothing else.  It
 * is what a K4510 at that clock IS, on any host, on any day.  The last column
 * is the other thing -- the host's wall clock (SYS+$36) over the same work.
 * 100% is a host keeping up; 250% is a host that took two and a half seconds
 * to deliver each second of the machine, and everything the user saw was that
 * much slower than the figures beside it.  Both, because one without the other
 * is how a Dell that benches at 40.5 came to be sitting at 10.
 *
 * The clock is asked for as SETUP asks (SYS+$23), read back as what it became
 * (SYS+0/1/$26), the governor held off meanwhile (SYS+$29), and put back.
 * The report is on the screen and in /SYSTEM/LOG/MARK.TXT.
 *
 * More later: a section is a row in run_clock() and a column in the table. */
#include "k4510.h"

void __fastcall__ rom_chrout(unsigned char c);
unsigned char rom_getin(void);
static unsigned char rom_args(void) { return ((unsigned char (*)(void))0xFF95)(); }

void __fastcall__ mk_spin(unsigned char n);
unsigned mk_bcd(void);
unsigned mk_sieve(void);
void mk_mandel(void);
void __fastcall__ mk_copy(unsigned char times);
extern unsigned char mk_img[], mk_flags[], mk_tobcd[];
extern unsigned char mk_math;
#pragma zpsym("mk_math")

#define FS 0xD300u
#define PICTURE_SUM 16897u
#define SPIN_N      50
#define SPIN_CYCLES (328711UL * SPIN_N - 1UL + 20UL)   /* mk_spin(50) in NMOS 6502 cycles, the call and return with it */

static char log_[3072]; static unsigned logn;
static void out(char c) { rom_chrout((uint8_t)c); if (logn < sizeof log_ - 1) log_[logn++] = c; }
static void say(const char *s) { while (*s) out(*s++); }
static void num(unsigned long v, uint8_t width)              /* right-justified */
{
    char b[11]; uint8_t k = 0;
    do { b[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (width > k) { out(' '); width--; }
    while (k) out(b[--k]);
}
static void fix2(unsigned long hundredths, uint8_t width)     /* 1234 -> "12.34" */
{
    num(hundredths / 100, (uint8_t)(width > 3 ? width - 3 : 1)); out('.');
    out((char)('0' + (hundredths / 10) % 10)); out((char)('0' + hundredths % 10));
}

/* the two clocks.  Each is several bytes read one at a time while it runs: read until two readings agree */
static unsigned long frames(void)
{
    unsigned long a, b;
    do { a = (unsigned long)REG(SYS + 0x0D) | ((unsigned long)REG(SYS + 0x0E) << 8) | ((unsigned long)REG(SYS + 0x0F) << 16);
         b = (unsigned long)REG(SYS + 0x0D) | ((unsigned long)REG(SYS + 0x0E) << 8) | ((unsigned long)REG(SYS + 0x0F) << 16); } while (a != b);
    return a;
}
static unsigned long wall_ms(void)
{
    unsigned long a, b;
    do { a = (unsigned long)REG(SYS + 0x36) | ((unsigned long)REG(SYS + 0x37) << 8) | ((unsigned long)REG(SYS + 0x38) << 16) | ((unsigned long)REG(SYS + 0x39) << 24);
         b = (unsigned long)REG(SYS + 0x36) | ((unsigned long)REG(SYS + 0x37) << 8) | ((unsigned long)REG(SYS + 0x38) << 16) | ((unsigned long)REG(SYS + 0x39) << 24); } while ((a ^ b) & 0xFFFFFF00UL);
    return b;
}
static unsigned long khz(void) { return (unsigned long)REG(SYS) | ((unsigned long)REG(SYS + 1) << 8) | ((unsigned long)REG(SYS + 0x26) << 16); }
static void next_frame(void) { uint8_t f = REG(SYS + 0x0D); while (REG(SYS + 0x0D) == f) ; }

/* a stopwatch over both clocks; the totals are what the last column is made of */
static unsigned long f0, w0, tot_f, tot_w;
static void start(void) { next_frame(); f0 = frames(); w0 = wall_ms(); }
static unsigned long stop(void)                               /* -> machine time in hundredths of a second */
{
    unsigned long f = (frames() - f0) & 0xFFFFFFUL, w = wall_ms() - w0;
    tot_f += f; tot_w += w;
    return f * 5 / 3;
}

static unsigned picture_sum(void)
{
    unsigned i, s = 0;
    for (i = 0; i < 134u * 80u; i++) s += mk_img[i];
    return s;
}
static void picture(void)                                     /* every fifth row, every other column: 67 x 16 */
{
    static const char chars[] = " WM@#BXFGODC$&%*=FCODC$&%*=+~-;:,.";
    uint8_t r, c;
    for (r = 0; r < 80; r += 5) { for (c = 0; c < 134; c += 2) rom_chrout((uint8_t)chars[mk_img[(unsigned)r * 134u + c]]); rom_chrout('\n'); }
}

static uint8_t bad;                                           /* something computed wrongly: the figures are not to be trusted */

static void run_clock(void)
{
    unsigned long k = khz(), t, calls = 0, f;
    unsigned primes;
    tot_f = tot_w = 0;
    num(k / 1000, 4); out('.'); out((char)('0' + (k % 1000) / 100));

    start();                                                  /* 6502: whole calls until two seconds of machine time have gone */
    do { mk_spin(SPIN_N); calls++; f = (frames() - f0) & 0xFFFFFFUL; } while (f < 120);
    stop();
    fix2((SPIN_CYCLES / 10000UL) * calls * 60UL / f, 10);     /* cycles / seconds / 1e6, in hundredths */

    start(); primes = mk_sieve(); t = stop();
    fix2(t, 9); if (primes != 1899u) { out('!'); bad = 1; } else out(' ');

    start(); mk_copy(250); t = stop();                        /* 250 x 8192 bytes */
    if (t) num(2000UL * 100UL / t, 8); else say("       -");   /* KB a second */

    mk_math = 0; start(); mk_mandel(); t = stop();
    fix2(t, 10); if (picture_sum() != PICTURE_SUM) { out('!'); bad = 1; } else out(' ');
    mk_math = 1; start(); mk_mandel(); t = stop();
    fix2(t, 9); if (picture_sum() != PICTURE_SUM) { out('!'); bad = 1; } else out(' ');

    if (tot_f) { num(tot_w * 6UL / tot_f, 7); out('%'); }     /* wall ms over machine ms: a frame is 16.67 ms, and 100 / 16.67 is 6 */
    out('\n');
}

void main(void)
{
    uint8_t na, i, steps, was, only = 0; const char *a; unsigned e, n;
    unsigned long want = 0, best = 0xFFFFFFFFUL, d, k;

    na = rom_args(); a = *(const char **)0xF0;
    while (na && *a == ' ') { a++; na--; }
    while (na && *a >= '0' && *a <= '9') { want = want * 10 + (unsigned long)(*a++ - '0'); na--; }
    want *= 1000;

    for (n = 0; n < 200; n++) mk_tobcd[n] = (uint8_t)((((n % 100) / 10) << 4) | ((n % 100) % 10));
    REG(0xD772) = 0; REG(0xD773) = 0; REG(0xD776) = 0; REG(0xD777) = 0;   /* the multiplier's top halves: mk_mandel writes 16 bits */

    say("K4510 MARK   machine time: the frame counter, 60 to the second\n\n");
    e = mk_bcd();
    say("BCD      40000 decimal ADC and SBC against binary: ");
    if (e) { num(e, 1); say(" WRONG\n"); bad = 1; } else say("all right\n");
    mk_math = 0; mk_mandel(); n = picture_sum();
    mk_math = 1; mk_mandel();
    say("PICTURE  Mandelbrot sums: "); num(n, 1);
    say(" by shift and add, "); num(picture_sum(), 1); say(" by the MATH unit"); say(n == PICTURE_SUM && picture_sum() == PICTURE_SUM ? ": right\n\n" : ": NOT 16897\n\n");
    if (n != PICTURE_SUM || picture_sum() != PICTURE_SUM) bad = 1;
    picture();

    say("\n clock  as a 6502    SIEVE    COPY    MANDEL    +MATH     host\n");
    say("  MHz      at MHz        s    KB/s         s        s     took\n");

    was = REG(SYS + 0x23); steps = REG(SYS + 0x27);
    REG(SYS + 0x29) = 1;                                      /* the governor stands down, as for SETUP */
    if (want)                                                 /* MARK 40: the one step nearest */
        for (i = 0; i < steps; i++) {
            REG(SYS + 0x23) = i; next_frame(); next_frame();
            k = khz(); d = k > want ? k - want : want - k;
            if (d < best) { best = d; only = i; }
        }
    /* the ladder runs from the fastest down: take it from the bottom, 10 MHz first */
    for (i = steps; i-- > 0; ) {
        if (want && i != only) continue;
        REG(SYS + 0x23) = i; next_frame(); next_frame();      /* the frontend applies it on the next frame */
        k = khz();
        if (!want && (k < 10000UL || k > 60000UL)) continue;
        run_clock();
        if (rom_getin() == 27) { say("(Esc)\n"); break; }
    }
    REG(SYS + 0x23) = was; next_frame(); next_frame();
    REG(SYS + 0x29) = 0;

    say("\nhost took: wall time over machine time.  100% = this host kept up;\nabove it, what was on the glass was that much slower than these figures.\n");
    if (bad) say("\n*** SOMETHING COMPUTED WRONGLY (marked !): these are not speeds ***\n");

    REG(FS + 4) = (uint8_t)(unsigned)"/SYSTEM/LOG/MARK.TXT"; REG(FS + 5) = (uint8_t)((unsigned)"/SYSTEM/LOG/MARK.TXT" >> 8); REG(FS + 6) = 0; REG(FS + 7) = 0;
    REG(FS + 8) = (uint8_t)(unsigned)log_; REG(FS + 9) = (uint8_t)((unsigned)log_ >> 8); REG(FS + 10) = 0; REG(FS + 11) = 0;
    REG(FS + 12) = (uint8_t)logn; REG(FS + 13) = (uint8_t)(logn >> 8); REG(FS + 14) = 0; REG(FS + 15) = 0;
    REG(FS) = 10;
    say(REG(FS + 1) ? "(could not write /SYSTEM/LOG/MARK.TXT)\n" : "written to /SYSTEM/LOG/MARK.TXT\n");
}
