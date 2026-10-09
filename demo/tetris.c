/* K4510: TETRIS -- the falling blocks, in the Personality Chooser's look.
 *
 * A ten-by-twenty well, the next piece, score, level and lines, and the five
 * best scores kept in /APPS/TETRIS/HISCORE.DAT.  Korobeiniki plays on the
 * OPL2 (MELODY), a little faster as the levels go up.  The pieces come out of
 * a shuffled bag of all seven, turn the modern way (nudged off a wall or the
 * floor when they would not fit), and wait half a second on the floor before
 * they lock, so a piece can still be slid under a ledge.
 *
 *   Left Right  move           Up X   turn            Z       turn back
 *   Down        soft drop      Space  hard drop       P       pause
 *   M           music on/off                          Escape  end the game
 *
 * The screen, since 2026-10-09, is CHESS's and the Chooser's (Doc: "I very
 * much like the Chess game ... Please redo Tetris ... in a similar style"):
 * 320x240 doubled, grey bands with the time and the keys, the blue glass,
 * the banner's bars, unscii drawn into a bitmap through the blitter, and
 * chunky bevelled 10 px blocks blitted from tiles built at the start -- only
 * the cells that changed are drawn.  The title, the pause and the end of a
 * game are the Chooser's list.  The bitmap is layer 1 with layer 0 switched
 * off, as BREAKOUT does, so the console is left as it was and comes back
 * when the program ends.
 */
#include "k4510.h"

#define TERM     0xDA00u
#define OPL_ADDR 0xD480u
#define OPL_DATA 0xD481u
#define OPL_ID   0xD482u

static void rom_video(void) { ((void (*)(void))0xFF92)(); }
static unsigned char rom_save(void) { return ((unsigned char (*)(void))0xFF8C)(); }
static unsigned char rom_load(void) { return ((unsigned char (*)(void))0xFF89)(); }
static void zp16(uint8_t a, uint16_t v) { REG(a) = v; REG(a + 1) = v >> 8; }
static void zp32(uint8_t a, uint32_t v) { REG(a) = v; REG(a + 1) = v >> 8; REG(a + 2) = v >> 16; REG(a + 3) = v >> 24; }

enum { BLACK, WHITE, RED, CYAN, PURPLE, GREEN, BLUE, YELLOW, ORANGE, BROWN, LRED, DGREY, GREY, LGREEN, LBLUE, LGREY };
#define K_UP    0x80
#define K_DOWN  0x81
#define K_LEFT  0x82
#define K_RIGHT 0x83
#define K_ESC   0x1B

/* ---- the pieces --------------------------------------------------------- */
/* I J L O S T Z, four turns each, as 4x4 masks (bit 15 the top-left cell);
 * the turns are the modern standard's, about the same centre. */
static const uint16_t shape[7][4] = {
    { 0x0F00, 0x2222, 0x00F0, 0x4444 },
    { 0x8E00, 0x6440, 0x0E20, 0x44C0 },
    { 0x2E00, 0x4460, 0x0E80, 0xC440 },
    { 0x6600, 0x6600, 0x6600, 0x6600 },
    { 0x6C00, 0x4620, 0x06C0, 0x8C40 },
    { 0x4E00, 0x4640, 0x0E40, 0x4C40 },
    { 0xC600, 0x2640, 0x0C60, 0x4C80 } };
static const uint8_t pcol[8] = { BLACK, CYAN, LBLUE, ORANGE, YELLOW, GREEN, PURPLE, RED };
static const uint8_t gravity[20] = { 48, 43, 38, 33, 28, 23, 18, 13, 8, 6, 5, 5, 5, 4, 4, 4, 3, 3, 3, 2 };
static const uint16_t points[5] = { 0, 40, 100, 300, 1200 };

static uint8_t bd[20][10];                   /* 0 empty, 1-7 a piece's colour, 8 a full row flashing */
static uint8_t cur, rot, nxt, active, over, paused, dirty, clr_t, lockt, resets, start, level;
static int8_t px, py;
static uint32_t score;
static uint16_t lines;

/* ---- the best five ------------------------------------------------------ */
static char hname[] = "/APPS/TETRIS/HISCORE.DAT";
static uint32_t hs_s[5];
static char hs_n[5][4];
static uint8_t hbuf[41];
static void hs_load(void)
{
    uint8_t i, j;
    for (i = 0; i < 5; i++) { hs_s[i] = 0; strcpy(hs_n[i], "---"); }
    zp16(0xF0, (uint16_t) hname); zp32(0xF2, (uint32_t)(uint16_t) hbuf);
    if (rom_load() || hbuf[40] != 'T') return;
    for (i = 0; i < 5; i++) {
        uint8_t *b = hbuf + i * 8;
        hs_s[i] = (uint32_t) b[0] | ((uint32_t) b[1] << 8) | ((uint32_t) b[2] << 16) | ((uint32_t) b[3] << 24);
        for (j = 0; j < 3; j++) hs_n[i][j] = (char) b[4 + j];
        hs_n[i][3] = 0;
    }
}
static void hs_save(void)
{
    uint8_t i, j;
    for (i = 0; i < 5; i++) {
        uint8_t *b = hbuf + i * 8;
        b[0] = (uint8_t) hs_s[i]; b[1] = (uint8_t)(hs_s[i] >> 8); b[2] = (uint8_t)(hs_s[i] >> 16); b[3] = (uint8_t)(hs_s[i] >> 24);
        for (j = 0; j < 3; j++) b[4 + j] = (uint8_t) hs_n[i][j];
        b[7] = 0;
    }
    hbuf[40] = 'T';
    zp16(0xF0, (uint16_t) hname); zp32(0xF2, (uint32_t)(uint16_t) hbuf); zp32(0xF6, 41);
    rom_save();
}

