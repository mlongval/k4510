/* K4510: SNAKE -- eat, grow, and do not bite yourself.  In MODE 7 (45 x 33).
 *
 * The snake goes on by itself; the arrows turn it (two turns can wait their
 * turn, so a quick corner is not lost).  An apple makes it three longer and
 * the next one appears; every fifth apple is a level, a little faster, and
 * from the second level on the walls grow bars.  Now and then a gold star
 * comes for a few seconds: five points and no growing.  A wall or its own
 * tail ends the game.  The five best scores are kept in /APPS/SNAKE/HISCORE.DAT.
 *
 *   arrows turn   P pause   M sound on/off   Escape end the game
 *
 * The screen is text32 cells written straight into the console, as TETRIS
 * does, with the status bands taken so all 33 rows are the field's; only the
 * cells that change are written, a head and a tail a step.
 */
#include "k4510.h"

#define TERM     0xDA00u
#define SCREEN   0x00030000UL
#define OPL_ADDR 0xD480u
#define OPL_DATA 0xD481u
#define OPL_ID   0xD482u
#define CMDLINE  ((char *)0x0300)

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

#define FW 45                                /* the field, the whole screen but the top line */
#define FH 32
#define E_EMPTY 0
#define E_WALL  1
#define E_BODY  2
#define E_APPLE 3
#define E_STAR  4

static uint8_t cols, rows, ox, oy, stride, flags_was;
static uint8_t field[FH][FW];
static uint8_t bx[FW * FH], by[FW * FH];     /* the body, a ring: head at hd, tail at tl */
static uint16_t hd, tl, len, grow;
static int8_t dx, dy;
static uint8_t qd[2], qn;                    /* turns waiting */
static uint8_t level, apples, over, paused, sound_on = 1, speed, star_x, star_y, star_t;
static uint16_t score;

/* ---- the screen --------------------------------------------------------- */
static uint8_t q4[4];
static void cell(uint8_t r, uint8_t c, uint8_t glyph, uint8_t fg, uint8_t bg)
{
    if (c >= cols || r >= rows) return;
    q4[0] = glyph; q4[1] = 0; q4[2] = fg; q4[3] = bg;
    dma_copy((uint32_t)(uint16_t) q4, SCREEN + ((uint32_t)(r + oy) * stride + c + ox) * 4, 4);
}
static void text(uint8_t r, uint8_t c, const char *s, uint8_t fg) { while (*s) cell(r, c++, (uint8_t) *s++, fg, BLACK); }
static void num(uint8_t r, uint8_t c, uint16_t v, uint8_t w, uint8_t fg)
{
    char b[6]; uint8_t i = 5;
    b[5] = 0;
    do { b[--i] = (char)('0' + v % 10); v /= 10; } while (v && i);
    while (i > 5 - w) b[--i] = ' ';
    text(r, c, b + i, fg);
}
static void draw_at(uint8_t x, uint8_t y)    /* one field cell, as the field says */
{
    switch (field[y][x]) {
    case E_WALL:  cell((uint8_t)(y + 1), x, 0xB1, GREY, DGREY); break;
    case E_BODY:  cell((uint8_t)(y + 1), x, 0xDB, GREEN, BLACK); break;
    case E_APPLE: cell((uint8_t)(y + 1), x, 0x03, LRED, BLACK); break;
    case E_STAR:  cell((uint8_t)(y + 1), x, 0x0F, YELLOW, BLACK); break;
    default:      cell((uint8_t)(y + 1), x, ' ', BLACK, BLACK); break;
    }
}
static void draw_field(void) { uint8_t x, y; for (y = 0; y < FH; y++) for (x = 0; x < FW; x++) draw_at(x, y); }
static void draw_status(void)
{
    uint8_t c;
    for (c = 0; c < cols && c < FW; c++) cell(0, c, ' ', WHITE, BLACK);
    text(0, 1, "SNAKE", LGREEN);
    text(0, 8, "score", GREY); num(0, 14, score, 5, WHITE);
    text(0, 21, "level", GREY); num(0, 27, level, 2, WHITE);
    text(0, 31, "length", GREY); num(0, 38, len, 4, WHITE);
}
static void banner(uint8_t r, const char *s, uint8_t fg)   /* a line across the middle of the field */
{
    uint8_t n = (uint8_t) strlen(s), c0 = (uint8_t)((FW - n) / 2 - 1), c;
    for (c = c0; c < c0 + n + 2; c++) cell(r, c, ' ', fg, BLACK);
    text(r, (uint8_t)(c0 + 1), s, fg);
}

