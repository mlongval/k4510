/* K4510: FLUFFY -- a platformer, in Game Boy green.  The tileset and the
 * hero are Chloe Wolfe's "Game Boy Platformer Tileset & Character" (CC0,
 * data/gbplatformer/); the levels, the slugs and the gems are the game's
 * own (tools/mkfluffy.py).  320x240: the level is 15 tiles tall and
 * scrolls sideways on VICKY layer 0 at 4 bpp; Fluffy, the slugs and the
 * gems are 4 bpp sprites; the caption is a text8 layer.
 *
 * Run, jump, collect the gems, stomp the slugs (from above -- a slug met
 * sideways costs a life), mind the spikes, reach the smiling block.  Two
 * levels; the second is longer and meaner.  Holding jump jumps higher.
 *
 *   left/right (or a pad)   run       space / A / up   jump     Esc   leave
 */
#include "k4510.h"
#include "fluffy.h"

#define FL_PHYS   0x00110000UL
#define TILES     FL_PHYS
#define SPRD      (FL_PHYS + FL_SPR)
#define SPRTAB_A  0x00130000UL
#define SPRTAB_B  0x00131000UL
#define TEXTMAP   0x00132000UL
#define SEQ       0xD5E0u
#define CAPTION_PAL 127

#define VIEW_W  320
#define MAXGEM  64
#define MAXSLUG 24
enum { S_HERO, S_GEM, S_SLUG = S_GEM + MAXGEM, S_PUFF = S_SLUG + MAXSLUG, NSPR };

/* positions in eighths of a pixel */
#define U 8
#define GRAV     3
#define JUMP_V   (-46)
#define JUMP_CUT (-14)
#define MAXFALL  56
#define RUN      14

typedef struct { int16_t x, y, vx, vy; uint8_t on_ground, face, frame, anim, dead, jump_buf; } hero_t;
typedef struct { int16_t x, y; int8_t dir; uint8_t alive, anim; } slug_t;
typedef struct { uint8_t tx, ty, alive; } gem_t;
static hero_t hero;
static slug_t slug[MAXSLUG];
static gem_t gem[MAXGEM];
static uint8_t ngem, nslug, level, lives, cur, puff_t;
static int16_t camx, puff_x, puff_y;
static uint16_t mapw, frame, gems_left;
static uint32_t map, score;
static int16_t startx, starty;

static void snd(uint8_t ch, uint8_t now, int8_t vol, uint8_t pitch, uint8_t dur)
{ REG(SEQ) = (uint8_t)((now ? 0x10 : 0) | ch); REG(SEQ + 1) = (uint8_t)vol; REG(SEQ + 2) = pitch; REG(SEQ + 3) = dur; }
static void snd_jump(void)  { snd(1, 1, -6, 60, 1); snd(1, 0, -6, 76, 1); snd(1, 0, -5, 92, 1); }
static void snd_gem(void)   { snd(2, 1, -7, 117, 1); snd(2, 0, -7, 133, 2); }
static void snd_stomp(void) { snd(0, 1, -10, 30, 1); snd(1, 1, -6, 40, 1); snd(1, 0, -6, 20, 1); }
static void snd_die(void)   { snd(0, 1, -14, 10, 6); snd(2, 1, -12, 72, 3); snd(2, 0, -12, 60, 3); snd(2, 0, -12, 48, 3); snd(2, 0, -12, 24, 8); }
static void snd_win(void)   { uint8_t i; snd(3, 1, -8, 69, 2); for (i = 0; i < 5; i++) snd(3, 0, -8, (uint8_t)(85 + i * 16), 2); snd(3, 0, -8, 165, 8); }
static void hush(void) { REG(SEQ) = 0x80; }

/* ---- the level ------------------------------------------------------------- */
static uint8_t tile_at(int16_t tx, int16_t ty)               /* the tile's kind at tile (tx,ty) */
{
    uint32_t e; uint16_t t;
    if (ty < 0 || tx < 0 || tx >= (int16_t)mapw) return 1;   /* walls at both ends, sky above */
    if (ty >= FL_MAPH) return 0;
    e = map + (((uint32_t)ty * mapw + tx) << 1);
    t = far_peek(e) | ((uint16_t)(far_peek(e + 1) & 3) << 8);
    return t < FL_NTILES ? tile_kind[t] : 0;
}
static uint8_t solid_px(int16_t x, int16_t y) { return tile_at(x >> 4, y >> 4) == 1; }
static uint8_t kind_px(int16_t x, int16_t y)  { return tile_at(x >> 4, y >> 4); }

