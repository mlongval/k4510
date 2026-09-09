/* K4510: LODE -- a Lode Runner, 320x240, three levels.
 *
 * A 20x15 grid of 16x16 8 bpp tiles on VICKY layer 0 (drawn by code at
 * startup -- bricks, stone, ladders, rope, gold; no vendored art), the
 * runner and the guards as 16x16 8 bpp sprites pixelled here in ASCII and
 * recoloured per side, a text8 status line on layer 1.  All the sprite
 * frames are multicolour: outline, suit, skin, boots, each its own
 * palette entry (indices 32-47, so the console's own 16 stay untouched).
 *
 * The rules are the 1983 ones: run, climb, hang from the rope, fall off
 * edges, dig through brick left and right, collect every gold bag; the
 * exit ladder appears and the top of the screen wins the level.  Guards
 * chase, fall into dug holes, climb out again; a hole that closes over
 * anyone is the end of them.  Stone does not dig.
 *
 *   arrows  run / climb while held; Z/X dig left/right; G guards on/off; - + pace
 *   space   stop            Z / X   dig left / right
 *   Esc     back to the shell
 */
#include "k4510.h"

#define TERM 0xDA00u

/* far memory: everything the chips read */
#define TILES    0x00110000UL            /* 16 tiles x 256 bytes */
#define SPR      0x00112000UL            /* sprite frames, hero then guard */
#define MAPF     0x00114000UL            /* the tile map, 20x15 x 2 bytes */
#define TEXTMAP  0x00115000UL            /* text8, 40x30 */
#define SPRTAB_A 0x00116000UL
#define SPRTAB_B 0x00117000UL

#define GW 20
#define GH 15
#define NGUARD 3
#define NSPR   (1 + NGUARD)

/* grid cells */
enum { T_EMPTY, T_BRICK, T_STONE, T_LADDER, T_ROPE, T_GOLD, T_EXIT,
       T_HOLE1, T_HOLE2, T_HOLE3 };      /* HOLE1 open, 2/3 closing */
/* palette indices (32-47: the console keeps 0-15) */
#define C_BG   32
#define C_BRK  33
#define C_BRKD 34
#define C_STN  35
#define C_STND 36
#define C_LAD  37
#define C_ROPE 38
#define C_GLD  39
#define C_GLDD 40
#define C_WHT  41
#define C_SUIT 42
#define C_SKIN 43
#define C_GBODY 44
#define C_GDARK 45
#define C_OUT  46

/* sprite frames (per side: hero at SPR, guard at SPR + 7*256) */
enum { F_RUN1, F_RUN2, F_CLIMB1, F_CLIMB2, F_HANG1, F_HANG2, F_FALL };

/* ---- the levels: 20 x 15.  # brick  @ stone  H ladder  - rope  $ gold
 *      E hidden exit ladder  P player  G guard  (short rows pad right) */
static const char *levels[3][GH] = {
 {  "E",
    "E    $        H",
    "E#####H########H",
    "      H        H   $",
    "  $   H  ######H####",
    "###H###  H",
    "   H     H--------H",
    "   H     H        H",
    "   H  $  H    $   H",
    "  #H#####H######  H",
    "   H            $ H",
    "   H  G   ####H####",
    "  ##########  H",
    "P             H    G",
    "@@@@@@@@@@@@@@@@@@@@" },
 {  "                   E",
    "  $                E",
    "H######--------####E",
    "H     $        $",
    "H###H#####H#####H###",
    "    H     H     H",
    " $  H  G  H  $  H",
    "####H#####H#####H",
    "    H     H     H  $",
    "    H  $  H     H###",
    "  ##H###@@H@@   H",
    "    H         G H",
    " P  H           H",
    "########H###########",
    "@@@@@@@@@@@@@@@@@@@@" },
 {  "         E",
    "  $      E        $",
    "###H#####E#####H####",
    "   H     E     H",
    "   H  ---E---  H",
    " G H     E   $ H  G",
    "###H###  E  ###H####",
    "   H  $  E     H",
    "   H  ###H###  H",
    "   H     H     H",
    "  #H## $ H  ###H##",
    "   H  ###H###  H  $",
    "   H     H   G H ###",
    "P  H     H     H",
    "@@@@@@@@@@@@@@@@@@@@" },
};

