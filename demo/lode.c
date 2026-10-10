/* K4510: LODE -- a Lode Runner, in the Personality Chooser's look.
 *
 * The rules are the 1983 ones: run, climb, hang from the rope, fall off
 * edges, dig through brick left and right, collect every gold bag; the
 * exit ladder appears and the top of the screen wins the level.  Guards
 * chase, fall into dug holes, climb out again; a hole that closes over
 * anyone is the end of them.  Stone does not dig.  The levels are files,
 * /APPS/LODE/LEVELnn.TXT, and E on the title opens the level editor.
 *
 *   arrows  run / climb while held     Z / X   dig left / right
 *   G       guards on / off            - +     slower / faster
 *   P       pause                      Esc     back to the title
 *
 * The screen, since 2026-10-09, is CHESS's, TETRIS's and SKYFIRE's -- the
 * Personality Chooser's: 320x240 doubled, grey bands with K4510 LODE and the
 * time and the keys, the blue glass, the banner's bars, unscii drawn through
 * the blitter into an 8 bpp bitmap on layer 1 (layer 0, the console's, off
 * and left as it was), in the machine's sixteen colours.  The title, the P
 * pause and the end of a game are the Chooser's list.
 *
 * The grid is still 20x15 and the rules still count in 16 px cells, but a
 * 320x240 field leaves no room for the bands, and row 0 (the exit's top,
 * where the level is won) and row 14 (a floor that may be brick, and dug)
 * are both played on -- the bands may not cover either.  So the field is
 * drawn at three quarters: 12 px cells, 240x180, framed at the left under a
 * strip with the bars and "Lode", the score panel to its right; every cell
 * of every level in view.  The cells are bevelled tiles built at the start
 * and blitted, only the ones that changed; the runner and the guards are
 * 16x16 8 bpp sprites at Z 1 (over the bitmap) with 12x12 figures in them,
 * placed at x*3/4, y*3/4 of where the rules have them.  Their outline is
 * palette 253 set to black: colour 0 is transparent.
 */
#include "k4510.h"

#define TERM 0xDA00u
static void rom_video(void) { ((void (*)(void))0xFF92)(); }
static unsigned char rom_save(void) { return ((unsigned char (*)(void))0xFF8C)(); }   /* $F0 name, $F2 addr, $F6 length */
static void zp16(uint8_t a, uint16_t v) { REG(a) = (uint8_t)v; REG(a + 1) = (uint8_t)(v >> 8); }
static void zp32(uint8_t a, uint32_t v) { REG(a) = (uint8_t)v; REG(a + 1) = (uint8_t)(v >> 8); REG(a + 2) = (uint8_t)(v >> 16); REG(a + 3) = (uint8_t)(v >> 24); }
static void far_w32(uint16_t a, uint32_t v) { w32(a, v); }

enum { BLACK, WHITE, RED, CYAN, PURPLE, GREEN, BLUE, YELLOW, ORANGE, BROWN, LRED, DGREY, GREY, LGREEN, LBLUE, LGREY };
#define INK 253                                  /* black that is not colour 0: the figures' outline */
#define K_UP    0x80
#define K_DOWN  0x81
#define K_LEFT  0x82
#define K_RIGHT 0x83
#define K_ESC   0x1B

/* far memory: what the chips read */
#define SPR      0x00112000UL            /* sprite frames, hero then guard, 16x16 8 bpp */
#define SPRTAB_A 0x00116000UL
#define SPRTAB_B 0x00117000UL
#define BMP      0x00200000UL            /* 320x240, 8 bpp, layer 1 */

#define GW 20
#define GH 15
#define NGUARD 3
#define NSPR   (1 + NGUARD)

/* grid cells */
enum { T_EMPTY, T_BRICK, T_STONE, T_LADDER, T_ROPE, T_GOLD, T_EXIT,
       T_HOLE1, T_HOLE2, T_HOLE3 };      /* HOLE1 open, 2/3 closing */
#define NTILE 10

/* sprite frames (per side: hero at SPR, guard at SPR + 7*256) */
enum { F_RUN1, F_RUN2, F_CLIMB1, F_CLIMB2, F_HANG1, F_HANG2, F_FALL };

/* ---- the levels: files.  /APPS/LODE/LEVELnn.TXT, nn = 01 up, 15 lines of
 * up to 20 characters:  # brick  @ stone  H ladder  - rope  $ gold
 * E hidden exit ladder  P player  G guard  (anything else, and a short row,
 * is empty).  As many as are there, up to LEVEL_MAX; the editor (E on the
 * title card) writes them.  Until 2026-09-09 three were built in. */