/* ---- the screen: the Chooser's look ------------------------------------ */
#define SW 320
#define BAND 12                                       /* a band: 8-pixel text with 2 above and below */
#define GY BAND                                       /* the glass: lines 12..227 */
#define BBOT (240 - BAND)
#define BMP 0x00200000UL                              /* 320x240, 8 bpp, layer 1 */
#define CELL 10
#define WXP 24                                        /* the well's inside */
#define WYP 18
#define PXP 140                                       /* the panel */
#define F8 0
#define F16 1
#define F8X2 2
#define FONT8P  0x00010000UL                          /* unscii-8 and -16, where the frontend puts them */
#define FONT16P 0x00010800UL
static uint8_t f8[96 * 8], f16[96 * 16];              /* ' '..DEL, copied near once */
static uint8_t gbuf[256];
static uint8_t tiles[16][CELL * CELL];                /* 0 empty, 1-7 the pieces, 8 a row going, 9-15 the shadows */
static uint8_t shown[20][10];                         /* the tile each cell of the well shows: only a change is drawn */
static uint8_t newest = 0xFF;                         /* the best-five row just written: the Chooser's bar */
static void rect(uint16_t x, uint8_t y, uint16_t w, uint8_t h, uint8_t c)
{
    uint32_t p = BMP + (uint32_t)y * SW + x;
    while (h--) { dma_fill(c, p, w); p += SW; }
}
static void blit_from(const uint8_t *src, uint16_t x, uint8_t y, uint8_t w, uint8_t h)   /* near pixels, w x h, to the screen */
{
    w32(VICKY + 0x70, (uint32_t)(uint16_t)src); w32(VICKY + 0x74, BMP + (uint32_t)y * SW + x);
    w16(VICKY + 0x78, w); w16(VICKY + 0x7A, h); w16(VICKY + 0x7C, w); w16(VICKY + 0x7E, SW);
    REG(VICKY + 0x80) = 0; REG(VICKY + 0x81) = 0; REG(VICKY + 0x82) = 1;
}
static void fonts_near(void)
{
    uint16_t i;
    for (i = 0; i < sizeof f8; i++) f8[i] = far_peek(FONT8P + 256 + i);
    for (i = 0; i < sizeof f16; i++) f16[i] = far_peek(FONT16P + 512 + i);
}
static uint16_t text(uint16_t x, uint8_t y, const char *s, uint8_t fg, uint8_t bg, uint8_t font)   /* the x after it */
{
    uint8_t c, r, b, bits, *g, h = font == F8 ? 8 : 16, w = font == F8X2 ? 16 : 8;
    const uint8_t *src;
    for (; *s; s++, x += w) {
        c = (uint8_t)*s; if (c < 32 || c > 127) c = '?';
        if (font == F16) src = f16 + (c - 32) * 16; else src = f8 + (c - 32) * 8;
        g = gbuf;
        for (r = 0; r < h; r++) {
            bits = font == F8X2 ? src[r >> 1] : src[r];
            if (font == F8X2) { for (b = 0x80; b; b >>= 1) { *g++ = (bits & b) ? fg : bg; *g++ = (bits & b) ? fg : bg; } }
            else for (b = 0x80; b; b >>= 1) *g++ = (bits & b) ? fg : bg;
        }
        blit_from(gbuf, x, y, w, h);
    }
    return x;
}
static char nb[12];
static const char *fmt(uint32_t v, uint8_t w)         /* v right-aligned in w places */
{
    uint8_t i = 10;
    nb[10] = 0;
    do { nb[--i] = (char)('0' + (uint8_t)(v % 10)); v /= 10; } while (v && i);
    while (i > 10 - w) nb[--i] = ' ';
    return nb + i;
}
static void band_text(uint8_t y, const char *s)        /* centred in a band, black on grey */
{
    uint8_t n = (uint8_t) strlen(s);
    rect(0, y, SW, BAND, LGREY);
    text((uint16_t)((SW - n * 8) / 2), (uint8_t)(y + 2), s, BLACK, LGREY, F8);
}
static uint8_t clock_shown = 0xFF, rtc_read, frame_seen;
static void rtc_latch(void) { rtc_read = REG(SYS + 4); }   /* a store, not a (void) read: cc65 drops that */
static void put2(char *b, uint8_t v) { b[0] = (char)('0' + v / 10); b[1] = (char)('0' + v % 10); }
static void draw_top(void)                            /* K4510 TETRIS on the left, the time on the right, as the Chooser's band */
{
    char b[17]; uint16_t y;
    rtc_latch();
    clock_shown = REG(SYS + 6);
    put2(b, REG(SYS + 7)); b[2] = ':'; put2(b + 3, REG(SYS + 6)); b[5] = ' ';
    put2(b + 6, REG(SYS + 8)); b[8] = '.'; put2(b + 9, REG(SYS + 9)); b[11] = '.';
    y = REG(SYS + 0x0A) | (REG(SYS + 0x0B) << 8);
    b[12] = (char)('0' + (y / 1000) % 10); b[13] = (char)('0' + (y / 100) % 10); b[14] = (char)('0' + (y / 10) % 10); b[15] = (char)('0' + y % 10); b[16] = 0;
    rect(0, 0, SW, BAND, LGREY);
    text(8, 2, "K4510 TETRIS", BLACK, LGREY, F8);
    text(SW - 8 - 16 * 8, 2, b, BLACK, LGREY, F8);
}
static void tick_top(void)                            /* once a frame at most: each read of the clock asks the host */
{
    uint8_t f = REG(SYS + 0x0D);
    if (f == frame_seen) return;
    frame_seen = f; rtc_latch();
    if (REG(SYS + 6) != clock_shown) draw_top();
}
static void bars(uint16_t x, uint8_t y, uint8_t h, uint8_t w)   /* the banner's five bars */
{
    static const uint8_t col[5] = { RED, ORANGE, YELLOW, GREEN, LBLUE };
    static const uint8_t len[5] = { 8, 6, 4, 6, 8 };
    uint8_t i;
    for (i = 0; i < 5; i++) rect(x, (uint8_t)(y + i * h), (uint16_t)(len[i] * w), h, col[i]);
}
static void glass(void) { rect(0, GY, SW, BBOT - GY, BLUE); }
static void header(const char *sub, uint8_t subcol)   /* a full-glass screen: the bars, Tetris, a line under it */
{
    glass();
    bars(12, GY + 8, 5, 6);
    text(72, GY + 8, "Tetris", WHITE, BLUE, F8X2);
    text(72, GY + 28, sub, subcol, BLUE, F8);
}

