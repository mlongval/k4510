/* K4510: SETUP -- the machine measures itself, thoroughly, when asked.
 *
 * The boot does not measure.  Doc's decision, 2026-08-27: a probe at every
 * first boot buys an answer that changes once, and buys it badly -- two
 * hundred milliseconds cannot measure video, or sound that anyone can hear,
 * or the network.  So the boot is instantaneous at a safe clock, the banner
 * says the machine has not been measured, and this program is what measures
 * it.  What it settles on is kept in k4510.cfg through SYS+$28 and read back
 * at every later boot, so the cost is paid once, deliberately, with someone
 * watching.
 *
 * Nothing here knows a clock by its position in a menu: the ladder's length
 * comes from SYS+$27 and every step is labelled with the clock it actually
 * became, read back from SYS+0/1/$26.  BENCH learned that the hard way on
 * 2026-08-27, when the ladder grew five faster steps at the front and it went
 * on printing "40.5 MHz" over a measurement of 202.5.
 *
 * The screen is JIM, the terminal chip at $DA00, driven with ANSI -- the same
 * road vi.c takes.  */
#include "k4510.h"
#include "oplnote.h"
#include "far.h"

#define TERM     0xDA00u
#define NET      0xD900u
#define CLKMAX   16
#define SWEEP_S  2                    /* seconds per ladder step */
/* A 2 s window is 96,000 samples at 48 kHz.  Some filling happens even on a
 * healthy host -- the ring is topped to a lead, so it dips and is refilled
 * every frame -- so the bar is a fraction of the window rather than zero.
 * 2% is quiet enough that Doc cannot hear it; 17% (hdieu at 20 MHz, 60 fps
 * and 0 gaps) he can, which is what sent us looking. */
#define FILL_CLEAN 1920

void __fastcall__ rom_chrout(unsigned char c);
unsigned char __fastcall__ rom_shell(const char *line);
/* The ROM owns the keyboard -- its IRQ drains $D100 -- so a program reads
 * keys through the ROM, as edit.c does, not through k4510.h key_get(). */
unsigned char rom_getin(void);
/* the ROM's file entries, as bench.c uses them: name at $F0, buffer at $F2,
 * length at $F6 */
static unsigned char rom_save(void) { return ((unsigned char (*)(void))0xFF8C)(); }
static void zp16(uint8_t a, uint16_t v) { REG(a) = v; REG(a + 1) = v >> 8; }
static void zp32(uint8_t a, uint32_t v) { REG(a)=v; REG(a+1)=v>>8; REG(a+2)=v>>16; REG(a+3)=v>>24; }
static char REPORT[] = "/SYSTEM/SETUP.TXT";   /* in RAM, as bench.c keeps its name */
static uint8_t rpt_ok, rpt_st;
/* JIM keeps its own idea of the default colours: DEFBG at $DA15 is where SGR 0
 * and SGR 49 land, and $DA0C is what it paints with now.  A program that sets a
 * background and leaves is a program that hands back a green screen on a green
 * screen -- which is what SETUP did the first time Doc ran it.  Save both, and
 * put them back on every road out. */
static uint8_t odefbg, ocurbg;

/* ---- the terminal ------------------------------------------------------- */
static uint8_t cols, rows;
static void put(char c) { REG(TERM) = (uint8_t)c; }
static void say(const char *s) { while (*s) put(*s++); }
static void num(unsigned long v)
{
    char t[11]; uint8_t n = 0;
    do { t[n++] = (char)('0' + (uint8_t)(v % 10)); v /= 10; } while (v);
    while (n) put(t[--n]);
}
/* CSI */
static void csi(void) { put(27); put('['); }
static void at(uint8_t x, uint8_t y) { csi(); num(y); put(';'); num(x); put('H'); }
static void sgr(uint8_t n) { csi(); num(n); put('m'); }
static void sgr2(uint8_t a, uint8_t b) { csi(); num(a); put(';'); num(b); put('m'); }
static void eol(void) { csi(); put('K'); }

