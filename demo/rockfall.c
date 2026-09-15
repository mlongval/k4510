/* K4510: ROCKFALL -- dig for diamonds under rocks that fall.  In MODE 7.
 *
 * A cave of dirt, rocks and diamonds.  You dig through the dirt; a rock or
 * a diamond with nothing under it falls, and one resting on another rock,
 * a diamond or a wall rolls off sideways when there is room.  A rock that
 * falls on you ends that life -- so does the clock.  Gather enough diamonds
 * and the exit opens; reach it for the time that is left as points.  Rocks
 * can be pushed sideways into space.  Every cave is made from its number,
 * so cave 7 is always cave 7, and there is no last one.
 *
 *   arrows dig and walk (hold them)   Space + an arrow: take without moving
 *   R give up the cave (a life)   P pause   M sound   Escape end the game
 *
 * The rules are the ones the old cave games used: the cave is looked at
 * from the bottom up ten times a second, each thing moving at most once a
 * look.  The screen is text32 cells, a row of the cave to a DMA.  The best
 * five scores are kept in /APPS/ROCKFALL/HISCORE.DAT.
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
#define K_ESC 0x1B

#define CW 40                                /* the cave */
#define CH 26
#define CX 2                                 /* where it sits on the screen */
#define CY 2
enum { T_EMPTY, T_DIRT, T_ROCK, T_DIAMOND, T_WALL, T_STEEL, T_EXIT, T_PLAYER, T_BOOM };
#define F_FALL  0x10                         /* a rock or diamond on its way down */
#define F_MOVED 0x20                         /* it has had its move this look */
#define KIND(t) ((t) & 15)

static uint8_t g[CH][CW];
static uint8_t cols, rows, ox, oy, stride, flags_was;
static uint8_t cave, lives, got, need, pxx, pyy, alive, done, over, paused, sound_on = 1, tick, push_try, exit_open;
static uint16_t score, tleft;