/* The blocks: chunky and bevelled, a light edge top and left, a dark one
 * bottom and right, in the VIC colours; the shadow is the piece's colour
 * as a frame; a row going is white. */
static const uint8_t plite[8] = { BLACK, WHITE, CYAN, YELLOW, WHITE, LGREEN, LRED, LRED };
static const uint8_t pdark[8] = { BLACK, LBLUE, BLUE, BROWN, ORANGE, DGREY, BLUE, BROWN };
static void tile_fill(uint8_t *t, uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t c)
{
    uint8_t i, j;
    for (j = y; j < y + h; j++) for (i = x; i < x + w; i++) t[j * CELL + i] = c;
}
static void make_tiles(void)
{
    uint8_t k, c, l, d, *t;
    for (k = 0; k < 16; k++) {
        t = tiles[k]; memset(t, BLACK, CELL * CELL);
        if (k == 0) t[4 * CELL + 4] = DGREY;                               /* the well's dot grid */
        else if (k <= 8) {
            c = k == 8 ? WHITE : pcol[k]; l = k == 8 ? WHITE : plite[k]; d = k == 8 ? LGREY : pdark[k];
            tile_fill(t, 0, 0, CELL, CELL, d);
            tile_fill(t, 0, 0, CELL - 1, CELL - 1, l);
            tile_fill(t, 1, 1, CELL - 2, CELL - 2, c);
        } else {
            tile_fill(t, 1, 1, CELL - 2, CELL - 2, pcol[k - 8]);
            tile_fill(t, 2, 2, CELL - 4, CELL - 4, BLACK);
        }
    }
}
static void cell(uint16_t x, uint8_t y, uint8_t k) { blit_from(tiles[k], x, y, CELL, CELL); }

/* the best five, as the Chooser's list: a number box, the initials, the score */
static void draw_best(uint16_t x, uint8_t y)
{
    uint8_t i, on; char d[2];
    for (i = 0; i < 5; i++, y += 16) {
        on = i == newest;
        rect(x - 2, y - 2, 168, 16, on ? LBLUE : BLUE);
        rect(x, y, 12, 12, on ? BLUE : LBLUE);
        d[0] = (char)('1' + i); d[1] = 0; text(x + 2, y + 2, d, on ? LBLUE : BLUE, on ? BLUE : LBLUE, F8);
        text(x + 20, y + 2, hs_n[i], on ? BLUE : YELLOW, on ? LBLUE : BLUE, F8);
        { const char *v = "        -";                  /* an if, not a ?: -- cc65 will not mix the two pointer types */
          if (hs_s[i]) v = fmt(hs_s[i], 9);
          text(x + 56, y + 2, v, on ? BLUE : WHITE, on ? LBLUE : BLUE, F8); }
    }
}