/* ---- the sound ---------------------------------------------------------- */
static const uint8_t opslot[9] = { 0, 1, 2, 8, 9, 10, 16, 17, 18 };
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
static const uint8_t p_pluck[11] = { 0x11, 0x28, 0xF8, 0x88, 0x00, 0x01, 0x00, 0xF8, 0x69, 0x00, 0x04 };
static const uint8_t p_bell[11]  = { 0x31, 0x1E, 0xF6, 0x27, 0x02, 0x11, 0x00, 0xF4, 0x37, 0x00, 0x06 };
static const uint8_t p_thud[11]  = { 0x0E, 0x00, 0xF6, 0xF6, 0x03, 0x0E, 0x00, 0xF8, 0xF7, 0x03, 0x0E };
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
static void sound_init(void)
{
    uint8_t v;
    opl_ok = REG(OPL_ID) == 0x02;
    if (!opl_ok) return;
    for (v = 0; v < 0xF6; v++) opl(v, 0);
    opl(0x01, 0x20);
    patch(0, p_pluck); patch(1, p_bell); patch(2, p_thud);
}
static void sound_off(void) { uint8_t v; if (!opl_ok) return; for (v = 0; v < 0xF6; v++) opl(v, 0); }

/* ---- the best five (TETRIS's file, in SNAKE's directory) ---------------- */
static char hname[] = "/APPS/SNAKE/HISCORE.DAT";
static uint16_t hs_s[5];
static char hs_n[5][4];
static uint8_t hbuf[41];
static void hs_load(void)
{
    uint8_t i, j;
    for (i = 0; i < 5; i++) { hs_s[i] = 0; strcpy(hs_n[i], "---"); }
    zp16(0xF0, (uint16_t) hname); zp32(0xF2, (uint32_t)(uint16_t) hbuf);
    if (rom_load() || hbuf[40] != 'S') return;
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
    hbuf[40] = 'S';
    zp16(0xF0, (uint16_t) hname); zp32(0xF2, (uint32_t)(uint16_t) hbuf); zp32(0xF6, 41);
    rom_save();
}

