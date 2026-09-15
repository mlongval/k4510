/* K4510: TETRIS -- the falling blocks, in MODE 7 (45 x 33).
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
 * The screen is text32 cells written straight into the console, a row at a
 * time by DMA, the way BANNER writes them.  The program takes the status
 * bands for itself (JIM $DA0E bit 3), so MODE 7's 33 rows are all its own,
 * and it puts the bands and the user's MODE back when it leaves.
 */
#include "k4510.h"

#define TERM     0xDA00u
#define SCREEN   0x00030000UL
#define OPL_ADDR 0xD480u
#define OPL_DATA 0xD481u
#define OPL_ID   0xD482u
#define CMDLINE  ((char *)0x0300)            /* rom_shell's line: page 3, the ROM cannot read our image during the call */

void __fastcall__ rom_chrout(unsigned char c);
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

/* ---- the screen --------------------------------------------------------- */
static uint8_t cols, rows, ox, oy, stride, top, flags_was;
#define WX 2                                 /* the well's left wall */
#define PX 27                                /* the panel */
static uint8_t rb[48 * 4], rn;               /* a row being built */

static void rb_put(uint8_t glyph, uint8_t fg, uint8_t bg)
{
    uint8_t *q = rb + rn * 4;
    if (rn >= 48) return;
    q[0] = glyph; q[1] = 0; q[2] = fg; q[3] = bg; rn++;
}
static void rb_out(uint8_t r, uint8_t c)
{
    if (r < rows && rn) dma_copy((uint32_t)(uint16_t)rb, SCREEN + ((uint32_t)(r + oy) * stride + c + ox) * 4, (uint32_t)rn * 4);
    rn = 0;
}
static void text(uint8_t r, uint8_t c, const char *s, uint8_t fg)
{
    while (*s) rb_put((uint8_t) *s++, fg, BLACK);
    rb_out(r, c);
}
static char nb[12];
static void num(uint8_t r, uint8_t c, uint32_t v, uint8_t w, uint8_t fg)
{
    uint8_t i = 10;
    nb[10] = 0;
    do { nb[--i] = (char)('0' + (uint8_t)(v % 10)); v /= 10; } while (v && i);
    while (i > 10 - w) nb[--i] = ' ';
    text(r, c, nb + i, fg);
}

