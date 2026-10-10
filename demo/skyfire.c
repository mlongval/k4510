/* K4510: SKYFIRE -- a Galaxian, with Kenney's Pixel Shmup planes (CC0,
 * data/pixelshmup/) instead of aliens: 320x240, the ground scrolling by
 * on VICKY layer 0, the planes 32x32 8 bpp sprites, shots and bursts
 * 16x16 sprites cut from the same sheet.
 *
 * The rules are Galaxian's.  A formation of 22 sways above you; every so
 * often one peels off, swoops at you dropping bombs, and if it misses it
 * flies back up and rejoins.  One shot on screen at a time -- fire again
 * when it lands or leaves.  A diver is worth double.  Clear the formation
 * and the next wave dives sooner, harder and drops more.
 *
 *   left/right (or a pad)   fly       space / A   fire       P   pause       Esc   leave
 *
 * The screen, since 2026-10-09, is CHESS's and TETRIS's -- the Personality
 * Chooser's look: 320x240 doubled, grey bands with the time and the keys,
 * the blue glass, the banner's bars, unscii drawn through the blitter into a
 * bitmap on layer 1 that sits over the ground and the planes (the bands and
 * the score strip are opaque in it, the sky is left transparent).  The title,
 * the pause and the end of a game are the Chooser's list.  Only the machine's
 * sixteen colours are used for the chrome; the Pixel Shmup art keeps its own
 * palette entries from SKY_BASE up (the ground's tiles are opaque: the sea
 * is a tile, not the backdrop).
 *
 * The ground's scroll is written to VICKY once a frame, right after the
 * vblank, together with the sprite table: VICKY reads the register per line,
 * and a write in the middle of the frame put a seam across the ground that
 * wandered with the frame's work (fixed 2026-10-09).
 */
#include "k4510.h"
#include "skyfire.h"

#define SKY_PHYS  0x00110000UL
#define SHIPS     SKY_PHYS
#define TILES     (SKY_PHYS + SKY_TILES)
#define MAP       (SKY_PHYS + SKY_MAP)
#define SPRTAB_A  0x00130000UL
#define SPRTAB_B  0x00131000UL
#define BMP       0x00140000UL                          /* 320x240, 8 bpp, layer 1: the chrome */
#define SEQ       0xD5E0u
#define TERM      0xDA00u

static void rom_video(void) { ((void (*)(void))0xFF92)(); }
enum { BLACK, WHITE, RED, CYAN, PURPLE, GREEN, BLUE, YELLOW, ORANGE, BROWN, LRED, DGREY, GREY, LGREEN, LBLUE, LGREY };
/* The bands' ink: black, but not colour 0 -- in the bitmap 0 is transparent,
 * and the planes fly under the bands (a diver comes back in from above the
 * top one, the player sits under the bottom one): with colour 0 they showed
 * through the letters.  Palette 253, set to black; VIDEO puts the palette
 * back on the way out. */
#define INK 253
#define K_UP    0x80
#define K_DOWN  0x81
#define K_LEFT  0x82
#define K_RIGHT 0x83
#define K_ESC   0x1B

#define NCOL   6
#define NROW   4
#define NEN    (NCOL * NROW)
#define NBOMB  4
#define NBURST 4
enum { S_PLAYER, S_SHOT, S_BOMB, S_BURST = S_BOMB + NBOMB, S_ENEMY = S_BURST + NBURST, NSPR = S_ENEMY + NEN };

/* positions in quarter pixels */
typedef struct { int16_t x, y, hx, hy, vx, vy; uint8_t alive, state, ship, t, row, col; } enemy_t;
typedef struct { int16_t x, y; uint8_t on; } shot_t;
typedef struct { int16_t x, y; uint8_t t; } burst_t;
static enemy_t en[NEN];
static shot_t shot, bomb[NBOMB];
static burst_t burst[NBURST];
static int16_t px, fx, fy;                   /* the player, the formation's origin */
static int8_t sway;
static uint8_t cur, lives, wave, divers, dive_timer, dead_timer, alive_n;
static uint16_t frame; static uint32_t score, hiscore;
static uint16_t seed = 0x4510;
static uint8_t difficulty = 1;
static const char *const DIFF_NAME[3] = { "Easy", "Normal", "Hard" };
static uint8_t rnd(void) { seed = seed * 25173u + 13849u; return (uint8_t)(seed >> 8); }
static const uint8_t row_ship[NROW]  = { SH_YELLOW, SH_RED, SH_REDB, SH_GREENB };
static const uint8_t row_score[NROW] = { 6, 4, 3, 2 };   /* x10; a diver is worth double */

