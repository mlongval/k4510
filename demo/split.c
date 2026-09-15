/* K4510: SPLIT -- graphics above, four lines of text below, the way an
 * Apple II or a C64 split its screen (Doc, 2026-09-13: "does vicky support
 * split screen, ala c64 or apple ii with graphics at top and 4 bottom lines
 * text?").
 *
 * The CPU does nothing to hold the split.  SHEILA, VICKY's display-list
 * coprocessor, runs eight instructions every frame:
 *
 *   line 0      the console's text on: the top status band, if there is one
 *   WAIT top    the band's end: text off, layer 1 (a bitmap) on
 *   WAIT split  the first glass line of the console's last four rows:
 *               the bitmap off, text on -- those rows and the bottom band
 *   END
 *
 * Where the split falls is read from the console, not assumed: JIM's window
 * ($DA06 rows, $DA08 the rows above it -- the top status band) and VICKY's
 * line-halving bit (a text row is 16 glass lines at 640x240, 8 at 640x480).
 * The first version took the plain 80x30 console for granted; on the Dell,
 * with the status bands up, three of its four lines fell outside the band
 * (Doc, 2026-09-14).
 *
 * Meanwhile the program draws a ribbon of blitter lines into the bitmap
 * above the split as fast as it can, erasing the line sixteen behind, and
 * prints in the text band how many lines a second it managed.
 * EX/SPLIT.BAS in EhBASIC and SPLIT.BAS in /LANG/MSBASIC do the same thing,
 * to see what a BASIC makes of it.
 *
 * Any key leaves: SHEILA off, the layers back, the ROM's screen restored --
 * nothing in the ROM turns SHEILA off after a program, so this must. */
#include "k4510.h"

unsigned char rom_getin(void);
static void rom_video(void) { ((void (*)(void))0xFF92)(); }

#define TERM    0xDA00u
#define VIC     0xD000u
#define FRAMES  0xD50Du                 /* 24-bit frame count, 60 a second */
#define LIST    0x0D000000UL            /* SHEILA's list, in far memory */
#define BITMAP  0x00200000UL
#define TRAIL   16

/* Three parts: the top status band (text), the bitmap, the console's last
 * four rows and the bottom band (text).  With no top band the first WAIT
 * is line 0 and the text is off from the start, as a plain split. */
static uint8_t list[] = {
    2, 0x20, 0,    0,                   /* line 0: MOVE layer 1 control <- 0: the bitmap off */
    2, 0x10, 7,    0,                   /*         MOVE layer 0 control <- the console's: text on (the top band) */
    1, 0,    0,    0,                   /* WAIT for the top band's end (the line, below) */
    2, 0x10, 0,    0,                   /*         text off */
    2, 0x20, 0x19, 0,                   /*         the bitmap on, 8 bpp */
    1, 0,    0,    0,                   /* WAIT for the split */
    2, 0x20, 0,    0,                   /*         the bitmap off */
    2, 0x10, 7,    0,                   /*         text on: the last four rows, and the bottom band */
    0, 0,    0,    0                    /* END: again from the top next frame */
};

static int tx0[TRAIL], ty0[TRAIL], tx1[TRAIL], ty1[TRAIL];
static uint8_t rows;                    /* the console window's rows */

static void raw(char c) { REG(TERM) = (uint8_t)c; }
static void say(const char *s) { while (*s) raw(*s++); }
static void num(unsigned v)
{
    char b[6]; uint8_t i = 5;
    b[5] = 0;
    do { b[--i] = (char)('0' + v % 10); v /= 10; } while (v);
    say(b + i);
}
static void row(uint8_t r)              /* to the start of band row r (0-3), cleared */
{
    raw(27); raw('['); num(rows - 4 + r + 1); say(";1H"); raw(27); say("[K");
}
static unsigned frames(void) { return REG(FRAMES) | ((unsigned)REG(FRAMES + 1) << 8); }

static void line(int x0, int y0, int x1, int y1, uint8_t c)
{
    REG(VIC + 0x70) = c;
    REG(VIC + 0x84) = (uint8_t)x0; REG(VIC + 0x85) = (uint8_t)(x0 >> 8);
    REG(VIC + 0x86) = (uint8_t)y0; REG(VIC + 0x87) = (uint8_t)(y0 >> 8);
    REG(VIC + 0x88) = (uint8_t)x1; REG(VIC + 0x89) = (uint8_t)(x1 >> 8);
    REG(VIC + 0x8A) = (uint8_t)y1; REG(VIC + 0x8B) = (uint8_t)(y1 >> 8);
    REG(VIC + 0x82) = 1;                /* go: a LINE is one blitter operation */
}

static void bounce(int *p, int *d, int lo, int hi)
{
    *p += *d;
    if (*p < lo || *p > hi) { *d = -*d; *p += 2 * *d; }
}

