/* K4510: BENCH -- the machine measures itself and writes the numbers to
 * disk.
 *
 * Doc found a Pi build "about 10x slower" than the one before it, and the
 * same code on the desktop is not slower at all, so the difference is the
 * Pi's alone. A screen you have to read back is no use when the screen may
 * be the slow thing, so this runs from STARTUP.BAT, writes
 * /SYSTEM/LOG/BENCH-<when>.TXT onto the card, and the card carries the numbers
 * to a machine that can read them.
 *
 * The headline is frames per second. The emulator steps a fixed number of
 * CPU cycles per scanline inside its frame loop, so the host's frame rate
 * and the emulated machine's speed are the same number: 60 means the host
 * is keeping up and the machine runs at its nominal 40.5 MHz, and 6 means
 * it is not, and everything the machine does is ten times slower for that
 * one reason. The other three figures separate the paths -- plain CPU work,
 * the ROM's console (far memory and scrolling), and the DMA engine -- so if
 * the frame rate is right and one of those is not, it says which.
 *
 * Timing is the host wall clock at SYS+4, which counts whole seconds, so
 * each figure is work counted over a fixed number of seconds rather than a
 * time measured for fixed work. That keeps the run the same length whatever
 * the speed: a slow machine reports smaller numbers, not a longer wait. */
#include "k4510.h"
#include "oplnote.h"

#define TERM 0xDA00u
#define SECS 3                       /* per figure; four figures, so about 12 s */
#define CLKMAX 16                    /* as many ladder steps as BENCH will sweep */

void __fastcall__ rom_chrout(unsigned char c);
unsigned char __fastcall__ rom_shell(const char *line);
static unsigned char rom_save(void) { return ((unsigned char (*)(void))0xFF8C)(); }
static unsigned char rom_load(void) { return ((unsigned char (*)(void))0xFF89)(); }
#define SCRATCH 0x0E800000UL          /* somewhere to land a probe read */
static void zp16(uint8_t a, uint16_t v) { REG(a) = v; REG(a + 1) = v >> 8; }
static void zp32(uint8_t a, uint32_t v) { REG(a)=v; REG(a+1)=v>>8; REG(a+2)=v>>16; REG(a+3)=v>>24; }

#define BUFMAX 1024
static char BUF[BUFMAX]; static unsigned len;
static char name[64];
static volatile unsigned long sink;          /* so the CPU loop cannot be optimised away */

static void say(const char *s) { while (*s) rom_chrout((unsigned char)*s++); }
static void add(const char *s) { while (*s && len < BUFMAX - 1) BUF[len++] = *s++; }
static void addc(char c) { if (len < BUFMAX - 1) BUF[len++] = c; }
static void nl(void) { addc('\n'); }
static void addn(unsigned long v, uint8_t w)
{
    char t[12]; uint8_t n = 0;
    do { t[n++] = (char)('0' + (uint8_t)(v % 10)); v /= 10; } while (v);
    while (w > n) { addc('0'); w--; }
    while (n) addc(t[--n]);
}
/* screen only: this must NOT go through addn(), which appends to the
 * report and would put every figure into the file twice. */
static void sayn(unsigned long v)
{
    char t[12]; uint8_t n = 0;
    do { t[n++] = (char)('0' + (uint8_t)(v % 10)); v /= 10; } while (v);
    while (n) rom_chrout((unsigned char)t[--n]);
}

/* the clock: SYS+4 latches, and the seconds are at SYS+5 */
/* The latch read must land in a variable: cc65 drops `(void)REG(SYS+4);`
 * entirely -- it emitted `lda $D505` and no `lda $D504` -- so the clock
 * never updated and the first wait span forever. bug.c does it this way
 * for the same reason. */
static uint8_t now(void) { volatile uint8_t d = REG(SYS + 4); (void)d; return REG(SYS + 5); }
/* the clock in force, in kHz.  Three bytes: the ladder reaches 202500, which
 * is past what SYS+0/1 can hold on their own. */
static unsigned long clk_khz(void)
{
    volatile uint8_t d = REG(SYS + 4); (void)d;
    return (unsigned long)REG(SYS) | ((unsigned long)REG(SYS + 1) << 8)
         | ((unsigned long)REG(SYS + 0x26) << 16);
}
/* kHz as the machine says it: 202500 -> "202.5 MHz" */
static uint8_t mhzfmt(char *o, unsigned long khz)
{
    char t[8]; uint8_t n = 0, k = 0; unsigned long w = khz / 1000; uint8_t f = (uint8_t)((khz % 1000) / 100);
    do { t[k++] = (char)('0' + (uint8_t)(w % 10)); w /= 10; } while (w);
    while (k) o[n++] = t[--k];
    if (f) { o[n++] = '.'; o[n++] = (char)('0' + f); }
    o[n++] = ' '; o[n++] = 'M'; o[n++] = 'H'; o[n++] = 'z';
    return n;
}
static void saymhz(unsigned long khz) { char b[16]; uint8_t n = mhzfmt(b, khz), i; for (i = 0; i < n; i++) rom_chrout((unsigned char)b[i]); }
static void addmhz(unsigned long khz) { char b[16]; uint8_t n = mhzfmt(b, khz), i; for (i = 0; i < n; i++) addc(b[i]); while (n < 11) { addc(' '); n++; } }
static uint8_t since(uint8_t s0, uint8_t s) { return (uint8_t)(s >= s0 ? s - s0 : 60 - s0 + s); }
static uint8_t edge(void) { uint8_t s = now(); while (now() == s) ; return now(); }
static unsigned long fcount(void)
{
    return (unsigned long)REG(SYS + 0x0D) | ((unsigned long)REG(SYS + 0x0E) << 8)
         | ((unsigned long)REG(SYS + 0x0F) << 16);
}

