/* K4510: BREAKOUT -- a wall of bricks, a ball, and a paddle to keep it up.
 *
 * On a 640x480 bitmap (VICKY layer 1, as PAINT and BOOK's pictures use it).
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
 * The blitter does all the drawing (fills); the ball and the paddle are
 * rubbed out and drawn again where they have moved, once a frame.
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
#define W 640
#define H 480

static void rom_video(void) { ((void (*)(void))0xFF92)(); }
static unsigned char rom_save(void) { return ((unsigned char (*)(void))0xFF8C)(); }
static unsigned char rom_load(void) { return ((unsigned char (*)(void))0xFF89)(); }
static void zp16(uint8_t a, uint16_t v) { REG(a) = v; REG(a + 1) = v >> 8; }
static void zp32(uint8_t a, uint32_t v) { REG(a) = v; REG(a + 1) = v >> 8; REG(a + 2) = v >> 16; REG(a + 3) = v >> 24; }

/* the game's colours, 16-27 (put back on the way out) */
#define C_BLACK  0
#define C_WALL   16
#define C_PADDLE 17
#define C_BALL   18
#define C_TEXT   19
#define C_DIM    20
#define C_RED    21
#define C_ORANGE 22
#define C_GREEN  23
#define C_YELLOW 24
#define C_BLUE   25
static const uint8_t mypal[30] = {
    0x80,0x80,0x90,  0x60,0xC0,0xFF,  0xFF,0xFF,0xFF,  0xFF,0xFF,0xFF,  0x80,0x80,0x80,
    0xD8,0x30,0x30,  0xF0,0x90,0x20,  0x30,0xB0,0x40,  0xF0,0xE0,0x40,  0x40,0x60,0xE0 };

#define BCOLS 14
#define BROWS 8
#define BX0   12                             /* the wall's left edge */
#define BY0   72
#define BW    44                             /* a brick and its gap */
#define BH    18
#define TOP   40                             /* the play area's roof */
#define PY    440                            /* the paddle's top */
#define PH    10
#define BALL  8

static uint8_t brick[BROWS][BCOLS], left;
static const uint8_t rowcol[BROWS] = { C_RED, C_RED, C_ORANGE, C_ORANGE, C_GREEN, C_GREEN, C_YELLOW, C_YELLOW };
static const uint8_t rowpts[BROWS] = { 7, 7, 5, 5, 3, 3, 1, 1 };
static int bx, by, vx, vy;                   /* the ball, in 1/16 pixels */
static int px, pw = 80, opx = -1, obx = -1, oby = -1;
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
static uint8_t gl[16];
static void text(int x, int y, const char *s, uint8_t fg)   /* the 8x8 font, twice the size */
{
    uint8_t r, b, i;
    for (; *s; s++, x += 16)
        for (r = 0; r < 8; r++) {
            b = far_peek(FONT8 + (uint16_t)(uint8_t) *s * 8 + r);
            for (i = 0; i < 8; i++) gl[i * 2] = gl[i * 2 + 1] = (b & (0x80 >> i)) ? fg : C_BLACK;
            dma_copy((uint32_t)(uint16_t) gl, BITMAP + (uint32_t)(y + r * 2) * W + x, 16);
            dma_copy((uint32_t)(uint16_t) gl, BITMAP + (uint32_t)(y + r * 2 + 1) * W + x, 16);
        }
}
static char nb[8];
static const char *num(uint16_t v, uint8_t w)
{
    uint8_t i = 7;
    nb[7] = 0;
    do { nb[--i] = (char)('0' + v % 10); v /= 10; } while (v && i);
    while (i > 7 - w) nb[--i] = ' ';
    return nb + i;
}
static void center(int y, const char *s, uint8_t fg)
{
    int n = (int) strlen(s);
    box(W / 2 - n * 8 - 8, y - 4, n * 16 + 16, 24, C_BLACK);
    text(W / 2 - n * 8, y, s, fg);
}

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