/* ---- the game ----------------------------------------------------------- */
static uint16_t seed = 1;
static uint8_t rnd(void) { seed = seed * 25173u + 13849u; return (uint8_t)(seed >> 8); }
static void place(uint8_t what, uint8_t *px, uint8_t *py)   /* somewhere empty, not right in front of the head */
{
    uint8_t x, y, tries = 0;
    do {
        x = (uint8_t)(1 + rnd() % (FW - 2)); y = (uint8_t)(1 + rnd() % (FH - 2));
        if (++tries > 200) break;
    } while (field[y][x] != E_EMPTY || (x == (uint8_t)(bx[hd] + dx) && y == (uint8_t)(by[hd] + dy)));
    field[y][x] = what; draw_at(x, y);
    if (px) { *px = x; *py = y; }
}
static void build_level(void)
{
    uint8_t x, y, i;
    memset(field, E_EMPTY, sizeof field);
    for (x = 0; x < FW; x++) { field[0][x] = E_WALL; field[FH - 1][x] = E_WALL; }
    for (y = 0; y < FH; y++) { field[y][0] = E_WALL; field[y][FW - 1] = E_WALL; }
    if (level >= 2) for (x = 10; x < 35; x++) { field[8][x] = E_WALL; field[FH - 9][x] = E_WALL; }        /* two bars */
    if (level >= 3) for (y = 12; y < 20; y++) { field[y][8] = E_WALL; field[y][FW - 9] = E_WALL; }       /* two posts */
    if (level >= 4) for (i = 0; i < 4; i++) { field[15][20 + i] = E_WALL; field[16][20 + i] = E_WALL; }  /* a block in the middle */
    /* the snake: five long, in the middle of the bottom half, going right */
    hd = 4; tl = 0; len = 5; grow = 0; dx = 1; dy = 0; qn = 0;
    for (i = 0; i < 5; i++) { bx[i] = (uint8_t)(6 + i); by[i] = FH - 5; field[FH - 5][6 + i] = E_BODY; }
    star_t = 0;
    draw_field();
    place(E_APPLE, 0, 0);
    speed = (uint8_t)(level < 8 ? 8 - level : 3);
    if (speed < 3) speed = 3;
}
static void turn(uint8_t k)
{
    int8_t nx = 0, ny = 0, lx = dx, ly = dy;
    if (qn) { uint8_t last = qd[qn - 1]; lx = last == K_LEFT ? -1 : last == K_RIGHT ? 1 : 0; ly = last == K_UP ? -1 : last == K_DOWN ? 1 : 0; }
    switch (k) { case K_UP: ny = -1; break; case K_DOWN: ny = 1; break; case K_LEFT: nx = -1; break; default: nx = 1; }
    if (nx == -lx && ny == -ly) return;       /* straight back into itself: no */
    if (nx == lx && ny == ly) return;         /* the way it goes already */
    if (qn < 2) qd[qn++] = k;
}
static void step(void)
{
    uint8_t nx, ny, what;
    if (qn) {
        uint8_t k = qd[0];
        dx = (int8_t)(k == K_LEFT ? -1 : k == K_RIGHT ? 1 : 0); dy = (int8_t)(k == K_UP ? -1 : k == K_DOWN ? 1 : 0);
        qd[0] = qd[1]; qn--;
    }
    nx = (uint8_t)(bx[hd] + dx); ny = (uint8_t)(by[hd] + dy);
    what = field[ny][nx];
    if (what == E_BODY && !(nx == bx[tl] && ny == by[tl] && !grow)) what = E_WALL;   /* the tail moves out of the way, unless it is growing */
    if (what == E_WALL) { over = 1; sfx(2, N(2, C), 20); return; }
    if (!grow) { field[by[tl]][bx[tl]] = E_EMPTY; draw_at(bx[tl], by[tl]); tl = (uint16_t)((tl + 1) % (FW * FH)); }
    else { grow--; len++; }
    cell((uint8_t)(by[hd] + 1), bx[hd], 0xDB, GREEN, BLACK);              /* the old head is body now */
    hd = (uint16_t)((hd + 1) % (FW * FH)); bx[hd] = nx; by[hd] = ny;
    field[ny][nx] = E_BODY;
    cell((uint8_t)(ny + 1), nx, 0x02, LGREEN, BLACK);                      /* the head: a face */
    if (what == E_APPLE) {
        score += (uint16_t)(1 + level); grow += 3; apples++;
        sfx(0, (uint8_t)(N(5, C) + (apples % 5) * 2), 6);
        if (apples % 5 == 0) {
            level++; sfx(1, N(6, C), 30);
            build_level();
            draw_status();
            return;
        }
        place(E_APPLE, 0, 0);
        if (!star_t && rnd() < 60) { place(E_STAR, &star_x, &star_y); star_t = 200; }
    } else if (what == E_STAR) {
        score += 5; star_t = 0; sfx(1, N(6, G), 20);
    }
    draw_status();
}