/* ---- the sprite frames, 16x16 ASCII: . none  O outline  B suit
 *      S skin  W boots/hands  (recoloured per side at build time) */
static const char *shape[7][16] = {
 { /* RUN1: right leg forward */
   "................", ".....OOOO.......", "....OSSSSO......", "....OSSSSO......",
   ".....OSSO.......", "....OBBBBO......", "...OBOBBOBO.....", "..OWO.BB.OWO....",
   "......BB........", ".....OBBO.......", ".....OBBO.......", "....OB..BO......",
   "...OB....BO.....", "...OB.....BO....", "..OWWO...OWWO...", "................" },
 { /* RUN2: legs passing */
   "................", ".....OOOO.......", "....OSSSSO......", "....OSSSSO......",
   ".....OSSO.......", "....OBBBBO......", "....OBBBBO......", "...OWOBBOWO.....",
   "......BB........", ".....OBBO.......", ".....OBBO.......", ".....OBBO.......",
   ".....OBBO.......", ".....OBBO.......", "....OWWWWO......", "................" },
 { /* CLIMB1: back view, left arm up */
   "...OW...........", "...OBOOOO.......", "...OBSSSSO......", "....OSSSSO......",
   ".....OSSO.......", "....OBBBBO......", "....OBBBBOW.....", ".....OBBOBO.....",
   "......BB........", ".....OBBO.......", ".....OBBO.......", "....OB.BO.......",
   "....OB..BO......", "....OB..BO......", "...OWWO.OWWO....", "................" },
 { /* CLIMB2: right arm up */
   "..........WO....", "......OOOOBO....", ".....OSSSSBO....", ".....OSSSSO.....",
   "......OSSO......", ".....OBBBBO.....", "....WOBBBBO.....", "....OBOBBO......",
   ".......BB.......", "......OBBO......", "......OBBO......", ".......OB.BO....",
   "......OB..BO....", "......OB..BO....", ".....OWWO.OWWO..", "................" },
 { /* HANG1: both hands on the rope, legs left */
   "..OWO....OWO....", "..OBO....OBO....", "..OBOOOOOOBO....", "...OBSSSSBO.....",
   "....OSSSSO......", ".....OSSO.......", "....OBBBBO......", "....OBBBBO......",
   ".....OBBO.......", ".....OBBO.......", "....OBBBO.......", "...OB.OBO.......",
   "..OB...BO.......", ".OWWO.OWO.......", "................", "................" },
 { /* HANG2: legs right */
   "..OWO....OWO....", "..OBO....OBO....", "..OBOOOOOOBO....", "...OBSSSSBO.....",
   "....OSSSSO......", ".....OSSO.......", "....OBBBBO......", "....OBBBBO......",
   ".....OBBO.......", ".....OBBO.......", ".....OBBBO......", ".....OBO.BO.....",
   ".....OB...BO....", ".....OWO.OWWO...", "................", "................" },
 { /* FALL: arms out, legs spread */
   "................", ".....OOOO.......", "....OSSSSO......", "....OSSSSO......",
   ".....OSSO.......", "..O..OBBO..O....", ".OWOOBBBBOOWO...", "..OBBBBBBBBO....",
   ".....OBBO.......", ".....OBBO.......", "....OB..BO......", "...OB....BO.....",
   "..OB......BO....", ".OB........BO...", ".OWO......OWO...", "................" },
};

void __fastcall__ rom_chrout(unsigned char c);
unsigned char rom_getin(void);

/* ---- state -------------------------------------------------------------- */
static uint8_t grid[GH][GW];             /* what the rules see */
static uint8_t level, lives, guards;
static uint16_t goldleft; static uint32_t score;
static uint8_t unlocked, frame, cur, grace;   /* frames the guards still hold their posts */

typedef struct {
    int16_t x, y;                        /* pixels, top-left of the 16x16 */
    int8_t dx, dy;                       /* the standing order, -1/0/1 */
    uint8_t flip, fr, alive, trapped;    /* fr: F_* frame */
    uint8_t sx, sy;                      /* spawn cell */
} actor_t;
static actor_t men[NSPR];                /* 0 = the runner */

