/* K4510: SPLIT -- graphics above, four lines of text below, the way an
 * Apple II or a C64 split its screen (Doc, 2026-09-13: "does vicky support
 * split screen, ala c64 or apple ii with graphics at top and 4 bottom lines
 * text?").
 *
 * The CPU does nothing to hold the split.  SHEILA, VICKY's display-list
 * coprocessor, runs six instructions every frame:
 *
 *   line 0     MOVE layer 0 (the console's text) off, layer 1 (a bitmap) on
 *   WAIT 416   26 text rows x 16 glass lines: the split
 *              MOVE layer 1 off, layer 0 on -- rows 26-29 are text
 *   END
 *
 * Meanwhile the program draws a ribbon of blitter lines into the top 208
 * bitmap rows (the console's 640x240 mode, each row shown twice) as fast as
 * it can, erasing the line sixteen behind, and prints in the text band how
 * many lines a second it managed.  EX/SPLIT.BAS in EhBASIC and SPLIT.BAS in
 * /LANG/MSBASIC do the same thing, to see what a BASIC makes of it.
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
#define TOPH    208                     /* bitmap rows above the split */
#define SPLITL  416                     /* the glass line of the split */
#define TRAIL   16

static uint8_t list[] = {
    2, 0x10, 0,    0,                   /* MOVE layer 0 control <- 0: text off */
    2, 0x20, 0x19, 0,                   /* MOVE layer 1 control <- on, bitmap, 8 bpp */
    1, SPLITL & 255, SPLITL >> 8, 0,    /* WAIT for line 416 */
    2, 0x20, 0,    0,                   /* bitmap off */
    2, 0x10, 7,    0,                   /* text on (the value is the console's, below) */
    0, 0,    0,    0                    /* END: again from the top next frame */
};

static int tx0[TRAIL], ty0[TRAIL], tx1[TRAIL], ty1[TRAIL];

static void raw(char c) { REG(TERM) = (uint8_t)c; }
static void say(const char *s) { while (*s) raw(*s++); }
static void num(unsigned v)
{
    char b[6]; uint8_t i = 5;
    b[5] = 0;
    do { b[--i] = (char)('0' + v % 10); v /= 10; } while (v);
    say(b + i);
}
static void row(uint8_t r)              /* to the start of console row r, cleared */
{
    raw(27); raw('['); num(r + 1); say(";1H"); raw(27); say("[K");
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

static void bounce(int *p, int *d, int hi)
{
    *p += *d;
    if (*p < 0 || *p > hi) { *d = -*d; *p += 2 * *d; }
}

int main(void)
{
    uint8_t l0, i, k = 0, c = 16;
    int x0 = 100, y0 = 20, x1 = 500, y1 = 180, a = 7, b = 3, e = -5, f = 4;
    unsigned t, n = 0, dt;

    l0 = REG(VIC + 0x10);
    list[18] = l0;
    dma_copy((uint32_t)(uint16_t)list, LIST, sizeof list);

    /* layer 1: a 640-wide 8 bpp bitmap at $200000, cleared above the split */
    for (i = 0x21; i <= 0x25; i++) REG(VIC + i) = 0;
    REG(VIC + 0x26) = 640 & 255; REG(VIC + 0x27) = 640 >> 8;
    REG(VIC + 0x28) = 0; REG(VIC + 0x29) = 0; REG(VIC + 0x2A) = 0x20; REG(VIC + 0x2B) = 0;
    dma_fill(0, BITMAP, 640UL * TOPH);

    /* the blitter: LINE into that bitmap, clipped to the rows above the split */
    REG(VIC + 0x74) = 0; REG(VIC + 0x75) = 0; REG(VIC + 0x76) = 0x20; REG(VIC + 0x77) = 0;
    REG(VIC + 0x78) = 640 & 255; REG(VIC + 0x79) = 640 >> 8;      /* width */
    REG(VIC + 0x7A) = TOPH; REG(VIC + 0x7B) = 0;                   /* height */
    REG(VIC + 0x7E) = 640 & 255; REG(VIC + 0x7F) = 640 >> 8;      /* stride */
    REG(VIC + 0x80) = 6;                                           /* LINE */

    /* the text band: console rows 26-29, the only rows the list shows */
    REG(TERM + 0x0E) = 0;                                          /* no cursor */
    row(26); say("SPLIT -- a 640x208 bitmap above, four rows of text below.");
    row(27); say("SHEILA holds the split: six instructions a frame, no CPU at all.");
    row(29); say("Any key leaves.");

    /* SHEILA on: the list restarts at line 0 every frame */
    REG(VIC + 0x60) = (uint8_t)LIST; REG(VIC + 0x61) = (uint8_t)(LIST >> 8);
    REG(VIC + 0x62) = (uint8_t)(LIST >> 16); REG(VIC + 0x63) = (uint8_t)(LIST >> 24);
    REG(VIC + 0x64) = 1;

    t = frames();
    for (;;) {
        bounce(&x0, &a, 639); bounce(&y0, &b, TOPH - 1);
        bounce(&x1, &e, 639); bounce(&y1, &f, TOPH - 1);
        line(tx0[k], ty0[k], tx1[k], ty1[k], 0);                  /* the oldest, erased */
        if (++c == 0) c = 16;                                      /* 16-255: 0-15 are the text's */
        line(x0, y0, x1, y1, c);
        tx0[k] = x0; ty0[k] = y0; tx1[k] = x1; ty1[k] = y1;
        k = (k + 1) & (TRAIL - 1);
        n++;
        dt = frames() - t;
        if (dt >= 120) {
            row(28); say("C: "); num((unsigned)((unsigned long)n * 60 / dt)); say(" lines a second");
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