/* the play screen's still parts: the well's frame, the panel's labels */
static void draw_frame(void)
{
    glass();
    rect(WXP - 2, WYP - 2, 10 * CELL + 4, 20 * CELL + 4, LBLUE);
    rect(WXP, WYP, 10 * CELL, 20 * CELL, BLACK);
    memset(shown, 0xFF, sizeof shown);
    bars(PXP, WYP, 3, 3);
    text(PXP + 32, WYP - 1, "Tetris", WHITE, BLUE, F8X2);
    text(PXP, WYP + 24, "NEXT", LBLUE, BLUE, F8);
    text(PXP + 60, WYP + 24, "SCORE", LBLUE, BLUE, F8);
    text(PXP, WYP + 70, "LEVEL", LBLUE, BLUE, F8);
    text(PXP + 60, WYP + 70, "LINES", LBLUE, BLUE, F8);
    text(PXP, WYP + 106, "BEST", WHITE, BLUE, F8);
    band_text(BBOT, "X/Up turn  Z back  SPACE drop  P pause");
}

static uint8_t ov[20][10];                   /* the falling piece and its shadow, over the board */
static void stamp(int8_t y, uint8_t v)
{
    uint16_t m = shape[cur][rot], bit = 0x8000u;
    uint8_t i; int8_t cy;
    for (i = 0; i < 16; i++, bit >>= 1)
        if (m & bit) { cy = (int8_t)(y + (i >> 2)); if (cy >= 0) ov[cy][px + (i & 3)] = v; }
}
static uint8_t fits(uint8_t p, uint8_t r, int8_t x, int8_t y)
{
    uint16_t m = shape[p][r], bit = 0x8000u;
    uint8_t i; int8_t cx, cy;
    for (i = 0; i < 16; i++, bit >>= 1) {
        if (!(m & bit)) continue;
        cx = (int8_t)(x + (i & 3)); cy = (int8_t)(y + (i >> 2));
        if (cx < 0 || cx >= 10 || cy >= 20) return 0;
        if (cy >= 0 && bd[cy][cx]) return 0;
    }
    return 1;
}

static void draw_well(void)                  /* only the cells whose tile changed */
{
    uint8_t r, c, v, k; int8_t gy;
    memset(ov, 0, sizeof ov);
    if (active) {
        gy = py; while (fits(cur, rot, px, (int8_t)(gy + 1))) gy++;
        stamp(gy, 9); stamp(py, (uint8_t)(cur + 1));
    }
    for (r = 0; r < 20; r++)
        for (c = 0; c < 10; c++) {
            v = ov[r][c]; if (!v) v = bd[r][c];
            k = v == 9 ? (uint8_t)(9 + cur) : v;                       /* the shadow: tile 9-15 by the piece */
            if (shown[r][c] != k) { shown[r][c] = k; cell(WXP + c * CELL, (uint8_t)(WYP + r * CELL), k); }
        }
}
static void draw_panel(void)
{
    uint8_t dy, dx;
    uint16_t m = nxt < 7 ? shape[nxt][0] : 0;
    rect(PXP, WYP + 34, 48, 28, BLACK);
    for (dy = 0; dy < 2; dy++)
        for (dx = 0; dx < 4; dx++)
            if (m & (0x8000u >> (dy * 4 + dx))) cell(PXP + 4 + dx * CELL, (uint8_t)(WYP + 38 + dy * CELL), (uint8_t)(nxt + 1));
    text(PXP + 60, WYP + 34, fmt(score, 10), YELLOW, BLUE, F16);
    text(PXP, WYP + 80, fmt(level, 3), YELLOW, BLUE, F16);
    text(PXP + 60, WYP + 80, fmt(lines, 5), YELLOW, BLUE, F16);
    draw_best(PXP + 2, WYP + 118);
}

/* ---- the sound ---------------------------------------------------------- */
static const uint8_t opslot[9] = { 0, 1, 2, 8, 9, 10, 16, 17, 18 };
static const uint16_t fnum[12] = { 345, 365, 387, 410, 434, 460, 488, 517, 547, 580, 614, 651 };
#define N(o, s) ((o) * 12 + (s))
enum { C, Cs, D, Ds, E, F, Fs, G, Gs, A, As, B };
static uint8_t opl_ok, music_on = 1, sfx_t[4];