typedef struct { uint8_t x, y, t; } hole_t;
#define NHOLE 12
static hole_t holes[NHOLE];
#define HOLE_LIFE 220                    /* frames a hole stays open */

/* ---- tiles, drawn by code ---------------------------------------------- */
static uint8_t trow[16];
static void tile_row(uint32_t t, uint8_t y) { uint8_t i; for (i = 0; i < 16; i++) far_poke(t + y * 16 + i, trow[i]); }
static void fill_row(uint8_t c) { uint8_t i; for (i = 0; i < 16; i++) trow[i] = c; }

static void draw_tiles(void)
{
    uint8_t y, i; uint32_t t;
    /* EMPTY: the night behind everything */
    t = TILES; fill_row(C_BG); for (y = 0; y < 16; y++) tile_row(t, y);
    /* BRICK: courses with mortar lines, offset every other course */
    t = TILES + 256UL * T_BRICK;
    for (y = 0; y < 16; y++) {
        fill_row(C_BRK);
        if ((y & 7) == 7) fill_row(C_BRKD);              /* horizontal mortar */
        else { uint8_t off = (y & 8) ? 3 : 11; trow[off] = C_BRKD; }
        tile_row(t, y);
    }
    /* STONE: big blocks, bevelled */
    t = TILES + 256UL * T_STONE;
    for (y = 0; y < 16; y++) {
        fill_row(C_STN);
        if (y == 0) fill_row(C_WHT); else if (y == 15) fill_row(C_STND);
        else { trow[0] = C_WHT; trow[15] = C_STND; }
        tile_row(t, y);
    }
    /* LADDER: two rails, rungs every four */
    t = TILES + 256UL * T_LADDER;
    for (y = 0; y < 16; y++) {
        fill_row(C_BG);
        trow[3] = trow[4] = C_LAD; trow[11] = trow[12] = C_LAD;
        if ((y & 3) == 1) for (i = 5; i < 11; i++) trow[i] = C_LAD;
        tile_row(t, y);
    }
    /* ROPE: a bar along the top */
    t = TILES + 256UL * T_ROPE;
    for (y = 0; y < 16; y++) {
        fill_row(C_BG);
        if (y == 1 || y == 2) fill_row(C_ROPE);
        tile_row(t, y);
    }
    /* GOLD: a little chest */
    t = TILES + 256UL * T_GOLD;
    for (y = 0; y < 16; y++) {
        fill_row(C_BG);
        if (y >= 6 && y <= 13) {
            for (i = 3; i <= 12; i++) trow[i] = (y == 6 || y == 13 || i == 3 || i == 12) ? C_GLDD : C_GLD;
            if (y == 9) for (i = 3; i <= 12; i++) trow[i] = C_GLDD;
            if (y == 8 || y == 10) { trow[7] = trow[8] = C_WHT; }
        }
        tile_row(t, y);
    }
    /* EXIT ladder looks exactly like a ladder */
    for (i = 0; i < 255; i++) far_poke(TILES + 256UL * T_EXIT + i, far_peek(TILES + 256UL * T_LADDER + i));
    far_poke(TILES + 256UL * T_EXIT + 255, far_peek(TILES + 256UL * T_LADDER + 255));
    /* the closing hole: brick growing back from the top */
    t = TILES + 256UL * T_HOLE1;                         /* open: empty */
    fill_row(C_BG); for (y = 0; y < 16; y++) tile_row(t, y);
    t = TILES + 256UL * T_HOLE2;                         /* a lip of brick */
    for (y = 0; y < 16; y++) { fill_row(y < 5 ? C_BRK : C_BG); if (y == 4) fill_row(C_BRKD); tile_row(t, y); }
    t = TILES + 256UL * T_HOLE3;                         /* nearly whole */
    for (y = 0; y < 16; y++) { fill_row(y < 11 ? C_BRK : C_BG); if (y == 7) fill_row(C_BRKD); if (y == 10) fill_row(C_BRKD); tile_row(t, y); }
}

