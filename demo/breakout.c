/* K4510: BREAKOUT -- a wall of bricks, a ball, and a paddle to keep it up.
 *
 * Eight rows of bricks in the old colours -- yellow, green, orange, red --
 * worth 1, 3, 5 and 7.  The mouse moves the paddle, or the arrow keys do;
 * the button or Space sends the ball.  Where the ball meets the paddle
 * decides where it goes: the middle straight up, the ends out wide.  It
 * quickens after 4 and 12 hits and when it first reaches the orange and the
 * red rows.  A cleared wall brings the next one, faster.  Five balls; the
 * five best scores are kept in /APPS/BREAKOUT/HISCORE.DAT.
 *
 *   mouse or Left/Right   the paddle     button or Space   serve
 *   P pause   M sound on/off   Escape end the game
 *
 * The look, since 2026-10-09, is the Personality Chooser's, as CHESS has it
 * (Doc: "redo Tetris and Blockout in a similar style"): 320x240 doubled,
 * grey bands above and below (the name and the time; the keys), the blue
 * glass, the banner's five bars, unscii drawn into the bitmap, bevelled
 * 8-bit bricks, a panel on the right with the score, the level, the balls
 * and the best, and the start and the end as the Chooser's list.  Only the
 * machine's first sixteen colours, never written here.
 *
 * The blitter does all the drawing (fills, and each glyph built in gl[] and
 * copied); the ball and the paddle are rubbed out and drawn again where
 * they have moved, once a frame.
 */
#include "k4510.h"

#define TERM    0xDA00u
#define BLT     0xD070u
#define BITMAP  0x00200000UL
#define MOUSEX  0xD108u
#define MOUSEB  0xD10Cu
#define OPL_ADDR 0xD480u
#define OPL_DATA 0xD481u
#define OPL_ID   0xD482u
#define FONT16  0x00010800UL                 /* unscii-16, beside FONT8 (unscii-8) */
#define W 320
#define H 240
#define BAND 12                              /* a band: 8-pixel text with 2 above and below */
#define GY BAND                              /* the glass: lines 12..227 */
#define GB (H - BAND)                        /* the bottom band's first line */

static void rom_video(void) { ((void (*)(void))0xFF92)(); }
static unsigned char rom_save(void) { return ((unsigned char (*)(void))0xFF8C)(); }
static unsigned char rom_load(void) { return ((unsigned char (*)(void))0xFF89)(); }
static void zp16(uint8_t a, uint16_t v) { REG(a) = v; REG(a + 1) = v >> 8; }
static void zp32(uint8_t a, uint32_t v) { REG(a) = v; REG(a + 1) = v >> 8; REG(a + 2) = v >> 16; REG(a + 3) = v >> 24; }

/* the machine's sixteen (a .PAL changes them, as it changes the Chooser) */
#define C_BLACK  0
#define C_WHITE  1
#define C_RED    2
#define C_GREEN  5
#define C_BLUE   6
#define C_YELLOW 7
#define C_ORANGE 8
#define C_BROWN  9
#define C_LRED   10
#define C_DGREY  11
#define C_LGREEN 13
#define C_LBLUE  14
#define C_LGREY  15

/* the field: framed in light blue, open at the bottom */
#define FX0   8                              /* the inside */
#define FX1   218
#define FY0   (GY + 6)                       /* the roof */
#define BCOLS 14
#define BROWS 8
#define BX0   FX0                            /* the wall's left edge */
#define BY0   (FY0 + 18)
#define BW    15                             /* a brick and its gap */
#define BH    8
#define PY    214                            /* the paddle's top */
#define PH    4
#define BALL  4
#define PX    228                            /* the panel */