/* A line of the well, centred, over whatever was there. */
static void well_msg(uint8_t r, const char *s, uint8_t fg)
{
    uint8_t n = (uint8_t) strlen(s), i, l;
    if (n > 20) n = 20;
    l = (uint8_t)((20 - n) / 2);
    for (i = 0; i < 20; i++) rb_put((uint8_t)(i >= l && i < l + n ? s[i - l] : ' '), fg, BLACK);
    rb_out((uint8_t)(top + 4 + r), WX + 1);
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

static void draw_well(void)
{
    uint8_t r, c, v, g; int8_t gy;
    memset(ov, 0, sizeof ov);
    if (active) {
        gy = py; while (fits(cur, rot, px, (int8_t)(gy + 1))) gy++;
        stamp(gy, 9); stamp(py, (uint8_t)(cur + 1));
    }
    g = pcol[cur + 1];
    for (r = 0; r < 20; r++) {
        rb_put(0xBA, GREY, BLACK);
        for (c = 0; c < 10; c++) {
            v = ov[r][c]; if (!v) v = bd[r][c];
            if (v == 8)      { rb_put(0xDB, WHITE, WHITE); rb_put(0xDB, WHITE, WHITE); }
            else if (v == 9) { rb_put(0xB0, g, BLACK); rb_put(0xB0, g, BLACK); }
            else if (v)      { rb_put('[', BLACK, pcol[v]); rb_put(']', BLACK, pcol[v]); }
            else             { rb_put(' ', DGREY, BLACK); rb_put(0xFA, DGREY, BLACK); }
        }
        rb_put(0xBA, GREY, BLACK);
        rb_out((uint8_t)(top + 4 + r), WX);
    }
}

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

/* ---- the panel ---------------------------------------------------------- */
static void box_row(uint8_t r, uint8_t l, uint8_t m, uint8_t rt)
{
    uint8_t i;
    rb_put(l, GREY, BLACK);
    for (i = 0; i < 12; i++) rb_put(m, GREY, BLACK);
    rb_put(rt, GREY, BLACK);
    rb_out(r, PX);
}
static void draw_frame(void)                 /* everything that does not change */
{
    static const char title[] = "T E T R I S";
    static const uint8_t tcol[6] = { RED, ORANGE, YELLOW, GREEN, LBLUE, PURPLE };
    uint8_t r, c;
    for (r = 0; r < rows; r++) { for (c = 0; c < cols && c < 48; c++) rb_put(' ', WHITE, BLACK); rb_out(r, 0); }
    for (c = 0; title[c]; c++) rb_put((uint8_t) title[c], tcol[c / 2], BLACK);
    rb_out((uint8_t)(top + 1), WX + 5);
    text((uint8_t)(top + 2), WX + 5, "on the K4510", DGREY);
    rb_put(0xC8, GREY, BLACK); for (c = 0; c < 20; c++) rb_put(0xCD, GREY, BLACK); rb_put(0xBC, GREY, BLACK);
    rb_out((uint8_t)(top + 24), WX);
    box_row((uint8_t)(top + 4), 0xC9, 0xCD, 0xBB);
    for (r = 5; r < 9; r++) box_row((uint8_t)(top + r), 0xBA, ' ', 0xBA);
    box_row((uint8_t)(top + 9), 0xC8, 0xCD, 0xBC);
    text((uint8_t)(top + 4), PX + 2, " NEXT ", WHITE);
    text((uint8_t)(top + 11), PX, "SCORE", GREY);
    text((uint8_t)(top + 14), PX, "LEVEL", GREY);
    text((uint8_t)(top + 17), PX, "LINES", GREY);
    text((uint8_t)(top + 20), PX, "BEST", GREY);
    text((uint8_t)(top + 26), WX, "\x1B \x1A move   \x18 X turn   Z turn back", GREY);
    text((uint8_t)(top + 27), WX, "\x19 soft drop   SPACE drop   P pause", GREY);
    text((uint8_t)(top + 28), WX, "M music   ESC end the game", GREY);
}
static void draw_panel(void)
{
    uint8_t dy, dx, i;
    uint16_t m = nxt < 7 ? shape[nxt][0] : 0;
    for (dy = 0; dy < 2; dy++) {
        for (dx = 0; dx < 4; dx++) {
            if (m & (0x8000u >> (dy * 4 + dx))) { rb_put('[', BLACK, pcol[nxt + 1]); rb_put(']', BLACK, pcol[nxt + 1]); }
            else { rb_put(' ', WHITE, BLACK); rb_put(' ', WHITE, BLACK); }
        }
        rb_out((uint8_t)(top + 6 + dy), PX + 3);
    }
    num((uint8_t)(top + 12), PX, score, 10, WHITE);
    num((uint8_t)(top + 15), PX, level, 3, WHITE);
    num((uint8_t)(top + 18), PX, lines, 5, WHITE);
    for (i = 0; i < 5; i++) {
        text((uint8_t)(top + 21 + i), PX, hs_n[i], i ? LGREY : YELLOW);
        if (hs_s[i]) num((uint8_t)(top + 21 + i), PX + 4, hs_s[i], 9, i ? LGREY : YELLOW);
        else text((uint8_t)(top + 21 + i), PX + 4, "         ", LGREY);
    }
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
static void frame(void)
{
    uint8_t k, held, edge;
    while ((k = key_get()) != 0) {
        if (k == K_ESC) { over = 1; active = 0; return; }
        if (k == 'p' || k == 'P') { paused ^= 1; dirty = 3; if (paused) music_stop(); continue; }
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
        while (d-- && !over) frame();
        if (dirty) {
            draw_well();
            if (paused) well_msg(9, "PAUSED", YELLOW);
            if (dirty & 2) draw_panel();
            dirty = 0;
        }
    }
    music_stop();
}

static uint8_t wait_key(void)
{
    uint8_t k;
    while ((k = key_get()) == 0) { seed++; sfx_tick(); }
    return k;
}
static uint8_t title(void)
{
    static char lv[] = "\x1B  0  \x1A";
    uint8_t k;
    memset(bd, 0, sizeof bd); active = 0; nxt = 7;
    draw_well(); draw_panel();
    well_msg(6, "PRESS SPACE", WHITE);
    well_msg(10, "START LEVEL", GREY);
    for (;;) {
        lv[3] = (char)('0' + start);
        well_msg(11, lv, YELLOW);
        k = wait_key();
        if (k == K_ESC) return 0;
        if (k == ' ' || k == 13) return 1;
        if (k == K_LEFT && start > 0) start--;
        if (k == K_RIGHT && start < 9) start++;
    }
}
static uint8_t game_over(void)
{
    static char ini[4];
    uint8_t k, i, j, pos = 0;
    draw_well(); draw_panel();
    for (k = 0; k < 60; k++) wait_vblank();  /* a second to see how it ended */
    while (key_get()) ;
    memset(bd, 0, sizeof bd); active = 0; draw_well();   /* then a clean well to write in */
    well_msg(3, "GAME OVER", RED);
    for (i = 0; i < 5 && hs_s[i] >= score; i++) ;
    if (i < 5 && score) {
        well_msg(5, "A NEW BEST SCORE!", YELLOW);
        well_msg(7, "YOUR INITIALS", WHITE);
        strcpy(ini, "___");
        for (;;) {
            well_msg(8, ini, YELLOW);
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
            hs_save(); draw_panel();
            well_msg(8, ini, YELLOW);
        }
    }
    well_msg(12, "SPACE  PLAY AGAIN", WHITE);
    well_msg(13, "ESC  QUIT", GREY);
    for (;;) {
        k = wait_key();
        if (k == ' ' || k == 13) return 1;
        if (k == K_ESC) return 0;
    }
}

/* ---- in and out --------------------------------------------------------- */
static char mode_was;
static void mode_run(char digit)
{
    strcpy(CMDLINE, "MODE 0"); CMDLINE[5] = digit;
    rom_shell(CMDLINE);
}

void main(void)
{
    switch (REG(VICKY) & 0x3E) {             /* VICKY CTRL -> the MODE digit, as LOGO reads it */
    case 0x00: mode_was = '0'; break;
    case 0x20: mode_was = '5'; break;
    case 0x26: mode_was = '6'; break;
    case 0x36: mode_was = '7'; break;
    case 0x02: mode_was = '2'; break;
    case 0x0A: mode_was = '3'; break;
    case 0x1A: mode_was = '4'; break;
    default:   mode_was = '1'; break;
    }
    if (mode_was != '7') mode_run('7');
    flags_was = REG(TERM + 0x0E);
    REG(TERM + 0x0F) = 0; REG(TERM + 0x16) = 0;
    REG(TERM + 0x0E) = (uint8_t)((flags_was | 8) & ~1);   /* the bands are ours; no cursor */
    rom_video();
    cols = REG(TERM + 5); rows = REG(TERM + 6);
    ox = REG(TERM + 7);   oy = REG(TERM + 8);
    stride = REG(TERM + 0x0D);
    if (!cols) cols = 45;
    if (!rows) rows = 33;
    if (!stride) stride = cols;
    top = (uint8_t)(rows >= 31 ? (rows - 29) / 2 : 0);
    seed = (uint16_t)(REG(SYS + 0x0D) * 257u + 1);

    sound_init();
    hs_load();
    draw_frame();
    while (title()) {
        play_game();
        if (!game_over()) break;
    }

    sound_off();
    REG(TERM + 0x0E) = (uint8_t)(flags_was & ~8);
    rom_video();
    if (mode_was != '7') mode_run(mode_was);
    else rom_chrout(12);
}