static void draw_sprites(void)
{
    uint8_t f, y, x, side;
    for (side = 0; side < 2; side++) {
        uint32_t base = SPR + (uint32_t)side * 7 * 256;
        for (f = 0; f < 7; f++)
            for (y = 0; y < 16; y++) {
                const char *r = shape[f][y];
                uint8_t n = 0;
                for (x = 0; r[x] && x < 16; x++, n++) {
                    uint8_t c = 0;
                    switch (r[x]) {
                    case 'O': c = C_OUT; break;
                    case 'B': c = side ? C_GBODY : C_SUIT; break;
                    case 'S': c = side ? C_GDARK : C_SKIN; break;   /* the guards helmeted */
                    case 'W': c = side ? C_WHT : C_WHT; break;
                    }
                    far_poke(base + (uint32_t)f * 256 + y * 16 + x, c);
                }
                for (; n < 16; n++) far_poke(base + (uint32_t)f * 256 + y * 16 + n, 0);
            }
    }
}

static void make_palette(void)
{
    pal(C_BG,   12,  8, 30);   pal(C_BRK, 178,  62,  36); pal(C_BRKD, 108, 32, 20);
    pal(C_STN, 130,130, 140);  pal(C_STND, 70,  70,  80); pal(C_LAD,   80,220,220);
    pal(C_ROPE,190,150,  80);  pal(C_GLD, 240, 200,  40); pal(C_GLDD, 160,120,  10);
    pal(C_WHT, 240,240, 240);  pal(C_SUIT,220,  50,  50); pal(C_SKIN, 240,190,150);
    pal(C_GBODY,70,110,230);   pal(C_GDARK,30,  50, 140); pal(C_OUT,   16, 12, 16);
    pal(255, 255, 255, 255);                             /* the caption */
}

/* ---- the map ------------------------------------------------------------ */
static void set_tile(uint8_t x, uint8_t y, uint8_t t)    /* what VICKY shows */
{
    uint8_t shown = t;
    if (t == T_EXIT && !unlocked) shown = T_EMPTY;       /* the exit hides */
    if (t == T_GOLD || t == T_LADDER || t == T_ROPE) {}  /* themselves */
    far_poke16(MAPF + (((uint16_t)y * GW + x) << 1), shown);
}
static void put(uint8_t x, uint8_t y, uint8_t t) { grid[y][x] = t; set_tile(x, y, t); }

static void load_level(void)
{
    uint8_t x, y; const char *r;
    goldleft = 0; guards = 0; unlocked = 0; grace = 110;
    for (y = 0; y < NHOLE; y++) holes[y].t = 0;
    for (y = 0; y < GH; y++) {
        r = levels[level][y];
        for (x = 0; x < GW; x++) {
            char c = *r ? *r : ' '; if (*r) r++;
            switch (c) {
            case '#': put(x, y, T_BRICK); break;
            case '@': put(x, y, T_STONE); break;
            case 'H': put(x, y, T_LADDER); break;
            case '-': put(x, y, T_ROPE); break;
            case '$': put(x, y, T_GOLD); goldleft++; break;
            case 'E': put(x, y, T_EXIT); break;
            case 'P': put(x, y, T_EMPTY);
                men[0].sx = x; men[0].sy = y; break;
            case 'G': put(x, y, T_EMPTY);
                if (guards < NGUARD) { men[1 + guards].sx = x; men[1 + guards].sy = y; guards++; }
                break;
            default:  put(x, y, T_EMPTY); break;
            }
        }
    }
    for (x = 0; x < NSPR; x++) {
        actor_t *a = &men[x];
        a->x = (int16_t)a->sx << 4; a->y = (int16_t)a->sy << 4;
        a->dx = a->dy = 0; a->fr = F_RUN1; a->flip = 0; a->trapped = 0;
        a->alive = (x == 0) || (x <= guards);
    }
}