static void snd(uint8_t ch, uint8_t now, int8_t vol, uint8_t pitch, uint8_t dur)
{ REG(SEQ) = (uint8_t)((now ? 0x10 : 0) | ch); REG(SEQ + 1) = (uint8_t)vol; REG(SEQ + 2) = pitch; REG(SEQ + 3) = dur; }
static void snd_shot(void)  { snd(1, 1, -7, 100, 1); snd(1, 0, -5, 80, 1); }
static void snd_hit(void)   { snd(0, 1, -12, 40, 2); snd(0, 0, -6, 20, 2); }
static void snd_dive(void)  { snd(3, 1, -5, 70, 1); snd(3, 0, -5, 84, 1); snd(3, 0, -4, 96, 1); }
static void snd_death(void) { snd(0, 1, -15, 12, 8); snd(2, 1, -12, 60, 3); snd(2, 0, -12, 48, 3); snd(2, 0, -12, 36, 3); snd(2, 0, -10, 24, 8); }
static void snd_wave(void)  { uint8_t i; snd(3, 1, -8, 53, 2); for (i = 0; i < 4; i++) snd(3, 0, -8, (uint8_t)(69 + i * 16), 2); }
static void hush(void) { REG(SEQ) = 0x80; }

/* ---- the screen: the Chooser's look (as TETRIS and CHESS) ------------------ */
#define SW 320
#define BAND 12                                       /* a band: 8-pixel text with 2 above and below */
#define GY BAND                                       /* the glass: lines 12..227 */
#define BBOT (240 - BAND)
#define HUDY GY                                       /* the score strip: the glass's first 12 lines */
#define PLAYY (GY + BAND)                             /* the sky: lines 24..227 */
#define F8 0
#define F16 1
#define F8X2 2
#define FONT8P  0x00010000UL                          /* unscii-8 and -16, where the frontend puts them */
#define FONT16P 0x00010800UL
static uint8_t f8[96 * 8], f16[96 * 16];              /* ' '..DEL, copied near once */
static uint8_t gbuf[256];
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
static void icon8(const uint8_t *bits, uint16_t x, uint8_t y, uint8_t fg, uint8_t bg)   /* an 8x8 1-bit shape */
{
    uint8_t r, b, *g = gbuf;
    for (r = 0; r < 8; r++) for (b = 0x80; b; b >>= 1) *g++ = (bits[r] & b) ? fg : bg;
    blit_from(gbuf, x, y, 8, 8);
}
static const uint8_t plane_icon[8] = { 0x18, 0x18, 0x7E, 0xFF, 0x18, 0x3C, 0x18, 0x00 };   /* a life */
static char nb[12];
static const char *fmt(uint32_t v, uint8_t w)         /* v right-aligned in w places */
{
    uint8_t i = 10;
    nb[10] = 0;
    do { nb[--i] = (char)('0' + (uint8_t)(v % 10)); v /= 10; } while (v && i);
    while (i > 10 - w) nb[--i] = ' ';
    return nb + i;
}
static const char *fmt0(uint32_t v, uint8_t w)        /* v in w places, zero-filled, as a score counter */
{
    const char *p = fmt(v, w); char *q = nb + (p - nb);
    while (*q == ' ') *q++ = '0';
    return p;
}
static void band_text(uint8_t y, const char *s)        /* centred in a band, black on grey */
{
    uint8_t n = (uint8_t) strlen(s);
    rect(0, y, SW, BAND, LGREY);
    text((uint16_t)((SW - n * 8) / 2), (uint8_t)(y + 2), s, INK, LGREY, F8);
}
static uint8_t clock_shown = 0xFF, rtc_read, frame_seen;
static void rtc_latch(void) { rtc_read = REG(SYS + 4); }   /* a store, not a (void) read: cc65 drops that */
static void put2(char *b, uint8_t v) { b[0] = (char)('0' + v / 10); b[1] = (char)('0' + v % 10); }
static void draw_top(void)                            /* K4510 SKYFIRE on the left, the time on the right, as the Chooser's band */
{
    char b[17]; uint16_t y;
    rtc_latch();
    clock_shown = REG(SYS + 6);
    put2(b, REG(SYS + 7)); b[2] = ':'; put2(b + 3, REG(SYS + 6)); b[5] = ' ';
    put2(b + 6, REG(SYS + 8)); b[8] = '.'; put2(b + 9, REG(SYS + 9)); b[11] = '.';
    y = REG(SYS + 0x0A) | (REG(SYS + 0x0B) << 8);
    b[12] = (char)('0' + (y / 1000) % 10); b[13] = (char)('0' + (y / 100) % 10); b[14] = (char)('0' + (y / 10) % 10); b[15] = (char)('0' + y % 10); b[16] = 0;
    rect(0, 0, SW, BAND, LGREY);
    text(8, 2, "K4510 SKYFIRE", INK, LGREY, F8);
    text(SW - 8 - 16 * 8, 2, b, INK, LGREY, F8);
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
static void header(const char *sub, uint8_t subcol)   /* a full-glass screen: the bars, Skyfire, a line under it */
{
    glass();
    bars(12, GY + 8, 5, 6);
    text(72, GY + 8, "Skyfire", WHITE, BLUE, F8X2);
    text(72, GY + 28, sub, subcol, BLUE, F8);
}

/* the score strip under the top band: SCORE, HI, WAVE and its difficulty,
 * the lives as little planes; drawn again only when a number changes */
static uint32_t hud_score = 0xFFFFFFFFUL, hud_hi = 0xFFFFFFFFUL; static uint8_t hud_wave = 0xFF, hud_lives = 0xFF;
static void hud_labels(void)
{
    rect(0, HUDY, SW, BAND, BLUE);
    text(4, HUDY + 2, "SCORE", LBLUE, BLUE, F8);
    text(108, HUDY + 2, "HI", LBLUE, BLUE, F8);
    text(188, HUDY + 2, "WAVE", LBLUE, BLUE, F8);
    hud_score = hud_hi = 0xFFFFFFFFUL; hud_wave = hud_lives = 0xFF;
}
static void hud(void)
{
    uint8_t i; static char dl[2];
    if (score != hud_score) { hud_score = score; text(48, HUDY + 2, fmt0(score, 6), YELLOW, BLUE, F8); }
    if (hiscore != hud_hi) { hud_hi = hiscore; text(128, HUDY + 2, fmt0(hiscore, 6), WHITE, BLUE, F8); }
    if (wave != hud_wave) {
        hud_wave = wave;
        text(224, HUDY + 2, fmt0(wave, 2), YELLOW, BLUE, F8);
        dl[0] = DIFF_NAME[difficulty][0]; text(244, HUDY + 2, dl, LGREY, BLUE, F8);   /* E, N or H */
    }
    if (lives != hud_lives) {
        hud_lives = lives;
        for (i = 0; i < 4; i++) icon8(plane_icon, (uint16_t)(272 + i * 11), HUDY + 2, i < lives ? LBLUE : BLUE, BLUE);
    }
}
static void sky_clear(void) { rect(0, PLAYY, SW, BBOT - PLAYY, 0); }   /* the glass over the sky: transparent, the ground shows */
static void play_screen(void)                         /* after a list has had the glass: the bands, the strip, the sky */
{
    hud_labels(); hud(); sky_clear();
    band_text(BBOT, "< > fly  SPACE fire  P pause  ESC quits");
}

/* ---- the formation ------------------------------------------------------ */
static void new_wave(void)
{
    uint8_t r, c, i = 0;
    for (r = 0; r < NROW; r++) for (c = 0; c < NCOL; c++, i++) {
        enemy_t *e = &en[i];
        e->row = r; e->col = c; e->ship = row_ship[r];
        e->hx = (int16_t)(c * 40 * 4); e->hy = (int16_t)(r * 28 * 4);
        e->alive = (r == 0 && (c == 0 || c == NCOL - 1)) ? 0 : 1;   /* the flagship row is four wide */
        e->state = 0; e->x = fx + e->hx; e->y = fy + e->hy;
    }
    alive_n = NEN - 2; divers = 0;
    dive_timer = 120;
    for (i = 0; i < NBOMB; i++) bomb[i].on = 0;
}
/* Difficulty: three levels on the title, Normal in the middle, and
 * /APPS/SKYFIRE/SKYFIRE.CFG can move the numbers behind them.  Doc, 2026-09-07:
 * the first cut was "too hard" -- it was what is now Hard. */
typedef struct { uint8_t gap, gapmin, gapstep, divers0, diversdiv, bomb, bombstep, dive, divestep, sway; } diff_t;
static diff_t diff[3] = {
    { 220, 80, 10, 1, 4, 6, 0, 5, 0, 3 },        /* Easy: one diver at a time until wave 5, slow bombs, a lazy sway */
    { 150, 50, 10, 1, 3, 7, 1, 6, 0, 2 },        /* Normal */
    { 100, 30, 10, 1, 2, 10, 2, 8, 1, 2 },       /* Hard: the original */
};
static uint8_t dive_gap(void) { int16_t g = (int16_t)diff[difficulty].gap - (int16_t)wave * diff[difficulty].gapstep; return g < diff[difficulty].gapmin ? diff[difficulty].gapmin : (uint8_t)g; }
static uint8_t max_divers(void) { uint8_t m = (uint8_t)(diff[difficulty].divers0 + wave / diff[difficulty].diversdiv); return m > 3 ? 3 : m; }
static int16_t bomb_speed(void) { return (int16_t)(diff[difficulty].bomb + wave * diff[difficulty].bombstep); }
static int16_t dive_speed(void) { return (int16_t)(diff[difficulty].dive + wave * diff[difficulty].divestep); }
/* /APPS/SKYFIRE/SKYFIRE.CFG: lines of NAME VALUE for the Normal numbers -- DIVEGAP,
 * DIVEMIN, DIVERS (1..3), BOMB, DIVE, SWAY (frames per pixel of sway); Easy and
 * Hard stay in step with what is set.  The same shape as INVADER2.CFG. */
static char cfgbuf[512];
static uint8_t word_is(const char *w, uint8_t n, const char *k) { uint8_t i; for (i = 0; i < n; i++) if (k[i] != w[i]) return 0; return k[n] == 0; }
static void load_cfg(void)
{
    static char name[] = "/APPS/SKYFIRE/SKYFIRE.CFG"; uint16_t n, i = 0;
    w32(0xD304u, (uint16_t)name); w32(0xD308u, (uint16_t)cfgbuf); w32(0xD30Cu, sizeof cfgbuf - 1);
    REG(0xD300u) = 9;
    if (REG(0xD301u)) return;
    n = REG(0xD30Cu) | ((uint16_t)REG(0xD30Du) << 8); if (n >= sizeof cfgbuf) n = sizeof cfgbuf - 1;
    cfgbuf[n] = 0;
    while (i < n) {
        const char *w = cfgbuf + i; uint8_t wl = 0; uint16_t v = 0;
        if (cfgbuf[i] == '#' || cfgbuf[i] == '\n' || cfgbuf[i] == '\r') { while (i < n && cfgbuf[i] != '\n') i++; i++; continue; }
        while (i < n && cfgbuf[i] > ' ') { i++; wl++; }
        while (i < n && (cfgbuf[i] == ' ' || cfgbuf[i] == '\t')) i++;
        while (i < n && cfgbuf[i] >= '0' && cfgbuf[i] <= '9') v = (uint16_t)(v * 10 + (cfgbuf[i++] - '0'));
        while (i < n && cfgbuf[i] != '\n') i++; i++;
        if (v > 250) v = 250;
        if (word_is(w, wl, "DIVEGAP")) { diff[1].gap = (uint8_t)v; diff[0].gap = (uint8_t)(v + 70 > 250 ? 250 : v + 70); diff[2].gap = (uint8_t)(v > 50 ? v - 50 : 1); }
        else if (word_is(w, wl, "DIVEMIN")) { diff[1].gapmin = (uint8_t)v; diff[0].gapmin = (uint8_t)(v + 30 > 250 ? 250 : v + 30); diff[2].gapmin = (uint8_t)(v > 20 ? v - 20 : 1); }
        else if (word_is(w, wl, "DIVERS")) { uint8_t d = (uint8_t)(v < 1 ? 1 : v > 3 ? 3 : v); diff[1].divers0 = d; diff[2].divers0 = d; diff[0].divers0 = 1; }
        else if (word_is(w, wl, "BOMB")) { diff[1].bomb = (uint8_t)v; diff[0].bomb = (uint8_t)(v > 1 ? v - 1 : 1); diff[2].bomb = (uint8_t)(v + 3); }
        else if (word_is(w, wl, "DIVE")) { diff[1].dive = (uint8_t)v; diff[0].dive = (uint8_t)(v > 1 ? v - 1 : 1); diff[2].dive = (uint8_t)(v + 2); }
        else if (word_is(w, wl, "SWAY")) { uint8_t sw = (uint8_t)(v < 1 ? 1 : v); diff[1].sway = sw; diff[2].sway = sw; diff[0].sway = (uint8_t)(sw + 1); }
    }
}
static void start_dive(void)
{
    uint8_t tries = 20, i;
    while (tries--) {
        i = rnd() % NEN;
        if (en[i].alive && en[i].state == 0) {
            enemy_t *e = &en[i];
            e->state = 1; e->t = 0;
            e->vx = (int16_t)((e->x < px) ? 6 : -6); e->vy = 2;
            divers++; snd_dive(); return;
        }
    }
}
static void drop_bomb(int16_t x, int16_t y)
{
    uint8_t i;
    for (i = 0; i < NBOMB; i++) if (!bomb[i].on) { bomb[i].on = 1; bomb[i].x = x + 8 * 4; bomb[i].y = y + 24 * 4; return; }
}
static void move_enemies(void)
{
    uint8_t i;
    for (i = 0; i < NEN; i++) {
        enemy_t *e = &en[i];
        if (!e->alive) continue;
        if (e->state == 0) { e->x = fx + e->hx; e->y = fy + e->hy; continue; }
        if (e->state == 1) {
            e->t++;
            if (e->t < 24) { e->x += e->vx; e->y += e->vy; e->vy += 1; }
            else {
                int16_t want = px - e->x;
                if (want > 0 && e->vx < 8 + (int16_t)wave) e->vx += 1;
                if (want < 0 && e->vx > -8 - (int16_t)wave) e->vx -= 1;
                e->x += e->vx; e->y += dive_speed();
                if ((e->t & 31) == 0 && (want < 40 * 4 && want > -40 * 4) && e->y < 160 * 4) drop_bomb(e->x, e->y);
            }
            if (e->x < 0) { e->x = 0; e->vx = 4; }
            if (e->x > (320 - 32) * 4) { e->x = (320 - 32) * 4; e->vx = -4; }
            if (e->y > 240 * 4) { e->state = 2; e->y = -32 * 4; e->x = fx + e->hx; }
        } else {                                              /* 2: flying back to the slot */
            int16_t tx = fx + e->hx, ty = fy + e->hy;
            if (e->x < tx - 6) e->x += 6; else if (e->x > tx + 6) e->x -= 6; else e->x = tx;
            if (e->y < ty - 6) e->y += 6; else if (e->y > ty + 6) e->y -= 6; else e->y = ty;
            if (e->x == tx && e->y == ty) { e->state = 0; divers--; }
        }
    }
}
static void add_burst(int16_t x, int16_t y)
{
    uint8_t i;
    for (i = 0; i < NBURST; i++) if (!burst[i].t) { burst[i].t = 1; burst[i].x = x; burst[i].y = y; return; }
}
static void kill_enemy(enemy_t *e)
{
    uint16_t pts = (uint16_t)row_score[e->row] * 10;
    if (e->state) { pts *= 2; divers--; }
    e->alive = 0; alive_n--;
    add_burst(e->x + 8 * 4, e->y + 8 * 4); snd_hit();
    score += pts; if (score > hiscore) hiscore = score;
}

/* ---- the sprite tables ---------------------------------------------------- */
static void init_tables(void)
{
    uint8_t i; uint32_t t;
    dma_fill(0, SPRTAB_A, 4096); dma_fill(0, SPRTAB_B, 4096);
    for (t = SPRTAB_A; t <= SPRTAB_B; t += SPRTAB_B - SPRTAB_A)
        for (i = 0; i < NSPR; i++)
            far_poke(t + (uint32_t)i * 16 + 9, (uint8_t)((i == S_PLAYER || i >= S_ENEMY) ? 2 | (2 << 2) : 1 | (1 << 2)));   /* 32x32, or 16x16 */
}
static void put_spr(uint32_t t, int16_t x, int16_t y, uint32_t d, uint8_t ctrl)
{
    far_poke16(t, (uint16_t)x); far_poke16(t + 2, (uint16_t)y);
    far_poke16(t + 4, (uint16_t)d); far_poke16(t + 6, (uint16_t)(d >> 16));
    far_poke(t + 8, ctrl);
}
static void write_table(uint32_t t)
{
    uint8_t i;
    put_spr(t + S_PLAYER * 16, px >> 2, 200, SHIPS + (uint32_t)SH_PLAYER * 1024, dead_timer ? 0 : 3);
    put_spr(t + S_SHOT * 16, shot.x >> 2, shot.y >> 2, TILES + T_SHOT * 256, shot.on ? 3 : 0);
    for (i = 0; i < NBOMB; i++) put_spr(t + (S_BOMB + i) * 16, bomb[i].x >> 2, bomb[i].y >> 2, TILES + T_BOMB * 256, bomb[i].on ? 3 | 8 : 0);
    for (i = 0; i < NBURST; i++) {
        uint8_t f = burst[i].t ? (uint8_t)((burst[i].t - 1) / 6) : 0;
        put_spr(t + (S_BURST + i) * 16, burst[i].x >> 2, burst[i].y >> 2, TILES + (uint32_t)(T_BURST0 + f) * 256, burst[i].t ? 3 : 0);
    }
    for (i = 0; i < NEN; i++) {
        enemy_t *e = &en[i];
        put_spr(t + (S_ENEMY + i) * 16, e->x >> 2, e->y >> 2, SHIPS + (uint32_t)e->ship * 1024, e->alive ? 3 | 8 : 0);   /* V-flip: nose down */
    }
}
/* the title's planes: the formation's four in a row at the top right, at
 * Z 1 so they sit over the glass (layer 1) */
static void title_planes(void)
{
    static const uint8_t ship[4] = { SH_YELLOW, SH_RED, SH_REDB, SH_GREENB };
    uint8_t i;
    dma_fill(0, SPRTAB_A, 4096);
    for (i = 0; i < 4; i++) {
        far_poke(SPRTAB_A + (uint32_t)i * 16 + 9, 2 | (2 << 2));
        put_spr(SPRTAB_A + (uint32_t)i * 16, (int16_t)(200 + i * 28), GY + 2, SHIPS + (uint32_t)ship[i] * 1024, 3 | (1 << 4));
    }
    w32(V_SPRTAB, SPRTAB_A); REG(V_SPRCTL) = 1;
}

/* ---- the machine ----------------------------------------------------------- */
#define GROUND_ON (1 | (1 << 1) | (3 << 3) | (1 << 5))   /* layer 0: enable, tile, 8 bpp, 16 px cells */
static uint8_t ctrl_was, bg_was, spr_was;
static void setup(void)
{
    uint8_t i; uint16_t L = V_LAYER(0);
    ctrl_was = REG(V_CTRL); bg_was = REG(V_BGCOL); spr_was = REG(V_SPRCTL);
    REG(V_CTRL) = 0;
    fonts_near();
    for (i = 0; i < SKY_NCOL; i++) pal((uint8_t)(SKY_BASE + i), sky_pal[i][0], sky_pal[i][1], sky_pal[i][2]);
    pal(INK, 0, 0, 0);
    REG(V_BGCOL) = BLACK;                                /* a pixel of colour 0 is the ground: black */
    REG(L + 1) = 0; w16(L + 2, 0); w16(L + 4, 0); w16(L + 6, SKY_MAPW);
    w32(L + 8, TILES); w32(L + 12, MAP);
    REG(L) = 0;                                          /* the ground, off until a game starts */
    L = V_LAYER(1);                                      /* the chrome: a bitmap over everything */
    dma_fill(BLUE, BMP, (uint32_t)SW * 240);
    REG(L + 1) = 0; w16(L + 2, 0); w16(L + 4, 0); w16(L + 6, SW); w32(L + 8, BMP); w32(L + 12, BMP);
    REG(L) = 1 | (0 << 1) | (3 << 3);                   /* bitmap, 8 bpp */
    init_tables(); REG(V_SPRCTL) = 0;
    REG(V_CTRL) = 1 | 2 | 4;                             /* 320 x 240, doubled: the Chooser's chunky pixels */
    draw_top();
}
/* The ground's scroll: computed from the frame counter, written to VICKY
 * only at the top of a frame (scroll_commit, right after wait_vblank) --
 * VICKY reads the register per line. */
static uint16_t scroll_next;
static void scroll_compute(void) { scroll_next = (uint16_t)(SKY_PERIOD - (frame % SKY_PERIOD)); }
static void scroll_commit(void) { w16(V_LAYER(0) + 4, scroll_next); }

/* ---- keys: the keyboard queue, and a pad's edges as keys ------------------ */
static uint8_t held_was;
static uint8_t get_key(void)
{
    uint8_t k = key_get(), h = keys_held(), e = (uint8_t)(h & ~held_was);
    held_was = h;
    if (k) return k;
    if (e & HELD_FIRE) return ' ';
    if (e & HELD_LEFT) return K_LEFT;
    if (e & HELD_RIGHT) return K_RIGHT;
    if (e & HELD_UP) return K_UP;
    if (e & HELD_DOWN) return K_DOWN;
    return 0;
}

/* ---- the Chooser's list: the title, the pause, the end of a game -------- */
#define LIST_X 12
#define ROW_H 20
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
    while ((k = get_key()) == 0) { seed++; tick_top(); wait_vblank(); }
    return k;
}
/* a list of n rows at y0: 1-n, the arrows and RETURN; 255 for ESC.  `extra`
 * gets the keys the list does not know (SPACE, < >, P), and its answer,
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

static char dv[12];
static void diff_text(void) { strcpy(dv, "< "); strcat(dv, DIFF_NAME[difficulty]); strcat(dv, " >"); }
static const char *const T_ITEMS[3] = { "Play", "Difficulty", "Quit" };
static const char *t_right[3] = { 0, dv, 0 };
static uint8_t title_key(uint8_t k, uint8_t cur)
{
    (void) cur;
    if (k == ' ') return 0;
    if (k == K_LEFT && difficulty > 0) difficulty--;
    if (k == K_RIGHT && difficulty < 2) difficulty++;
    diff_text();
    return 254;
}
#define KEYS_Y 132
#define RULES_X 164
static uint8_t title(void)
{
    uint8_t r;
    REG(V_LAYER(0)) = 0;                                 /* plain glass behind the list, the planes in a row */
    header("a Galaxian", YELLOW);
    title_planes();
    text(LIST_X, KEYS_Y - 12, "THE KEYS", WHITE, BLUE, F8);
    text(LIST_X, KEYS_Y, "< > / pad   fly", LBLUE, BLUE, F8);
    text(LIST_X, KEYS_Y + 11, "SPACE/fire  shoot", LBLUE, BLUE, F8);
    text(LIST_X, KEYS_Y + 22, "P           pause", LBLUE, BLUE, F8);
    text(LIST_X, KEYS_Y + 33, "ESC         leave", LBLUE, BLUE, F8);
    text(RULES_X, KEYS_Y - 12, "THE RULES", WHITE, BLUE, F8);
    text(RULES_X, KEYS_Y, "one shot at a time", LBLUE, BLUE, F8);
    text(RULES_X, KEYS_Y + 11, "a diver pays double", LBLUE, BLUE, F8);
    text(RULES_X, KEYS_Y + 22, "waves dive sooner", LBLUE, BLUE, F8);
    text(RULES_X, KEYS_Y + 33, "and drop more", LBLUE, BLUE, F8);
    text(LIST_X, KEYS_Y + 56, "HI SCORE", WHITE, BLUE, F8);
    text(LIST_X + 76, KEYS_Y + 56, fmt0(hiscore, 6), YELLOW, BLUE, F8);
    text(LIST_X, BBOT - 12, "planes and ground: Kenney, CC0", LGREY, BLUE, F8);
    band_text(BBOT, "SPACE plays, < > difficulty, ESC quits");
    diff_text();
    while (key_get()) ;
    held_was = keys_held();
    for (;;) {
        r = list_pick(GY + 46, T_ITEMS, t_right, 3, 0, title_key);
        if (r == 0) break;
        if (r == 255 || r == 2) { REG(V_SPRCTL) = 0; return 0; }
        if (r == 1) { difficulty = (uint8_t)(difficulty == 2 ? 0 : difficulty + 1); diff_text(); }   /* RETURN on the difficulty: the next one */
    }
    REG(V_SPRCTL) = 0;
    return 1;
}