/* ---- the clock ---------------------------------------------------------- */
/* three bytes: the ladder reaches 202500 kHz, past what SYS+0/1 hold alone */
static unsigned long clk_khz(void)
{
    volatile uint8_t d = REG(SYS + 4); (void)d;
    return (unsigned long)REG(SYS) | ((unsigned long)REG(SYS + 1) << 8)
         | ((unsigned long)REG(SYS + 0x26) << 16);
}
static void say_mhz(unsigned long khz)
{
    uint8_t f = (uint8_t)((khz % 1000) / 100);
    num(khz / 1000);
    if (f) { put('.'); put((char)('0' + f)); }
    say(" MHz");
}
static uint8_t now_s(void) { volatile uint8_t d = REG(SYS + 4); (void)d; return REG(SYS + 5); }
static uint8_t since(uint8_t s0, uint8_t s) { return (uint8_t)(s >= s0 ? s - s0 : 60 - s0 + s); }
static uint8_t edge(void) { uint8_t s = now_s(); while (now_s() == s) ; return now_s(); }
static unsigned long frames(void)
{
    return (unsigned long)REG(SYS + 0x0D) | ((unsigned long)REG(SYS + 0x0E) << 8)
         | ((unsigned long)REG(SYS + 0x0F) << 16);
}

/* ---- results ------------------------------------------------------------ */
static uint8_t nclk;
static unsigned long swp_khz[CLKMAX];
static uint8_t  swp_fps[CLKMAX];
static unsigned swp_gap[CLKMAX], swp_fill[CLKMAX];
static uint8_t  chosen = 0xFF, settled_short;   /* settled_short: the best available, not a clean one */
static uint8_t  vid_ok, aud_ok, net_ok;      /* 0 fail, 1 pass, 2 skipped, 3 not fitted */

/* every road out of SETUP goes through here */
static uint8_t opr[16], opg[16], opb[16], pal_saved;
static void pal_restore(void) { uint8_t i; if (!pal_saved) return; for (i = 0; i < 16; i++) pal(i, opr[i], opg[i], opb[i]); }
static void restore(void)
{
    pal_restore();
    sgr(0);
    REG(TERM + 0x15) = odefbg; REG(TERM + 0x0C) = ocurbg;
    sgr(0);                                       /* again, so SGR 0 lands on the restored default */
    REG(SYS + 0x29) = 0;                          /* the governor may resume */
    REG(TERM + 0x0E) = 1;                         /* cursor back */
    REG(TERM + 4) = 2;                            /* and a clean screen for the shell */
}
static void head(const char *title)
{
    uint8_t i;
    REG(TERM + 4) = 2;                       /* clear */
    sgr2(1, 44); at(1, 1); for (i = 0; i < cols; i++) put(' ');
    at(3, 1); say("K4510 SETUP  --  "); say(title);
    at((uint8_t)(cols - 20), 1); say(REG(SYS + 0x22) ? "Raspberry Pi 3B+" : "desktop");
    sgr(0);
}
static void footer(const char *s)
{
    uint8_t i;
    sgr2(1, 40); at(1, rows); for (i = 0; i < cols; i++) put(' ');
    at(3, rows); say(s); sgr(0);
}
static uint8_t ask(const char *q)            /* Y/N, returns 1 for yes */
{
    uint8_t k;
    at(3, (uint8_t)(rows - 2)); eol(); sgr(1); say(q); say("  [Y/N] "); sgr(0);
    for (;;) {
        k = rom_getin();
        if (k == 'y' || k == 'Y') { say("yes"); return 1; }
        if (k == 'n' || k == 'N') { say("no");  return 0; }
    }
}
static void anykey(const char *s)
{
    at(3, (uint8_t)(rows - 2)); eol(); sgr(1); say(s); sgr(0);
    while (!rom_getin()) ;
}

/* ---- 1. the clock ladder ------------------------------------------------- */
/* Every step the machine offers, two seconds each, with a note sounding on
 * the OPL2.  The host counts the audio callbacks that found nothing to play --
 * that is what "choppy" is -- so each row says whether the machine kept up
 * AND whether the sound did.  A clock is right for this host when it holds
 * 60 fps with no gaps. */