/* ---- the screen --------------------------------------------------------- */
static uint8_t rb[48 * 4], rn;
static void rb_put(uint8_t glyph, uint8_t fg, uint8_t bg)
{
    uint8_t *q = rb + rn * 4;
    if (rn >= 48) return;
    q[0] = glyph; q[1] = 0; q[2] = fg; q[3] = bg; rn++;
}
static void rb_out(uint8_t r, uint8_t c)
{
    if (r < rows && rn) dma_copy((uint32_t)(uint16_t) rb, SCREEN + ((uint32_t)(r + oy) * stride + c + ox) * 4, (uint32_t) rn * 4);
    rn = 0;
}
static void text(uint8_t r, uint8_t c, const char *s, uint8_t fg) { while (*s) rb_put((uint8_t) *s++, fg, BLACK); rb_out(r, c); }
static char nb[6];
static const char *num(uint16_t v, uint8_t w)
{
    uint8_t i = 5;
    nb[5] = 0;
    do { nb[--i] = (char)('0' + v % 10); v /= 10; } while (v && i);
    while (i > 5 - w) nb[--i] = ' ';
    return nb + i;
}
static void clear_screen(void) { uint8_t r, c; for (r = 0; r < rows; r++) { for (c = 0; c < cols && c < 48; c++) rb_put(' ', WHITE, BLACK); rb_out(r, 0); } }
static void banner(uint8_t r, const char *s, uint8_t fg)
{
    uint8_t n = (uint8_t) strlen(s), c0 = (uint8_t)((45 - n) / 2 - 1), i;
    rb_put(' ', fg, BLACK); for (i = 0; i < n; i++) rb_put((uint8_t) s[i], fg, BLACK); rb_put(' ', fg, BLACK);
    rb_out(r, c0);
}
static void draw_cave(void)
{
    uint8_t x, y, t, k;
    for (y = 0; y < CH; y++) {
        for (x = 0; x < CW; x++) {
            t = g[y][x]; k = KIND(t);
            switch (k) {
            case T_DIRT:    rb_put(0xB0, BROWN, BLACK); break;
            case T_ROCK:    rb_put(0x09, LGREY, BLACK); break;
            case T_DIAMOND: rb_put(0x04, (uint8_t)(((x + y + tick) & 3) ? CYAN : WHITE), BLACK); break;
            case T_WALL:    rb_put(0xB1, LRED, BLACK); break;
            case T_STEEL:   rb_put(0xDB, DGREY, BLACK); break;
            case T_EXIT:    if (exit_open) rb_put(0x7F, (uint8_t)((tick & 4) ? YELLOW : WHITE), BLACK); else rb_put(0xDB, DGREY, BLACK); break;
            case T_PLAYER:  rb_put(0x02, YELLOW, BLACK); break;
            case T_BOOM:    rb_put(0x0F, ORANGE, BLACK); break;
            default:        rb_put(' ', BLACK, BLACK); break;
            }
        }
        rb_out((uint8_t)(CY + y), CX);
    }
}
static void draw_status(void)
{
    uint8_t c;
    for (c = 0; c < 45; c++) rb_put(' ', WHITE, BLACK);
    rb_out(0, 0);
    text(0, 1, "ROCKFALL", YELLOW);
    text(0, 10, "cave", GREY); text(0, 15, num(cave, 2), WHITE);
    rb_put(0x04, CYAN, BLACK); rb_out(0, 19);
    text(0, 20, num(got, 2), got >= need ? YELLOW : WHITE); text(0, 22, "/", GREY); text(0, 23, num(need, 2), WHITE);
    text(0, 27, "time", GREY); text(0, 32, num(tleft, 3), tleft < 20 ? LRED : WHITE);
    text(0, 36, num(score, 5), WHITE);
    for (c = 0; c < 3; c++) rb_put((uint8_t)(c < lives ? 0x02 : ' '), YELLOW, BLACK);
    rb_out(1, 41);
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
static const uint8_t p_dig[11]  = { 0x0F, 0x08, 0xF8, 0xF8, 0x03, 0x0E, 0x10, 0xF9, 0xF9, 0x03, 0x0E };
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
static char hname[] = "/APPS/ROCKFALL/HISCORE.DAT";
static uint16_t hs_s[5];
static char hs_n[5][4];
static uint8_t hbuf[41];
static void hs_load(void)
{
    uint8_t i, j;
    for (i = 0; i < 5; i++) { hs_s[i] = 0; strcpy(hs_n[i], "---"); }
    zp16(0xF0, (uint16_t) hname); zp32(0xF2, (uint32_t)(uint16_t) hbuf);
    if (rom_load() || hbuf[40] != 'R') return;
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
    hbuf[40] = 'R';
    zp16(0xF0, (uint16_t) hname); zp32(0xF2, (uint32_t)(uint16_t) hbuf); zp32(0xF6, 41);
    rom_save();
}

/* ---- a cave, from its number -------------------------------------------- */
static uint16_t seed;
static uint8_t rnd(void) { seed = seed * 25173u + 13849u; return (uint8_t)(seed >> 8); }
static void make_cave(void)
{
    uint8_t x, y, i, n, len, count = 0, rock, gem;
    seed = (uint16_t)(cave * 7919u + 17);
    rock = (uint8_t)(40 + cave * 3 > 80 ? 80 : 40 + cave * 3);        /* out of 256: more rocks as the caves go */
    gem = 26;
    for (y = 0; y < CH; y++) for (x = 0; x < CW; x++) {
        if (!x || !y || x == CW - 1 || y == CH - 1) { g[y][x] = T_STEEL; continue; }
        n = rnd();
        g[y][x] = n < rock ? T_ROCK : n < rock + gem ? T_DIAMOND : n < rock + gem + 20 ? T_EMPTY : T_DIRT;
    }
    for (i = 0; i < (uint8_t)(3 + cave / 2) && i < 9; i++) {            /* walls */
        y = (uint8_t)(3 + rnd() % (CH - 6)); x = (uint8_t)(2 + rnd() % (CW - 16)); len = (uint8_t)(5 + rnd() % 10);
        for (n = 0; n < len && x + n < CW - 1; n++) g[y][x + n] = T_WALL;
    }
    pxx = 3; pyy = 2;                                                   /* the start: a pocket of dirt, nothing to fall in */
    for (y = 1; y <= 3; y++) for (x = 2; x <= 4; x++) g[y][x] = T_DIRT;
    g[pyy][pxx] = T_PLAYER;
    y = (uint8_t)(4 + rnd() % (CH - 8));
    g[y][CW - 2] = T_EXIT; g[y][CW - 3] = T_DIRT;
    for (y = 1; y < CH - 1; y++) for (x = 1; x < CW - 1; x++) if (g[y][x] == T_DIAMOND) count++;
    need = (uint8_t)(count / 3 + cave);                             /* a third of what is there, and one more a cave */
    if (need > count) need = count;
    if (need < 5) need = count < 5 ? count : 5;
    got = 0; exit_open = 0; alive = 1; done = 0; push_try = 0;
    tleft = (uint16_t)(cave < 10 ? 160 - cave * 6 : 100);
}

/* ---- the rules ---------------------------------------------------------- */
static void explode(uint8_t cx, uint8_t cy)  /* three by three, the steel excepted */
{
    uint8_t x, y;
    for (y = (uint8_t)(cy - 1); y <= (uint8_t)(cy + 1); y++)
        for (x = (uint8_t)(cx - 1); x <= (uint8_t)(cx + 1); x++)
            if (KIND(g[y][x]) != T_STEEL && KIND(g[y][x]) != T_EXIT) g[y][x] = T_BOOM | F_MOVED;
    alive = 0;
    sfx(2, N(1, C), 40);
}
static void physics(void)
{
    uint8_t x, y, t, k, below;
    for (y = CH - 2; y >= 1; y--) {
        for (x = 1; x < CW - 1; x++) {
            t = g[y][x];
            if (t & F_MOVED) continue;
            k = KIND(t);
            if (k == T_BOOM) { g[y][x] = T_EMPTY; continue; }
            if (k != T_ROCK && k != T_DIAMOND) continue;
            below = KIND(g[y + 1][x]);
            if (below == T_EMPTY) {
                g[y + 1][x] = (uint8_t)(k | F_FALL | F_MOVED); g[y][x] = T_EMPTY;
            } else if (below == T_PLAYER && (t & F_FALL)) {
                explode(x, (uint8_t)(y + 1));
            } else if (below == T_ROCK || below == T_DIAMOND || below == T_WALL) {
                if (KIND(g[y][x - 1]) == T_EMPTY && KIND(g[y + 1][x - 1]) == T_EMPTY) { g[y][x - 1] = (uint8_t)(k | F_FALL | F_MOVED); g[y][x] = T_EMPTY; }
                else if (KIND(g[y][x + 1]) == T_EMPTY && KIND(g[y + 1][x + 1]) == T_EMPTY) { g[y][x + 1] = (uint8_t)(k | F_FALL | F_MOVED); g[y][x] = T_EMPTY; }
                else { if (t & F_FALL) sfx(2, k == T_ROCK ? N(2, G) : N(5, E), 4); g[y][x] = k; }
            } else {
                if (t & F_FALL) sfx(2, k == T_ROCK ? N(2, G) : N(5, E), 4);
                g[y][x] = k;
            }
        }
    }
    for (y = 0; y < CH; y++) for (x = 0; x < CW; x++) g[y][x] &= (uint8_t) ~F_MOVED;
}
static void player(void)
{
    uint8_t held = keys_held(), tx, ty, t, k, grab;
    int8_t dx = 0, dy = 0;
    if (!alive) return;
    if (held & HELD_UP) dy = -1; else if (held & HELD_DOWN) dy = 1; else if (held & HELD_LEFT) dx = -1; else if (held & HELD_RIGHT) dx = 1;
    if (!dx && !dy) { push_try = 0; return; }
    grab = (held & HELD_FIRE) != 0;
    tx = (uint8_t)(pxx + dx); ty = (uint8_t)(pyy + dy);
    t = g[ty][tx]; k = KIND(t);
    switch (k) {
    case T_DIRT: sfx(0, N(4, C), 2); /* fall through */
    case T_EMPTY: break;
    case T_DIAMOND:
        got++; score += exit_open ? 15 : 10; sfx(1, (uint8_t)(N(6, C) + got % 7), 12);
        if (got == need) { exit_open = 1; sfx(1, N(6, G), 30); }
        break;
    case T_EXIT:
        if (!exit_open) return;
        if (grab) return;
        done = 1; break;
    case T_ROCK:
        if (dy || (t & F_FALL) || KIND(g[ty][tx + dx]) != T_EMPTY) return;
        if (++push_try < 2) return;                                    /* a rock takes a moment to shift */
        push_try = 0;
        if (grab) return;
        g[ty][tx + dx] = T_ROCK | F_MOVED; sfx(2, N(3, C), 3);
        break;
    default: return;
    }
    if (grab) { g[ty][tx] = T_EMPTY; return; }
    g[pyy][pxx] = T_EMPTY;
    pxx = tx; pyy = ty;
    g[pyy][pxx] = T_PLAYER | F_MOVED;
}

static uint8_t wait_key(void) { uint8_t k; while ((k = key_get()) == 0) sfx_tick(); return k; }
static void play_cave(void)                  /* until the cave is done, a life is lost, or Esc */
{
    uint8_t lf, fc, d, k, frames = 0, sec = 0, wait = 0;
    make_cave(); clear_screen(); draw_status(); draw_cave();
    lf = REG(SYS + 0x0D);
    for (;;) {
        fc = REG(SYS + 0x0D);
        if (fc == lf) continue;
        d = (uint8_t)(fc - lf); lf = fc;
        while ((k = key_get()) != 0) {
            if (k == K_ESC) { over = 1; return; }
            if (k == 'p' || k == 'P') { paused ^= 1; if (paused) banner(CY + CH / 2, "PAUSED -- P goes on", YELLOW); else draw_cave(); }
            else if (k == 'm' || k == 'M') sound_on ^= 1;
            else if ((k == 'r' || k == 'R') && alive && !paused) { explode(pxx, pyy); }
        }
        if (paused) continue;
        while (d--) {
            sfx_tick();
            if (++frames >= 6) {             /* ten looks a second */
                frames = 0; tick++;
                player();
                if (done) break;
                physics();
                if (!alive) wait++;
                if (++sec >= 10) { sec = 0; if (alive && tleft) { tleft--; if (!tleft) explode(pxx, pyy); } draw_status(); }
                draw_cave();
                if (got == need && exit_open == 1) { exit_open = 2; draw_status(); }
            }
        }
        if (done) {
            sfx(1, N(6, C), 40);
            while (tleft) { tleft--; score++; if (!(tleft & 7)) { draw_status(); wait_vblank(); } }
            draw_status();
            banner(CY + CH / 2, "OUT! -- the next cave", YELLOW);
            for (k = 0; k < 90; k++) { wait_vblank(); sfx_tick(); }
            cave++;
            return;
        }
        if (!alive && wait > 15) {           /* a moment to see it, then the cave again */
            lives--;
            if (!lives) { over = 1; return; }
            banner(CY + CH / 2, "OUCH -- the cave again", LRED);
            for (k = 0; k < 60; k++) { wait_vblank(); sfx_tick(); }
            return;
        }
    }
}
static uint8_t game_over(void)
{
    static char ini[4], line[12];
    uint8_t k, i, j, pos = 0, n, r = CY + 5;
    uint16_t v;
    for (k = 0; k < 30; k++) wait_vblank();
    while (key_get()) ;
    clear_screen();
    banner(r, "GAME OVER", LRED);
    banner((uint8_t)(r + 1), "score", GREY); banner((uint8_t)(r + 2), num(score, 1), WHITE);
    for (i = 0; i < 5 && hs_s[i] >= score; i++) ;
    if (i < 5 && score) {
        banner((uint8_t)(r + 4), "A NEW BEST SCORE -- YOUR INITIALS", YELLOW);
        strcpy(ini, "___");
        for (;;) {
            banner((uint8_t)(r + 5), ini, WHITE);
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
    banner((uint8_t)(r + 7), "BEST", GREY);
    for (j = 0; j < 5; j++) {
        strcpy(line, hs_n[j]); strcat(line, " ");
        v = hs_s[j];
        for (n = 0; n < 5; n++) { line[8 - n] = (char)(n && !v ? ' ' : '0' + v % 10); v /= 10; }
        line[9] = 0;
        banner((uint8_t)(r + 8 + j), line, j == i ? YELLOW : WHITE);
    }
    banner((uint8_t)(r + 14), "SPACE AGAIN   ESC QUIT", WHITE);
    for (;;) { k = wait_key(); if (k == ' ' || k == 13) return 1; if (k == K_ESC) return 0; }
}

/* ---- in and out --------------------------------------------------------- */
static char mode_was;
static void mode_run(char digit) { strcpy(CMDLINE, "MODE 0"); CMDLINE[5] = digit; rom_shell(CMDLINE); }
void main(void)
{
    uint8_t k, i;
    switch (REG(VICKY) & 0x3E) {
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
    opl_ok = REG(OPL_ID) == 0x02;
    if (opl_ok) { for (i = 0; i < 0xF6; i++) opl(i, 0); opl(0x01, 0x20); patch(0, p_dig); patch(1, p_bell); patch(2, p_thud); }
    hs_load();

    cave = 1; make_cave(); clear_screen(); draw_cave();
    banner(CY + 9, "ROCKFALL", YELLOW);
    banner(CY + 11, "dig for diamonds, mind the rocks", GREY);
    banner(CY + 13, "arrows dig   SPACE+arrow take", GREY);
    banner(CY + 14, "R give up   P pause   M sound", GREY);
    banner(CY + 16, "SPACE STARTS   ESC LEAVES", WHITE);
    for (;;) { k = wait_key(); if (k == ' ' || k == 13) break; if (k == K_ESC) goto out; }
    do {
        cave = 1; lives = 3; score = 0; over = 0; paused = 0;
        while (!over) play_cave();
    } while (game_over());
out:
    if (opl_ok) for (i = 0; i < 0xF6; i++) opl(i, 0);
    REG(TERM + 0x0E) = (uint8_t)(flags_was & ~8);
    rom_video();
    if (mode_was != '7') mode_run(mode_was);
    else rom_chrout(12);
}