static void opl(uint8_t reg, uint8_t val) { REG(OPL_ADDR) = reg; REG(OPL_DATA) = val; }
static void patch(uint8_t ch, uint8_t mmul, uint8_t mlvl, uint8_t mad, uint8_t msr, uint8_t mwave,
                  uint8_t cmul, uint8_t clvl, uint8_t cad, uint8_t csr, uint8_t cwave, uint8_t conn)
{
    uint8_t m = opslot[ch], c = (uint8_t)(m + 3);
    opl((uint8_t)(0x20 + m), mmul); opl((uint8_t)(0x40 + m), mlvl);
    opl((uint8_t)(0x60 + m), mad);  opl((uint8_t)(0x80 + m), msr);  opl((uint8_t)(0xE0 + m), mwave);
    opl((uint8_t)(0x20 + c), cmul); opl((uint8_t)(0x40 + c), clvl);
    opl((uint8_t)(0x60 + c), cad);  opl((uint8_t)(0x80 + c), csr);  opl((uint8_t)(0xE0 + c), cwave);
    opl((uint8_t)(0xC0 + ch), conn);
}
static void keyoff(uint8_t ch) { if (opl_ok) opl((uint8_t)(0xB0 + ch), 0); }
static void play(uint8_t ch, uint8_t note)
{
    uint16_t f = fnum[note % 12];
    if (!opl_ok) return;
    keyoff(ch);
    opl((uint8_t)(0xA0 + ch), (uint8_t) f);
    opl((uint8_t)(0xB0 + ch), (uint8_t)(0x20 | ((note / 12) << 2) | ((f >> 8) & 3)));
}
static void sfx(uint8_t ch, uint8_t note, uint8_t frames) { play(ch, note); sfx_t[ch] = frames; }
static void sound_init(void)
{
    uint8_t v;
    opl_ok = REG(OPL_ID) == 0x02;
    if (!opl_ok) return;
    for (v = 0; v < 0xF6; v++) opl(v, 0);
    opl(0x01, 0x20); opl(0x08, 0x00); opl(0xBD, 0x00);
    /*      ch   modulator: mul lvl a/d s/r wave      carrier: mul lvl a/d s/r wave   conn */
    patch(0, 0x21, 0x1C, 0xF4, 0x26, 0x01,   0x21, 0x06, 0xF3, 0x36, 0x00, 0x08);   /* the tune: a reedy lead */
    patch(1, 0x01, 0x1A, 0xF2, 0x53, 0x00,   0x01, 0x04, 0xF3, 0x53, 0x00, 0x0A);   /* the bass */
    patch(2, 0x11, 0x28, 0xF8, 0x88, 0x00,   0x01, 0x00, 0xF8, 0x69, 0x00, 0x04);   /* a piece locks: a pluck */
    patch(3, 0x31, 0x1E, 0xF6, 0x27, 0x02,   0x11, 0x00, 0xF4, 0x37, 0x00, 0x06);   /* rows go: a bell */
}
static void sound_off(void)
{
    uint8_t v;
    if (!opl_ok) return;
    for (v = 0; v < 9; v++) keyoff(v);
    for (v = 0; v < 0xF6; v++) opl(v, 0);
}

/* Korobeiniki, the eight bars everyone knows: note, length in eighths. */
static const uint8_t tune[] = {
    N(5,E),2, N(4,B),1, N(5,C),1, N(5,D),2, N(5,C),1, N(4,B),1,
    N(4,A),2, N(4,A),1, N(5,C),1, N(5,E),2, N(5,D),1, N(5,C),1,
    N(4,B),3, N(5,C),1, N(5,D),2, N(5,E),2,
    N(5,C),2, N(4,A),2, N(4,A),2, 0,2,
    N(5,D),3, N(5,F),1, N(5,A),2, N(5,G),1, N(5,F),1,
    N(5,E),3, N(5,C),1, N(5,E),2, N(5,D),1, N(5,C),1,
    N(4,B),2, N(4,B),1, N(5,C),1, N(5,D),2, N(5,E),2,
    N(5,C),2, N(4,A),2, N(4,A),2, 0,2,
    0, 0 };
static const uint8_t broot[8] = { N(2,E), N(2,A), N(2,E), N(2,A), N(2,D), N(2,C), N(2,E), N(2,A) };
static uint8_t mpos, mleft, mframe, meighth;

static void music_reset(void) { mpos = 0; mleft = 0; mframe = 0; meighth = 0; }
static void music_tick(void)
{
    uint8_t n, b;
    if (!music_on || !opl_ok) return;
    if (mframe) { mframe--; return; }
    mframe = (uint8_t)(level < 3 ? 9 : level < 6 ? 8 : level < 9 ? 7 : 6);
    if (!mleft) {
        n = tune[mpos]; mleft = tune[mpos + 1]; mpos += 2;
        if (!tune[mpos + 1]) mpos = 0;
        if (n) play(0, n); else keyoff(0);
    }
    mleft--;
    b = broot[(meighth >> 3) & 7];
    play(1, (uint8_t)(meighth & 1 ? b + 12 : b));
    meighth = (uint8_t)((meighth + 1) & 63);
}
static void music_stop(void) { keyoff(0); keyoff(1); }
static void sfx_tick(void)
{
    uint8_t i;
    for (i = 2; i < 4; i++) if (sfx_t[i] && !--sfx_t[i]) keyoff(i);
}