static uint8_t brick[BROWS][BCOLS], left;
static const uint8_t rowcol[BROWS] = { C_RED, C_RED, C_ORANGE, C_ORANGE, C_GREEN, C_GREEN, C_YELLOW, C_YELLOW };
static const uint8_t rowlit[BROWS] = { C_LRED, C_LRED, C_YELLOW, C_YELLOW, C_LGREEN, C_LGREEN, C_WHITE, C_WHITE };
static const uint8_t rowdim[BROWS] = { C_BROWN, C_BROWN, C_BROWN, C_BROWN, C_DGREY, C_DGREY, C_ORANGE, C_ORANGE };
static const uint8_t rowpts[BROWS] = { 7, 7, 5, 5, 3, 3, 1, 1 };
static int bx, by, vx, vy;                   /* the ball, in 1/16 pixels */
static int px, pw = 30, opx = -1, obx = -1, oby = -1;
static uint8_t lives, level, served, over, paused, sound_on = 1, hits, reached_orange, reached_red, speed;
static uint16_t score;

/* ---- the blitter -------------------------------------------------------- */
static void box(int x, int y, int w, int h, uint8_t c)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > W) w = W - x;
    if (y + h > H) h = H - y;
    if (w <= 0 || h <= 0) return;
    REG(BLT) = c; REG(BLT + 1) = 0; REG(BLT + 2) = 0; REG(BLT + 3) = 0;
    w32(BLT + 4, BITMAP + (uint32_t) y * W + x); w16(BLT + 8, (uint16_t) w); w16(BLT + 10, (uint16_t) h); w16(BLT + 14, W);
    REG(BLT + 0x10) = 2; REG(BLT + 0x12) = 1;
}
#define F8 0                                 /* text(): unscii-8, unscii-16, unscii-8 doubled */
#define F16 1
#define F8X2 2
static uint8_t gl[256];
static int text(int x, int y, const char *s, uint8_t fg, uint8_t bg, uint8_t font)   /* the x after it */
{
    uint8_t r, b, bits, h = font == F8 ? 8 : 16, w = font == F8X2 ? 16 : 8, *g;
    for (; *s; s++, x += w) {
        g = gl;
        for (r = 0; r < h; r++) {
            bits = font == F16 ? far_peek(FONT16 + (uint16_t)(uint8_t) *s * 16 + r)
                 : far_peek(FONT8 + (uint16_t)(uint8_t) *s * 8 + (font == F8X2 ? r >> 1 : r));
            if (font == F8X2) { for (b = 0x80; b; b >>= 1) { *g++ = (bits & b) ? fg : bg; *g++ = (bits & b) ? fg : bg; } }
            else for (b = 0x80; b; b >>= 1) *g++ = (bits & b) ? fg : bg;
        }
        w32(BLT, (uint32_t)(uint16_t) gl); w32(BLT + 4, BITMAP + (uint32_t) y * W + x);
        w16(BLT + 8, w); w16(BLT + 10, h); w16(BLT + 12, w); w16(BLT + 14, W);
        REG(BLT + 0x10) = 0; REG(BLT + 0x12) = 1;
    }
    return x;
}
static char nb[8];
static const char *num(uint16_t v, uint8_t w)  /* w digits, leading zeros: an arcade score */
{
    uint8_t i = 7;
    nb[7] = 0;
    do { nb[--i] = (char)('0' + v % 10); v /= 10; } while (i > 7 - w);
    return nb + i;
}
static void center(int x0, int w, int y, const char *s, uint8_t fg, uint8_t font)   /* centred in x0..x0+w, on the glass */
{
    int n = (int) strlen(s) * (font == F8X2 ? 16 : 8);
    text(x0 + (w - n) / 2, y, s, fg, C_BLUE, font);
}
static void band_text(int y, const char *s)  /* centred in a band, black on grey */
{
    box(0, y, W, BAND, C_LGREY);
    text((W - (int) strlen(s) * 8) / 2, y + 2, s, C_BLACK, C_LGREY, F8);
}
static void bars(int x, int y, uint8_t h, uint8_t w)   /* the banner's five */
{
    static const uint8_t col[5] = { C_RED, C_ORANGE, C_YELLOW, C_GREEN, C_LBLUE };
    static const uint8_t len[5] = { 8, 6, 4, 6, 8 };
    uint8_t i;
    for (i = 0; i < 5; i++) box(x, y + i * h, len[i] * w, h, col[i]);
}
static uint8_t clock_shown = 0xFF, rtc_read;
static void put2(char *b, uint8_t v) { b[0] = (char)('0' + v / 10); b[1] = (char)('0' + v % 10); }
static void draw_top(void)                   /* the name on the left, the time on the right, as the Chooser's band */
{
    static char b[17]; uint16_t y;
    rtc_read = REG(SYS + 4);                 /* latch the clock: a store, as cc65 drops a (void) read */
    clock_shown = REG(SYS + 6);
    put2(b, REG(SYS + 7)); b[2] = ':'; put2(b + 3, REG(SYS + 6)); b[5] = ' ';
    put2(b + 6, REG(SYS + 8)); b[8] = '.'; put2(b + 9, REG(SYS + 9)); b[11] = '.';
    y = REG(SYS + 0x0A) | (REG(SYS + 0x0B) << 8);
    b[12] = (char)('0' + (y / 1000) % 10); b[13] = (char)('0' + (y / 100) % 10); b[14] = (char)('0' + (y / 10) % 10); b[15] = (char)('0' + y % 10); b[16] = 0;
    box(0, 0, W, BAND, C_LGREY);
    text(8, 2, "K4510 BREAKOUT", C_BLACK, C_LGREY, F8);
    text(W - 8 - 16 * 8, 2, b, C_BLACK, C_LGREY, F8);
}
static void tick_top(void) { rtc_read = REG(SYS + 4); if (REG(SYS + 6) != clock_shown) draw_top(); }
static const char KEYS[] = "ARROWS/MOUSE  SPACE SERVE  P PAUSE  ESC";
static void glass(void) { box(0, GY, W, GB - GY, C_BLUE); }