static uint8_t wait_key(void) { uint8_t k; while ((k = key_get()) == 0) { seed++; sfx_tick(); } return k; }
static void play(void)
{
    uint8_t lf, fc, d, k, t = 0;
    level = 1; apples = 0; score = 0; over = 0; paused = 0;
    build_level(); draw_status();
    lf = REG(SYS + 0x0D);
    while (!over) {
        fc = REG(SYS + 0x0D);
        if (fc == lf) continue;
        d = (uint8_t)(fc - lf); lf = fc;
        while ((k = key_get()) != 0) {
            if (k == K_ESC) { over = 1; break; }
            if (k == 'p' || k == 'P') { paused ^= 1; if (paused) banner(FH / 2, "PAUSED -- P goes on", YELLOW); else draw_field(); }
            else if (k == 'm' || k == 'M') sound_on ^= 1;
            else if (!paused && k >= K_UP && k <= K_RIGHT) turn(k);
        }
        if (paused) continue;
        while (d-- && !over) {
            sfx_tick();
            if (star_t && !--star_t) { field[star_y][star_x] = E_EMPTY; draw_at(star_x, star_y); }
            if (++t >= speed) { t = 0; step(); }
        }
    }
}
static uint8_t game_over(void)
{
    static char ini[4];
    uint8_t k, i, j, pos = 0, r;
    for (k = 0; k < 45; k++) wait_vblank();
    while (key_get()) ;
    r = FH / 2 - 4;
    banner(r, "GAME OVER", LRED);
    for (i = 0; i < 5 && hs_s[i] >= score; i++) ;
    if (i < 5 && score) {
        banner((uint8_t)(r + 2), "A NEW BEST SCORE -- YOUR INITIALS", YELLOW);
        strcpy(ini, "___");
        for (;;) {
            banner((uint8_t)(r + 3), ini, WHITE);
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
            hs_save();
        }
    }
    banner((uint8_t)(r + 5), "BEST", GREY);
    for (j = 0; j < 5; j++) {
        static char line[12];
        uint16_t v = hs_s[j]; uint8_t n;
        strcpy(line, hs_n[j]); strcat(line, "  ");
        for (n = 0; n < 5; n++) line[9 - n] = (char)(n && !v ? ' ' : '0' + v % 10), v /= 10;
        line[10] = 0;
        banner((uint8_t)(r + 6 + j), line, j == i ? YELLOW : WHITE);
    }
    banner((uint8_t)(r + 12), "SPACE AGAIN   ESC QUIT", WHITE);
    for (;;) { k = wait_key(); if (k == ' ' || k == 13) return 1; if (k == K_ESC) return 0; }
}

/* ---- in and out --------------------------------------------------------- */
static char mode_was;
static void mode_run(char digit) { strcpy(CMDLINE, "MODE 0"); CMDLINE[5] = digit; rom_shell(CMDLINE); }
void main(void)
{
    uint8_t k;
    switch (REG(VICKY) & 0x3E) {             /* VICKY CTRL -> the MODE digit, as LOGO and TETRIS read it */
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
    REG(TERM + 0x0E) = (uint8_t)((flags_was | 8) & ~1);
    rom_video();
    cols = REG(TERM + 5); rows = REG(TERM + 6);
    ox = REG(TERM + 7); oy = REG(TERM + 8);
    stride = REG(TERM + 0x0D);
    if (!cols) cols = 45;
    if (!rows) rows = 33;
    if (!stride) stride = cols;
    sound_init();
    hs_load();

    level = 1; build_level(); draw_status();
    banner(FH / 2 - 2, "SNAKE", LGREEN);
    banner(FH / 2, "arrows turn   P pause   M sound", GREY);
    banner(FH / 2 + 2, "SPACE starts   ESC leaves", WHITE);
    for (;;) { k = wait_key(); if (k == ' ' || k == 13) break; if (k == K_ESC) goto out; }
    do play(); while (game_over());
out:
    sound_off();
    REG(TERM + 0x0E) = (uint8_t)(flags_was & ~8);
    rom_video();
    if (mode_was != '7') mode_run(mode_was);
    else rom_chrout(12);
}