#define LEVEL_MAX 20
static char    ed[GH][GW];                        /* the level as text: what the file holds, what the editor paints */
static uint8_t nlevels;
static char    lvname[] = "/APPS/LODE/LEVEL00.TXT";
static void lv_name(uint8_t n) { lvname[16] = (char)('0' + (n + 1) / 10); lvname[17] = (char)('0' + (n + 1) % 10); }
static uint8_t lv_exists(uint8_t n) { lv_name(n); far_w32(0xD304, (uint16_t)lvname); REG(0xD300) = 8; return REG(0xD301) == 0; }   /* 8 = STAT (11 is CHDIR, which bit once) */
static uint8_t lv_read(uint8_t n)                 /* the file into ed[][]; 1 if it was there */
{
    static char buf[GH * (GW + 2) + 2]; uint16_t len, i = 0; uint8_t x, y;
    lv_name(n);
    far_w32(0xD304, (uint16_t)lvname); far_w32(0xD308, (uint16_t)buf); far_w32(0xD30C, sizeof buf - 1);
    REG(0xD300) = 9;
    if (REG(0xD301)) return 0;
    len = (uint16_t)REG(0xD30C) | ((uint16_t)REG(0xD30D) << 8);
    for (y = 0; y < GH; y++) {
        for (x = 0; x < GW; x++) ed[y][x] = ' ';
        for (x = 0; i < len && buf[i] != '\n'; i++) { if (buf[i] != '\r' && x < GW) ed[y][x++] = buf[i]; }
        if (i < len) i++;                                                     /* the newline */
    }
    return 1;
}
static uint8_t lv_write(uint8_t n)                /* ed[][] to the file, rows trimmed; 1 if written */
{
    static char buf[GH * (GW + 1) + 1]; uint16_t i = 0; uint8_t x, y, w;
    for (y = 0; y < GH; y++) {
        for (w = GW; w && ed[y][w - 1] == ' '; w--) ;
        for (x = 0; x < w; x++) buf[i++] = ed[y][x];
        buf[i++] = '\n';
    }
    lv_name(n);
    zp16(0xF0, (uint16_t)lvname); zp32(0xF2, (uint32_t)(uint16_t)buf); zp32(0xF6, i);
    return rom_save() == 0;
}
static void lv_count(void) { nlevels = 0; while (nlevels < LEVEL_MAX && lv_exists(nlevels)) nlevels++; }

/* ---- the sprite frames, 12x12 ASCII: . none  O outline  B suit
 *      S skin  W boots/hands  (recoloured per side at build time).  The
 *      16x16 figures of before, a row of the head, of the legs and the
 *      blank edges taken out, for the three-quarter field. */