/* ---- the sound ---------------------------------------------------------- */
static const uint8_t opslot[3] = { 0, 1, 2 };
static const uint16_t fnum[12] = { 345, 365, 387, 410, 434, 460, 488, 517, 547, 580, 614, 651 };
#define N(o, s) ((o) * 12 + (s))
enum { C, Cs, D, Ds, E, F, Fs, G, Gs, A, As, B };
static uint8_t opl_ok, sfx_t[3];
static void opl(uint8_t reg, uint8_t val) { REG(OPL_ADDR) = reg; REG(OPL_DATA) = val; }
static void patch(uint8_t ch, const uint8_t *p)
{
    uint8_t m = opslot[ch], c = (uint8_t)(m + 3);
    opl((uint8_t)(0x20 + m), p[0]); opl((uint8_t)(0x40 + m), p[1]); opl((uint8_t)(0x60 + m), p[2]); opl((uint8_t)(0x80 + m), p[3]); opl((uint8_t)(0xE0 + m), p[4]);
    opl((uint8_t)(0x20 + c), p[5]); opl((uint8_t)(0x40 + c), p[6]); opl((uint8_t)(0x60 + c), p[7]); opl((uint8_t)(0x80 + c), p[8]); opl((uint8_t)(0xE0 + c), p[9]);
    opl((uint8_t)(0xC0 + ch), p[10]);
}
static const uint8_t p_blip[11] = { 0x21, 0x10, 0xF8, 0x88, 0x02, 0x21, 0x00, 0xF8, 0x88, 0x02, 0x0E };
static const uint8_t p_bell[11] = { 0x31, 0x1E, 0xF6, 0x27, 0x02, 0x11, 0x00, 0xF4, 0x37, 0x00, 0x06 };
static const uint8_t p_thud[11] = { 0x0E, 0x00, 0xF6, 0xF6, 0x03, 0x0E, 0x00, 0xF8, 0xF7, 0x03, 0x0E };
static void keyoff(uint8_t ch) { if (opl_ok) opl((uint8_t)(0xB0 + ch), 0); }
static void sfx(uint8_t ch, uint8_t note, uint8_t frames)
{
    uint16_t f = fnum[note % 12];
    if (!opl_ok || !sound_on) return;
    keyoff(ch);
    opl((uint8_t)(0xA0 + ch), (uint8_t) f);
    opl((uint8_t)(0xB0 + ch), (uint8_t)(0x20 | ((note / 12) << 2) | ((f >> 8) & 3)));
    sfx_t[ch] = frames;
}
static void sfx_tick(void) { uint8_t i; for (i = 0; i < 3; i++) if (sfx_t[i] && !--sfx_t[i]) keyoff(i); }