/* ---- the game ----------------------------------------------------------- */
static uint16_t seed = 1;
static uint8_t rnd(void) { seed = seed * 25173u + 13849u; return (uint8_t)(seed >> 8); }
static uint8_t bag[7], bagn = 7;
static uint8_t bag_next(void)
{
    uint8_t i, j, t;
    if (bagn >= 7) {
        for (i = 0; i < 7; i++) bag[i] = i;
        for (i = 6; i > 0; i--) { j = (uint8_t)(rnd() % (i + 1)); t = bag[i]; bag[i] = bag[j]; bag[j] = t; }
        bagn = 0;
    }
    return bag[bagn++];
}
static void spawn(void)
{
    cur = nxt; nxt = bag_next(); rot = 0;
    px = 3; py = (int8_t)(cur == 0 ? -1 : 0);
    lockt = 0; resets = 0; active = 1; dirty = 3;
    if (!fits(cur, rot, px, py)) { over = 1; active = 0; }
}
static void touched(void)                    /* a move or a turn on the floor buys time, fifteen times a piece */
{
    if (lockt && resets < 15) { lockt = 0; resets++; }
    dirty |= 1;
}
static uint8_t move(int8_t dx, int8_t dy)
{
    if (!fits(cur, rot, (int8_t)(px + dx), (int8_t)(py + dy))) return 0;
    px = (int8_t)(px + dx); py = (int8_t)(py + dy);
    touched();
    return 1;
}
static void turn(uint8_t d)                  /* d 1 clockwise, 3 back */
{
    static const int8_t kick[5] = { 0, -1, 1, -2, 2 };
    uint8_t nr = (uint8_t)((rot + d) & 3), k;
    int8_t up;
    for (up = 0; up >= -1; up--)
        for (k = 0; k < 5; k++)
            if (fits(cur, nr, (int8_t)(px + kick[k]), (int8_t)(py + up))) {
                rot = nr; px = (int8_t)(px + kick[k]); py = (int8_t)(py + up);
                touched();
                return;
            }
}
static void lock(void)
{
    uint16_t m = shape[cur][rot], bit = 0x8000u;
    uint8_t i, r, c, n = 0; int8_t cy;
    for (i = 0; i < 16; i++, bit >>= 1)
        if (m & bit) { cy = (int8_t)(py + (i >> 2)); if (cy < 0) over = 1; else bd[cy][px + (i & 3)] = (uint8_t)(cur + 1); }
    active = 0; dirty = 3;
    sfx(2, N(3,G), 5);
    for (r = 0; r < 20; r++) {
        for (c = 0; c < 10 && bd[r][c]; c++) ;
        if (c == 10) { memset(bd[r], 8, 10); n++; }
    }
    if (n) {
        score += (uint32_t) points[n] * (level + 1);
        lines += n;
        if (start + lines / 10 > level) level = (uint8_t)(start + lines / 10);
        clr_t = 18;
        sfx(3, (uint8_t)(N(6,C) + (n - 1) * 4), 30);
    } else if (!over) spawn();
}
static void collapse(void)                   /* the flashed rows go, and the rest come down */
{
    int8_t r, w = 19;
    for (r = 19; r >= 0; r--) if (bd[r][0] != 8) { if (w != r) memcpy(bd[w], bd[r], 10); w--; }
    for (; w >= 0; w--) memset(bd[w], 0, 10);
}

static uint8_t held_was, das, softc, grav;
static void pause_screen(void);
static void frame(void)
{
    uint8_t k, held, edge;
    while ((k = key_get()) != 0) {
        if (k == K_ESC) { over = 1; active = 0; return; }
        if (k == 'p' || k == 'P') { paused = 1; return; }       /* the pause screen, from play_game */
        if (k == 'm' || k == 'M') { music_on ^= 1; if (!music_on) music_stop(); continue; }
        if (paused || !active) continue;
        if (k == 'x' || k == 'X') turn(1);
        else if (k == 'z' || k == 'Z') turn(3);
        else if (k == ' ') { while (move(0, 1)) score += 2; lock(); return; }
    }
    if (paused) return;
    music_tick(); sfx_tick();
    if (clr_t) { if (--clr_t == 0) { collapse(); spawn(); } return; }
    if (!active) return;

    held = keys_held(); edge = (uint8_t)(held & ~held_was); held_was = held;
    if (edge & HELD_UP) turn(1);
    if (edge & HELD_LEFT) { move(-1, 0); das = 0; }
    else if (edge & HELD_RIGHT) { move(1, 0); das = 0; }
    else if (held & (HELD_LEFT | HELD_RIGHT)) {
        if (++das >= 12 && !(das & 1)) move((int8_t)(held & HELD_LEFT ? -1 : 1), 0);
        if (das > 200) das = 12;
    } else das = 0;
    if (held & HELD_DOWN) {
        if (!(++softc & 1) && move(0, 1)) { score++; grav = 0; dirty |= 2; }
    }
    if (++grav >= gravity[level < 19 ? level : 19]) { grav = 0; move(0, 1); }
    if (!fits(cur, rot, px, (int8_t)(py + 1))) { if (++lockt >= 30) lock(); }
    else lockt = 0;
}

static void play_game(void)
{
    uint8_t lfc, fc, d;
    memset(bd, 0, sizeof bd);
    score = 0; lines = 0; level = start; over = 0; paused = 0; clr_t = 0;
    held_was = keys_held(); das = 0; softc = 0; grav = 0;
    bagn = 7; nxt = bag_next(); spawn();
    music_reset();
    lfc = REG(SYS + 0x0D);
    while (!over) {
        fc = REG(SYS + 0x0D);
        if (fc == lfc) continue;
        d = (uint8_t)(fc - lfc); lfc = fc;
        if (d > 4) d = 4;
        while (d-- && !over && !paused) frame();
        if (paused) { pause_screen(); lfc = REG(SYS + 0x0D); continue; }
        if (dirty) {
            draw_well();
            if (dirty & 2) draw_panel();
            dirty = 0;
        }
        tick_top();
    }
    music_stop();
}


