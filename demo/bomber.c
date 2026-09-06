/* K4510: BOMBER -- a Bomberman, 320x240, three levels.
 *
 * The arena is a 20x15 grid of 16x16 8 bpp tiles on VICKY layer 0: the
 * classic ring wall, the pillar lattice, crates hiding pickups.  Art is
 * the Bomb Party sheet (CC-BY 3.0, data/bombparty/), palettized to
 * indices 32+ by tools/mkbomber.py so the console's colours survive.
 * The hero and up to six monsters are 16x16 8 bpp sprites; bombs, blasts
 * and pickups live in the tile map itself.
 *
 * Drop a bomb, run, let the cross of flame take the crates and the
 * monsters -- and not you.  Chain reactions chain.  Crates drop extra
 * bombs and longer flames.  Clear the monsters to clear the level;
 * three levels clear the game.
 *
 *   arrows  walk (the machine way: he keeps walking; the same arrow
 *           again stops him -- so does a wall)
 *   space   drop a bomb          Esc     back to the shell
 */
#include "k4510.h"
#include "bomber.h"

#define TERM 0xDA00u

#define TILES    0x00110000UL
#define SPRD     0x00112000UL
#define MAPF     0x00114000UL
#define TEXTMAP  0x00115000UL
#define SPRTAB_A 0x00116000UL
#define SPRTAB_B 0x00117000UL

#define GW 20
#define GH 15
#define NEN 6
#define NSPR (1 + NEN)

/* what a cell holds (grid[]); the shown tile mostly matches */
enum { C_FLOOR, C_WALL, C_BOX, C_BOMB, C_PUB, C_PUF, C_BLAST };

/* sprite frame order in bp_spr */
enum { H_D0, H_D1, H_S0, H_S1, H_U0, H_U1, E_G0, E_G1, E_B0, E_B1, E_P0, E_P1 };

void __fastcall__ rom_chrout(unsigned char c);
unsigned char rom_getin(void);

static uint8_t grid[GH][GW];
static uint8_t level, lives, foes;
static uint32_t score;
static uint8_t frame, cur, nb, rng, planted;
static uint16_t seed = 0x5A11;
static uint8_t rnd(void) { seed = seed * 25173u + 13849u; return (uint8_t)(seed >> 8); }

typedef struct {
    int16_t x, y; int8_t dx, dy;
    int8_t wx, wy;                       /* the wish: adopted at the next cell */
    uint8_t base, flip, alive;           /* base: first frame in bp_spr */
} actor_t;
static actor_t men[NSPR];

#define NBOMB 6
typedef struct { uint8_t x, y, t, on; } bomb_t;
static bomb_t bombs[NBOMB];
#define FUSE 150

#define NBL 40
typedef struct { uint8_t x, y, t, tile; } blast_t;
static blast_t blasts[NBL];

/* ---- the map ------------------------------------------------------------ */
static void show(uint8_t x, uint8_t y, uint8_t t)
{
    far_poke16(MAPF + (((uint16_t)y * GW + x) << 1), t);
}
static void put(uint8_t x, uint8_t y, uint8_t c)
{
    static const uint8_t face[] = { BT_FLOOR, BT_WALL, BT_BOX, BT_BOMB0, BT_PUB, BT_PUF, BT_BLASTC };
    grid[y][x] = c; show(x, y, face[c]);
}