static void bar(uint8_t x, uint8_t y, uint8_t fps, unsigned gaps, unsigned fill)
{
    uint8_t w = (uint8_t)((unsigned)fps * 40u / 60u), i;
    if (w > 40) w = 40;
    at(x, y);
    if (fps >= 59 && !gaps && fill < FILL_CLEAN) sgr2(1, 42);   /* green: holds it */
    else if (fps >= 59 || !gaps)          sgr2(1, 43);   /* yellow: keeping up on one count, not both */
    else                                  sgr2(1, 41);   /* red: neither */
    for (i = 0; i < w; i++) put(' ');
    sgr(0);
    for (; i < 40; i++) put(' ');
}
static void test_clock(void)
{
    uint8_t i, y, clk0, s0, s;
    unsigned long f0;
    head("1 of 4  --  CPU clock");
    footer("measuring every step of the clock ladder, 2 s each");
    at(3, 3); say("The machine runs its cycles inside the host's frame, so the frame");
    at(3, 4); say("rate IS the machine's speed.  60 fps and no audio gaps means this");
    at(3, 5); say("host holds it.  Gaps are silence; filled is sound the chip made");
    at(3, 6); say("while the machine was too late to -- what you hear as choppy.");

    clk0 = REG(SYS + 0x23);
    nclk = REG(SYS + 0x27);
    if (nclk == 0 || nclk > CLKMAX) nclk = CLKMAX;

    for (i = 0; i < nclk; i++) {
        y = (uint8_t)(7 + i);
        REG(SYS + 0x23) = i;
        { uint8_t f = REG(SYS + 0x0D); while (REG(SYS + 0x0D) == f) ; f = REG(SYS + 0x0D); while (REG(SYS + 0x0D) == f) ; }
        swp_khz[i] = clk_khz();
        at(3, y); sgr(1); say_mhz(swp_khz[i]); sgr(0); eol();
        at(3, y); csi(); num(14); put('C');                   /* a fixed column for the bar */
        opl_note_on(0, OPL_A440_FNUM, OPL_A440_BLOCK);
        REG(SYS + 0x24) = 0;                                  /* clear the gap count */
        s0 = edge(); f0 = frames();
        do { s = now_s(); } while (since(s0, s) < SWEEP_S);
        swp_fps[i] = (uint8_t)((frames() - f0) / SWEEP_S);
        swp_gap[i] = (unsigned)REG(SYS + 0x24) | ((unsigned)REG(SYS + 0x25) << 8);
        swp_fill[i] = (unsigned)REG(SYS + 0x2A) | ((unsigned)REG(SYS + 0x2B) << 8);
        opl_note_off(0);
        bar(18, y, swp_fps[i], swp_gap[i], swp_fill[i]);
        at(58, y); num(swp_fps[i]); say(" fps ");
        if (swp_gap[i])            { sgr(31); num(swp_gap[i]); say(" gaps"); sgr(0); }
        else if (swp_fill[i] >= FILL_CLEAN) { sgr(33); num(swp_fill[i]); say(" filled"); sgr(0); }
        else if (swp_fps[i] < 59)  { sgr(33); say("behind"); sgr(0); }
        else                       { sgr(32); say("clean"); sgr(0); }
        eol();
        if (chosen == 0xFF && swp_fps[i] >= 59 && !swp_gap[i] && swp_fill[i] < FILL_CLEAN) chosen = i;
    }
    REG(SYS + 0x23) = clk0;
    /* Nothing clean anywhere: rather than keep nothing, take the quietest step
     * that at least holds 60 fps without a gap, and say at the end that it is
     * the best this host can do rather than a clean bill. */
    if (chosen == 0xFF) {
        unsigned best = 0xFFFFu;
        for (i = 0; i < nclk; i++)
            if (swp_fps[i] >= 59 && !swp_gap[i] && swp_fill[i] < best) { best = swp_fill[i]; chosen = i; }
        if (chosen != 0xFF) settled_short = 1;
    }
}

/* ---- 2. video ------------------------------------------------------------ */
/* The palette is sixteen registers that read back, and the DMA engine moves
 * far memory.  Both are checked against what they were told, which needs no
 * table of modes that could go stale the way BENCH's clock names did. */