/* ---- the game ----------------------------------------------------------- */
static void draw_brick(uint8_t r, uint8_t c)
{
    int x = BX0 + c * BW, y = BY0 + r * BH;
    if (brick[r][c]) { box(x, y, BW - 4, BH - 4, rowcol[r]); box(x, y, BW - 4, 2, C_TEXT); }
    else box(x, y, BW - 4, BH - 4, C_BLACK);
}
static void draw_status(void)
{
    uint8_t i;
    box(0, 0, W, TOP - 8, C_BLACK);
    text(16, 8, "SCORE", C_DIM); text(112, 8, num(score, 5), C_TEXT);
    text(260, 8, "LEVEL", C_DIM); text(356, 8, num(level, 2), C_TEXT);
    for (i = 0; i < lives && i < 8; i++) box(W - 30 - i * 24, 12, 16, 8, C_PADDLE);
}
static void new_wall(void)
{
    uint8_t r, c;
    box(0, TOP - 8, W, H - TOP + 8, C_BLACK);
    box(0, TOP - 8, W, 8, C_WALL); box(0, TOP - 8, 8, H - TOP + 8, C_WALL); box(W - 8, TOP - 8, 8, H - TOP + 8, C_WALL);
    for (r = 0; r < BROWS; r++) for (c = 0; c < BCOLS; c++) { brick[r][c] = 1; draw_brick(r, c); }
    left = BROWS * BCOLS; hits = 0; reached_orange = reached_red = 0;
    speed = (uint8_t)(3 + level);            /* sixteenths of a pixel a frame, times sixteen: see serve() */
    opx = obx = oby = -1;
}
static void serve(void)
{
    served = 0;
    bx = (px + pw / 2 - BALL / 2) * 16; by = (PY - BALL - 1) * 16;
    vx = 0; vy = 0;
}
static void launch(void)
{
    served = 1;
    vx = (level & 1 ? 1 : -1) * speed * 8; vy = -speed * 14;
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
    if ((x - BX0) % BW >= BW - 4 || (y - BY0) % BH >= BH - 4) return 0;   /* the gap */
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
    if (x < 8) { nx = 8 * 16; vx = -vx; sfx(1, N(5, G), 4); }
    else if (x + BALL > W - 8) { nx = (W - 8 - BALL) * 16; vx = -vx; sfx(1, N(5, G), 4); }
    else if (hit_brick(x, y)) { vx = -vx; nx = bx; }
    bx = nx;
    ny = by + vy; x = bx / 16; y = ny / 16;
    if (y < TOP) { ny = TOP * 16; vy = -vy; sfx(1, N(5, G), 4); }
    else if (vy > 0 && y + BALL >= PY && y + BALL <= PY + PH && x + BALL > px && x < px + pw) {
        off = (x + BALL / 2) - (px + pw / 2);            /* -pw/2 .. pw/2 */
        vy = -(speed * 14);
        vx = (int)((long) off * speed * 26 / (pw / 2));
        ny = (PY - BALL) * 16;
        sfx(1, N(4, C), 5);
    }
    else if (hit_brick(x, y)) { vy = -vy; ny = by; }
    else if (y > H) {                                    /* missed */
        sfx(2, N(2, C), 25);
        box(obx, oby, BALL, BALL, C_BLACK); obx = -1;
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
        if (opx >= 0) { if (px > opx) box(opx, PY, px - opx, PH, C_BLACK); else box(px + pw, PY, opx - px, PH, C_BLACK); }
        box(px, PY, pw, PH, C_PADDLE); box(px, PY, pw, 2, C_TEXT);
        opx = px;
    }
    if (x != obx || y != oby) {
        if (obx >= 0) box(obx, oby, BALL, BALL, C_BLACK);
        box(x, y, BALL, BALL, C_BALL);
        obx = x; oby = y;
    }
}

static int mouse_was = -1;
static uint8_t bwas;
static void control(void)
{
    uint8_t held = keys_held(), b = REG(MOUSEB);
    int mx = REG(MOUSEX) | (REG(MOUSEX + 1) << 8);
    if (mouse_was >= 0 && mx != mouse_was) px = mx - pw / 2;          /* the mouse moved: it has the paddle */
    mouse_was = mx;
    if (held & HELD_LEFT) px -= 9;
    if (held & HELD_RIGHT) px += 9;
    if (px < 8) px = 8;
    if (px > W - 8 - pw) px = W - 8 - pw;
    if (!served) { bx = (px + pw / 2 - BALL / 2) * 16; if ((b & 1) && !(bwas & 1)) launch(); }
    bwas = b;
}