static void load_level(void)
{
    uint8_t x, y, i, density = 90 + level * 30;
    for (i = 0; i < NBOMB; i++) bombs[i].on = 0;
    for (i = 0; i < NBL; i++) blasts[i].t = 0;
    planted = 0;
    for (y = 0; y < GH; y++) for (x = 0; x < GW; x++) {
        if (x == 0 || y == 0 || x == GW - 1 || y == GH - 1) { put(x, y, C_WALL); continue; }
        if ((x & 1) == 0 && (y & 1) == 0) { put(x, y, C_WALL); continue; }
        put(x, y, (rnd() < density) ? C_BOX : C_FLOOR);
    }
    /* the hero's corner stays open */
    put(1, 1, C_FLOOR); put(2, 1, C_FLOOR); put(1, 2, C_FLOOR);
    men[0].x = 16; men[0].y = 16; men[0].dx = men[0].dy = 0; men[0].wx = men[0].wy = 0;
    men[0].base = H_D0; men[0].alive = 1; men[0].flip = 0;
    foes = 3 + level + (level == 2 ? 1 : 0);             /* 4, 5, 6 */
    for (i = 1; i <= foes; i++) {
        actor_t *e = &men[i]; uint8_t ex, ey;
        do { ex = 1 + rnd() % (GW - 2); ey = 1 + rnd() % (GH - 2); }
        while (grid[ey][ex] != C_FLOOR || (ex < 6 && ey < 6));
        put(ex, ey, C_FLOOR);
        e->x = (int16_t)ex << 4; e->y = (int16_t)ey << 4;
        e->base = (uint8_t)(E_G0 + 2 * ((i - 1) % (1 + (level ? level : 0) + (level == 2 ? 1 : 0)) % 3));
        if (level == 0) e->base = E_G0;
        if (level == 1) e->base = (i & 1) ? E_G0 : E_B0;
        if (level == 2) e->base = (uint8_t)(E_G0 + 2 * ((i - 1) % 3));
        e->dx = 1; e->dy = 0; e->wx = 1; e->wy = 0; e->alive = 1; e->flip = 0;
    }
    for (; i < NSPR; i++) men[i].alive = 0;
}

/* ---- movement ----------------------------------------------------------- */
static uint8_t open_for(actor_t *a, int8_t cx, int8_t cy)
{
    uint8_t c;
    if (cx < 0 || cx >= GW || cy < 0 || cy >= GH) return 0;
    c = grid[cy][cx];
    if (c == C_WALL || c == C_BOX) return 0;
    if (c == C_BOMB && !((a == &men[0]) && (men[0].x >> 4) == cx && (men[0].y >> 4) == cy)) return 0;
    return 1;                                            /* blast and pickups are walkable */
}
static uint8_t tick_actor(actor_t *a)                    /* 2 px along its heading */
{
    int8_t cx, cy;
    if ((a->x & 15) || (a->y & 15)) { a->x += a->dx * 2; a->y += a->dy * 2; return 1; }
    a->dx = a->wx; a->dy = a->wy;                        /* on the grid: adopt the wish */
    cx = (int8_t)(a->x >> 4); cy = (int8_t)(a->y >> 4);
    if (a->dx || a->dy) {
        if (open_for(a, (int8_t)(cx + a->dx), (int8_t)(cy + a->dy))) {
            if (a->dx) a->flip = a->dx < 0;
            a->x += a->dx * 2; a->y += a->dy * 2; return 1;
        }
        a->dx = a->dy = 0; a->wx = a->wy = 0;
    }
    return 0;
}