/* ---- the best five ------------------------------------------------------ */
static char hname[] = "/APPS/BREAKOUT/HISCORE.DAT";
static uint16_t hs_s[5];
static char hs_n[5][4];
static uint8_t hbuf[41];
static void hs_load(void)
{
    uint8_t i, j;
    for (i = 0; i < 5; i++) { hs_s[i] = 0; strcpy(hs_n[i], "---"); }
    zp16(0xF0, (uint16_t) hname); zp32(0xF2, (uint32_t)(uint16_t) hbuf);
    if (rom_load() || hbuf[40] != 'B') return;
    for (i = 0; i < 5; i++) {
        hs_s[i] = hbuf[i * 8] | ((uint16_t) hbuf[i * 8 + 1] << 8);
        for (j = 0; j < 3; j++) hs_n[i][j] = (char) hbuf[i * 8 + 4 + j];
        hs_n[i][3] = 0;
    }
}
static void hs_save(void)
{
    uint8_t i, j;
    memset(hbuf, 0, sizeof hbuf);
    for (i = 0; i < 5; i++) {
        hbuf[i * 8] = (uint8_t) hs_s[i]; hbuf[i * 8 + 1] = (uint8_t)(hs_s[i] >> 8);
        for (j = 0; j < 3; j++) hbuf[i * 8 + 4 + j] = (uint8_t) hs_n[i][j];
    }
    hbuf[40] = 'B';
    zp16(0xF0, (uint16_t) hname); zp32(0xF2, (uint32_t)(uint16_t) hbuf); zp32(0xF6, 41);
    rom_save();
}
static void hs_line(int x, int y, uint8_t j, uint8_t fg)   /* "1  ABC  01234" */
{
    static char line[16];
    uint16_t v = hs_s[j]; uint8_t n;
    line[0] = (char)('1' + j); line[1] = ' '; line[2] = ' ';
    strcpy(line + 3, hs_n[j]); line[6] = ' '; line[7] = ' ';
    for (n = 0; n < 5; n++) { line[12 - n] = (char)('0' + v % 10); v /= 10; }
    line[13] = 0;
    text(x, y, line, fg, C_BLUE, F8);
}

/* ---- the Chooser's list ---------------------------------------------------- */
#define ROW_H 20
static void menu_row(int x, int y, int w, uint8_t i, const char *s, uint8_t on)
{
    static char d[2];
    box(x, y, w, ROW_H - 2, on ? C_LBLUE : C_BLUE);
    box(x + 6, y + 3, 12, 12, on ? C_BLUE : C_LBLUE);   /* the number box */
    d[0] = (char)('1' + i); d[1] = 0; text(x + 8, y + 5, d, on ? C_LBLUE : C_BLUE, on ? C_BLUE : C_LBLUE, F8);
    text(x + 28, y + 1, s, on ? C_BLUE : C_YELLOW, on ? C_LBLUE : C_BLUE, F16);
}
/* the rows at x,y,w; the choice, or 255 for Escape.  Space picks the first. */
static uint8_t list(int x, int y, int w, const char *const *items, uint8_t n, uint8_t cur)
{
    uint8_t i, k, b, bwas = REG(MOUSEB); int my;
    for (i = 0; i < n; i++) menu_row(x, y + i * ROW_H, w, i, items[i], i == cur);
    for (;;) {
        k = key_get();
        if (k == 0x80 || k == 0x81) {
            menu_row(x, y + cur * ROW_H, w, cur, items[cur], 0);
            cur = (uint8_t)(k == 0x80 ? (cur + n - 1) % n : (cur + 1) % n);
            menu_row(x, y + cur * ROW_H, w, cur, items[cur], 1);
        }
        else if (k == 13) return cur;
        else if (k == ' ') return 0;
        else if (k >= '1' && k < '1' + n) return (uint8_t)(k - '1');
        else if (k == 0x1B) return 255;
        b = REG(MOUSEB);
        if ((b & 1) && !(bwas & 1)) {                         /* a click on a row */
            my = (REG(MOUSEX + 2) | (REG(MOUSEX + 3) << 8)) >> 1;     /* the glass is 640x480: halved */
            if (my >= y && my < y + n * ROW_H) return (uint8_t)((my - y) / ROW_H);
        }
        bwas = b;
        sfx_tick(); tick_top();
        wait_vblank();
    }
}