static void test_video(void)
{
    uint8_t i, r, g, b, bad = 0;
    uint32_t p;
    head("2 of 4  --  video");
    footer("VICKY: palette write/readback, then DMA over far memory");
    at(3, 3); say("Palette, 16 entries, written and read back ....... ");
    /* Entries 0-15 ARE the text colours.  Writing a ramp over them and walking
     * away leaves the machine green on green -- chip state, not screen state,
     * so it outlives the program and the terminal reset that follows it.  Doc
     * got exactly that, 2026-08-27.  Read them first, put them back after, and
     * do it inside one frame so the ramp is never seen. */
    for (i = 0; i < 16; i++) {
        REG(V_PALIDX) = i; opr[i] = REG(V_PALR); opg[i] = REG(V_PALG); opb[i] = REG(V_PALB);
    }
    pal_saved = 1;
    for (i = 0; i < 16; i++) {
        r = (uint8_t)(i * 17); g = (uint8_t)(255 - i * 17); b = (uint8_t)(i * 5);
        pal(i, r, g, b);
        REG(V_PALIDX) = i;
        if (REG(V_PALR) != r || REG(V_PALG) != g || REG(V_PALB) != b) bad++;
    }
    for (i = 0; i < 16; i++) pal(i, opr[i], opg[i], opb[i]);      /* the machine's colours, back */
    if (bad) { sgr(31); say("FAIL "); num(bad); say(" of 16"); sgr(0); }
    else     { sgr(32); say("pass"); sgr(0); }

    at(3, 5); say("DMA fill and copy, 4 KB of far memory ........... ");
    dma_fill(0xA5, 0x0E000000UL, 4096UL);
    dma_copy(0x0E000000UL, 0x0E100000UL, 4096UL);
    for (i = 0, p = 0x0E100000UL; i < 32; i++, p += 128)
        if (far_peek(p) != 0xA5) bad++;
    if (bad) { sgr(31); say("FAIL"); sgr(0); }
    else     { sgr(32); say("pass"); sgr(0); }

    at(3, 7); say("Text geometry now ............................... ");
    sgr(1); num(REG(TERM + 5)); put('x'); num(REG(TERM + 6)); sgr(0);
    at(3, 9); say("The picture you are reading is itself the test of the text");
    at(3, 10); say("path: these characters came through VICKY from far memory.");
    vid_ok = bad ? 0 : 1;
    anykey("press a key to test the sound");
}

/* ---- 3. audio, with someone listening ------------------------------------ */
/* The gap counter in the sweep says the ring never ran dry.  It cannot say
 * that anything reached the room.  Only a person can, so this asks one. */
static void test_audio(void)
{
    uint8_t c, i;
    head("3 of 4  --  audio");
    footer("four notes on the OPL2, rising -- listen");
    at(3, 3); say("The clock sweep counted audio gaps, which says the ring never ran");
    at(3, 4); say("dry.  It cannot say the sound reached the room.  That needs you.");
    for (c = 0; c < 4; c++) {
        static const uint16_t fn[4] = { 580, 651, 730, 819 };   /* A, B, C#, D#: block 4 */
        at(3, (uint8_t)(6 + c)); sgr(1); say("note "); num(c + 1); sgr(0); say("  ");
        opl_note_on(c, fn[c], 4);
        sgr2(1, 42);
        for (i = 0; i < 40; i++) { put(' '); wait_vblank(); }
        sgr(0);
        opl_note_off(c);
        for (i = 0; i < 15; i++) wait_vblank();
    }
    aud_ok = ask("Did you hear four notes, rising?");
    if (!aud_ok) {
        at(3, (uint8_t)(rows - 4)); sgr(33);
        say("Noted.  The sweep's gap counts are still valid: the machine fed the");
        at(3, (uint8_t)(rows - 3));
        say("sound card on time.  Look at the host's own volume and device.");
        sgr(0);
        anykey("press a key");
    }
}

/* ---- 4. the network ------------------------------------------------------ */
/* $D901 answers 6 when the device is not fitted, which is the whole of the
 * test that needs no outside world.  Anything further reaches off this
 * machine, so it is asked for rather than assumed. */
