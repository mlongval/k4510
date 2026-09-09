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
 *   left/right (or a pad)   fly       space / A   fire       Esc   leave
 */
#include "k4510.h"
#include "skyfire.h"

#define SKY_PHYS  0x00110000UL
#define SHIPS     SKY_PHYS
#define TILES     (SKY_PHYS + SKY_TILES)
#define MAP       (SKY_PHYS + SKY_MAP)
#define SPRTAB_A  0x00130000UL
#define SPRTAB_B  0x00131000UL
#define TEXTMAP   0x00132000UL
#define SEQ       0xD5E0u
#define CAPTION_PAL 127

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

/* ---- the caption layer: score, wave, lives ---------------------------- */
static void put_str(uint8_t x, uint8_t y, const char *s) { uint32_t p = TEXTMAP + (uint32_t)y * 40 + x; while (*s) far_poke(p++, *s++); }
static void put_dec(uint8_t x, uint8_t y, uint32_t v, uint8_t w)
{ uint32_t p = TEXTMAP + (uint32_t)y * 40 + x + w; while (w--) { far_poke(--p, (uint8_t)('0' + v % 10)); v /= 10; } }
static void hud(void)
{
    uint8_t i;
    put_str(0, 0, "SCORE "); put_dec(6, 0, score, 6);
    put_str(15, 0, "HI "); put_dec(18, 0, hiscore, 6);
    put_str(26, 0, "WAVE "); put_dec(31, 0, wave, 2); far_poke(TEXTMAP + 34, (uint8_t)DIFF_NAME[difficulty][0]);
    for (i = 0; i < 4; i++) far_poke(TEXTMAP + 36 + i, (uint8_t)(i < lives ? 3 : ' '));   /* CP437 3 is a heart */
}
static void centre(uint8_t y, const char *s) { uint8_t n = 0; const char *q = s; while (*q++) n++; put_str((uint8_t)(n >= 40 ? 0 : (40 - n) / 2), y, s); }
static void clear_text(uint8_t y0, uint8_t y1) { dma_fill(' ', TEXTMAP + (uint32_t)y0 * 40, (uint32_t)(y1 - y0 + 1) * 40); }

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

/* ---- the machine ----------------------------------------------------------- */
static void setup(void)
{
    uint8_t i; uint16_t L = V_LAYER(0);
    REG(V_CTRL) = 0; REG(V_BGCOL) = 0;
    pal(0, 120, 200, 230);
    for (i = 0; i < SKY_NCOL; i++) pal((uint8_t)(SKY_BASE + i), sky_pal[i][0], sky_pal[i][1], sky_pal[i][2]);
    pal(255, 24, 28, 60);                                /* the caption: dark, so it reads on sea and land */
    dma_fill(' ', TEXTMAP, 40 * 30);
    text8_layer(1, TEXTMAP, 40, CAPTION_PAL);
    REG(L + 1) = 0; w16(L + 2, 0); w16(L + 4, 0); w16(L + 6, SKY_MAPW);
    w32(L + 8, TILES); w32(L + 12, MAP);
    REG(L) = 1 | (1 << 1) | (3 << 3) | (1 << 5);         /* enable, tile, 8 bpp, 16 px cells */
    init_tables(); write_table(SPRTAB_A); w32(V_SPRTAB, SPRTAB_A); REG(V_SPRCTL) = 1;
    REG(V_CTRL) = 1 | 2 | 4;                             /* 320 x 240 */
}
static void scroll_ground(void) { w16(V_LAYER(0) + 4, (uint16_t)(SKY_PERIOD - (frame % SKY_PERIOD))); }

static uint8_t title_mode;
static void show_diff(void)
{
    char b[40]; uint8_t i = 0; const char *p = "difficulty:  < ";
    while (*p) b[i++] = *p++; p = DIFF_NAME[difficulty]; while (*p) b[i++] = *p++; p = " >   (left/right)"; while (*p) b[i++] = *p++; b[i] = 0;
    clear_text(23, 23); centre(23, b);
}
static uint8_t wait_key(uint8_t frames)                    /* space/fire to go on (1), Esc to leave (2), 0 on timeout */
{
    uint8_t k, h, hl = 0;
    while (frames == 0 || frames--) {
        k = key_get(); h = keys_held();
        if (k == 0x1B) return 2;
        if (k == ' ' || k == 0x0D || (h & HELD_FIRE)) return 1;
        if (title_mode) {
            if (k == 0x82 || ((h & HELD_LEFT) && !(hl & HELD_LEFT))) { if (difficulty) difficulty--; show_diff(); }
            if (k == 0x83 || ((h & HELD_RIGHT) && !(hl & HELD_RIGHT))) { if (difficulty < 2) difficulty++; show_diff(); }
        }
        hl = h;
        frame++; scroll_ground(); wait_vblank();
    }
    return 0;
}
static uint8_t title(void)
{
    uint8_t r;
    REG(V_LAYER(0)) = 0; REG(V_SPRCTL) = 0;               /* plain sky behind the words, no planes */
    clear_text(0, 29);
    centre(6, "S K Y F I R E");
    centre(9, "a Galaxian, over Pixel Shmup islands");
    centre(11, "planes and ground: Kenney, CC0");
    put_str(6, 15, "left/right or a pad   flies");
    put_str(6, 16, "space or fire         shoots");
    put_str(6, 17, "Esc                   leaves");
    centre(21, "one shot at a time. a diver pays double.");
    show_diff();
    centre(25, "space to start");
    while (key_get()) ;
    title_mode = 1; r = wait_key(0); title_mode = 0;
    clear_text(0, 29);
    REG(V_LAYER(0)) = 1 | (1 << 1) | (3 << 3) | (1 << 5); REG(V_SPRCTL) = 1;
    return r;
}

void main(void)
{
    uint8_t i, k, h, r;
    setup();
    load_cfg();
    hiscore = 0;
    for (;;) {
        if (title() == 2) break;
        score = 0; lives = 3; wave = 1;
        fx = 40 * 4; fy = 24 * 4; sway = 1; px = 144 * 4;
        shot.on = 0; dead_timer = 0;
        for (i = 0; i < NBURST; i++) burst[i].t = 0;
        new_wave(); hud();
        for (;;) {
            uint32_t back = cur ? SPRTAB_A : SPRTAB_B;
            k = key_get(); h = keys_held();
            if (k == 0x1B) { lives = 0; break; }
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
                centre(14, "WAVE CLEAR"); r = wait_key(90); clear_text(14, 14);
                if (r == 2) { lives = 0; break; }
                new_wave();
            }
            hud();
            frame++; scroll_ground();
            write_table(back);
            wait_vblank();
            w32(V_SPRTAB, back); cur ^= 1;
        }
        hush();
        for (i = 0; i < NEN; i++) en[i].alive = 0;
        for (i = 0; i < NBOMB; i++) bomb[i].on = 0;
        shot.on = 0; dead_timer = 1; write_table(SPRTAB_A); write_table(SPRTAB_B);   /* both tables: VICKY shows one of them */
        centre(13, "G A M E   O V E R");
        centre(15, "space plays again, Esc leaves");
        if (wait_key(0) == 2) break;
    }
    hush();
    REG(V_SPRCTL) = 0; REG(V_LAYER(0)) = 0; REG(V_LAYER(1)) = 0;
}