/* ---- the game ----------------------------------------------------------- */
static void draw_brick(uint8_t r, uint8_t c)  /* bevelled: lit above and left, shaded below and right */
{
    int x = BX0 + c * BW, y = BY0 + r * BH;
    if (brick[r][c]) {
        box(x, y, BW - 1, BH - 1, rowcol[r]);
        box(x, y, BW - 1, 1, rowlit[r]); box(x, y, 1, BH - 1, rowlit[r]);
        box(x + 1, y + BH - 2, BW - 2, 1, rowdim[r]); box(x + BW - 2, y + 1, 1, BH - 2, rowdim[r]);
    }
    else box(x, y, BW - 1, BH - 1, C_BLUE);
}
static void draw_panel(void)                 /* what does not change in a game */
{
    box(PX - 4, GY, W - PX + 4, GB - GY, C_BLUE);
    bars(PX, GY + 8, 3, 3);
    text(PX - 4, GY + 28, "Breakout", C_WHITE, C_BLUE, F16);
    text(PX, GY + 52, "SCORE", C_LBLUE, C_BLUE, F8);
    text(PX, GY + 84, "LEVEL", C_LBLUE, C_BLUE, F8);
    text(PX, GY + 116, "BALLS", C_LBLUE, C_BLUE, F8);
    text(PX, GY + 144, "BEST", C_LBLUE, C_BLUE, F8);
}
static void draw_status(void)
{
    uint8_t i;
    text(PX, GY + 62, num(score, 5), C_YELLOW, C_BLUE, F16);
    text(PX, GY + 94, num(level, 2), C_YELLOW, C_BLUE, F16);
    box(PX, GY + 127, 96, 4, C_BLUE);
    for (i = 0; i < lives && i < 8; i++) { box(PX + i * 11, GY + 127, 9, 3, C_LBLUE); box(PX + i * 11, GY + 127, 9, 1, C_WHITE); }
    text(PX, GY + 154, num(hs_s[0] > score ? hs_s[0] : score, 5), C_WHITE, C_BLUE, F16);
    text(PX, GY + 174, hs_s[0] > score ? hs_n[0] : "YOU", C_LBLUE, C_BLUE, F8);
    text(PX, GY + 196, sound_on ? "         " : "SOUND OFF", C_LRED, C_BLUE, F8);
}
static void new_wall(void)
{
    uint8_t r, c;
    box(0, GY, PX - 4, GB - GY, C_BLUE);
    box(FX0 - 2, FY0 - 2, FX1 - FX0 + 4, 2, C_LBLUE);           /* the frame: roof and walls */
    box(FX0 - 2, FY0, 2, GB - FY0, C_LBLUE); box(FX1, FY0, 2, GB - FY0, C_LBLUE);
    for (r = 0; r < BROWS; r++) for (c = 0; c < BCOLS; c++) { brick[r][c] = 1; draw_brick(r, c); }
    left = BROWS * BCOLS; hits = 0; reached_orange = reached_red = 0;
    speed = (uint8_t)(3 + level);            /* see launch() */
    opx = obx = oby = -1;
}
static void serve(void)
{
    served = 0;
    bx = (px + pw / 2 - BALL / 2) * 16; by = (PY - BALL - 1) * 16;
    vx = 0; vy = 0;
}
static void launch(void)                     /* sixteenths of a pixel a frame: half the 640-wide game's */
{
    served = 1;
    vx = (level & 1 ? 1 : -1) * speed * 4; vy = -speed * 7;
}
static void quicken(void)                    /* the same direction, a step faster */
{
    speed++;
    vx = vx < 0 ? -(int)(((long) -vx * speed) / (speed - 1)) : (int)(((long) vx * speed) / (speed - 1));
    vy = vy < 0 ? -(int)(((long) -vy * speed) / (speed - 1)) : (int)(((long) vy * speed) / (speed - 1));
}
static uint8_t brick_at(int x, int y, uint8_t *pr, uint8_t *pc)   /* the brick under a pixel, if any */
{
    int c, r;
    if (x < BX0 || y < BY0) return 0;
    c = (x - BX0) / BW; r = (y - BY0) / BH;
    if (c >= BCOLS || r >= BROWS) return 0;
    if ((x - BX0) % BW >= BW - 1 || (y - BY0) % BH >= BH - 1) return 0;   /* the gap */
    if (!brick[r][c]) return 0;
    *pr = (uint8_t) r; *pc = (uint8_t) c;
    return 1;
}
static uint8_t hit_brick(int x, int y)       /* any corner of the ball at (x,y) on a brick: break it */
{
    uint8_t r, c;
    if (!brick_at(x, y, &r, &c) && !brick_at(x + BALL - 1, y, &r, &c) &&
        !brick_at(x, y + BALL - 1, &r, &c) && !brick_at(x + BALL - 1, y + BALL - 1, &r, &c)) return 0;
    brick[r][c] = 0; draw_brick(r, c); left--;
    score += rowpts[r];
    sfx(0, (uint8_t)(N(6, C) - r * 2), 5);
    if (++hits == 4 || hits == 12) quicken();
    if (r <= 3 && !reached_orange) { reached_orange = 1; quicken(); }
    if (r <= 1 && !reached_red) { reached_red = 1; quicken(); }
    draw_status();
    return 1;
}
static void move_ball(void)
{
    int nx, ny, x, y, off;
    nx = bx + vx; x = nx / 16; y = by / 16;
    if (x < FX0) { nx = FX0 * 16; vx = -vx; sfx(1, N(5, G), 4); }
    else if (x + BALL > FX1) { nx = (FX1 - BALL) * 16; vx = -vx; sfx(1, N(5, G), 4); }
    else if (hit_brick(x, y)) { vx = -vx; nx = bx; }
    bx = nx;
    ny = by + vy; x = bx / 16; y = ny / 16;
    if (y < FY0) { ny = FY0 * 16; vy = -vy; sfx(1, N(5, G), 4); }
    else if (vy > 0 && y + BALL >= PY && y + BALL <= PY + PH && x + BALL > px && x < px + pw) {
        off = (x + BALL / 2) - (px + pw / 2);            /* -pw/2 .. pw/2 */
        vy = -(speed * 7);
        vx = (int)((long) off * speed * 13 / (pw / 2));
        ny = (PY - BALL) * 16;
        sfx(1, N(4, C), 5);
    }
    else if (hit_brick(x, y)) { vy = -vy; ny = by; }
    else if (y + BALL > GB) {                            /* missed: gone through the floor */
        sfx(2, N(2, C), 25);
        box(obx, oby, BALL, BALL, C_BLUE); obx = -1;
        if (!--lives) { over = 1; return; }
        draw_status(); serve();
        return;
    }
    by = ny;
}
static void draw_moving(void)
{
    int x = bx / 16, y = by / 16;
    if (px != opx) {
        if (opx >= 0) { if (px > opx) box(opx, PY, px - opx, PH, C_BLUE); else box(px + pw, PY, opx - px, PH, C_BLUE); }
        box(px, PY, pw, PH, C_LBLUE); box(px, PY, pw, 1, C_WHITE); box(px, PY + PH - 1, pw, 1, C_LGREY);
        opx = px;
    }
    if (x != obx || y != oby) {
        if (obx >= 0) box(obx, oby, BALL, BALL, C_BLUE);
        box(x, y, BALL, BALL, C_WHITE);
        obx = x; oby = y;
    }
}