/* P: the pause, as a list -- the planes are hidden while it lasts; 1 to go on, 0 to end the game */
static const char *const P_ITEMS[2] = { "Resume", "End the game" };
static const char *p_right[2] = { "P", "ESC" };
static uint8_t pause_key(uint8_t k, uint8_t cur)
{
    (void) cur;
    if (k == 'p' || k == 'P' || k == ' ') return 0;
    return 254;
}
static uint8_t pause_screen(void)
{
    uint8_t r;
    REG(V_SPRCTL) = 0;
    header("Paused", YELLOW);
    band_text(BBOT, "P resumes, ESC ends the game");
    r = list_pick(GY + 46, P_ITEMS, p_right, 2, 0, pause_key);
    play_screen();
    REG(V_SPRCTL) = 1;
    held_was = keys_held();
    return r == 0;
}

/* the wave is clear: a bar across the sky for a second and a half, the
 * ground still going and the last burst playing out; 0 to go on, 1 if ESC
 * ended the game */
static uint8_t wave_clear(void)
{
    uint8_t k, i, n = 90, esc = 0;
    char b[20];
    strcpy(b, "Wave "); strcat(b, fmt(wave - 1, 1)); strcat(b, " clear");
    rect(LIST_X, 110, SW - 2 * LIST_X, ROW_H - 2, LBLUE);
    text((uint16_t)((SW - strlen(b) * 8) / 2), 111, b, BLUE, LBLUE, F16);
    while (n--) {
        uint32_t back = cur ? SPRTAB_A : SPRTAB_B;
        k = get_key();
        if (k == K_ESC) { esc = 1; break; }
        if (k == ' ' || k == 13) break;
        for (i = 0; i < NBURST; i++) if (burst[i].t) { if (++burst[i].t > 18) burst[i].t = 0; }
        tick_top();
        frame++; scroll_compute();
        write_table(back);
        wait_vblank();
        w32(V_SPRTAB, back); cur ^= 1; scroll_commit();
    }
    rect(LIST_X, 110, SW - 2 * LIST_X, ROW_H - 2, 0);
    return esc;
}