/* count how many times `body` runs in SECS whole seconds */
#define MEASURE(out, body) do { \
        uint8_t s0_ = edge(), s_; \
        (out) = 0; \
        do { body; (out)++; s_ = now(); } while (since(s0_, s_) < SECS); \
        (out) /= SECS; \
    } while (0)

static void line(const char *label, unsigned long v, const char *unit)
{
    add(label); add(": "); addn(v, 0); add(unit); nl();
    say(label); say(": "); sayn(v); say(unit); say("\n");
}

void main(void)
{
    unsigned long fps, cpu, chr, dma, f0, sweep_fps[CLKMAX]; unsigned sweep_gap[CLKMAX], sweep_fill[CLKMAX]; unsigned long sweep_khz[CLKMAX];
    uint8_t s0, s, i, clk0, nclk;

    { volatile uint8_t d = REG(SYS + 4); (void)d; }
    say("\nBENCH -- measuring; the sweep is 2 s a step, so the length follows the ladder.\n\n");

    /* 1. frames per second: the one that says whether the host keeps up */
    s0 = edge(); f0 = fcount();
    do { s = now(); } while (since(s0, s) < SECS);
    fps = (fcount() - f0) / SECS;

    /* 2. plain CPU work, no I/O but the clock */
    MEASURE(cpu, { unsigned j; for (j = 0; j < 64; j++) sink += j; });

    /* 3. the ROM's console: 64 characters and a newline, so this includes
     *    the far-memory screen and, every 30 lines, a scroll */
    MEASURE(chr, { for (i = 0; i < 64; i++) rom_chrout('.'); rom_chrout('\r'); });

    /* 4. the DMA engine: 4 KB far to far */
    MEASURE(dma, { dma_copy(0x0E000000UL, 0x0E100000UL, 4096UL); });

    /* 5. the clock sweep: every CPU clock the host offers, two seconds each,
     *    with a note sounding on the OPL2 the whole time. The host counts the
     *    audio callbacks that found nothing to play -- that is what "choppy"
     *    is -- so each line says whether the machine kept up AND whether the
     *    sound did. The clock in force before is put back at the end. */
    say("\nclock sweep, with a note on the OPL2, every step the machine offers...\n");
    clk0 = REG(SYS + 0x23);
    /* How many steps are there?  The machine says, at SYS+0x27.  BENCH once
     * had the five clock names of 2026-08-26 frozen into a table and swept
     * indices 0-4 by number; when the ladder gained five faster steps at the
     * front it went on printing 40.5 MHz over a measurement of 202.5, and
     * read as a tenfold slowdown that had not happened.  Nothing here knows a
     * clock by its position any more: the count comes from the machine, and
     * each label is the clock that step actually became.
     * (Asking by writing 0xFF and reading the clamp back does NOT work: cc65
     * answers the read from the value it just stored, the same habit the
     * SYS+4 latch comment above describes.  Tried; the sweep came out empty.) */
    nclk = REG(SYS + 0x27);
    if (nclk == 0 || nclk > CLKMAX) nclk = CLKMAX;
    for (i = 0; i < nclk; i++) {
        REG(SYS + 0x23) = i;
        { uint8_t f = REG(SYS + 0x0D); while (REG(SYS + 0x0D) == f) ; f = REG(SYS + 0x0D); while (REG(SYS + 0x0D) == f) ; }   /* two frames to settle */
        opl_note_on(0, OPL_A440_FNUM, OPL_A440_BLOCK);
        REG(SYS + 0x24) = 0;                                      /* clear the gap count */
        s0 = edge(); f0 = fcount();
        do { s = now(); } while (since(s0, s) < 2);
        sweep_fps[i] = (fcount() - f0) / 2;
        sweep_gap[i] = (unsigned)REG(SYS + 0x24) | ((unsigned)REG(SYS + 0x25) << 8);
        /* the sound the machine did not make: 0 gaps no longer means clean */
        sweep_fill[i] = (unsigned)REG(SYS + 0x2A) | ((unsigned)REG(SYS + 0x2B) << 8);
        /* the clock this step actually became, from the machine, not a table */
        sweep_khz[i] = clk_khz();
        opl_note_off(0);
        say("  "); saymhz(sweep_khz[i]); say(": "); sayn(sweep_fps[i]); say(" fps, "); sayn(sweep_gap[i]); say(" audio gaps\n");
    }
    REG(SYS + 0x23) = clk0;

    rom_shell("CLS");
    say("\nBENCH results\n\n");

    add("K4510 self-test\n===================\n\n");
    add("Machine:  "); add(REG(SYS + 0x22) ? "Raspberry Pi 3B+" : "desktop"); nl();
    add("Build:    ");
    for (i = 0; i < 16 && REG(SYS + 0x10 + i); i++) addc((char)REG(SYS + 0x10 + i));
    nl();
    add("Nominal:  "); addn((unsigned long)REG(SYS) | ((unsigned long)REG(SYS + 1) << 8), 0); add(" kHz, ");
    addn((unsigned long)REG(SYS + 2) | ((unsigned long)REG(SYS + 3) << 8), 0); add(" MB\n");
    add("Screen:   "); addn(REG(TERM + 5), 0); addc('x'); addn(REG(TERM + 6), 0);
    /* the geometry says the mode without my having to decode it: 80x60 is
     * MODE 0, 80x30 MODE 1, 40x30 MODE 2. The raw CTRL byte is there too, so
     * a reader can check it against VICKY's register map. */
    add(", VICKY CTRL $"); { uint8_t c = REG(0xD000), hi = (uint8_t)(c >> 4), lo = (uint8_t)(c & 15);
      addc((char)(hi < 10 ? '0' + hi : 'A' + hi - 10)); addc((char)(lo < 10 ? '0' + lo : 'A' + lo - 10)); }
    nl();
    add("When:     ");
    addn((unsigned long)REG(SYS + 0x0A) | ((unsigned long)REG(SYS + 0x0B) << 8), 4); addc('-');
    addn(REG(SYS + 9), 2); addc('-'); addn(REG(SYS + 8), 2); addc(' ');
    addn(REG(SYS + 7), 2); addc(':'); addn(REG(SYS + 6), 2); addc(':'); addn(REG(SYS + 5), 2);
    nl(); nl();

    line("Frames per second   ", fps, "   (60 is right)");
    line("CPU loops per second", cpu, "");
    line("Console lines/second", chr, "");
    line("DMA 4K copies/second", dma, "");
    nl();
    add("Clock sweep, a note sounding on the OPL2, 2 s each:\n");
    for (i = 0; i < nclk; i++) {
        add("  "); addmhz(sweep_khz[i]); addn(sweep_fps[i], 2); add(" fps   ");
        addn(sweep_gap[i], 0); add(" gaps  "); addn(sweep_fill[i], 0); add(" filled");
        /* clean means the machine made every frame AND every sample of it */
        add((sweep_fps[i] >= 59 && !sweep_gap[i] && sweep_fill[i] < 800) ? "   (clean)" : ""); nl();
    }
    add("  A clock is right when it holds 60 fps, 0 gaps and nothing filled.\n");
    add("  Filled = samples the sound made while the machine was too late to.\n");
    add("  Gaps count only silence; filled is what you actually hear as choppy.\n");
    add("  (the clock in force before the sweep was put back)\n");
    nl();
    add("The frame rate is the machine's speed: the emulator runs its cycle\n");
    add("budget inside the host's frame loop, so 60 fps is 40.5 MHz and half\n");
    add("that frame rate is half the machine. If the frame rate is right and\n");
    add("one of the others is low, the fault is in that path instead.\n");
    add("(Written by BENCH on the machine itself.)\n");

    rom_shell("MKDIR /SYSTEM/LOG");
    /* The Pi has no clock of its own: every boot reports the same date, so a
     * name built from it overwrote the previous run -- and comparing runs is
     * the whole point. Take the first free number instead, found by trying to
     * open each one. The date and time are inside the file either way. */
    { uint8_t k;
      for (k = 1; k < 100; k++) {
          const char *p = "/SYSTEM/LOG/BENCH-"; uint8_t n = 0;
          while (*p) name[n++] = *p++;
          name[n++] = (char)('0' + k / 10); name[n++] = (char)('0' + k % 10);
          { const char *e = ".TXT"; while (*e) name[n++] = *e++; }
          name[n] = 0;
          zp16(0xF0, (uint16_t)name); zp32(0xF2, SCRATCH);
          if (rom_load()) break;                    /* will not open: free */
      }
    }

    zp16(0xF0, (uint16_t)name); zp32(0xF2, (uint32_t)(uint16_t)BUF); zp32(0xF6, (uint32_t)len);
    if (rom_save()) { say("\nBENCH: could not write "); say(name); say("\n"); return; }
    say("\nWritten: "); say(name); say("\n");
}