static int mouse_was = -1;
static uint8_t bwas;
static void control(void)
{
    uint8_t held = keys_held(), b = REG(MOUSEB);
    int mx = (REG(MOUSEX) | (REG(MOUSEX + 1) << 8)) >> 1;          /* the glass is 640 wide */
    if (mouse_was >= 0 && mx != mouse_was) px = mx - pw / 2;          /* the mouse moved: it has the paddle */
    mouse_was = mx;
    if (held & HELD_LEFT) px -= 4;
    if (held & HELD_RIGHT) px += 4;
    if (px < FX0) px = FX0;
    if (px > FX1 - pw) px = FX1 - pw;
    if (!served) { bx = (px + pw / 2 - BALL / 2) * 16; if ((b & 1) && !(bwas & 1)) launch(); }
    bwas = b;
}

#define PAUSE_X 54
#define PAUSE_Y 130
static void pause_box(uint8_t on)
{
    if (on) { box(PAUSE_X, PAUSE_Y, 120, 32, C_LBLUE); box(PAUSE_X + 2, PAUSE_Y + 2, 116, 28, C_BLUE); center(PAUSE_X, 120, PAUSE_Y + 8, "PAUSED", C_YELLOW, F16); band_text(GB, "P GOES ON   ESC ENDS THE GAME"); }
    else { box(PAUSE_X, PAUSE_Y, 120, 32, C_BLUE); band_text(GB, KEYS); obx = -1; opx = -1; }
}
static uint8_t wait_key(void) { uint8_t k; while ((k = key_get()) == 0) { sfx_tick(); tick_top(); } return k; }
static void play(void)
{
    uint8_t lf, fc, d, k;
    level = 1; lives = 5; score = 0; over = 0; paused = 0;
    px = (FX0 + FX1) / 2 - pw / 2;
    glass(); draw_panel(); new_wall(); draw_status(); serve();
    band_text(GB, KEYS);
    lf = REG(SYS + 0x0D);
    while (!over) {
        fc = REG(SYS + 0x0D);
        if (fc == lf) continue;
        d = (uint8_t)(fc - lf); lf = fc;
        if (d > 3) d = 3;
        while ((k = key_get()) != 0) {
            if (k == 0x1B) { over = 1; break; }
            if (k == 'p' || k == 'P') { paused ^= 1; pause_box(paused); }
            else if (k == 'm' || k == 'M') { sound_on ^= 1; draw_status(); }
            else if (k == ' ' && !served && !paused) launch();
        }
        tick_top();
        if (paused) continue;
        while (d--) {
            sfx_tick();
            control();
            if (served) move_ball();
            if (over) break;
            if (!left) {                     /* a cleared wall: the next, faster */
                level++; sfx(1, N(6, C), 30);
                new_wall(); draw_status(); serve();
            }
        }
        draw_moving();
    }
}
static const char *const AGAIN[2] = { "Play again", "Leave" };
static uint8_t game_over(void)               /* 1: again */
{
    static char ini[4];
    uint8_t k, i, j, pos = 0;
    for (k = 0; k < 45; k++) { sfx_tick(); wait_vblank(); }
    while (key_get()) ;
    box(16, 34, FX1 - 24, 186, C_LBLUE); box(18, 36, FX1 - 28, 182, C_BLUE);   /* the field's middle, framed */
    center(16, FX1 - 24, 42, "GAME OVER", C_LRED, F8X2);
    for (i = 0; i < 5 && hs_s[i] >= score; i++) ;
    if (i < 5 && score) {
        center(16, FX1 - 24, 66, "YOUR INITIALS", C_YELLOW, F8);
        band_text(GB, "LETTERS, RETURN KEEPS THEM");
        strcpy(ini, "___");
        for (;;) {
            center(16, FX1 - 24, 78, ini, C_WHITE, F8X2);
            k = wait_key();
            if (k >= 'a' && k <= 'z') k = (uint8_t)(k - 32);
            if (((k >= 'A' && k <= 'Z') || (k >= '0' && k <= '9')) && pos < 3) ini[pos++] = (char) k;
            else if ((k == 8 || k == 0x7F || k == 0x14) && pos) ini[--pos] = '_';
            else if (k == 13 && pos) break;
            else if (k == 0x1B) { pos = 0; break; }
        }
        if (pos) {
            for (j = 4; j > i; j--) { hs_s[j] = hs_s[j - 1]; strcpy(hs_n[j], hs_n[j - 1]); }
            for (j = pos; j < 3; j++) ini[j] = ' ';
            hs_s[i] = score; strcpy(hs_n[i], ini);
            hs_save();
        }
        box(18, 64, FX1 - 28, 32, C_BLUE);
    }
    center(16, FX1 - 24, 72, "THE BEST FIVE", C_LBLUE, F8);
    for (j = 0; j < 5; j++) hs_line(64, 86 + j * 11, j, j == i ? C_YELLOW : C_WHITE);
    band_text(GB, "1-2 or cursor keys, RETURN picks");
    k = list(24, 148, FX1 - 40, AGAIN, 2, 0);
    return k == 0;
}
static const char *const TITLE[2] = { "Play", "Leave" };
static uint8_t title(void)                   /* 1: play */
{
    uint8_t j, k;
    glass();
    bars(12, GY + 8, 5, 6);
    text(72, GY + 8, "Breakout", C_WHITE, C_BLUE, F8X2);
    text(72, GY + 28, "Mouse or arrows, SPACE serves", C_YELLOW, C_BLUE, F8);
    text(24, 128, "THE BEST FIVE", C_LBLUE, C_BLUE, F8);
    for (j = 0; j < 5; j++) hs_line(24, 142 + j * 11, j, C_WHITE);
    text(184, 142, "P   pause", C_LBLUE, C_BLUE, F8);
    text(184, 153, "M   sound", C_LBLUE, C_BLUE, F8);
    text(184, 164, "ESC ends", C_LBLUE, C_BLUE, F8);
    band_text(GB, "1-2 or cursor keys, RETURN picks");
    k = list(12, GY + 44, W - 24, TITLE, 2, 0);
    return k == 0;
}