static uint8_t space_key(uint8_t k, uint8_t cur) { (void) cur; return k == ' ' ? 0 : 254; }
static uint8_t game_over(void)
{
    static const char *const G_ITEMS[2] = { "Play again", "Quit" };
    uint8_t k, r;
    for (k = 0; k < 60; k++) { tick_top(); frame++; scroll_compute(); wait_vblank(); scroll_commit(); }   /* a second to see how it ended */
    while (key_get()) ;
    REG(V_SPRCTL) = 0; REG(V_LAYER(0)) = 0;
    header("Game over", LRED);
    text(LIST_X, GY + 44, "SCORE", LBLUE, BLUE, F8);
    text(LIST_X + 48, GY + 40, fmt(score, 10), WHITE, BLUE, F16);
    text(LIST_X, GY + 62, "HI", LBLUE, BLUE, F8);
    text(LIST_X + 48, GY + 58, fmt(hiscore, 10), score >= hiscore && score ? YELLOW : WHITE, BLUE, F16);
    text(LIST_X, GY + 80, "WAVE", LBLUE, BLUE, F8);
    text(LIST_X + 48, GY + 76, fmt(wave, 10), WHITE, BLUE, F16);
    text(LIST_X + 136, GY + 80, DIFF_NAME[difficulty], LGREY, BLUE, F8);
    if (score >= hiscore && score) text(LIST_X + 136, GY + 62, "the best so far", YELLOW, BLUE, F8);
    band_text(BBOT, "SPACE plays again, ESC quits");
    held_was = keys_held();
    r = list_pick(GY + 104, G_ITEMS, 0, 2, 0, space_key);
    return r == 0;
}