/* ---- rules -------------------------------------------------------------- */
static uint8_t at(int8_t x, int8_t y)
{
    if (x < 0 || x >= GW || y < 0) return T_STONE;
    if (y >= GH) return T_STONE;
    return grid[y][x];
}
static uint8_t solid(uint8_t t)    { return t == T_BRICK || t == T_STONE || t == T_HOLE3; }
static uint8_t is_open(uint8_t t)  { return !solid(t); }      /* can occupy */
static uint8_t guard_in(int8_t x, int8_t y)                   /* a trapped guard is a floor */
{
    uint8_t i;
    for (i = 1; i <= guards; i++)
        if (men[i].alive && men[i].trapped && (men[i].x >> 4) == x && ((men[i].y + 8) >> 4) == y) return 1;
    return 0;
}
static uint8_t climbable(uint8_t t) { return t == T_LADDER || (t == T_EXIT && unlocked); }
static uint8_t supported(actor_t *a)
{
    int8_t cx = (int8_t)(a->x >> 4), cy = (int8_t)(a->y >> 4);
    uint8_t here = at(cx, cy), below = at(cx, (int8_t)(cy + 1));
    if (climbable(here) || here == T_ROPE) return 1;
    if (solid(below) || climbable(below)) return 1;
    if (guard_in(cx, (int8_t)(cy + 1))) return 1;
    return 0;
}

/* can the actor take one whole cell step (dx,dy) from its aligned cell? */
static uint8_t can_step(actor_t *a, int8_t dx, int8_t dy)
{
    int8_t cx = (int8_t)(a->x >> 4), cy = (int8_t)(a->y >> 4);
    int8_t nx = cx + dx, ny = cy + dy;
    uint8_t here = at(cx, cy), there = at(nx, ny);
    if (nx < 0 || nx >= GW || ny < 0 || ny >= GH) return 0;
    if (!is_open(there)) return 0;
    if (dy < 0) return climbable(here) || climbable(there);   /* up: on a ladder, or into one from its foot (level 2's exit stands over air -- Doc, 2026-09-09) */
    if (dy > 0) {                                        /* down: ladder, or step off into air */
        if (guard_in(cx, (int8_t)(cy + 1))) return 0;
        return climbable(there) || climbable(here) || here == T_ROPE || !solid(there);
    }
    return 1;                                            /* sideways into anything open */
}

static void snap(actor_t *a) { a->x &= ~15; a->y &= ~15; }

/* one 2-px tick for an actor; at cell boundaries the standing order is
 * re-examined.  Returns 1 while genuinely moving. */
static uint8_t tick_actor(actor_t *a)
{
    uint8_t moved = 0;
    int8_t cx, cy; uint8_t here;
    if ((a->x & 15) || (a->y & 15)) {                    /* between cells: keep going */
        a->x += a->dx * 2; a->y += a->dy * 2;
        return 1;
    }
    cx = (int8_t)(a->x >> 4); cy = (int8_t)(a->y >> 4);
    here = at(cx, cy);
    if (!supported(a)) {                                 /* nothing under him: fall */
        a->dx = 0; a->dy = 1; a->fr = F_FALL;
        a->y += 2; return 1;
    }
    if (a->dx || a->dy) {
        if (can_step(a, a->dx, a->dy)) {
            if (a->dx) a->flip = a->dx < 0;
            a->x += a->dx * 2; a->y += a->dy * 2; moved = 1;
        } else { a->dx = 0; a->dy = 0; }
    }
    /* the frame: what he is on decides the pose */
    if (here == T_ROPE && !solid(at(cx, (int8_t)(cy + 1)))) a->fr = (frame & 8) && moved ? F_HANG2 : F_HANG1;
    else if (climbable(here) && a->dy) a->fr = (frame & 8) ? F_CLIMB2 : F_CLIMB1;
    else if (climbable(here) && !a->dx) a->fr = F_CLIMB1;
    else a->fr = (frame & 8) && moved ? F_RUN2 : F_RUN1;
    return moved;
}