static void test_net(void)
{
    uint8_t st, i;
    head("4 of 4  --  network");
    footer("the N: device at $D900");
    at(3, 3); say("N: device fitted ................................ ");
    st = REG(NET + 1);
    if (st == 6) {
        sgr(31); say("not fitted"); sgr(0);
        at(3, 5); say("No network stack on this host.  Nothing else to test here.");
        net_ok = 3;
        anykey("press a key for the result");
        return;
    }
    sgr(32); say("yes"); sgr(0);
    if (REG(SYS + 0x22)) {
        at(3, 5); sgr(33);
        say("This is the Pi, where the network has never run on real hardware.");
        at(3, 6);
        say("A failure here may be the test being wrong, not the machine.");
        sgr(0);
    }
    at(3, 8); say("A reachability check fetches a URL, which leaves this machine.");
    if (!ask("Try to reach the network?")) { net_ok = 2; return; }
    at(3, 10); say("Fetching ........................................ ");
    REG(NET + 2) = 0;
    far_poke32(0x0E200000UL, 0);
    /* the URL, into low RAM where NAMEPTR can see it */
    { const char *u = "http://example.com/"; uint32_t p = 0x00008000UL;
      while (*u) far_poke(p++, (uint8_t)*u++); far_poke(p, 0); }
    w32(NET + 4, 0x00008000UL);
    w32(NET + 8, 0x0E200000UL);
    w32(NET + 12, 4096UL);
    REG(NET) = 6;                                    /* GET */
    for (i = 0; i < 200 && REG(NET + 1) == 0 && !far_r32(NET + 12); i++) wait_vblank();
    st = REG(NET + 1);
    if (st == 0 && far_r32(NET + 12)) {
        sgr(32); say("pass, "); num(far_r32(NET + 12)); say(" bytes"); sgr(0); net_ok = 1;
    } else {
        sgr(31); say("no answer (status "); num(st); put(')'); sgr(0); net_ok = 0;
        at(3, 12); say("The device is fitted; the far end did not answer.  That is the");
        at(3, 13); say("link or the internet, not the machine.");
    }
    REG(NET) = 4;                                    /* close */
    anykey("press a key for the result");
}

/* ---- the report ---------------------------------------------------------- */
static char BUF[1400]; static unsigned blen;
static void add(const char *s) { while (*s && blen < sizeof BUF - 1) BUF[blen++] = *s++; }
static void addn(unsigned long v)
{
    char t[11]; uint8_t n = 0;
    do { t[n++] = (char)('0' + (uint8_t)(v % 10)); v /= 10; } while (v);
    while (n && blen < sizeof BUF - 1) BUF[blen++] = t[--n];
}
static void addmhz(unsigned long khz)
{
    uint8_t f = (uint8_t)((khz % 1000) / 100);
    addn(khz / 1000);
    if (f) { add("."); BUF[blen++] = (char)('0' + f); }
    add(" MHz");
}
static const char *verdict(uint8_t v)
{
    return v == 1 ? "pass" : v == 2 ? "skipped" : v == 3 ? "not fitted" : "FAIL";
}
static void write_report(void)
{
    uint8_t i;
    blen = 0;
    add("K4510 SETUP\n===============\n\n");
    add("Machine:  "); add(REG(SYS + 0x22) ? "Raspberry Pi 3B+" : "desktop"); add("\n");
    add("Build:    ");
    for (i = 0; i < 16 && REG(SYS + 0x10 + i); i++) BUF[blen++] = (char)REG(SYS + 0x10 + i);
    add("\n\nClock ladder, a note sounding on the OPL2, ");
    addn(SWEEP_S); add(" s each:\n");
    for (i = 0; i < nclk; i++) {
        add("  "); addmhz(swp_khz[i]);
        add("  "); addn(swp_fps[i]); add(" fps  ");
        addn(swp_gap[i]); add(" gaps  "); addn(swp_fill[i]); add(" filled");
        if (i == chosen) add(settled_short ? "   <- kept (best available)" : "   <- kept");
        add("\n");
    }
    add("\nVideo:    "); add(verdict(vid_ok));
    add("\nAudio:    "); add(verdict(aud_ok));
    add("\nNetwork:  "); add(verdict(net_ok));
    add("\n\nThe kept clock is written to k4510.cfg and read at every later\n");
    add("boot, so the boot itself never stops to measure.\n");
    add("(Written by SETUP on the machine itself.)\n");
    BUF[blen] = 0;
    rom_shell("MKDIR /SYSTEM");
    /* One fixed name, unlike BENCH's numbered files: BENCH exists to be
     * compared run against run, SETUP to say what this machine is now. */
    zp16(0xF0, (uint16_t)REPORT); zp32(0xF2, (uint32_t)(uint16_t)BUF); zp32(0xF6, (uint32_t)blen);
    rpt_st = rom_save();
    rpt_ok = rpt_st ? 0 : 1;
}