static uint8_t wait_key(void) { uint8_t k; while ((k = key_get()) == 0) sfx_tick(); return k; }
static void play(void)
{
    uint8_t lf, fc, d, k;
    level = 1; lives = 5; score = 0; over = 0; paused = 0;
    px = W / 2 - pw / 2;
    new_wall(); draw_status(); serve();
    lf = REG(SYS + 0x0D);
    while (!over) {
        fc = REG(SYS + 0x0D);
        if (fc == lf) continue;
        d = (uint8_t)(fc - lf); lf = fc;
        if (d > 3) d = 3;
        while ((k = key_get()) != 0) {
            if (k == 0x1B) { over = 1; break; }
            if (k == 'p' || k == 'P') { paused ^= 1; if (paused) center(300, "PAUSED", C_YELLOW); else { box(0, 290, W, 30, C_BLACK); obx = -1; opx = -1; } }
            else if (k == 'm' || k == 'M') sound_on ^= 1;
            else if (k == ' ' && !served && !paused) launch();
        }
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
static uint8_t game_over(void)
{
    static char ini[4], line[12];
    uint8_t k, i, j, pos = 0, n;
    uint16_t v;
    for (k = 0; k < 45; k++) wait_vblank();
    while (key_get()) ;
    box(120, 200, W - 240, 250, C_BLACK);
    center(210, "GAME OVER", C_RED);
    for (i = 0; i < 5 && hs_s[i] >= score; i++) ;
    if (i < 5 && score) {
        center(240, "YOUR INITIALS", C_YELLOW);
        strcpy(ini, "___");
        for (;;) {
            center(266, ini, C_TEXT);
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
        box(120, 236, W - 240, 50, C_BLACK);
    }
    for (j = 0; j < 5; j++) {
        strcpy(line, hs_n[j]); strcat(line, " ");
        v = hs_s[j];
        for (n = 0; n < 5; n++) { line[8 - n] = (char)(n && !v ? ' ' : '0' + v % 10); v /= 10; }
        line[9] = 0;
        center(300 + j * 24, line, j == i ? C_YELLOW : C_TEXT);
    }
    center(430, "SPACE AGAIN  ESC QUIT", C_DIM);
    for (;;) { k = wait_key(); if (k == ' ' || k == 13) return 1; if (k == 0x1B) return 0; }
}

void main(void)
{
    static uint8_t palsave[10 * 3];
    uint8_t i, k, ctrl_was, l0_was, bg_was;
    for (i = 0; i < 10; i++) { REG(V_PALIDX) = (uint8_t)(16 + i); palsave[i * 3] = REG(V_PALR); palsave[i * 3 + 1] = REG(V_PALG); palsave[i * 3 + 2] = REG(V_PALB); }
    for (i = 0; i < 10; i++) pal((uint8_t)(16 + i), mypal[i * 3], mypal[i * 3 + 1], mypal[i * 3 + 2]);
    opl_ok = REG(OPL_ID) == 0x02;
    if (opl_ok) { for (i = 0; i < 0xF6; i++) opl(i, 0); opl(0x01, 0x20); patch(0, p_blip); patch(1, p_bell); patch(2, p_thud); }
    hs_load();

    ctrl_was = REG(VICKY); l0_was = REG(VICKY + 0x10); bg_was = REG(V_BGCOL);
    for (i = 0x21; i <= 0x25; i++) REG(VICKY + i) = 0;
    REG(VICKY + 0x26) = W & 255; REG(VICKY + 0x27) = W >> 8;
    REG(VICKY + 0x28) = 0; REG(VICKY + 0x29) = 0; REG(VICKY + 0x2A) = 0x20; REG(VICKY + 0x2B) = 0;
    REG(BLT + 0x11) = 0;
    box(0, 0, W, H, C_BLACK);
    REG(V_BGCOL) = 0;
    REG(VICKY) = (uint8_t)(ctrl_was & 0xD9);  /* 640x480, as PAINT */
    REG(VICKY + 0x10) = 0;
    REG(VICKY + 0x20) = 0x19;

    level = 1; lives = 5; score = 0;
    new_wall(); draw_status();
    center(300, "BREAKOUT", C_YELLOW);
    center(340, "MOUSE OR ARROWS  SPACE SERVES", C_DIM);
    center(370, "SPACE STARTS  ESC LEAVES", C_TEXT);
    for (;;) { k = wait_key(); if (k == ' ' || k == 13) break; if (k == 0x1B) goto out; }
    do play(); while (game_over());
out:
    if (opl_ok) for (i = 0; i < 0xF6; i++) opl(i, 0);
    REG(VICKY + 0x20) = 0; REG(VICKY + 0x10) = l0_was; REG(VICKY) = ctrl_was; REG(V_BGCOL) = bg_was;
    for (i = 0; i < 10; i++) pal((uint8_t)(16 + i), palsave[i * 3], palsave[i * 3 + 1], palsave[i * 3 + 2]);
    rom_video();
    REG(TERM + 4) = 2;
}