/* ---- the Chooser's list: the title, the pause, the end of a game -------- */
#define LIST_X 12
#define ROW_H 20
#define BEST_Y 140                                    /* the best five under a list, the keys beside them */
#define KEYS_X 196
#define GO_BEST_Y 146                                 /* at the end of a game: under its list */
static void list_row(uint8_t y0, uint8_t i, const char *s, const char *right, uint8_t on)
{
    uint8_t y = (uint8_t)(y0 + i * ROW_H); char d[2];
    rect(LIST_X, y, SW - 2 * LIST_X, ROW_H - 2, on ? LBLUE : BLUE);
    rect(LIST_X + 6, y + 3, 12, 12, on ? BLUE : LBLUE);   /* the number box */
    d[0] = (char)('1' + i); d[1] = 0; text(LIST_X + 8, y + 5, d, on ? LBLUE : BLUE, on ? BLUE : LBLUE, F8);
    text(LIST_X + 28, y + 1, s, on ? BLUE : YELLOW, on ? LBLUE : BLUE, F16);
    if (right) text((uint16_t)(SW - LIST_X - 8 - strlen(right) * 8), y + 1, right, on ? BLUE : WHITE, on ? LBLUE : BLUE, F16);
}
static uint8_t wait_key(void)
{
    uint8_t k;
    while ((k = key_get()) == 0) { seed++; sfx_tick(); tick_top(); }
    return k;
}
/* a list of n rows at y0: 1-n, the arrows and RETURN; 255 for ESC.  `extra`
 * gets the keys the list does not know (SPACE, < >, M, P), and its answer,
 * if not 254, is the list's. */
static uint8_t list_pick(uint8_t y0, const char *const *items, const char *const *right, uint8_t n, uint8_t cur, uint8_t (*extra)(uint8_t k, uint8_t cur))
{
    uint8_t i, k, r;
    for (i = 0; i < n; i++) list_row(y0, i, items[i], right ? right[i] : 0, i == cur);
    for (;;) {
        k = wait_key();
        if (k == K_UP || k == K_DOWN) {
            list_row(y0, cur, items[cur], right ? right[cur] : 0, 0);
            cur = (uint8_t)(k == K_UP ? (cur + n - 1) % n : (cur + 1) % n);
            list_row(y0, cur, items[cur], right ? right[cur] : 0, 1);
            continue;
        }
        if (k == 13) return cur;
        if (k >= '1' && k < '1' + n) return (uint8_t)(k - '1');
        if (k == K_ESC) return 255;
        if (extra && (r = extra(k, cur)) != 254) return r;
        if (extra) for (i = 0; i < n; i++) list_row(y0, i, items[i], right ? right[i] : 0, i == cur);   /* it may have changed a row */
    }
}

static char lv[] = "<  0  >";
static const char *const T_ITEMS[3] = { "Play", "Start level", "Quit" };
static const char *t_right[3] = { 0, lv, 0 };
static uint8_t title_key(uint8_t k, uint8_t cur)
{
    (void) cur;
    if (k == ' ') return 0;
    if (k == K_LEFT && start > 0) start--;
    if (k == K_RIGHT && start < 9) start++;
    lv[3] = (char)('0' + start);
    return 254;
}
static uint8_t title(void)
{
    uint8_t r;
    header("Falling blocks, on the K4510", YELLOW);
    text(LIST_X, BEST_Y - 12, "BEST", WHITE, BLUE, F8);
    draw_best(LIST_X + 2, BEST_Y);
    text(KEYS_X, BEST_Y - 12, "THE KEYS", WHITE, BLUE, F8);
    text(KEYS_X, BEST_Y, "<> move", LBLUE, BLUE, F8);
    text(KEYS_X, BEST_Y + 11, "X or Up turn", LBLUE, BLUE, F8);
    text(KEYS_X, BEST_Y + 22, "Z turn back", LBLUE, BLUE, F8);
    text(KEYS_X, BEST_Y + 33, "Down soft drop", LBLUE, BLUE, F8);
    text(KEYS_X, BEST_Y + 44, "SPACE drop", LBLUE, BLUE, F8);
    text(KEYS_X, BEST_Y + 55, "P pause", LBLUE, BLUE, F8);
    text(KEYS_X, BEST_Y + 66, "M music", LBLUE, BLUE, F8);
    text(KEYS_X, BEST_Y + 77, "ESC end game", LBLUE, BLUE, F8);
    band_text(BBOT, "SPACE plays, < > the level, ESC quits");
    lv[3] = (char)('0' + start);
    for (;;) {
        r = list_pick(GY + 46, T_ITEMS, t_right, 3, 0, title_key);
        if (r == 0) return 1;
        if (r == 255 || r == 2) return 0;
        if (r == 1) { if (start < 9) start++; else start = 0; lv[3] = (char)('0' + start); }   /* RETURN on the level: the next one */
    }
}