/* ---- the result screen ---------------------------------------------------- */
static void result(void)
{
    head("done");
    footer("SETUP has finished");
    if (chosen != 0xFF) {
        REG(SYS + 0x23) = chosen;
        { uint8_t f = REG(SYS + 0x0D); while (REG(SYS + 0x0D) == f) ; }
        REG(SYS + 0x28) = 1;                          /* the frontend keeps it */
        if (settled_short) {
            at(3, 4); sgr2(1, 33); say("The quietest this machine manages is "); say_mhz(swp_khz[chosen]); sgr(0);
            at(3, 6); say("It holds 60 fps with no gaps, but the sound is still filling in");
            at(3, 7); say("sound the machine is too late to make, so you may hear it.");
            at(3, 8); say("The host itself is the limit here, not the emulated clock.");
        } else {
            at(3, 4); sgr2(1, 32); say("This machine holds "); say_mhz(swp_khz[chosen]); sgr(0);
            at(3, 6); say("Kept.  Every later boot starts there without measuring again.");
        }
    } else {
        at(3, 4); sgr2(1, 31); say("No step of the ladder holds 60 fps with clean sound."); sgr(0);
        at(3, 6); say("The clock is left as it was.  The lowest step is the one to try.");
    }
    at(3, 8);  say("Video    "); say(verdict(vid_ok));
    at(3, 9);  say("Audio    "); say(verdict(aud_ok));
    at(3, 10); say("Network  "); say(verdict(net_ok));
    at(3, 12);
    if (rpt_ok) { say("The full sweep is in "); say(REPORT); put('.'); }
    else { sgr(31); say("The report could not be written ("); say(REPORT); say(", status ");
           num(rpt_st); put(')'); sgr(0); }
    anykey("press a key to return to the shell");
}

void main(void)
{
    cols = REG(TERM + 5); rows = REG(TERM + 6);
    odefbg = REG(TERM + 0x15); ocurbg = REG(TERM + 0x0C);
    if (cols < 40) cols = 40;
    if (rows < 24) rows = 24;
    REG(TERM + 0x0E) = 0;                             /* cursor off */
    /* The governor steps the clock down when the sound starves, and the
     * sweep starves it deliberately at the top of the ladder.  Hold it off
     * for the whole run: it fought the first sweep on hdieu and made the
     * numbers non-monotonic. */
    REG(SYS + 0x29) = 1;
    head("this will take about a minute");
    footer("Enter to begin, Q to leave");
    at(3, 4);  say("SETUP measures this machine and keeps the answer, so that the");
    at(3, 5);  say("boot never has to.  It will:");
    at(5, 7);  sgr(1); say("1"); sgr(0); say("  sweep every CPU clock, with a note sounding");
    at(5, 8);  sgr(1); say("2"); sgr(0); say("  check VICKY's palette and the DMA engine");
    at(5, 9);  sgr(1); say("3"); sgr(0); say("  play four notes and ask you what you heard");
    at(5, 10); sgr(1); say("4"); sgr(0); say("  look for the network");
    at(3, 12); say("The sound will change pitch and the picture may stutter while");
    at(3, 13); say("the clock is swept.  Both are the measurement, not a fault.");
    for (;;) {
        uint8_t k = rom_getin();
        if (k == 13) break;
        if (k == 'q' || k == 'Q') { restore(); return; }
    }
    test_clock();
    anykey("press a key to test the video");
    test_video();
    test_audio();
    test_net();
    write_report();
    result();
    restore();
}