void main(void)
{
    uint8_t i, ctrl_was, l0_was, l1_was, bg_was;
    opl_ok = REG(OPL_ID) == 0x02;
    if (opl_ok) { for (i = 0; i < 0xF6; i++) opl(i, 0); opl(0x01, 0x20); patch(0, p_blip); patch(1, p_bell); patch(2, p_thud); }
    hs_load();

    ctrl_was = REG(VICKY); l0_was = REG(VICKY + 0x10); l1_was = REG(VICKY + 0x20); bg_was = REG(V_BGCOL);
    for (i = 0x21; i <= 0x25; i++) REG(VICKY + i) = 0;
    REG(VICKY + 0x26) = W & 255; REG(VICKY + 0x27) = W >> 8;
    REG(VICKY + 0x28) = 0; REG(VICKY + 0x29) = 0; REG(VICKY + 0x2A) = 0x20; REG(VICKY + 0x2B) = 0;
    REG(BLT + 0x11) = 0;
    box(0, 0, W, H, C_BLUE);
    REG(V_BGCOL) = 0;                        /* a pixel of colour 0 is the ground: black */
    REG(VICKY) = 1 | 2 | 4;                  /* 320 x 240, doubled: the Chooser's chunky pixels */
    REG(VICKY + 0x10) = 0;
    REG(VICKY + 0x20) = 0x19;
    draw_top();

    if (title()) do play(); while (game_over());
    if (opl_ok) for (i = 0; i < 0xF6; i++) opl(i, 0);
    REG(VICKY + 0x20) = l1_was; REG(VICKY + 0x10) = l0_was; REG(VICKY) = ctrl_was; REG(V_BGCOL) = bg_was;
    rom_video();
    REG(TERM + 4) = 2;
}