void main(void)
{
    uint8_t i, k, h, again = 1;
    setup();
    load_cfg();
    hiscore = 0;
    for (;;) {
        if (!again || !title()) break;
        score = 0; lives = 3; wave = 1;
        fx = 40 * 4; fy = 24 * 4; sway = 1; px = 144 * 4;
        shot.on = 0; dead_timer = 0;
        for (i = 0; i < NBURST; i++) burst[i].t = 0;
        new_wave();
        init_tables(); write_table(SPRTAB_A); w32(V_SPRTAB, SPRTAB_A); cur = 0;
        play_screen(); scroll_compute(); scroll_commit();
        REG(V_LAYER(0)) = GROUND_ON; REG(V_SPRCTL) = 1;
        for (;;) {
            uint32_t back = cur ? SPRTAB_A : SPRTAB_B;
            k = key_get(); h = keys_held();
            if (k == K_ESC) { lives = 0; break; }
            if (k == 'p' || k == 'P') { if (!pause_screen()) { lives = 0; break; } continue; }
            /* the formation sways 40 px either way */
            if (frame % diff[difficulty].sway == 0) { fx += sway * 4; if (fx >= 80 * 4) sway = -1; if (fx <= 0) sway = 1; }
            if (!dead_timer) {
                if ((h & HELD_LEFT) && px > 0) px -= 8;
                if ((h & HELD_RIGHT) && px < (320 - 32) * 4) px += 8;
                if ((h & HELD_FIRE) && !shot.on) { shot.on = 1; shot.x = px + 8 * 4; shot.y = 196 * 4; snd_shot(); }
            }
            if (shot.on) { shot.y -= 24; if (shot.y < -16 * 4) shot.on = 0; }
            if (dive_timer) dive_timer--;
            else if (divers < max_divers()) { start_dive(); dive_timer = dive_gap(); }
            move_enemies();
            for (i = 0; i < NBOMB; i++) if (bomb[i].on) {
                bomb[i].y += bomb_speed();
                if (bomb[i].y > 240 * 4) bomb[i].on = 0;
                else if (!dead_timer) {
                    int16_t bx = (bomb[i].x >> 2) + 8, by = (bomb[i].y >> 2) + 12, pl = (px >> 2);
                    if (bx > pl + 6 && bx < pl + 26 && by > 206 && by < 230) { bomb[i].on = 0; dead_timer = 90; add_burst(px + 8 * 4, 208 * 4); snd_death(); }
                }
            }
            for (i = 0; i < NEN; i++) {
                enemy_t *e = &en[i];
                if (!e->alive) continue;
                if (shot.on) {
                    int16_t sx = (shot.x >> 2) + 8, sy = (shot.y >> 2) + 4, ex = e->x >> 2, ey = e->y >> 2;
                    if (sx > ex + 4 && sx < ex + 28 && sy > ey + 4 && sy < ey + 28) { shot.on = 0; kill_enemy(e); continue; }
                }
                if (!dead_timer && e->state == 1) {
                    int16_t dx = (e->x >> 2) - (px >> 2), dy = (e->y >> 2) - 200;
                    if (dx > -20 && dx < 20 && dy > -20 && dy < 20) { kill_enemy(e); dead_timer = 90; add_burst(px + 8 * 4, 208 * 4); snd_death(); }
                }
            }
            for (i = 0; i < NBURST; i++) if (burst[i].t) { if (++burst[i].t > 18) burst[i].t = 0; }
            if (dead_timer) {
                if (--dead_timer == 0) {
                    if (--lives == 0) { hud(); break; }
                    px = 144 * 4; for (i = 0; i < NBOMB; i++) bomb[i].on = 0;
                }
            }
            if (alive_n == 0) {
                snd_wave(); wave++; hud();
                if (wave_clear()) { lives = 0; break; }
                new_wave();
            }
            hud(); tick_top();
            frame++; scroll_compute();
            write_table(back);
            wait_vblank();
            w32(V_SPRTAB, back); cur ^= 1; scroll_commit();   /* the frame's registers, all at its top */
        }
        hush();
        for (i = 0; i < NEN; i++) en[i].alive = 0;
        for (i = 0; i < NBOMB; i++) bomb[i].on = 0;
        shot.on = 0; dead_timer = 1; write_table(SPRTAB_A); write_table(SPRTAB_B);   /* both tables: VICKY shows one of them */
        again = game_over();
    }
    hush();
    REG(V_SPRCTL) = spr_was; REG(V_LAYER(0)) = 0; REG(V_LAYER(1)) = 0;   /* both layers were ours; VIDEO builds the console's again */
    REG(V_BGCOL) = bg_was; REG(V_CTRL) = ctrl_was;
    rom_video();
    REG(TERM + 4) = 2;                                /* JIM: a clean screen to come back to */
}