/* P: the pause, as a list -- the well is hidden while it lasts */
static char mus[] = "On ";
static const char *const P_ITEMS[3] = { "Resume", "Music", "End the game" };
static const char *p_right[3] = { "P", mus, "ESC" };
static uint8_t pause_key(uint8_t k, uint8_t cur)
{
    (void) cur;
    if (k == 'p' || k == 'P' || k == ' ') return 0;
    if (k == 'm' || k == 'M') return 1;
    return 254;
}
static void pause_screen(void)
{
    uint8_t r, cur = 0;
    music_stop();
    header("Paused", YELLOW);
    band_text(BBOT, "P resumes, M the music, ESC ends");
    for (;;) {
        strcpy(mus, music_on ? "On " : "Off");
        r = list_pick(GY + 46, P_ITEMS, p_right, 3, cur, pause_key);
        if (r == 1) { music_on ^= 1; if (!music_on) music_stop(); cur = 1; continue; }
        if (r == 255 || r == 2) { over = 1; active = 0; }
        break;
    }
    paused = 0;
    draw_frame(); dirty = 3;
    held_was = keys_held();
}

static uint8_t game_over(void)
{
    static char ini[4];
    static const char *const G_ITEMS[2] = { "Play again", "Quit" };
    uint8_t k, i, j, pos = 0, r;
    draw_well(); draw_panel();
    for (k = 0; k < 60; k++) wait_vblank();  /* a second to see how it ended */
    while (key_get()) ;
    header("Game over", LRED);
    text(LIST_X, GY + 40, "SCORE", LBLUE, BLUE, F8);
    text(LIST_X + 48, GY + 36, fmt(score, 10), WHITE, BLUE, F16);
    for (i = 0; i < 5 && hs_s[i] >= score; i++) ;
    newest = 0xFF;
    if (i < 5 && score) {
        text(LIST_X, GY + 58, "A new best score!  Your initials:", YELLOW, BLUE, F8);
        band_text(BBOT, "Type your initials, then RETURN");
        strcpy(ini, "___");
        for (;;) {
            text(LIST_X + 34 * 8, GY + 54, ini, WHITE, BLUE, F16);
            k = wait_key();
            if (k >= 'a' && k <= 'z') k = (uint8_t)(k - 32);
            if (((k >= 'A' && k <= 'Z') || (k >= '0' && k <= '9')) && pos < 3) ini[pos++] = (char) k;
            else if ((k == 8 || k == 0x7F || k == 0x14) && pos) ini[--pos] = '_';
            else if (k == 13 && pos) break;
            else if (k == K_ESC) { pos = 0; break; }
        }
        if (pos) {
            for (j = 4; j > i; j--) { hs_s[j] = hs_s[j - 1]; strcpy(hs_n[j], hs_n[j - 1]); }
            for (j = pos; j < 3; j++) ini[j] = ' ';
            hs_s[i] = score; strcpy(hs_n[i], ini);
            hs_save(); newest = i;
        }
        if (pos) text(LIST_X + 34 * 8, GY + 54, hs_n[i], WHITE, BLUE, F16);   /* the initials as kept, the blanks gone */
    }
    text(LIST_X, GO_BEST_Y - 12, "BEST", WHITE, BLUE, F8);
    draw_best(LIST_X + 2, GO_BEST_Y);
    band_text(BBOT, "SPACE plays again, ESC quits");
    r = list_pick(GY + 76, G_ITEMS, 0, 2, 0, title_key);
    return r == 0;
}

/* ---- in and out --------------------------------------------------------- */
void main(void)
{
    uint8_t ctrl_was = REG(VICKY), l0_was = REG(VICKY + 0x10), l1_was = REG(VICKY + 0x20), bg_was = REG(V_BGCOL), spr_was = REG(V_SPRCTL), i;
    seed = (uint16_t)(REG(SYS + 0x0D) * 257u + 1);
    fonts_near(); make_tiles();
    REG(V_CTRL) = 0; REG(V_BGCOL) = BLACK;             /* a pixel of colour 0 is the ground: black */
    dma_fill(BLUE, BMP, (uint32_t)SW * 240);
    REG(VICKY + 0x10) = 0;                            /* the console's layer off, left as it is; the bitmap on layer 1 */
    for (i = 0x21; i <= 0x25; i++) REG(VICKY + i) = 0;
    w16(VICKY + 0x26, SW); w32(VICKY + 0x28, BMP); w32(VICKY + 0x2C, BMP);
    REG(VICKY + 0x20) = 1 | (0 << 1) | (3 << 3);      /* bitmap, 8 bpp */
    REG(V_SPRCTL) = 0;
    REG(V_CTRL) = 1 | 2 | 4;                          /* 320 x 240, doubled: the Chooser's chunky pixels */
    draw_top();

    sound_init();
    hs_load();
    while (title()) {
        draw_frame();
        play_game();
        if (!game_over()) break;
    }

    sound_off();
    REG(VICKY + 0x20) = l1_was; REG(VICKY + 0x10) = l0_was; REG(V_SPRCTL) = spr_was;
    REG(VICKY) = ctrl_was; REG(V_BGCOL) = bg_was;
    rom_video();
    REG(TERM + 4) = 2;                                /* JIM: a clean screen to come back to */
}