static const char *shape[7][12] = {
 { /* RUN1: right leg forward */
   "....OOOO....", "...OSSSSO...", "...OSSSSO...", "....OSSO....",
   "...OBBBBO...", "..OBOBBOBO..", ".OWO.BB.OWO.", "....OBBO....",
   "...OB..BO...", "..OB....BO..", "..OB.....BO.", ".OWWO...OWWO" },
 { /* RUN2: legs passing */
   "....OOOO....", "...OSSSSO...", "...OSSSSO...", "....OSSO....",
   "...OBBBBO...", "...OBBBBO...", "..OWOBBOWO..", "....OBBO....",
   "....OBBO....", "....OBBO....", "....OBBO....", "...OWWWWO..." },
 { /* CLIMB1: back view, left arm up */
   "..OW........", "..OBOOOO....", "..OBSSSSO...", "....OSSO....",
   "...OBBBBO...", "...OBBBBOW..", "....OBBOBO..", ".....BB.....",
   "....OBBO....", "...OB.BO....", "...OB..BO...", "..OWWO.OWWO." },
 { /* CLIMB2: right arm up */
   "........WO..", "....OOOOBO..", "...OSSSSBO..", "....OSSO....",
   "...OBBBBO...", "..WOBBBBO...", "..OBOBBO....", ".....BB.....",
   "....OBBO....", ".....OB.BO..", "....OB..BO..", "...OWWO.OWWO" },
 { /* HANG1: both hands on the rope, legs left */
   ".OWO....OWO.", ".OBO....OBO.", ".OBOOOOOOBO.", "..OBSSSSBO..",
   "....OSSO....", "...OBBBBO...", "...OBBBBO...", "....OBBO....",
   "...OBBBO....", "..OB.OBO....", ".OB...BO....", "OWWO.OWO...." },
 { /* HANG2: legs right */
   ".OWO....OWO.", ".OBO....OBO.", ".OBOOOOOOBO.", "..OBSSSSBO..",
   "....OSSO....", "...OBBBBO...", "...OBBBBO...", "....OBBO....",
   "....OBBBO...", "....OBO.BO..", "....OB...BO.", "....OWO.OWWO" },
 { /* FALL: arms out, legs spread */
   "....OOOO....", "...OSSSSO...", "...OSSSSO...", "....OSSO....",
   ".O..OBBO..O.", "OWOOBBBBOOWO", ".OBBBBBBBBO.", "...OB..BO...",
   "..OB....BO..", ".OB......BO.", "OB........BO", "OWO......OWO" },
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

/* ---- the screen: the Chooser's look ------------------------------------ */
#define SW 320
#define BAND 12                                       /* a band: 8-pixel text with 2 above and below */
#define GY BAND                                       /* the glass: lines 12..227 */
#define BBOT (240 - BAND)
#define CELL 12                                       /* a grid cell on the screen: three quarters of the rules' 16 */
#define FX 6                                          /* the field: 240x180 */
#define FY 40
#define FW (GW * CELL)
#define FH (GH * CELL)
#define PXP 252                                       /* the panel, right of the field */
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
static char nb[12];
static const char *fmt(uint32_t v, uint8_t w)         /* v right-aligned in w places */
{
    uint8_t i = 10;
    nb[10] = 0;
    do { nb[--i] = (char)('0' + (uint8_t)(v % 10)); v /= 10; } while (v && i);
    while (i > 10 - w) nb[--i] = ' ';
    return nb + i;
}
static const char *fmt0(uint32_t v, uint8_t w)        /* v in w places, zeros in front */
{
    uint8_t i = 10;
    nb[10] = 0;
    while (i > 10 - w) { nb[--i] = (char)('0' + (uint8_t)(v % 10)); v /= 10; }
    return nb + i;
}
static void band_text(uint8_t y, const char *s)        /* centred in a band, black on grey */
{
    uint8_t n = (uint8_t) strlen(s);
    rect(0, y, SW, BAND, LGREY);
    text((uint16_t)((SW - n * 8) / 2), (uint8_t)(y + 2), s, BLACK, LGREY, F8);
}
static uint8_t clock_shown = 0xFF, rtc_read, frame_seen;
static void rtc_latch(void) { rtc_read = REG(SYS + 4); }   /* a store, not a (void) read: cc65 drops that */
static void put2(char *b, uint8_t v) { b[0] = (char)('0' + v / 10); b[1] = (char)('0' + v % 10); }
static void draw_top(void)                            /* K4510 LODE on the left, the time on the right, as the Chooser's band */
{
    char b[17]; uint16_t y;
    rtc_latch();
    clock_shown = REG(SYS + 6);
    put2(b, REG(SYS + 7)); b[2] = ':'; put2(b + 3, REG(SYS + 6)); b[5] = ' ';
    put2(b + 6, REG(SYS + 8)); b[8] = '.'; put2(b + 9, REG(SYS + 9)); b[11] = '.';
    y = REG(SYS + 0x0A) | (REG(SYS + 0x0B) << 8);
    b[12] = (char)('0' + (y / 1000) % 10); b[13] = (char)('0' + (y / 100) % 10); b[14] = (char)('0' + (y / 10) % 10); b[15] = (char)('0' + y % 10); b[16] = 0;
    rect(0, 0, SW, BAND, LGREY);
    text(8, 2, "K4510 LODE", BLACK, LGREY, F8);
    text(SW - 8 - 16 * 8, 2, b, BLACK, LGREY, F8);
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
static void header(const char *sub, uint8_t subcol)   /* a full-glass screen: the bars, Lode, a line under it */
{
    glass();
    bars(12, GY + 8, 5, 6);
    text(72, GY + 8, "Lode", WHITE, BLUE, F8X2);
    text(72, GY + 28, sub, subcol, BLUE, F8);
}
static void strip(const char *sub)                    /* the play screen's top: small bars, Lode, a line; the field's frame */
{
    glass();
    bars(FX, GY + 5, 3, 3);
    text(FX + 32, GY + 4, "Lode", WHITE, BLUE, F8X2);
    text(FX + 104, GY + 9, sub, YELLOW, BLUE, F8);
    rect(FX - 2, FY - 2, FW + 4, FH + 4, LBLUE);
}

/* ---- the tiles: bevelled, a light edge top and left, a dark one bottom and
 * right, in the VIC colours; built near at the start and blitted ----------- */
static uint8_t tiles[NTILE][CELL * CELL];
static uint8_t shown[GH][GW];                         /* the tile each cell shows: only a change is drawn */
static void tile_fill(uint8_t *t, uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t c)
{
    uint8_t i, j;
    for (j = y; j < y + h; j++) for (i = x; i < x + w; i++) t[j * CELL + i] = c;
}
static void bevel(uint8_t *t, uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t l, uint8_t c, uint8_t d)
{
    tile_fill(t, x, y, w, h, d);
    tile_fill(t, x, y, w - 1, h - 1, l);
    tile_fill(t, x + 1, y + 1, w - 2, h - 2, c);
}
static void bricks(uint8_t *t, uint8_t rows)          /* two courses of bricks, the second offset; the top `rows` of them */
{
    uint8_t x, y, yy, bx;
    for (y = 0; y < rows; y++)
        for (x = 0; x < CELL; x++) {
            yy = y % 6; bx = (uint8_t)((x + (y < 6 ? 0 : 3)) % 6);
            t[y * CELL + x] = (yy == 5 || bx == 5 || y == rows - 1) ? BROWN : (yy == 0 || bx == 0) ? LRED : RED;
        }
}
static void make_tiles(void)
{
    uint8_t *t, i;
    memset(tiles, BLACK, sizeof tiles);                         /* EMPTY and the open HOLE1: the night */
    bricks(tiles[T_BRICK], CELL);
    bevel(tiles[T_STONE], 0, 0, CELL, CELL, LGREY, GREY, DGREY);
    t = tiles[T_LADDER];                                        /* two rails, a rung every four */
    for (i = 0; i < CELL; i++) { t[i * CELL + 2] = WHITE; t[i * CELL + 3] = LBLUE; t[i * CELL + 8] = WHITE; t[i * CELL + 9] = LBLUE; }
    for (i = 1; i < CELL; i += 4) tile_fill(t, 4, i, 4, 1, LBLUE);
    memcpy(tiles[T_EXIT], t, CELL * CELL);                      /* the exit ladder looks exactly like a ladder */
    t = tiles[T_ROPE];                                          /* a bar along the top */
    tile_fill(t, 0, 1, CELL, 1, YELLOW); tile_fill(t, 0, 2, CELL, 1, ORANGE);
    t = tiles[T_GOLD];                                          /* a little chest */
    bevel(t, 1, 3, 10, 8, WHITE, YELLOW, ORANGE);
    tile_fill(t, 2, 6, 8, 1, ORANGE);
    tile_fill(t, 5, 5, 2, 3, WHITE);
    bricks(tiles[T_HOLE2], 4);                                  /* the hole closing: brick growing back from the top */
    bricks(tiles[T_HOLE3], 8);
}

static void make_sprites(void)
{
    uint8_t f, y, x, side, c;
    static uint8_t row[16];
    for (side = 0; side < 2; side++) {
        uint32_t base = SPR + (uint32_t)side * 7 * 256;
        for (f = 0; f < 7; f++)
            for (y = 0; y < 16; y++) {
                memset(row, 0, 16);
                if (y >= 2 && y < 14)
                    for (x = 0; x < 12; x++) {
                        switch (shape[f][y - 2][x]) {
                        case 'O': c = INK; break;
                        case 'B': c = side ? LRED : WHITE; break;
                        case 'S': c = side ? RED : ORANGE; break;      /* the guards helmeted */
                        case 'W': c = side ? WHITE : LBLUE; break;
                        default:  c = 0; break;
                        }
                        row[x + 2] = c;
                    }
                for (x = 0; x < 16; x++) far_poke(base + (uint32_t)f * 256 + y * 16 + x, row[x]);
            }
    }
}

/* ---- the map ------------------------------------------------------------ */
static void set_tile(uint8_t x, uint8_t y, uint8_t t)    /* what the screen shows */
{
    uint8_t k = t;
    if (t == T_EXIT && !unlocked) k = T_EMPTY;           /* the exit hides */
    if (shown[y][x] == k) return;
    shown[y][x] = k;
    blit_from(tiles[k], FX + x * CELL, (uint8_t)(FY + y * CELL), CELL, CELL);
}
static void put(uint8_t x, uint8_t y, uint8_t t) { grid[y][x] = t; set_tile(x, y, t); }

static void load_level(void)                      /* ed[][] (already read) into the grid and the men */
{
    uint8_t x, y;
    goldleft = 0; guards = 0; unlocked = 0; grace = 110;
    for (y = 0; y < NHOLE; y++) holes[y].t = 0;
    men[0].sx = 0; men[0].sy = GH - 2;
    for (y = 0; y < GH; y++) {
        for (x = 0; x < GW; x++) {
            char c = ed[y][x];
            switch (c) {
            case '#': put(x, y, T_BRICK); break;
            case '@': put(x, y, T_STONE); break;
            case 'H': case 'h': put(x, y, T_LADDER); break;
            case '-': put(x, y, T_ROPE); break;
            case '$': put(x, y, T_GOLD); goldleft++; break;
            case 'E': case 'e': put(x, y, T_EXIT); break;
            case 'P': case 'p': put(x, y, T_EMPTY);
                men[0].sx = x; men[0].sy = y; break;
            case 'G': case 'g': put(x, y, T_EMPTY);
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
/* the panel: what the status line said, a label and a value each */
#define PY(i) (FY + (i) * 30)
static void value(uint8_t i, const char *s, uint8_t col)    /* five places, the old value wiped */
{
    uint16_t x = text(PXP, (uint8_t)(PY(i) + 10), s, col, BLUE, F16);
    if (x < PXP + 5 * 8) rect(x, (uint8_t)(PY(i) + 10), PXP + 5 * 8 - x, 16, BLUE);
}
static void status_line(void)
{
    char b[6];
    value(0, fmt0(score, 5), YELLOW);
    b[0] = (char)('0' + lives); b[1] = 0; value(1, b, YELLOW);
    put2(b, (uint8_t)(level + 1)); b[2] = '/'; put2(b + 3, nlevels); b[5] = 0; value(2, b, YELLOW);
    if (goldleft) value(3, fmt(goldleft, 1), YELLOW); else value(3, "GO UP", LRED);
    b[0] = (char)('0' + pace); b[1] = 0; value(4, b, YELLOW);      /* the pace: 1 slow, 2, 3 full */
    value(5, noguards ? "Off" : "On", noguards ? LRED : YELLOW);
}
static void panel_labels(void)
{
    static const char *const lab[6] = { "SCORE", "MEN", "LEVEL", "GOLD", "PACE", "GUARDS" };
    uint8_t i;
    for (i = 0; i < 6; i++) text(PXP, (uint8_t)PY(i), lab[i], LBLUE, BLUE, F8);
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
/* the men where the rules have them, at three quarters: the 12x12 figure
 * sits at (2,2) in its 16x16 frame, so the frame goes 2 up and left (and an
 * H-flip mirrors it in place) */
static void write_table(uint32_t t)
{
    uint8_t i;
    for (i = 0; i < NSPR; i++, t += 16) {
        actor_t *a = &men[i];
        uint32_t d = SPR + (i ? 7UL * 256 : 0) + (uint32_t)a->fr * 256;
        far_poke16(t,     (uint16_t)(FX - 2 + ((a->x * 3) >> 2)));
        far_poke16(t + 2, (uint16_t)(FY - 2 + ((a->y * 3) >> 2)));
        far_poke16(t + 4, (uint16_t)d); far_poke16(t + 6, (uint16_t)(d >> 16));
        far_poke(t + 8, (uint8_t)((a->alive == 1 ? 1 : 0) | 2 | (a->flip ? 4 : 0) | (1 << 4)));   /* Z 1: over the bitmap */
    }
}
static void redraw_map(void)                             /* after unlock: exits appear */
{
    uint8_t x, y;
    for (y = 0; y < GH; y++) for (x = 0; x < GW; x++) set_tile(x, y, grid[y][x]);
}
static void field_again(void) { memset(shown, 0xFF, sizeof shown); redraw_map(); }   /* every cell, after something covered them */
#define PLAY_KEYS "Arrows run, Z X dig, G guards, P pause"
static void repaint(void)                                /* the play screen, whole: after a menu has had the glass */
{
    strip("a Lode Runner");
    panel_labels(); status_line();
    band_text(BBOT, PLAY_KEYS);
    field_again();
    write_table(SPRTAB_A); write_table(SPRTAB_B);
}
static uint8_t wait_key(void)
{
    uint8_t k;
    while ((k = key_get()) == 0) { tick_top(); wait_vblank(); }
    return k;
}
/* a bar across the field, the Chooser's highlight: a word, a line under it,
 * a key; the cells under it drawn again after.  The key pressed (0x1B Esc). */
#define BAN_Y (FY + 66)
static uint8_t banner(const char *s1, const char *s2)
{
    uint8_t k;
    write_table(SPRTAB_A); write_table(SPRTAB_B);        /* the truth, both buffers */
    rect(FX, BAN_Y, FW, 40, LBLUE);
    text((uint16_t)(FX + (FW - strlen(s1) * 8) / 2), BAN_Y + 4, s1, BLUE, LBLUE, F16);
    if (s2) text((uint16_t)(FX + (FW - strlen(s2) * 8) / 2), BAN_Y + 26, s2, BLUE, LBLUE, F8);
    k = wait_key();
    field_again();
    return k;
}

/* ---- the editor ---------------------------------------------------------- */
/* E on the title.  The level is painted as its file's own letters: the
 * cursor (a yellow frame round a cell) moves with the arrows and the key
 * under it paints -- # @ H - $ E P G, space clears.  PgUp/PgDn walk the
 * levels, N adds one after the last, S saves, T plays it from here, Esc
 * leaves.  What the game needs is checked on S: one P, an E, some gold.
 * The keys are in the panel, what happened in the bottom band. */
static uint8_t play(void);
static void ed_status(const char *msg)
{
    char b[3];
    put2(b, (uint8_t)(level + 1)); b[2] = 0;
    text(PXP + 48, FY + 12, b, YELLOW, BLUE, F8);
    band_text(BBOT, msg);
}
static void ed_panel(void)
{
    static const char *const keys[14] = { "# brick", "@ stone", "H ladder", "- rope", "$ gold", "E exit", "P runner",
                                          "G guard", "X clear", "S save", "T try", "N new", "PgUp/Dn", "ESC out" };
    uint8_t i;
    strip("the level editor");
    text(PXP, FY, "EDITOR", WHITE, BLUE, F8);
    text(PXP, FY + 12, "LEVEL", LBLUE, BLUE, F8);
    for (i = 0; i < 14; i++) text(PXP, (uint8_t)(FY + 30 + i * 10), keys[i], LBLUE, BLUE, F8);
}
static void ed_cursor(uint8_t cx, uint8_t cy, uint8_t on)
{
    uint16_t x = FX + cx * CELL; uint8_t y = (uint8_t)(FY + cy * CELL);
    if (on) {
        rect(x, y, CELL, 1, YELLOW); rect(x, (uint8_t)(y + CELL - 1), CELL, 1, YELLOW);
        rect(x, y, 1, CELL, YELLOW); rect(x + CELL - 1, y, 1, CELL, YELLOW);
    } else { shown[cy][cx] = 0xFF; set_tile(cx, cy, grid[cy][cx]); }
}
static void ed_show(void)                         /* the level as painted, on the screen */
{
    load_level(); unlocked = 1; redraw_map();             /* the editor shows the exit ladder that play hides */
    write_table(SPRTAB_A); write_table(SPRTAB_B); w32(V_SPRTAB, SPRTAB_A);
}
static uint8_t ed_check(void)                     /* 0 ok, else what is missing */
{
    uint8_t x, y, np = 0, ne = 0, ng = 0;
    for (y = 0; y < GH; y++) for (x = 0; x < GW; x++) {
        char c = ed[y][x];
        if (c == 'P' || c == 'p') np++; else if (c == 'E' || c == 'e') ne++; else if (c == '$') ng++;
    }
    if (np != 1) return 1;
    if (!ne) return 2;
    if (!ng) return 3;
    return 0;
}
#define ED_KEYS "Arrows move, the keys paint, ESC leaves"
static void editor(void)
{
    uint8_t cx = 0, cy = GH - 2, k, dirty = 0, x, y, keep_lives = lives;
    uint32_t keep_score = score;
    ed_panel(); memset(shown, 0xFF, sizeof shown);
    ed_show(); ed_status(ED_KEYS);
    REG(V_SPRCTL) = 1;
    ed_cursor(cx, cy, 1);
    for (;;) {
        wait_vblank(); tick_top();
        k = key_get();
        if (!k) continue;
        if (k >= 0x80 && (REG(KBDST) & 0x40)) {           /* a key code */
            ed_cursor(cx, cy, 0);
            switch (k) {
            case 0x80: if (cy) cy--; break;
            case 0x81: if (cy < GH - 1) cy++; break;
            case 0x82: if (cx) cx--; break;
            case 0x83: if (cx < GW - 1) cx++; break;
            case 0x86: case 0x87:                          /* PgUp / PgDn: another level */
                if (dirty) { ed_status("S to save first (or ESC)"); break; }
                if (k == 0x86 && level) level--; else if (k == 0x87 && level + 1 < nlevels) level++;
                lv_read(level); ed_show(); ed_status(ED_KEYS);
                break;
            case 0x89: ed[cy][cx] = ' '; dirty = 1; ed_show(); break;   /* Delete */
            default: break;
            }
            ed_cursor(cx, cy, 1);
            continue;
        }
        if (k == 0x1B) {
            if (dirty) { if (banner("Unsaved -- leave anyway?", "ESC LEAVES, ANY KEY STAYS") != 0x1B) { unlocked = 1; redraw_map(); ed_cursor(cx, cy, 1); continue; } }
            break;
        }
        switch (k) {
        case '#': case '@': case '-': case '$': case ' ':
            ed[cy][cx] = (char)k; dirty = 1; ed_show(); break;
        case 'h': case 'H': ed[cy][cx] = 'H'; dirty = 1; ed_show(); break;
        case 'e': case 'E': ed[cy][cx] = 'E'; dirty = 1; ed_show(); break;
        case 'x': case 'X': ed[cy][cx] = ' '; dirty = 1; ed_show(); break;
        case 'p': case 'P':                                /* one runner: the old P goes */
            for (y = 0; y < GH; y++) for (x = 0; x < GW; x++) if (ed[y][x] == 'P' || ed[y][x] == 'p') ed[y][x] = ' ';
            ed[cy][cx] = 'P'; dirty = 1; ed_show(); break;
        case 'g': case 'G': {                              /* up to NGUARD */
            uint8_t n = 0;
            for (y = 0; y < GH; y++) for (x = 0; x < GW; x++) if (ed[y][x] == 'G' || ed[y][x] == 'g') n++;
            if (ed[cy][cx] == 'G' || n < NGUARD) { ed[cy][cx] = 'G'; dirty = 1; ed_show(); }
            else ed_status("Three guards is the most");
            break; }
        case 'n': case 'N':                                /* a new level after the last */
            if (dirty) { ed_status("S to save first"); break; }
            if (nlevels >= LEVEL_MAX) { ed_status("Twenty levels is the most"); break; }
            ed_cursor(cx, cy, 0);
            for (y = 0; y < GH; y++) for (x = 0; x < GW; x++) ed[y][x] = y == GH - 1 ? '@' : ' ';
            ed[GH - 2][0] = 'P';
            level = nlevels++; if (lv_write(level)) { ed_show(); ed_status("New level -- paint it, S saves"); }
            else { nlevels--; ed_status("Could not write /APPS/LODE"); }
            cx = 0; cy = GH - 2;
            break;
        case 's': case 'S': {
            uint8_t why = ed_check();
            if (why == 1) ed_status("Needs exactly one P");
            else if (why == 2) ed_status("Needs an E (the exit ladder)");
            else if (why == 3) ed_status("Needs some gold ($)");
            else if (lv_write(level)) { dirty = 0; ed_status("Saved"); }
            else ed_status("Could not write the file");
            break; }
        case 't': case 'T':                                /* play it from here, then back */
            if (ed_check()) { ed_status("Fix it first (S says what)"); break; }
            lives = 5;
            load_level(); repaint();
            { uint8_t r = play();
              if (r == 1) banner("Cleared", "ANY KEY: BACK TO THE EDITOR");
              else if (r == 2) banner("Game over", "ANY KEY: BACK TO THE EDITOR"); }
            ed_panel(); memset(shown, 0xFF, sizeof shown);
            ed_show(); ed_status("Back in the editor");
            break;
        default: break;
        }
        ed_cursor(cx, cy, 1);
    }
    lives = keep_lives; score = keep_score;
    load_level();
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
/* a list of n rows at y0: 1-n, the arrows and RETURN; 255 for ESC.  `extra`
 * gets the keys the list does not know (SPACE, E, G, < >, P), and its
 * answer, if not 254, is the list's. */
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

/* the title: 0 play, 1 the editor, 2 quit */
static char nlv[] = "00 levels";
static const char *const T_ITEMS[3] = { "Play", "Level editor", "Quit" };
static const char *t_right[3] = { nlv, "E", "ESC" };
static uint8_t title_key(uint8_t k, uint8_t cur)
{
    (void) cur;
    if (k == ' ') return 0;
    if (k == 'e' || k == 'E') return 1;
    return 254;
}
/* the title's men: the runner with the three guards after him, top right,
 * as SKYFIRE puts its planes there (Z 1, over the glass) */
static void title_men(void)
{
    static const uint16_t tx[4] = { 284, 200, 224, 248 };
    uint8_t i; uint32_t t = SPRTAB_A, d;
    for (i = 0; i < NSPR; i++, t += 16) {
        d = SPR + (i ? 7UL * 256 : 0) + (uint32_t)(i & 1 ? F_RUN2 : F_RUN1) * 256;
        far_poke16(t, tx[i]); far_poke16(t + 2, GY + 16);
        far_poke16(t + 4, (uint16_t)d); far_poke16(t + 6, (uint16_t)(d >> 16));
        far_poke(t + 8, 1 | 2 | (1 << 4));
    }
    w32(V_SPRTAB, SPRTAB_A); cur = 0; REG(V_SPRCTL) = 1;
}
#define KEYS_Y 132
#define RULES_X 164
static uint8_t title(void)
{
    uint8_t r;
    REG(V_SPRCTL) = 0;
    header("a Lode Runner", YELLOW);
    title_men();
    text(LIST_X, KEYS_Y - 12, "THE KEYS", WHITE, BLUE, F8);
    text(LIST_X, KEYS_Y,      "Arrows  run, climb", LBLUE, BLUE, F8);
    text(LIST_X, KEYS_Y + 11, "Z  X    dig L / R", LBLUE, BLUE, F8);
    text(LIST_X, KEYS_Y + 22, "G       guards", LBLUE, BLUE, F8);
    text(LIST_X, KEYS_Y + 33, "-  +    the pace", LBLUE, BLUE, F8);
    text(LIST_X, KEYS_Y + 44, "P       pause", LBLUE, BLUE, F8);
    text(LIST_X, KEYS_Y + 55, "ESC     the title", LBLUE, BLUE, F8);
    text(RULES_X, KEYS_Y - 12, "THE RULES", WHITE, BLUE, F8);
    text(RULES_X, KEYS_Y,      "take all the gold;", LBLUE, BLUE, F8);
    text(RULES_X, KEYS_Y + 11, "the exit ladder", LBLUE, BLUE, F8);
    text(RULES_X, KEYS_Y + 22, "shows: climb out", LBLUE, BLUE, F8);
    text(RULES_X, KEYS_Y + 33, "dig: guards fall", LBLUE, BLUE, F8);
    text(RULES_X, KEYS_Y + 44, "in; holes close", LBLUE, BLUE, F8);
    text(RULES_X, KEYS_Y + 55, "stone does not dig", LBLUE, BLUE, F8);
    text(LIST_X, BBOT - 12, "the levels: /APPS/LODE/LEVELnn.TXT", LGREY, BLUE, F8);
    band_text(BBOT, "SPACE plays, E edits, ESC quits");
    put2(nlv, nlevels);
    if (nlv[0] == '0') nlv[0] = ' ';
    nlv[8] = nlevels == 1 ? ' ' : 's';
    r = list_pick(GY + 46, T_ITEMS, t_right, 3, 0, title_key);
    REG(V_SPRCTL) = 0;
    return r == 255 ? 2 : r;
}

/* P: the pause, as a list -- the men are hidden while it lasts.  G and the
 * pace can be changed here too.  1 to go on, 0 to end the game. */
static char gd[] = "On ", pc[] = "< 2 >";
static const char *const P_ITEMS[4] = { "Resume", "Guards", "Pace", "End the game" };
static const char *p_right[4] = { "P", gd, pc, "ESC" };
static void p_texts(void) { strcpy(gd, noguards ? "Off" : "On "); pc[2] = (char)('0' + pace); }
static uint8_t pause_key(uint8_t k, uint8_t cur)
{
    (void) cur;
    if (k == 'p' || k == 'P' || k == ' ') return 0;
    if (k == 'g' || k == 'G') noguards = !noguards;
    if ((k == K_LEFT || k == '-' || k == '_') && pace > 1) pace--;
    if ((k == K_RIGHT || k == '+' || k == '=') && pace < 3) pace++;
    p_texts();
    return 254;
}
static uint8_t pause_screen(void)
{
    uint8_t r, cur = 0;
    REG(V_SPRCTL) = 0;
    header("Paused", YELLOW);
    band_text(BBOT, "P resumes, G guards, < > pace, ESC ends");
    for (;;) {
        p_texts();
        r = list_pick(GY + 46, P_ITEMS, p_right, 4, cur, pause_key);
        if (r == 1) { noguards = !noguards; cur = 1; continue; }              /* RETURN on a setting: the next value */
        if (r == 2) { pace = (uint8_t)(pace == 3 ? 1 : pace + 1); cur = 2; continue; }
        break;
    }
    repaint();
    REG(V_SPRCTL) = 1;
    return r == 0;
}

/* the end of a game, lost or every level won: 0 play again, 1 the title, 2 quit */
static uint8_t space_key(uint8_t k, uint8_t cur) { (void) cur; return k == ' ' ? 0 : 254; }
static uint8_t end_screen(uint8_t won, uint8_t reached)
{
    static const char *const G_ITEMS[3] = { "Play again", "Title", "Quit" };
    char b[6];
    uint8_t r;
    for (r = 0; r < 60; r++) { tick_top(); wait_vblank(); }   /* a second to see how it ended */
    while (key_get()) ;
    REG(V_SPRCTL) = 0;
    header(won ? "Every level cleared" : "Game over", won ? YELLOW : LRED);
    text(LIST_X, GY + 44, "SCORE", LBLUE, BLUE, F8);
    text(LIST_X + 56, GY + 40, fmt0(score, 5), WHITE, BLUE, F16);
    text(LIST_X, GY + 62, "LEVEL", LBLUE, BLUE, F8);
    put2(b, reached); b[2] = '/'; put2(b + 3, nlevels); b[5] = 0;
    text(LIST_X + 56, GY + 58, b, WHITE, BLUE, F16);
    text(LIST_X, GY + 80, won ? "A true Lode Runner." : "The gold keeps its secret.", YELLOW, BLUE, F8);
    band_text(BBOT, "SPACE plays again, ESC the title");
    r = list_pick(GY + 98, G_ITEMS, 0, 3, 0, space_key);
    return r == 255 ? 1 : r;
}

/* ---- play --------------------------------------------------------------- */
/* one level: 0 Esc (or ended from the pause), 1 the top reached, 2 no lives left */
static uint8_t play(void)
{
    uint8_t i, k, running = 1;
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
        /* P: the pause, the Chooser's list; ending the game there is Esc */
        case 'p': case 'P': if (!pause_screen()) running = 0; continue;
        /* Z and X are remembered until he stands on a cell: pressed while
         * running they were simply dropped (dig() refuses mid-cell), which
         * read as "Z and X do not dig" (Doc, 2026-09-09).  Ten frames is
         * long enough to reach the next cell and short enough to forget. */
        case 'z': case 'Z': dig_req = -1; dig_ttl = 10; break;
        case 'x': case 'X': dig_req = 1;  dig_ttl = 10; break;
        /* G: the guards off, for learning a level (or testing it).  They stand
         * where they are and cannot catch; the panel says so. */
        case 'g': case 'G': noguards = !noguards; status_line(); break;
        /* - and +: slower and faster.  Every frame was "running too fast"
         * (Doc); the default is two moves in three frames, - drops to one
         * in three, + goes back to every frame. */
        case '-': case '_': if (pace > 1) pace--; status_line(); break;
        case '+': case '=': if (pace < 3) pace++; status_line(); break;
        }
        if (!running) break;
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
        if (unlocked && (p->y >> 4) == 0 && !(p->y & 15)) return 1;   /* the top wins */
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
            if (!lives) return 2;
            load_level(); redraw_map(); status_line();
            if (banner("Ouch -- again!", "ANY KEY") == 0x1B) return 0;
            continue;
        }
        tick_top();
        write_table(back);
        wait_vblank();
        w32(V_SPRTAB, back); cur ^= 1; frame++;
    }
    return 0;
}

/* a game, from the first level: 1 if the player chose to quit */
static uint8_t game(void)
{
    uint8_t r, won, reached;
    for (;;) {
        lives = 5; score = 0; level = 0; won = 0;
        lv_read(level); load_level(); repaint();
        REG(V_SPRCTL) = 1;
        for (;;) {
            r = play();
            if (r == 0) return 0;                        /* Esc: back to the title */
            if (r == 2) break;
            level++;
            if (level == nlevels) { won = 1; break; }
            score += 500; lv_read(level); load_level(); redraw_map(); status_line();
            if (banner("Next level", "ANY KEY STARTS") == 0x1B) return 0;
        }
        reached = won ? nlevels : (uint8_t)(level + 1);
        r = end_screen(won, reached);
        if (r != 0) return r == 2;
    }
}

/* ---- in and out --------------------------------------------------------- */
static uint8_t ctrl_was, l0_was, l1_was, bg_was, spr_was;
static void setup(void)
{
    uint8_t i;
    ctrl_was = REG(V_CTRL); l0_was = REG(VICKY + 0x10); l1_was = REG(VICKY + 0x20); bg_was = REG(V_BGCOL); spr_was = REG(V_SPRCTL);
    fonts_near(); make_tiles(); make_sprites();
    REG(V_CTRL) = 0; REG(V_BGCOL) = BLACK;               /* a pixel of colour 0 is the ground: black */
    pal(INK, 0, 0, 0);                                   /* VIDEO puts the palette back on the way out */
    dma_fill(BLUE, BMP, (uint32_t)SW * 240);
    REG(VICKY + 0x10) = 0;                               /* the console's layer off, left as it is; the bitmap on layer 1 */
    for (i = 0x21; i <= 0x25; i++) REG(VICKY + i) = 0;
    w16(VICKY + 0x26, SW); w32(VICKY + 0x28, BMP); w32(VICKY + 0x2C, BMP);
    REG(VICKY + 0x20) = 1 | (0 << 1) | (3 << 3);         /* bitmap, 8 bpp */
    init_tables(); write_table(SPRTAB_A); w32(V_SPRTAB, SPRTAB_A); REG(V_SPRCTL) = 0;
    REG(V_CTRL) = 1 | 2 | 4;                             /* 320 x 240, doubled: the Chooser's chunky pixels */
    draw_top();
}
static void leave(void)                                  /* every way out: the machine as it was */
{
    REG(V_SPRCTL) = spr_was; REG(VICKY + 0x20) = l1_was; REG(VICKY + 0x10) = l0_was;
    REG(VICKY) = ctrl_was; REG(V_BGCOL) = bg_was;
    rom_video();
    REG(TERM + 4) = 2;                                   /* JIM: a clean screen to come back to */
}

void main(void)
{
    uint8_t k;
    setup();
    lives = 5; score = 0; level = 0;
    lv_count();
    if (!nlevels) {
        /* nothing to play: the editor can still make LEVEL01 */
        for (k = 0; k < GH; k++) { uint8_t x; for (x = 0; x < GW; x++) ed[k][x] = k == GH - 1 ? '@' : ' '; }
        ed[GH - 2][0] = 'P';
        load_level(); repaint(); REG(V_SPRCTL) = 1;
        if (banner("No levels in /APPS/LODE", "E MAKES ONE, ESC LEAVES") == 0x1B) { leave(); return; }
        if (lv_write(0)) nlevels = 1; else { leave(); return; }
        editor();
        if (!nlevels) { leave(); return; }
    }
    for (;;) {
        k = title();
        if (k == 2) break;
        if (k == 1) { lv_read(level); editor(); level = 0; continue; }
        if (game()) break;
        level = 0;
    }
    leave();
}