/* ---- bombs and blasts --------------------------------------------------- */
static void status_line(void);
static void add_blast(uint8_t x, uint8_t y, uint8_t tile)
{
    uint8_t i;
    for (i = 0; i < NBL; i++) if (!blasts[i].t) break;
    if (i == NBL) return;
    blasts[i].x = x; blasts[i].y = y; blasts[i].t = 26; blasts[i].tile = tile;
    put(x, y, C_BLAST); show(x, y, tile);
}
static void boom(uint8_t bi);
static uint8_t burn(uint8_t x, uint8_t y, uint8_t tile)  /* 1 = the arm goes on */
{
    uint8_t c = grid[y][x], i;
    if (c == C_WALL) return 0;
    if (c == C_BOX) {                                    /* a crate: burn it, maybe drop a gift */
        uint8_t r = rnd();                           /* rnd() is 0-255: 40%% bombs, 40%% flames, 20%% nothing */
        if (r < 102) put(x, y, C_PUB);
        else if (r < 204) put(x, y, C_PUF);
        else add_blast(x, y, tile);
        score += 10;
        return 0;                                        /* the crate soaks the flame */
    }
    if (c == C_BOMB) {                                   /* chain reaction */
        for (i = 0; i < NBOMB; i++)
            if (bombs[i].on && bombs[i].x == x && bombs[i].y == y) { boom(i); break; }
        return 0;
    }
    add_blast(x, y, tile);
    return 1;
}
static void boom(uint8_t bi)
{
    bomb_t *b = &bombs[bi]; uint8_t r;
    if (!b->on) return;
    b->on = 0; planted--;
    put(b->x, b->y, C_FLOOR);
    add_blast(b->x, b->y, BT_BLASTC);
    for (r = 1; r <= rng; r++) if (!burn((uint8_t)(b->x - r), b->y, BT_BLASTH)) break;
    for (r = 1; r <= rng; r++) if (!burn((uint8_t)(b->x + r), b->y, BT_BLASTH)) break;
    for (r = 1; r <= rng; r++) if (!burn(b->x, (uint8_t)(b->y - r), BT_BLASTV)) break;
    for (r = 1; r <= rng; r++) if (!burn(b->x, (uint8_t)(b->y + r), BT_BLASTV)) break;
    status_line();                                       /* the crates just paid out */
}
static void drop_bomb(void)
{
    actor_t *p = &men[0];
    uint8_t cx = (uint8_t)((p->x + 8) >> 4), cy = (uint8_t)((p->y + 8) >> 4), i;
    if (planted >= nb) return;
    if (grid[cy][cx] != C_FLOOR) return;
    for (i = 0; i < NBOMB; i++) if (!bombs[i].on) break;
    if (i == NBOMB) return;
    bombs[i].x = cx; bombs[i].y = cy; bombs[i].t = FUSE; bombs[i].on = 1;
    planted++;
    put(cx, cy, C_BOMB);
}
static void tick_bombs(void)
{
    uint8_t i;
    for (i = 0; i < NBOMB; i++) {
        bomb_t *b = &bombs[i];
        if (!b->on) continue;
        if (--b->t == 0) { boom(i); continue; }
        show(b->x, b->y, (uint8_t)(b->t < 40 ? BT_BOMB2 : (b->t & 16) ? BT_BOMB1 : BT_BOMB0));
    }
    for (i = 0; i < NBL; i++) {
        blast_t *l = &blasts[i];
        if (!l->t) continue;
        if (--l->t == 0 && grid[l->y][l->x] == C_BLAST) put(l->x, l->y, C_FLOOR);
    }
}

/* ---- the cast ----------------------------------------------------------- */
static void enemy_brain(actor_t *e)
{
    int8_t cx, cy; uint8_t tries;
    if ((e->x & 15) || (e->y & 15)) return;
    cx = (int8_t)(e->x >> 4); cy = (int8_t)(e->y >> 4);
    if (e->wx || e->wy) {                                /* mostly keep going */
        if ((rnd() & 7) && open_for(e, (int8_t)(cx + e->wx), (int8_t)(cy + e->wy))) return;
    }
    for (tries = 0; tries < 8; tries++) {
        switch (rnd() & 3) {
        case 0: e->dx = 1; e->dy = 0; break;
        case 1: e->dx = -1; e->dy = 0; break;
        case 2: e->dx = 0; e->dy = 1; break;
        default: e->dx = 0; e->dy = -1; break;
        }
        if (open_for(e, (int8_t)(cx + e->dx), (int8_t)(cy + e->dy))) { e->wx = e->dx; e->wy = e->dy; return; }
    }
    e->dx = e->dy = 0; e->wx = e->wy = 0;
}
static uint8_t on_blast(actor_t *a)
{
    return grid[(a->y + 8) >> 4][(a->x + 8) >> 4] == C_BLAST;
}

