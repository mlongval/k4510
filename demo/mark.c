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
 * Beside each time, "=65C02": the clock a real 65C02 would need to do that work
 * in that time -- the loop's cycles, counted on a cycle-exact simulator from
 * this very program (demo/mark-cycles.h), over the seconds.  It is the
 * comparison with every other machine at once: an Apple IIe is 1, a BBC Master
 * 2, a Commander X16 8, and their times are these times multiplied up.  +MATH
 * is set against MANDEL's cycles -- the same picture, as if a 65C02 had done it
 * the long way; there is no 65C02 with a MATH unit to count.
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
#include "mark-cycles.h"                 /* what each loop costs a real 65C02, to the cycle (tools/mark-cycles.py) */

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
static unsigned long so_far(void) { return (frames() - f0) & 0xFFFFFFUL; }
static unsigned long stop(void)                               /* -> machine time, in frames */
{
    unsigned long f = so_far(), w = wall_ms() - w0;
    tot_f += f; tot_w += w;
    return f ? f : 1;
}
/* seconds (to a hundredth) for ONE of `calls' runs that took f frames together, and the 65C02 that would match it:
 * cycles x calls / (f / 60) / 1e6 MHz, to a tenth.  In thousands of cycles, or 158 million x 60 is past 32 bits. */
static void figures(unsigned long cyc, unsigned long calls, unsigned long f, uint8_t wt, uint8_t wm)
{
    unsigned long m = (cyc / 1000UL) * calls * 6UL / f / 10UL;
    fix2(f * 5UL / 3UL / calls, wt);
    num(m / 10, (uint8_t)(wm - 2)); out('.'); out((char)('0' + m % 10));
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
    unsigned long k = khz(), calls, f, m;
    unsigned primes = 1899u;
    tot_f = tot_w = 0;
    num(k / 1000, 4); out('.'); out((char)('0' + (k % 1000) / 100));

    /* the short ones are run again and again until two seconds of machine time have gone: at 60 MHz the sieve
     * is fourteen frames, and a figure made of fourteen of anything is good to one part in fourteen */
    calls = 0; start(); do { mk_spin(SPIN_N); calls++; } while (so_far() < 120); f = stop();
    m = (MK_CYC_SPIN / 1000UL) * calls * 6UL / f / 10UL;
    num(m / 10, 6); out('.'); out((char)('0' + m % 10));

    calls = 0; start(); do { if (mk_sieve() != 1899u) primes = 0; calls++; } while (so_far() < 120); f = stop();
    figures(MK_CYC_SIEVE, calls, f, 7, 7); if (!primes) { out('!'); bad = 1; } else out(' ');

    calls = 0; start(); do { mk_copy(250); calls++; } while (so_far() < 120); f = stop();   /* 250 x 8192 bytes a call */
    num(2000UL * 60UL * calls / f, 6);                         /* KB a second */
    m = (MK_CYC_COPY / 1000UL) * calls * 6UL / f / 10UL;
    num(m / 10, 5); out('.'); out((char)('0' + m % 10)); out(' ');

    mk_math = 0; start(); mk_mandel(); f = stop();
    figures(MK_CYC_MANDEL, 1, f, 7, 7); if (picture_sum() != PICTURE_SUM) { out('!'); bad = 1; } else out(' ');
    mk_math = 1; start(); mk_mandel(); f = stop();
    figures(MK_CYC_MANDEL, 1, f, 6, 7); if (picture_sum() != PICTURE_SUM) { out('!'); bad = 1; } else out(' ');

    num(tot_w * 6UL / tot_f, 4); out('%');                     /* wall ms over machine ms: a frame is 16.67 ms, and 100 / 16.67 is 6 */
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

    say("\n clock    SPIN    SIEVE           COPY        MANDEL         +MATH         host\n");
    say("  MHz   =65C02      s  =65C02   KB/s =65C02      s  =65C02     s  =65C02   took\n");

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

    say("\n=65C02: the MHz a real 65C02 needs to match it (Apple IIe 1, BBC Master 2,\nCommander X16 8): their times are these, multiplied up.\n");
    say("host took: wall over machine time.  100% = this host kept up; above it, what\nwas on the glass was that much slower than these figures.\n");
    if (bad) say("\n*** SOMETHING COMPUTED WRONGLY (marked !): these are not speeds ***\n");

    REG(FS + 4) = (uint8_t)(unsigned)"/SYSTEM/LOG/MARK.TXT"; REG(FS + 5) = (uint8_t)((unsigned)"/SYSTEM/LOG/MARK.TXT" >> 8); REG(FS + 6) = 0; REG(FS + 7) = 0;
    REG(FS + 8) = (uint8_t)(unsigned)log_; REG(FS + 9) = (uint8_t)((unsigned)log_ >> 8); REG(FS + 10) = 0; REG(FS + 11) = 0;
    REG(FS + 12) = (uint8_t)logn; REG(FS + 13) = (uint8_t)(logn >> 8); REG(FS + 14) = 0; REG(FS + 15) = 0;
    REG(FS) = 10;
    say(REG(FS + 1) ? "(could not write /SYSTEM/LOG/MARK.TXT)\n" : "written to /SYSTEM/LOG/MARK.TXT\n");
}