static void load_level(void)
{
    uint8_t i;
    if (level == 1) { map = FL_PHYS + FL_MAP;  mapw = FL_MAPW1; ngem = FL_NGEM1; nslug = FL_NSLUG1; }
    else            { map = FL_PHYS + FL_MAP2; mapw = FL_MAPW2; ngem = FL_NGEM2; nslug = FL_NSLUG2; }
    if (ngem > MAXGEM) ngem = MAXGEM;
    if (nslug > MAXSLUG) nslug = MAXSLUG;
    for (i = 0; i < ngem; i++) { const uint8_t *g = level == 1 ? gems1[i] : gems2[i]; gem[i].tx = g[0]; gem[i].ty = g[1]; gem[i].alive = 1; }
    for (i = 0; i < nslug; i++) { const uint8_t *g = level == 1 ? slugs1[i] : slugs2[i]; slug[i].x = (int16_t)(g[0] * 16 * U); slug[i].y = (int16_t)(g[1] * 16 * U); slug[i].dir = -1; slug[i].alive = 1; }
    gems_left = ngem;
    startx = 2 * 16 * U; starty = 11 * 16 * U;
    { uint16_t L = V_LAYER(0);
      REG(L + 1) = 0; w16(L + 2, 0); w16(L + 4, 0); w16(L + 6, mapw);
      w32(L + 8, TILES); w32(L + 12, map);
      REG(L) = 1 | (1 << 1) | (2 << 3) | (1 << 5); }      /* enable, tile, 4 bpp, 16 px cells */
}
static void spawn_hero(void)
{
    hero.x = startx; hero.y = starty; hero.vx = hero.vy = 0; hero.on_ground = 0; hero.face = 0; hero.frame = SP_IDLE; hero.dead = 0; hero.jump_buf = 0;
}

/* ---- the hero's physics: the hit box is columns 3..12, rows 2..15 --------- */
#define HB_L 3
#define HB_R 12
#define HB_T 2
#define HB_B 15
static void move_hero(uint8_t h, uint8_t jump_edge)
{
    int16_t x, y, nx, ny;
    if (h & HELD_LEFT)  { hero.vx = -RUN; hero.face = 1; }
    else if (h & HELD_RIGHT) { hero.vx = RUN; hero.face = 0; }
    else hero.vx = 0;
    /* a press up to six frames before landing still jumps on landing: the
     * arcade feel, and what a held button at a spawn or a landing expects */
    if (jump_edge) hero.jump_buf = 6; else if (hero.jump_buf) hero.jump_buf--;
    if (hero.jump_buf && hero.on_ground) { hero.vy = JUMP_V; hero.on_ground = 0; hero.jump_buf = 0; snd_jump(); }
    if (!(h & (HELD_FIRE | HELD_UP | HELD_A)) && hero.vy < JUMP_CUT) hero.vy = JUMP_CUT;   /* let go: a short hop */
    hero.vy += GRAV; if (hero.vy > MAXFALL) hero.vy = MAXFALL;
    /* sideways, then snap to the wall */
    nx = hero.x + hero.vx; y = hero.y / U;
    x = nx / U;
    if (hero.vx > 0 && (solid_px(x + HB_R, y + HB_T) || solid_px(x + HB_R, y + HB_B))) nx = ((((x + HB_R) >> 4) << 4) - HB_R - 1) * U;
    if (hero.vx < 0 && (solid_px(x + HB_L, y + HB_T) || solid_px(x + HB_L, y + HB_B))) nx = ((((x + HB_L) >> 4) << 4) + 16 - HB_L) * U;
    if (nx < 0) nx = 0;
    hero.x = nx; x = nx / U;
    /* up or down, then snap to the floor or the ceiling */
    ny = hero.y + hero.vy; y = ny / U;
    hero.on_ground = 0;
    if (hero.vy >= 0) {
        uint8_t kl = kind_px(x + HB_L, y + HB_B), kr = kind_px(x + HB_R, y + HB_B);
        uint8_t plat = (kl == 4 || kr == 4) && (((hero.y / U + HB_B) >> 4) < ((y + HB_B) >> 4) || ((hero.y / U + HB_B) & 15) == 15);
        if (kl == 1 || kr == 1 || plat) { ny = ((((y + HB_B) >> 4) << 4) - HB_B - 1) * U; hero.vy = 0; hero.on_ground = 1; }
    } else if (solid_px(x + HB_L, y + HB_T) || solid_px(x + HB_R, y + HB_T)) { ny = ((((y + HB_T) >> 4) << 4) + 16 - HB_T) * U; hero.vy = 0; }
    hero.y = ny;
    /* the picture */
    if (!hero.on_ground) hero.frame = hero.vy < 0 ? SP_JUMP : SP_FALL;
    else if (hero.vx) { if ((frame & 3) == 0) hero.anim = (uint8_t)((hero.anim + 1) & 3); hero.frame = (uint8_t)(SP_WALK1 + hero.anim); }
    else hero.frame = SP_IDLE;
}