/* ---- presentation ------------------------------------------------------- */
static void num5(uint32_t v, uint8_t x)
{
    uint8_t i; char b[6];
    for (i = 0; i < 5; i++) { b[4 - i] = (char)('0' + v % 10); v /= 10; }
    b[5] = 0; text8_print(TEXTMAP, 40, x, 0, b);
}
static void status_line(void)
{
    text8_print(TEXTMAP, 40, 0, 0, "SCORE");
    num5(score, 6);
    text8_print(TEXTMAP, 40, 13, 0, "MEN");
    far_poke(TEXTMAP + 17, (uint8_t)('0' + lives));
    text8_print(TEXTMAP, 40, 20, 0, "LVL");
    far_poke(TEXTMAP + 24, (uint8_t)('1' + level));
    text8_print(TEXTMAP, 40, 27, 0, "B");
    far_poke(TEXTMAP + 28, (uint8_t)('0' + nb));
    text8_print(TEXTMAP, 40, 31, 0, "F");
    far_poke(TEXTMAP + 32, (uint8_t)('0' + rng));
}
static void centre(uint8_t row, const char *s)
{
    uint8_t n = 0; const char *p = s; while (*p++) n++;
    text8_print(TEXTMAP, 40, (uint8_t)((40 - n) / 2), row, s);
}
static void write_table(uint32_t t)
{
    uint8_t i;
    for (i = 0; i < NSPR; i++, t += 16) {
        actor_t *a = &men[i];
        uint8_t fr = (uint8_t)(a->base + (((frame >> 3) & 1) && (a->dx || a->dy) ? 1 : 0));
        uint32_t d;
        if (i == 0 && a->dx) fr = (uint8_t)(H_S0 + ((frame >> 3) & 1));
        if (i == 0 && a->dy < 0) fr = (uint8_t)(H_U0 + ((frame >> 3) & 1));
        d = SPRD + (uint32_t)fr * 256;
        far_poke16(t, (uint16_t)a->x); far_poke16(t + 2, (uint16_t)a->y);
        far_poke16(t + 4, (uint16_t)d); far_poke16(t + 6, (uint16_t)(d >> 16));
        far_poke(t + 8, (uint8_t)((a->alive == 1 ? 1 : 0) | 2 | (a->flip ? 4 : 0)));
    }
}
static uint8_t pause_msg(const char *s1, const char *s2)
{
    uint8_t k;
    write_table(SPRTAB_A); write_table(SPRTAB_B);
    centre(13, s1); if (s2) centre(15, s2);
    for (;;) {
        k = key_get();
        if (k == 0x1B) return 1;
        if (k) break;
        wait_vblank();
    }
    dma_fill(' ', TEXTMAP + 40 * 13, 40); dma_fill(' ', TEXTMAP + 40 * 15, 40);
    return 0;
}
static void hw_init(void)
{
    uint16_t i, L = V_LAYER(0); uint32_t t;
    for (i = 0; i < BP_NPAL; i++) pal((uint8_t)(BP_BASE + i), bp_pal[i * 3], bp_pal[i * 3 + 1], bp_pal[i * 3 + 2]);
    pal(255, 255, 255, 255);
    for (t = 0; t < sizeof bp_tiles; t++) far_poke(TILES + t, bp_tiles[t]);
    for (t = 0; t < sizeof bp_spr; t++) far_poke(SPRD + t, bp_spr[t]);
    REG(L + 1) = 0; w16(L + 2, 0); w16(L + 4, 0); w16(L + 6, GW);
    w32(L + 8, TILES); w32(L + 12, MAPF);
    REG(L) = 1 | (1 << 1) | (3 << 3) | (1 << 5);
    dma_fill(' ', TEXTMAP, 40 * 30);
    text8_layer(1, TEXTMAP, 40, 127);
    dma_fill(0, SPRTAB_A, 4096); dma_fill(0, SPRTAB_B, 4096);
    for (t = SPRTAB_A; t <= SPRTAB_B; t += SPRTAB_B - SPRTAB_A)
        for (i = 0; i < NSPR; i++) {
            far_poke(t + (uint32_t)i * 16 + 8, 1 | 2);
            far_poke(t + (uint32_t)i * 16 + 9, 1 | (1 << 2));
        }
    w32(V_SPRTAB, SPRTAB_A); REG(V_SPRCTL) = 1;
}