int main(void)
{
    uint8_t l0, i, k = 0, c = 16, oy, rh, halved;
    int x0 = 100, y0 = 20, x1 = 500, y1 = 180, a = 7, b = 3, e = -5, f = 4;
    unsigned t, n = 0, dt, split, toph, top, topb;

    /* where the split falls: the first glass line of the console's last four rows */
    rows = REG(TERM + 6); oy = REG(TERM + 8);
    if (rows < 8) rows = 30;
    halved = (REG(VIC) & 0x24) == 4;    /* 640x240: each machine line drawn twice (not the HD family, drawn at its own size) */
    rh = (halved || (REG(VIC + 0x10) & 0x60)) ? 16 : 8;   /* a text row in glass lines: 16 when halved or in 8x16 cells (640x480) */
    split = (unsigned)(oy + rows - 4) * rh;
    toph = halved ? split / 2 : split;  /* bitmap rows above it */
    top = (unsigned)oy * rh;            /* the top band's end, a glass line */
    topb = halved ? top / 2 : top;      /* ... and in bitmap rows: the drawing stays below it */
    y0 = (int)(topb + toph) / 2; y1 = (int)toph - 1;

    l0 = REG(VIC + 0x10);
    list[6] = l0; list[30] = l0;
    list[9] = (uint8_t)top; list[10] = (uint8_t)(top >> 8);
    list[21] = (uint8_t)split; list[22] = (uint8_t)(split >> 8);
    dma_copy((uint32_t)(uint16_t)list, LIST, sizeof list);

    /* layer 1: a 640-wide 8 bpp bitmap at $200000, cleared above the split */
    for (i = 0x21; i <= 0x25; i++) REG(VIC + i) = 0;
    REG(VIC + 0x26) = 640 & 255; REG(VIC + 0x27) = 640 >> 8;
    REG(VIC + 0x28) = 0; REG(VIC + 0x29) = 0; REG(VIC + 0x2A) = 0x20; REG(VIC + 0x2B) = 0;
    dma_fill(0, BITMAP, 640UL * toph);

    /* the blitter: LINE into that bitmap, clipped to the rows above the split */
    REG(VIC + 0x74) = 0; REG(VIC + 0x75) = 0; REG(VIC + 0x76) = 0x20; REG(VIC + 0x77) = 0;
    REG(VIC + 0x78) = 640 & 255; REG(VIC + 0x79) = 640 >> 8;      /* width */
    REG(VIC + 0x7A) = (uint8_t)toph; REG(VIC + 0x7B) = (uint8_t)(toph >> 8);   /* height */
    REG(VIC + 0x7E) = 640 & 255; REG(VIC + 0x7F) = 640 >> 8;      /* stride */
    REG(VIC + 0x80) = 6;                                           /* LINE */

    /* the text band: the console's last four rows, the only ones the list shows */
    REG(TERM + 0x0E) = 0;                                          /* no cursor */
    row(0); say("SPLIT -- a bitmap above, four rows of text below.");
    row(1); say("SHEILA holds the split: eight instructions a frame, no CPU at all.");
    row(3); say("Any key leaves.");

    /* SHEILA on: the list restarts at line 0 every frame */
    REG(VIC + 0x60) = (uint8_t)LIST; REG(VIC + 0x61) = (uint8_t)(LIST >> 8);
    REG(VIC + 0x62) = (uint8_t)(LIST >> 16); REG(VIC + 0x63) = (uint8_t)(LIST >> 24);
    REG(VIC + 0x64) = 1;

    t = frames();
    for (;;) {
        bounce(&x0, &a, 0, 639); bounce(&y0, &b, topb, toph - 1);
        bounce(&x1, &e, 0, 639); bounce(&y1, &f, topb, toph - 1);
        line(tx0[k], ty0[k], tx1[k], ty1[k], 0);                  /* the oldest, erased */
        if (++c == 0) c = 16;                                      /* 16-255: 0-15 are the text's */
        line(x0, y0, x1, y1, c);
        tx0[k] = x0; ty0[k] = y0; tx1[k] = x1; ty1[k] = y1;
        k = (k + 1) & (TRAIL - 1);
        n++;
        dt = frames() - t;
        if (dt >= 120) {
            row(2); say("C: "); num((unsigned)((unsigned long)n * 60 / dt)); say(" lines a second");
            t = frames(); n = 0;
        }
        if (rom_getin()) break;
    }

    REG(VIC + 0x64) = 0;                /* SHEILA off: nothing else will */
    REG(VIC + 0x20) = 0;
    REG(VIC + 0x10) = l0;
    REG(TERM + 4) = 2;
    rom_video();
    return 0;
}