/* ---- slugs, gems, the goal, spikes ----------------------------------------- */
static void move_slugs(void)
{
    uint8_t i;
    for (i = 0; i < nslug; i++) {
        slug_t *s = &slug[i]; int16_t x, y, ahead;
        if (!s->alive) continue;
        if (s->x - camx * U > (VIEW_W + 32) * U || s->x - camx * U < -32 * U) continue;   /* asleep off screen */
        x = s->x / U; y = s->y / U;
        ahead = s->dir < 0 ? x + 1 : x + 14;
        if (solid_px(ahead + s->dir, y + 12) || !solid_px(ahead + s->dir, y + 16)) s->dir = (int8_t)-s->dir;
        else s->x += s->dir * 4;
        if ((frame & 7) == 0) s->anim ^= 1;
    }
}
static void puff(int16_t x, int16_t y) { puff_x = x; puff_y = y; puff_t = 12; }
static void hero_dies(void) { hero.dead = 60; snd_die(); }
static void touch(void)
{
    uint8_t i; int16_t hx = hero.x / U, hy = hero.y / U;
    /* spikes: any corner of the hit box */
    if (kind_px(hx + HB_L, hy + HB_B) == 2 || kind_px(hx + HB_R, hy + HB_B) == 2 || kind_px(hx + HB_L, hy + HB_T + 6) == 2 || kind_px(hx + HB_R, hy + HB_T + 6) == 2) { hero_dies(); return; }
    if (hy > 250) { hero_dies(); return; }              /* fell out of the world */
    for (i = 0; i < nslug; i++) {
        slug_t *s = &slug[i]; int16_t sx, sy;
        if (!s->alive) continue;
        sx = s->x / U; sy = s->y / U;
        if (hx + HB_R < sx + 2 || hx + HB_L > sx + 13 || hy + HB_B < sy + 5 || hy + HB_T > sy + 14) continue;
        if (hero.vy > 0 && hy + HB_B < sy + 11) {          /* from above: squashed */
            s->alive = 0; hero.vy = -30; score += 200; snd_stomp(); puff(s->x, s->y);
        } else { hero_dies(); return; }
    }
    for (i = 0; i < ngem; i++) {
        gem_t *g = &gem[i]; int16_t gx, gy;
        if (!g->alive) continue;
        gx = (int16_t)(g->tx * 16) + 8; gy = (int16_t)(g->ty * 16) + 6;
        if (gx > hx + HB_L - 2 && gx < hx + HB_R + 2 && gy > hy + HB_T - 2 && gy < hy + HB_B + 2) { g->alive = 0; gems_left--; score += 100; snd_gem(); }
    }
}
static uint8_t at_goal(void)
{
    int16_t hx = hero.x / U, hy = hero.y / U;
    return kind_px(hx + HB_L, hy + HB_B) == 3 || kind_px(hx + HB_R, hy + HB_B) == 3 || kind_px(hx + HB_L, hy + HB_T) == 3 || kind_px(hx + HB_R, hy + HB_T) == 3
        || kind_px(hx + HB_R + 1, hy + 8) == 3 || kind_px(hx + HB_L - 1, hy + 8) == 3;
}