/* ---- holes -------------------------------------------------------------- */
static void dig(int8_t side)
{
    actor_t *p = &men[0];
    int8_t cx = (int8_t)(p->x >> 4), cy = (int8_t)(p->y >> 4);
    int8_t hx = cx + side, hy = cy + 1; uint8_t i;
    if ((p->x & 15) || (p->y & 15)) return;              /* only from a whole cell */
    if (at(hx, hy) != T_BRICK) return;
    if (solid(at(hx, cy))) return;                       /* no room to swing the drill */
    for (i = 0; i < NHOLE; i++) if (!holes[i].t) break;
    if (i == NHOLE) return;
    holes[i].x = (uint8_t)hx; holes[i].y = (uint8_t)hy; holes[i].t = HOLE_LIFE;
    put((uint8_t)hx, (uint8_t)hy, T_HOLE1);
}
static void kill(actor_t *a);
static void tick_holes(void)
{
    uint8_t i, j;
    for (i = 0; i < NHOLE; i++) {
        hole_t *h = &holes[i];
        if (!h->t) continue;
        h->t--;
        if (h->t == 24) put(h->x, h->y, T_HOLE2);
        else if (h->t == 12) put(h->x, h->y, T_HOLE3);
        else if (h->t == 0) {
            put(h->x, h->y, T_BRICK);
            for (j = 0; j < NSPR; j++) {                 /* closed over someone? */
                actor_t *a = &men[j];
                if (a->alive && (a->x >> 4) == h->x && (a->y >> 4) == h->y) kill(a);
            }
        }
    }
}

/* ---- life and death ----------------------------------------------------- */
static uint8_t noguards; static int8_t dig_req; static uint8_t dig_ttl;
static uint8_t pace = 2, ptick;   /* the player moves on pace frames in every 3 (1..3; - and + change it); ptick counts his moves for the guards */
static void status_line(void);
static void kill(actor_t *a)
{
    if (a == &men[0]) {                                  /* the runner: a life gone */
        if (lives) lives--;
        a->alive = 2;                                    /* 2: restart pending */
        return;
    }
    a->trapped = 0;                                      /* a guard reappears at his post */
    a->x = (int16_t)a->sx << 4; a->y = (int16_t)a->sy << 4;
    a->dx = a->dy = 0;
    score += 75;
}