/* ---- main --------------------------------------------------------------- */
void main(void)
{
    uint8_t i, k, running = 1, dead;
    REG(V_CTRL) = 0;
    hw_init();
    lives = 3; score = 0; level = 0; nb = 1; rng = 1;
    load_level(); status_line();
    REG(V_CTRL) = 1 | 2 | 4;                             /* 320 x 240 */
    if (pause_msg("BOMBER -- CLEAR THE MONSTERS", "SPACE DROPS THE BOMB -- ANY KEY")) running = 0;

    while (running) {
        uint32_t back = cur ? SPRTAB_A : SPRTAB_B;
        actor_t *p = &men[0];
        /* The wish is the arrow HELD ($D104), adopted at the next cell as
         * before; no arrow held, no wish, and he stops on the grid.  This
         * used to toggle on each PRESS -- with the keyboard's autorepeat
         * re-sending a held arrow, that was stop, go, stop, go (2026-09-05).
         * The bomb stays an event: one press, one bomb. */
        { uint8_t h = keys_held();
          if      (h & HELD_UP)    { p->wx = 0;  p->wy = -1; p->base = H_U0; }
          else if (h & HELD_DOWN)  { p->wx = 0;  p->wy = 1;  p->base = H_D0; }
          else if (h & HELD_LEFT)  { p->wy = 0;  p->wx = -1; p->base = H_S0; }
          else if (h & HELD_RIGHT) { p->wy = 0;  p->wx = 1;  p->base = H_S0; }
          else                     { p->wx = 0;  p->wy = 0; } }
        k = key_get();
        switch (k) {
        case 0x1B: running = 0; break;
        case ' ': drop_bomb(); break;
        }
        tick_actor(p);
        {   /* pickups under his feet */
            uint8_t cx = (uint8_t)((p->x + 8) >> 4), cy = (uint8_t)((p->y + 8) >> 4);
            uint8_t c = grid[cy][cx];
            if (c == C_PUB) { if (nb < 6) nb++; put(cx, cy, C_FLOOR); score += 50; status_line(); }
            if (c == C_PUF) { if (rng < 5) rng++; put(cx, cy, C_FLOOR); score += 50; status_line(); }
        }
        for (i = 1; i <= foes; i++) {
            actor_t *e = &men[i];
            if (e->alive != 1) continue;
            enemy_brain(e);
            if (frame & 1) tick_actor(e);
            if (on_blast(e)) { e->alive = 0; score += 200; status_line(); }
        }
        tick_bombs();
        dead = 0;
        if (on_blast(p)) dead = 1;
        for (i = 1; i <= foes; i++)
            if (men[i].alive == 1) {
                int16_t ax = men[i].x - p->x, ay = men[i].y - p->y;
                if (ax < 0) ax = -ax; if (ay < 0) ay = -ay;
                if (ax < 10 && ay < 10) dead = 1;
            }
        if (dead) {
            if (lives) lives--;
            status_line();
            if (!lives) {
                centre(13, "GAME OVER");
                pause_msg("THE MONSTERS KEEP THE MAZE", "ANY KEY LEAVES");
                running = 0; continue;
            }
            load_level(); status_line();
            if (pause_msg("CAUGHT! AGAIN...", "ANY KEY")) running = 0;
            continue;
        }
        {   /* all monsters down? */
            uint8_t left = 0;
            for (i = 1; i <= foes; i++) if (men[i].alive == 1) left = 1;
            if (!left) {
                score += 500; level++;
                if (level == 3) {
                    centre(13, "ALL THREE MAZES CLEARED");
                    pause_msg("A TRUE BOMBER", "ANY KEY LEAVES");
                    running = 0; continue;
                }
                load_level(); status_line();
                if (pause_msg("MAZE CLEARED -- THE NEXT", "ANY KEY")) running = 0;
                continue;
            }
        }
        write_table(back);
        wait_vblank();
        w32(V_SPRTAB, back); cur ^= 1; frame++;
    }
    REG(TERM + 4) = 2;
}