/* ---- drawing ----------------------------------------------------------------- */
static void put_str(uint8_t x, uint8_t y, const char *s) { uint32_t p = TEXTMAP + (uint32_t)y * 40 + x; while (*s) far_poke(p++, *s++); }
static void put_dec(uint8_t x, uint8_t y, uint32_t v, uint8_t w)
{ uint32_t p = TEXTMAP + (uint32_t)y * 40 + x + w; while (w--) { far_poke(--p, (uint8_t)('0' + v % 10)); v /= 10; } }
static void centre(uint8_t y, const char *s) { uint8_t n = 0; const char *q = s; while (*q++) n++; put_str((uint8_t)(n >= 40 ? 0 : (40 - n) / 2), y, s); }
static void clear_text(uint8_t y0, uint8_t y1) { dma_fill(' ', TEXTMAP + (uint32_t)y0 * 40, (uint32_t)(y1 - y0 + 1) * 40); }
static void hud(void)
{
    uint8_t i;
    put_str(0, 0, "SCORE "); put_dec(6, 0, score, 6);
    put_str(15, 0, "GEMS "); put_dec(20, 0, gems_left, 2);
    put_str(25, 0, "LEVEL "); put_dec(31, 0, level, 1);
    for (i = 0; i < 4; i++) far_poke(TEXTMAP + 35 + i, (uint8_t)(i < lives ? 3 : ' '));
}
static void init_tables(void)
{
    uint8_t i; uint32_t t;
    dma_fill(0, SPRTAB_A, 4096); dma_fill(0, SPRTAB_B, 4096);
    for (t = SPRTAB_A; t <= SPRTAB_B; t += SPRTAB_B - SPRTAB_A)
        for (i = 0; i < NSPR; i++) { far_poke(t + (uint32_t)i * 16 + 9, 1 | (1 << 2)); far_poke(t + (uint32_t)i * 16 + 10, 0); }
}
static void put_spr(uint32_t t, int16_t x, int16_t y, uint8_t fr, uint8_t ctrl)
{
    uint32_t d = SPRD + ((uint32_t)fr << 7);
    far_poke16(t, (uint16_t)x); far_poke16(t + 2, (uint16_t)y);
    far_poke16(t + 4, (uint16_t)d); far_poke16(t + 6, (uint16_t)(d >> 16));
    far_poke(t + 8, ctrl);
}
static void write_table(uint32_t t)
{
    uint8_t i;
    put_spr(t + S_HERO * 16, hero.x / U - camx, hero.y / U, hero.frame, (uint8_t)((hero.dead && (frame & 4)) ? 0 : 1 | (hero.face ? 4 : 0)));
    for (i = 0; i < MAXGEM; i++) {
        gem_t *g = &gem[i]; int16_t sx;
        if (i >= ngem || !g->alive) { far_poke(t + (S_GEM + i) * 16 + 8, 0); continue; }
        sx = (int16_t)(g->tx * 16) - camx;
        if (sx < -16 || sx > VIEW_W) { far_poke(t + (S_GEM + i) * 16 + 8, 0); continue; }
        put_spr(t + (S_GEM + i) * 16, sx, (int16_t)(g->ty * 16), (uint8_t)(SP_GEM0 + ((frame >> 4) & 1)), 1);
    }
    for (i = 0; i < MAXSLUG; i++) {
        slug_t *s = &slug[i]; int16_t sx;
        if (i >= nslug || !s->alive) { far_poke(t + (S_SLUG + i) * 16 + 8, 0); continue; }
        sx = s->x / U - camx;
        if (sx < -16 || sx > VIEW_W) { far_poke(t + (S_SLUG + i) * 16 + 8, 0); continue; }
        put_spr(t + (S_SLUG + i) * 16, sx, s->y / U, (uint8_t)(SP_SLUG0 + s->anim), (uint8_t)(1 | (s->dir > 0 ? 4 : 0)));
    }
    put_spr(t + S_PUFF * 16, puff_x / U - camx, puff_y / U, SP_PUFF, puff_t ? 1 : 0);
}
static void camera(void)
{
    int16_t want = hero.x / U - 140;
    if (want < 0) want = 0;
    if (want > (int16_t)(mapw * 16 - VIEW_W)) want = (int16_t)(mapw * 16 - VIEW_W);
    camx = want;
    w16(V_LAYER(0) + 2, (uint16_t)camx);
}
static void setup(void)
{
    uint8_t i;
    REG(V_CTRL) = 0; REG(V_BGCOL) = 0;
    pal(0, fl_tone[0][0], fl_tone[0][1], fl_tone[0][2]);  /* the sky is the lightest green */
    for (i = 0; i < 4; i++) pal((uint8_t)(i + 1), fl_tone[i][0], fl_tone[i][1], fl_tone[i][2]);
    pal(255, fl_tone[3][0], fl_tone[3][1], fl_tone[3][2]);
    dma_fill(' ', TEXTMAP, 40 * 30);
    text8_layer(1, TEXTMAP, 40, CAPTION_PAL);
    init_tables(); w32(V_SPRTAB, SPRTAB_A); REG(V_SPRCTL) = 1;
    REG(V_CTRL) = 1 | 2 | 4;
}
static uint8_t wait_key(uint8_t frames)                    /* 1 go on, 2 leave, 3 the practice key, 0 timed out */
{
    uint8_t k;
    while (frames == 0 || frames--) {
        k = key_get();
        if (k == 0x1B) return 2;
        if (k == '2') return 3;
        if (k == ' ' || k == 0x0D || (keys_held() & HELD_FIRE)) return 1;
        frame++; wait_vblank();
    }
    return 0;
}
static uint8_t title(void)
{
    uint8_t r;
    REG(V_LAYER(0)) = 0; REG(V_SPRCTL) = 0;
    clear_text(0, 29);
    centre(5, "F L U F F Y");
    centre(8, "a platformer, in Game Boy green");
    centre(10, "tiles and Fluffy: Chloe Wolfe, CC0");
    put_str(3, 14, "left/right, pad   run");
    put_str(3, 15, "space, A or up    jump, hold: higher");
    put_str(3, 16, "Esc               leave");
    centre(20, "gems are points. slugs squash from above");
    centre(21, "spikes kill. the smiling block leads on");
    centre(25, "space to start  (2: start at level 2)");
    while (key_get()) ;
    r = wait_key(0);
    clear_text(0, 29);
    REG(V_SPRCTL) = 1;
    return r;
}