static void guard_brain(actor_t *g)
{
    actor_t *p = &men[0];
    int8_t cx, cy; int16_t ddx, ddy;
    if ((g->x & 15) || (g->y & 15)) return;              /* decisions on the grid */
    cx = (int8_t)(g->x >> 4); cy = (int8_t)(g->y >> 4);
    if (g->trapped) {                                    /* in a hole: wait, then climb out */
        if (g->trapped > 1) { g->trapped--; return; }
        if (is_open(at(cx, (int8_t)(cy - 1)))) {         /* pull himself up */
            g->trapped = 0; g->y -= 16;
            if (at((int8_t)(cx - 1), (int8_t)(cy - 1)) != T_BRICK && cx > (p->x >> 4)) g->x -= 16;
            else if (at((int8_t)(cx + 1), (int8_t)(cy - 1)) != T_BRICK) g->x += 16;
        }
        return;
    }
    if (at(cx, cy) == T_HOLE1) {                         /* fell in */
        g->trapped = 200; g->dx = g->dy = 0; snap(g); return;
    }
    ddy = p->y - g->y; ddx = p->x - g->x;
    /* prefer closing the vertical gap when a way exists */
    if (ddy < 0 && can_step(g, 0, -1)) { g->dx = 0; g->dy = -1; return; }
    if (ddy > 0 && can_step(g, 0,  1)) { g->dx = 0; g->dy =  1; return; }
    if (ddx < 0 && can_step(g, -1, 0)) { g->dx = -1; g->dy = 0; return; }
    if (ddx > 0 && can_step(g,  1, 0)) { g->dx =  1; g->dy = 0; return; }
    /* boxed in the corner: try anything */
    if (can_step(g, -1, 0)) { g->dx = -1; g->dy = 0; return; }
    if (can_step(g,  1, 0)) { g->dx =  1; g->dy = 0; return; }
    g->dx = g->dy = 0;
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
    text8_print(TEXTMAP, 40, 14, 0, "MEN");
    far_poke(TEXTMAP + 18, (uint8_t)('0' + lives));
    text8_print(TEXTMAP, 40, 22, 0, "LEVEL");
    far_poke(TEXTMAP + 28, (uint8_t)('1' + level));
    text8_print(TEXTMAP, 40, 31, 0, noguards ? "NO GUARDS" : goldleft ? "         " : "GO UP!   ");
    far_poke(TEXTMAP + 38, (uint8_t)'0' + pace);      /* the pace, top right: 1 slow, 2, 3 full */
}
static void centre(uint8_t row, const char *s)
{
    uint8_t n = 0; const char *p = s; while (*p++) n++;
    text8_print(TEXTMAP, 40, (uint8_t)((40 - n) / 2), row, s);
}
static void init_tables(void)
{
    uint8_t i; uint32_t t;
    dma_fill(0, SPRTAB_A, 4096); dma_fill(0, SPRTAB_B, 4096);
    for (t = SPRTAB_A; t <= SPRTAB_B; t += SPRTAB_B - SPRTAB_A)
        for (i = 0; i < NSPR; i++) {
            far_poke(t + (uint32_t)i * 16 + 8, 1 | 2);   /* enable, 8 bpp */
            far_poke(t + (uint32_t)i * 16 + 9, 1 | (1 << 2));   /* 16 x 16 */
        }
}
static void write_table(uint32_t t)
{
    uint8_t i;
    for (i = 0; i < NSPR; i++, t += 16) {
        actor_t *a = &men[i];
        uint32_t d = SPR + (i ? 7UL * 256 : 0) + (uint32_t)a->fr * 256;
        far_poke16(t,     (uint16_t)a->x);
        far_poke16(t + 2, (uint16_t)a->y);
        far_poke16(t + 4, (uint16_t)d); far_poke16(t + 6, (uint16_t)(d >> 16));
        far_poke(t + 8, (uint8_t)((a->alive == 1 ? 1 : 0) | 2 | (a->flip ? 4 : 0)));
    }
}
static void tile_layer(void)
{
    uint16_t L = V_LAYER(0);
    REG(L + 1) = 0; w16(L + 2, 0); w16(L + 4, 0); w16(L + 6, GW);
    w32(L + 8, TILES); w32(L + 12, MAPF);
    REG(L) = 1 | (1 << 1) | (3 << 3) | (1 << 5);         /* enable, tile, 8 bpp, 16 px */
}
static void redraw_map(void)                             /* after unlock: exits appear */
{
    uint8_t x, y;
    for (y = 0; y < GH; y++) for (x = 0; x < GW; x++) set_tile(x, y, grid[y][x]);
}
static void write_table(uint32_t t);
static uint8_t pause_msg(const char *s1, const char *s2)   /* 1 = Esc */
{
    uint8_t k;
    write_table(SPRTAB_A); write_table(SPRTAB_B);        /* the truth, both buffers */
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

/* ---- main --------------------------------------------------------------- */
void main(void)
{
    uint8_t i, k, running = 1;
    REG(V_CTRL) = 0;
    make_palette();
    draw_tiles(); draw_sprites();
    lives = 5; score = 0; level = 0;
    load_level();
    dma_fill(' ', TEXTMAP, 40 * 30);
    status_line();
    text8_layer(1, TEXTMAP, 40, 127);
    tile_layer();
    init_tables(); write_table(SPRTAB_A); w32(V_SPRTAB, SPRTAB_A); REG(V_SPRCTL) = 1;
    REG(V_CTRL) = 1 | 2 | 4;                             /* 320 x 240 */
    if (pause_msg("LODE -- ARROWS RUN, Z/X DIG", "G GUARDS OFF, - + SLOWER FASTER")) running = 0;

    while (running) {
        uint32_t back = cur ? SPRTAB_A : SPRTAB_B;
        actor_t *p = &men[0];
        int8_t pcx, pcy; uint8_t moving;
        /* Steering is the keys HELD ($D104), not the keys pressed: the man
         * runs while an arrow is down and stops when it is let go, the way
         * the original played.  Until 2026-09-05 this read the key queue,
         * which only knows presses -- so the man ran until you said SPACE,
         * and the keyboard's autorepeat kept re-sending the arrow anyway.
         * Digging and leaving stay events: one press, one hole. */
        /* ...but a new order is taken ON A CELL, where can_step() judges it.
         * Until 2026-09-09 the held keys overwrote dx/dy every frame, so
         * halfway through a cell any arrow steered him anywhere -- through
         * brick, up with no ladder -- and letting go left him standing in
         * mid-air between two cells, since the fall check only runs on a
         * cell (Doc, the laptop: "the player is hanging in the middle of
         * nothing").  Mid-cell the one thing allowed is turning back. */
        { uint8_t h = keys_held(); int8_t wx = 0, wy = 0;
          if      (h & HELD_UP)    wy = -1;
          else if (h & HELD_DOWN)  wy = 1;
          else if (h & HELD_LEFT)  wx = -1;
          else if (h & HELD_RIGHT) wx = 1;
          if (!(p->x & 15) && !(p->y & 15)) { p->dx = wx; p->dy = wy; }
          else if ((wx && wx == -p->dx) || (wy && wy == -p->dy)) { p->dx = wx; p->dy = wy; } }
        k = key_get();
        switch (k) {
        case 0x1B: running = 0; break;
        /* Z and X are remembered until he stands on a cell: pressed while
         * running they were simply dropped (dig() refuses mid-cell), which
         * read as "Z and X do not dig" (Doc, 2026-09-09).  Ten frames is
         * long enough to reach the next cell and short enough to forget. */
        case 'z': case 'Z': dig_req = -1; dig_ttl = 10; break;
        case 'x': case 'X': dig_req = 1;  dig_ttl = 10; break;
        /* G: the guards off, for learning a level (or testing it).  They stand
         * where they are and cannot catch; the status line says so. */
        case 'g': case 'G': noguards = !noguards; status_line(); break;
        /* - and +: slower and faster.  Every frame was "running too fast"
         * (Doc); the default is two moves in three frames, - drops to one
         * in three, + goes back to every frame. */
        case '-': case '_': if (pace > 1) pace--; status_line(); break;
        case '+': case '=': if (pace < 3) pace++; status_line(); break;
        }
        { static const uint8_t on[4][3] = { {0,0,0}, {1,0,0}, {1,0,1}, {1,1,1} };
          moving = on[pace][frame % 3]; }
        if (moving) { tick_actor(p); ptick++; }
        if (dig_req && !(p->x & 15) && !(p->y & 15)) { dig(dig_req); dig_req = 0; }
        else if (dig_req && !--dig_ttl) dig_req = 0;
        pcx = (int8_t)((p->x + 8) >> 4); pcy = (int8_t)((p->y + 8) >> 4);
        if (at(pcx, pcy) == T_GOLD) {
            put((uint8_t)pcx, (uint8_t)pcy, T_EMPTY);
            score += 100;
            if (--goldleft == 0) { unlocked = 1; redraw_map(); }
            status_line();
        }
        if (unlocked && (p->y >> 4) == 0 && !(p->y & 15)) {   /* the top wins */
            level++;
            if (level == 3) {
                centre(13, "YOU CLEARED ALL THREE LEVELS");
                pause_msg("A TRUE LODE RUNNER", "ANY KEY LEAVES");
                running = 0; continue;
            }
            score += 500; load_level(); redraw_map(); status_line();
            if (pause_msg("NEXT LEVEL", "ANY KEY STARTS")) running = 0;
            continue;
        }
        if (grace) grace--;                          /* a fresh level: a breath before the chase */
        else if (!noguards) for (i = 1; i <= guards; i++) {
            actor_t *g = &men[i];
            if (!g->alive) continue;
            guard_brain(g);
            if (!g->trapped && moving && (ptick & 1)) tick_actor(g);   /* guards at half the player's pace, whatever it is */
            if (g->trapped) g->fr = F_CLIMB1;
            if (!g->trapped && g->alive == 1 && p->alive == 1) {
                int16_t ax = g->x - p->x, ay = g->y - p->y;
                if (ax < 0) ax = -ax; if (ay < 0) ay = -ay;
                if (ax < 11 && ay < 11) kill(p);
            }
        }
        tick_holes();
        if (p->alive == 2) {                             /* caught, or bricked over */
            status_line();
            if (!lives) {
                centre(13, "GAME OVER");
                pause_msg("THE GOLD KEEPS ITS SECRET", "ANY KEY LEAVES");
                running = 0; continue;
            }
            load_level(); redraw_map(); status_line();
            if (pause_msg("OUCH -- AGAIN!", "ANY KEY")) running = 0;
            continue;
        }
        write_table(back);
        wait_vblank();
        w32(V_SPRTAB, back); cur ^= 1; frame++;
    }
    REG(TERM + 4) = 2;                                   /* leave a clean text screen */
}