void main(void)
{
    uint8_t k, h, last_h = 0, r, i;
    setup();
    for (;;) {
        r = title();
        if (r == 2) break;
        score = 0; lives = 3; level = (uint8_t)(r == 3 ? 2 : 1);
        load_level(); spawn_hero(); camera(); puff_t = 0;
        for (;;) {
            uint32_t back = cur ? SPRTAB_A : SPRTAB_B;
            k = key_get(); h = keys_held();
            if (h & HELD_UP) h |= HELD_FIRE;
            if (h & HELD_A) h |= HELD_FIRE;
            if (k == 0x1B) { lives = 0; break; }
            if (!hero.dead) {
                move_hero(h, (uint8_t)((h & HELD_FIRE) && !(last_h & HELD_FIRE)));
                touch();
                if (at_goal()) {
                    snd_win(); score += 1000 + (uint32_t)(gems_left == 0 ? 2000 : 0);
                    hud(); centre(13, level == 1 ? "LEVEL CLEAR" : "YOU WIN"); if (gems_left == 0) centre(15, "every gem: +2000");
                    r = wait_key(150); clear_text(13, 15);
                    if (r == 2 || level == 2) { lives = 0; break; }
                    level = 2; load_level(); spawn_hero(); last_h = h; continue;
                }
            } else if (--hero.dead == 0) {
                if (--lives == 0) { hud(); break; }
                spawn_hero();
            }
            last_h = h;
            move_slugs();
            if (puff_t) puff_t--;
            camera(); hud();
            write_table(back);
            wait_vblank();
            w32(V_SPRTAB, back); cur ^= 1; frame++;
        }
        hush();
        for (i = 0; i < MAXSLUG; i++) slug[i].alive = 0;
        centre(13, level == 2 && lives ? "T H E   E N D" : "G A M E   O V E R");
        centre(15, "space plays again, Esc leaves");
        write_table(cur ? SPRTAB_A : SPRTAB_B);
        if (wait_key(0) == 2) break;
    }
    hush();
    REG(V_SPRCTL) = 0; REG(V_LAYER(0)) = 0; REG(V_LAYER(1)) = 0;
}
