/* K4510: INVADERS -- the 1978 arcade, Taito's Space Invaders, on the K4510.
 *
 * Five rows of eleven: squids at the top (30), crabs (20), octopodes below
 * (10), the arcade's own shapes, two frames each (INVADER2.BAS has them).
 * The rack moves as the arcade's did, one invader a frame, from the bottom
 * left -- the ripple -- so a full rack takes 55 frames to step and the last
 * one steps every frame; 2 px across, 8 down at an edge, and if one of them
 * reaches the cannon's row the invasion has landed.  Four bunkers, kept as
 * pixels, are bitten away by the bombs from above and the shots from below
 * (the arcade's two explosion shapes), and by the invaders marching through
 * them.  Three kinds of bomb -- the rolling one aimed at you, the plunger and
 * the squiggly from the arcade's column tables -- one of each at most; one
 * shot of yours at a time; the mystery ship across the top every 25 seconds
 * or so, 50 to 300 by the arcade's table (the 23rd shot, and every 15th
 * after it, is the 300).  Three cannons, one more at 1500; each new wave
 * starts lower.  The five best are kept in /APPS/INVADERS/HISCORE.DAT.
 *
 * The sound is MELODY's (the OPL2): the four descending bass notes of the
 * march, one for each step of the rack and so its tempo; the shot, the
 * invader's end, the cannon's, and the mystery ship's warble.
 *
 *   Left/Right move   Space fire   P pause   M sound on/off   Escape end
 *
 * The look is the Personality Chooser's, as CHESS, TETRIS and BREAKOUT have
 * it: 320x240 doubled, grey bands above and below (the name and the time;
 * the keys), the blue glass, the banner's five bars, unscii drawn into the
 * bitmap, a panel on the right with the score, the best, the wave and the
 * cannons, and the start, the pause and the end as the Chooser's list.  The
 * field is the arcade's own width, 224 pixels, and as on the arcade's
 * monitor the colour comes from strips: red across the top where the
 * mystery ship flies, green across the bottom for the bunkers and the
 * cannon.  Between them the rack is coloured by row, as the colour
 * cabinets that came after had it (Doc, 2026-10-09: "a little more
 * colorful"): magenta squids with white eyes, cyan crabs and yellow
 * octopodes with red ones.  The green strip still wins: an invader that
 * comes down into it turns green, eyes and all, like everything else
 * there.
 *
 * Everything is drawn into one bitmap with the blitter, as the arcade drew
 * into its one: an invader is drawn again only when it steps, the shot, the
 * bombs and the cannon where they move.  No sprites, so nothing passes
 * under the bands and colour 0 is never needed.
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
#define C_CYAN   3
#define C_PURPLE 4
#define C_GREEN  5
#define C_BLUE   6
#define C_YELLOW 7
#define C_ORANGE 8
#define C_LRED   10
#define C_LGREEN 13
#define C_LBLUE  14
#define C_LGREY  15

/* the field: the arcade's 224 pixels across, framed in light blue */
#define FX0   8                              /* the inside: FX0..FX1-1 */
#define FX1   232
#define FY0   (GY + 4)                       /* the roof */
#define UFO_Y 26                             /* the mystery ship's lane (its first row is blank) */
#define RED_Y 40                             /* above it the red strip */
#define TOP0  48                             /* the rack's top row on wave 1 */
#define GREEN_Y 168                          /* below it the green strip */
#define BUNK_Y 176                           /* the bunkers: 22 x 16 */
#define BUNK_W 22
#define BUNK_H 16
#define CAN_Y 200                            /* the cannon: 8 rows */
#define GROUND_Y 212                         /* the line it stands on */
#define PX    246                            /* the panel: 12 px clear of the field's frame */
#define NAL   55

static uint8_t lives, wave, over, invaded, sound_on = 1, extra;
static uint16_t score;

/* ---- the shapes: the arcade's, a row a word, bit 15 the left ------------ */
static const uint16_t alien_shape[3][2][8] = {
    { { 0x0300, 0x0780, 0x0FC0, 0x1B60, 0x1FE0, 0x0480, 0x0B40, 0x14A0 },     /* squid, 30 */
      { 0x0300, 0x0780, 0x0FC0, 0x1B60, 0x1FE0, 0x0B40, 0x1020, 0x0840 } },
    { { 0x0820, 0x0440, 0x0FE0, 0x1BB0, 0x3FF8, 0x2FE8, 0x2828, 0x06C0 },     /* crab, 20 */
      { 0x0820, 0x2448, 0x2FE8, 0x3BB8, 0x3FF8, 0x1FF0, 0x0820, 0x1010 } },
    { { 0x03C0, 0x1FF8, 0x3FFC, 0x399C, 0x3FFC, 0x0E70, 0x1998, 0x300C },     /* octopus, 10 */
      { 0x03C0, 0x1FF8, 0x3FFC, 0x399C, 0x3FFC, 0x0660, 0x0DB0, 0x300C } } };
/* every shape keeps to columns 2..13 of its 16, so two neighbours a step
 * apart in the ripple never draw over each other's pixels */
static const uint8_t alien_pts[3] = { 30, 20, 10 };
/* the rack's colours by kind, and the eyes: the holes in the fourth row
 * (the same in both frames), filled in a second colour */
static const uint8_t alien_col[3] = { C_PURPLE, C_CYAN, C_YELLOW };
static const uint8_t eye_col[3] = { C_WHITE, C_RED, C_RED };
static const uint16_t eye_mask[3] = { 0x0480, 0x0440, 0x0660 };
#define EYE_ROW 3
#define UFO_LIGHTS 4                         /* the ship's row of windows */
static const uint16_t cannon_shape[8] = { 0x0100, 0x0380, 0x0380, 0x3FF8, 0x7FFC, 0x7FFC, 0x7FFC, 0x7FFC };
static const uint16_t ufo_shape[8] = { 0x0000, 0x07E0, 0x1FF8, 0x3FFC, 0x6DB6, 0xFFFF, 0x399C, 0x1008 };
static const uint16_t boom_shape[8] = { 0x0440, 0x2288, 0x1010, 0x0820, 0x600C, 0x0820, 0x1290, 0x2448 };
static const uint16_t die_shape[2][8] = {
    { 0x0400, 0x2020, 0x0500, 0x2420, 0x0D80, 0x5FE4, 0x3FF8, 0x7FFC },
    { 0x0808, 0x4100, 0x0824, 0x2200, 0x05A0, 0x1FE0, 0x7FFC, 0x3FFC } };
/* the bites: a shot's explosion (8 wide) and a bomb's (6 wide, bit 5 the left) */
static const uint8_t shot_bite[8] = { 0x91, 0x22, 0x7E, 0xFF, 0xFF, 0x7E, 0x24, 0x89 };
static const uint8_t bomb_bite[8] = { 0x22, 0x09, 0x1E, 0x3F, 0x1E, 0x2D, 0x0A, 0x21 };
/* the bombs, 3 x 7 (bit 2 the left), four frames: rolling, plunger, squiggly */
static const uint8_t bomb_shape[3][4][7] = {
    { { 2, 2, 6, 2, 2, 3, 2 }, { 2, 3, 2, 2, 6, 2, 2 }, { 2, 2, 3, 2, 2, 6, 2 }, { 2, 6, 2, 2, 3, 2, 2 } },
    { { 7, 2, 2, 2, 2, 2, 2 }, { 2, 2, 7, 2, 2, 2, 2 }, { 2, 2, 2, 2, 7, 2, 2 }, { 2, 2, 2, 2, 2, 2, 7 } },
    { { 2, 1, 2, 4, 2, 1, 2 }, { 1, 2, 4, 2, 1, 2, 4 }, { 2, 4, 2, 1, 2, 4, 2 }, { 4, 2, 1, 2, 4, 2, 1 } } };
/* the columns the plunger and the squiggly drop from, the arcade's tables */
static const uint8_t plunger_col[16] = { 1, 7, 1, 1, 1, 4, 11, 1, 6, 3, 1, 1, 11, 9, 2, 8 };
static const uint8_t squiggly_col[15] = { 11, 1, 6, 3, 1, 1, 11, 9, 2, 8, 2, 11, 4, 7, 10 };
/* the mystery ship's worth by the shots fired, the arcade's table */
static const uint16_t ufo_pts[15] = { 100, 50, 50, 100, 150, 100, 100, 50, 300, 100, 100, 100, 50, 150, 100 };
/* how much lower each wave starts (the arcade's table, scaled to this field) */
static const uint8_t wave_drop[9] = { 0, 16, 24, 32, 32, 32, 40, 40, 40 };

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
static uint8_t gl[512];
static void blit_gl(int x, int y, uint8_t w, uint8_t h)     /* gl[], w x h, to the bitmap */
{
    w32(BLT, (uint32_t)(uint16_t) gl); w32(BLT + 4, BITMAP + (uint32_t) y * W + x);
    w16(BLT + 8, w); w16(BLT + 10, h); w16(BLT + 12, w); w16(BLT + 14, W);
    REG(BLT + 0x10) = 0; REG(BLT + 0x12) = 1;
}
/* a 16-wide shape of h rows at x,y, drawn w wide with its left `shift`
 * columns in; the rest of the w x h is the glass, which rubs out where the
 * shape was a step ago */
static uint8_t sec_r = 255, sec_c; static uint16_t sec_m;   /* a second colour: holes sec_m in row sec_r (eyes, windows) */
static void shape16(const uint16_t *rows, uint8_t h, int x, int y, uint8_t w, uint8_t shift, uint8_t fg)
{
    uint8_t r, c, *g = gl; uint16_t bits, m, sm;
    for (r = 0; r < h; r++) {
        bits = rows[r]; sm = r == sec_r ? sec_m : 0;
        for (c = 0; c < shift; c++) *g++ = C_BLUE;
        for (m = 0x8000; m && c < w; m >>= 1, c++) *g++ = (bits & m) ? fg : (sm & m) ? sec_c : C_BLUE;
        for (; c < w; c++) *g++ = C_BLUE;
    }
    blit_gl(x, y, w, h);
}
static void shape8(const uint8_t *rows, uint8_t w, uint8_t h, int x, int y, uint8_t fg)   /* bit w-1 the left */
{
    uint8_t r, c, *g = gl;
    for (r = 0; r < h; r++) for (c = 0; c < w; c++) *g++ = (rows[r] >> (w - 1 - c)) & 1 ? fg : C_BLUE;
    blit_gl(x, y, w, h);
}
#define F8 0                                 /* text(): unscii-8, unscii-16, unscii-8 doubled */
#define F16 1
#define F8X2 2
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
        blit_gl(x, y, w, h);
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
    text(8, 2, "K4510 INVADERS", C_BLACK, C_LGREY, F8);
    text(W - 8 - 16 * 8, 2, b, C_BLACK, C_LGREY, F8);
}
static void tick_top(void) { rtc_read = REG(SYS + 4); if (REG(SYS + 6) != clock_shown) draw_top(); }
static const char KEYS[] = "< > move  SPACE fire  P pause  M sound";
static void glass(void) { box(0, GY, W, GB - GY, C_BLUE); }
static uint8_t ink(int y) { return y < RED_Y ? C_LRED : y >= GREEN_Y ? C_LGREEN : C_WHITE; }   /* the overlay's strips */

/* ---- the sound: MELODY, the OPL2 ---------------------------------------- */
/* ch 0 the march, 1 the shot, 2 an invader's end, 3 the cannon's, 4 the ship */
#define CH_BEAT 0
#define CH_SHOT 1
#define CH_BOOM 2
#define CH_DIE  3
#define CH_UFO  4
#define NCH 5
static const uint8_t opslot[NCH] = { 0, 1, 2, 8, 9 };
static uint8_t opl_ok, sw_t[NCH], sw_blk[NCH], ufo_ph, beat, beat_gap;
static int sw_f[NCH], sw_d[NCH];
static void opl(uint8_t reg, uint8_t val) { REG(OPL_ADDR) = reg; REG(OPL_DATA) = val; }
static void patch(uint8_t ch, const uint8_t *p)
{
    uint8_t m = opslot[ch], c = (uint8_t)(m + 3);
    opl((uint8_t)(0x20 + m), p[0]); opl((uint8_t)(0x40 + m), p[1]); opl((uint8_t)(0x60 + m), p[2]); opl((uint8_t)(0x80 + m), p[3]); opl((uint8_t)(0xE0 + m), p[4]);
    opl((uint8_t)(0x20 + c), p[5]); opl((uint8_t)(0x40 + c), p[6]); opl((uint8_t)(0x60 + c), p[7]); opl((uint8_t)(0x80 + c), p[8]); opl((uint8_t)(0xE0 + c), p[9]);
    opl((uint8_t)(0xC0 + ch), p[10]);
}
/* modulator 20 40 60 80 E0, carrier 20 40 60 80 E0, C0 */
static const uint8_t p_beat[11] = { 0x01, 0x16, 0xF5, 0x56, 0x00, 0x01, 0x00, 0xF4, 0x56, 0x00, 0x0A };   /* a soft thump, dying away */
static const uint8_t p_shot[11] = { 0x0F, 0x08, 0xF4, 0x48, 0x00, 0x01, 0x04, 0xF5, 0x46, 0x00, 0x0E };   /* noisy, falling */
static const uint8_t p_boom[11] = { 0x0F, 0x00, 0xF4, 0x46, 0x00, 0x00, 0x00, 0xF5, 0x45, 0x00, 0x0E };   /* a crunch */
static const uint8_t p_die[11]  = { 0x0E, 0x00, 0xF2, 0x24, 0x00, 0x00, 0x00, 0xF2, 0x24, 0x00, 0x0E };   /* a long rumble */
static const uint8_t p_ufo[11]  = { 0x21, 0x24, 0xF0, 0x07, 0x00, 0x21, 0x02, 0xF0, 0x07, 0x00, 0x04 };   /* a held whine */
/* the march's four notes, A G F E down at the bottom of the bass (110 to 82 Hz) */
static const uint16_t beat_f[4] = { 580, 517, 460, 434 };
static void keyoff(uint8_t ch) { if (opl_ok) opl((uint8_t)(0xB0 + ch), 0); }
static void tone(uint8_t ch, uint8_t blk, int f) { opl((uint8_t)(0xA0 + ch), (uint8_t) f); opl((uint8_t)(0xB0 + ch), (uint8_t)(0x20 | (blk << 2) | ((f >> 8) & 3))); }
/* a note on ch at fnum f in octave blk, moving d a frame, for `frames` frames */
static void sweep(uint8_t ch, uint8_t blk, int f, int d, uint8_t frames)
{
    if (!opl_ok || !sound_on) return;
    keyoff(ch);
    sw_blk[ch] = blk; sw_f[ch] = f; sw_d[ch] = d; sw_t[ch] = frames;
    tone(ch, blk, f);
}
static void hush(void) { uint8_t i; for (i = 0; i < NCH; i++) { sw_t[i] = 0; keyoff(i); } }
static uint8_t u_on;
static void snd_tick(void)                   /* a frame's: the sweeps, and the ship's warble while it flies */
{
    uint8_t i, p;
    if (beat_gap < 255) beat_gap++;
    if (!opl_ok) return;
    for (i = 0; i < NCH; i++) if (sw_t[i]) {
        if (!--sw_t[i]) keyoff(i);
        else if (sw_d[i]) {
            sw_f[i] += sw_d[i];
            if (sw_f[i] < 40) sw_f[i] = 40;
            if (sw_f[i] > 1020) sw_f[i] = 1020;
            tone(i, sw_blk[i], sw_f[i]);
        }
    }
    if (u_on && sound_on && !sw_t[CH_UFO]) {     /* up and down every 8 frames: wee-oo */
        p = (uint8_t)(++ufo_ph & 7);
        tone(CH_UFO, 5, 430 + (p < 4 ? p : 8 - p) * 45);
    }
}
static void march_beat(void)                 /* a step of the rack: the next of the four */
{
    if (beat_gap < 5) return;                /* the last invader steps every frame: a note on every fifth step at most, still on a step */
    beat_gap = 0;
    sweep(CH_BEAT, 2, beat_f[beat], 0, 7);
    beat = (uint8_t)((beat + 1) & 3);
}

/* ---- the best five ------------------------------------------------------ */
static char hname[] = "/APPS/INVADERS/HISCORE.DAT";
static uint16_t hs_s[5];
static char hs_n[5][4];
static uint8_t hbuf[41];
static void hs_load(void)
{
    uint8_t i, j;
    for (i = 0; i < 5; i++) { hs_s[i] = 0; strcpy(hs_n[i], "---"); }
    zp16(0xF0, (uint16_t) hname); zp32(0xF2, (uint32_t)(uint16_t) hbuf);
    if (rom_load() || hbuf[40] != 'I') return;
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
    hbuf[40] = 'I';
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
/* the rows at x,y,w; the choice, or 255 for Escape.  Space (and P) pick the first. */
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
        else if (k == ' ' || k == 'p' || k == 'P') return 0;
        else if (k >= '1' && k < '1' + n) return (uint8_t)(k - '1');
        else if (k == 0x1B) return 255;
        b = REG(MOUSEB);
        if ((b & 1) && !(bwas & 1)) {                         /* a click on a row */
            my = (REG(MOUSEX + 2) | (REG(MOUSEX + 3) << 8)) >> 1;     /* the glass is 640x480: halved */
            if (my >= y && my < y + n * ROW_H) return (uint8_t)((my - y) / ROW_H);
        }
        bwas = b;
        snd_tick(); tick_top();
        wait_vblank();
    }
}
static uint8_t wait_key(void) { uint8_t k; while ((k = key_get()) == 0) { snd_tick(); tick_top(); wait_vblank(); } return k; }

/* ---- the field ---------------------------------------------------------- */
static int ax[NAL];                          /* each invader's cell, 16 x 8: index row*11+col, row 0 the bottom */
static uint8_t ay[NAL], alive[NAL], nalive;
static uint8_t cur, anim, dropping;          /* the ripple: the next to step, the frame this pass draws, a pass that drops */
static int8_t dir, stepdx;
static uint8_t bun[4][BUNK_H][BUNK_W];        /* the bunkers, a byte a pixel: 1 standing */
#define BX(b) (FX0 + 33 + (b) * 45)
static int px;                                /* the cannon's cell, 16 wide */
static int sx, sy; static uint8_t shot_on;    /* the shot: 1 x 4, sx,sy its top */
static uint8_t shots;                         /* fired, for the ship's table */
static int bx[3], by[3]; static uint8_t b_on[3], b_fr[3], b_next, b_reload, pl_i, sq_i;
static int ux; static int8_t udir; static uint16_t u_timer; static uint8_t u_hit_t; static int u_hit_x; static uint16_t u_hit_pts;
static uint8_t exp_t, exp_i;                  /* an invader's explosion: the ripple waits for it */
static uint8_t dying, wave_t;
#define NSPLAT 4
static int spx[NSPLAT], spy[NSPLAT]; static uint8_t sp_t[NSPLAT], sp_w[NSPLAT];
static uint8_t fire_req, bturn;

static uint8_t kind(uint8_t i) { return i < 22 ? 2 : i < 44 ? 1 : 0; }   /* octopus, crab, squid */
static uint8_t frame_of(uint8_t i) { return i < cur ? anim : (uint8_t)(anim ^ 1); }   /* as it was last drawn */

static void bunker_rect(uint8_t b, int x0, int y0, int w, int h)   /* bunker-local, clipped: draw that part again */
{
    uint8_t r, c, *g = gl;
    if (x0 < 0) { w += x0; x0 = 0; }
    if (y0 < 0) { h += y0; y0 = 0; }
    if (x0 + w > BUNK_W) w = BUNK_W - x0;
    if (y0 + h > BUNK_H) h = BUNK_H - y0;
    if (w <= 0 || h <= 0) return;
    for (r = 0; r < h; r++) for (c = 0; c < w; c++) *g++ = bun[b][y0 + r][x0 + c] ? C_LGREEN : C_BLUE;
    blit_gl(BX(b) + x0, BUNK_Y + y0, (uint8_t) w, (uint8_t) h);
}
static void new_bunkers(void)                 /* the arcade's shape: rounded shoulders, an arch beneath */
{
    static const uint8_t arch[4] = { 7, 6, 5, 5 };
    uint8_t b, r, c, on;
    for (b = 0; b < 4; b++) for (r = 0; r < BUNK_H; r++) for (c = 0; c < BUNK_W; c++) {
        on = 1;
        if (r < 4 && (c < 4 - r || c >= BUNK_W - 4 + r)) on = 0;
        if (r >= 12 && c >= arch[r - 12] && c < BUNK_W - arch[r - 12]) on = 0;
        bun[b][r][c] = on;
    }
}
/* bite a bunker: the mask's set bits, its top-left at screen x,y */
static void bite(uint8_t b, int x, int y, const uint8_t *mask, uint8_t w)
{
    int lx = x - BX(b), ly = y - BUNK_Y, cx, cy; uint8_t r, c;
    for (r = 0; r < 8; r++) for (c = 0; c < w; c++) if ((mask[r] >> (w - 1 - c)) & 1) {
        cx = lx + c; cy = ly + r;
        if (cx >= 0 && cx < BUNK_W && cy >= 0 && cy < BUNK_H) bun[b][cy][cx] = 0;
    }
    bunker_rect(b, lx, ly, w, 8);
}
static uint8_t bunker_at(int x, int y)        /* the bunker standing at a pixel, +1; 0 for none */
{
    uint8_t b; int lx;
    if (y < BUNK_Y || y >= BUNK_Y + BUNK_H) return 0;
    for (b = 0; b < 4; b++) {
        lx = x - BX(b);
        if (lx >= 0 && lx < BUNK_W) return bun[b][y - BUNK_Y][lx] ? (uint8_t)(b + 1) : 0;
    }
    return 0;
}
static void bunker_clear(int x, int y, int w, int h)   /* an invader marching through: what it drew over is gone */
{
    uint8_t b, r, c; int lx, ly;
    for (b = 0; b < 4; b++) for (r = 0; r < h; r++) for (c = 0; c < w; c++) {
        lx = x + c - BX(b); ly = y + r - BUNK_Y;
        if (lx >= 0 && lx < BUNK_W && ly >= 0 && ly < BUNK_H) bun[b][ly][lx] = 0;
    }
}
static void splat(int x, int y, uint8_t w)    /* a shot's or a bomb's explosion where nothing is hit; it eats what it covers */
{
    uint8_t i;
    for (i = 0; i < NSPLAT; i++) if (!sp_t[i]) {
        spx[i] = x; spy[i] = y; sp_w[i] = w; sp_t[i] = 12;
        shape8(w == 8 ? shot_bite : bomb_bite, w, 8, x, y, ink(y));
        return;
    }
}
static void draw_cannon(void) { shape16(cannon_shape, 8, px - 1, CAN_Y, 18, 1, C_LGREEN); }
static void draw_ufo(void)                  /* its windows lit yellow, two and two in turn */
{
    sec_r = UFO_LIGHTS; sec_m = ux & 8 ? 0x1008 : 0x0240; sec_c = C_YELLOW;
    shape16(ufo_shape, 8, ux - 1, UFO_Y, 18, 1, C_LRED);
    sec_r = 255;
}
/* an invader's colour: its row's, with eyes -- or the green strip's, if it has come down that far */
static uint8_t alien_ink(uint8_t i)
{
    uint8_t k = kind(i);
    if (ay[i] + 7 >= GREEN_Y) { sec_r = 255; return C_LGREEN; }
    sec_r = EYE_ROW; sec_m = eye_mask[k]; sec_c = eye_col[k];
    return alien_col[k];
}
static void alien16(uint8_t i, const uint16_t *rows, int x, int y, uint8_t w, uint8_t shift)
{
    uint8_t fg = alien_ink(i);
    shape16(rows, 8, x, y, w, shift, fg);
    sec_r = 255;
}
static void draw_alien(uint8_t i) { alien16(i, alien_shape[kind(i)][frame_of(i)], ax[i], ay[i], 16, 0); }

/* ---- the panel ------------------------------------------------------------ */
static void draw_panel(void)                 /* what does not change in a game */
{
    box(PX - 4, GY, W - PX + 4, GB - GY, C_BLUE);
    bars(PX, GY + 8, 3, 3);
    text(PX, GY + 28, "Invaders", C_WHITE, C_BLUE, F16);
    text(PX, GY + 52, "SCORE", C_LBLUE, C_BLUE, F8);
    text(PX, GY + 84, "HI-SCORE", C_LBLUE, C_BLUE, F8);
    text(PX, GY + 126, "WAVE", C_LBLUE, C_BLUE, F8);
    text(PX, GY + 158, "CANNONS", C_LBLUE, C_BLUE, F8);
}
static void draw_status(void)
{
    uint8_t i;
    text(PX, GY + 62, num(score, 5), C_YELLOW, C_BLUE, F16);
    text(PX, GY + 94, num(hs_s[0] > score ? hs_s[0] : score, 5), C_WHITE, C_BLUE, F16);
    text(PX, GY + 112, hs_s[0] > score ? hs_n[0] : "YOU", C_LBLUE, C_BLUE, F8);
    text(PX, GY + 136, num(wave, 2), C_YELLOW, C_BLUE, F16);
    text(PX, GY + 168, num(lives, 1), C_YELLOW, C_BLUE, F16);
    box(PX + 12, GY + 172, W - PX - 12, 8, C_BLUE);
    for (i = 1; i < lives && i < 5; i++) shape16(cannon_shape, 8, PX + 12 + (i - 1) * 15, GY + 172, 15, 0, C_LGREEN);   /* the ones in reserve */
    text(PX, GY + 196, sound_on ? "         " : "SOUND OFF", C_LRED, C_BLUE, F8);
}
static void add_score(uint16_t p)
{
    score = score > 65535u - p ? 65535u : score + p;
    if (!extra && score >= 1500) { extra = 1; lives++; sweep(CH_UFO, 6, 600, 0, 20); }   /* one more cannon, the arcade's 1500 */
    draw_status();
}

/* the whole field again: after a list has had the glass */
static void draw_field(void)
{
    uint8_t i;
    glass();
    box(FX0 - 2, FY0 - 2, FX1 - FX0 + 4, 2, C_LBLUE);           /* the frame: roof and walls */
    box(FX0 - 2, FY0, 2, GB - FY0, C_LBLUE); box(FX1, FY0, 2, GB - FY0, C_LBLUE);
    box(FX0, GROUND_Y, FX1 - FX0, 1, C_LGREEN);
    for (i = 0; i < 4; i++) bunker_rect(i, 0, 0, BUNK_W, BUNK_H);
    for (i = 0; i < NAL; i++) if (alive[i]) draw_alien(i);
    if (u_on) draw_ufo();
    if (!dying) draw_cannon();
    draw_panel(); draw_status();
    band_text(GB, KEYS);
}
static void new_wave(void)
{
    uint8_t i, top = (uint8_t)(TOP0 + wave_drop[wave <= 9 ? wave - 1 : 1 + (wave - 2) % 8]);
    for (i = 0; i < NAL; i++) {
        alive[i] = 1;
        ax[i] = FX0 + 24 + (i % 11) * 16;
        ay[i] = (uint8_t)(top + (4 - i / 11) * 16);
    }
    nalive = NAL; cur = 0; anim = 0; dir = 1; stepdx = 2; dropping = 0;
    new_bunkers();
    for (i = 0; i < 3; i++) b_on[i] = 0;
    for (i = 0; i < NSPLAT; i++) sp_t[i] = 0;
    shot_on = 0; exp_t = 0; u_on = 0; u_hit_t = 0; u_timer = 1536; b_reload = 60; b_next = 0;
    px = FX0 + 1; keyoff(CH_UFO);
    draw_field();
}

/* ---- the march ---------------------------------------------------------- */
static void pass_done(void)                   /* the whole rack has stepped: the beat, and which way the next pass goes */
{
    uint8_t i; int lo = 999, hi = -999;
    march_beat();
    anim ^= 1;
    if (dropping) { dropping = 0; stepdx = (int8_t)(dir * 2); }
    else {
        for (i = 0; i < NAL; i++) if (alive[i]) { if (ax[i] < lo) lo = ax[i]; if (ax[i] > hi) hi = ax[i]; }
        if ((dir > 0 && hi + 19 > FX1) || (dir < 0 && lo - 2 < FX0))   /* a cell (and the 3 the last one takes) stays inside */ { dropping = 1; dir = (int8_t) -dir; stepdx = 0; }
        else stepdx = (int8_t)(nalive == 1 && dir > 0 ? 3 : dir * 2);   /* the last one, going right, the arcade's 3 */
    }
}
static void ripple(void)                      /* one invader steps, as the arcade's did each frame */
{
    uint8_t i; int x, y, w, h;
    for (;;) {
        while (cur < NAL && !alive[cur]) cur++;
        if (cur < NAL) break;
        cur = 0; pass_done();
        if (!nalive) return;
    }
    i = cur;
    if (dropping) {
        ay[i] += 8;
        x = ax[i]; y = ay[i] - 8; w = 16; h = 16;
        box(x, y, 16, 8, C_BLUE);
    } else {
        ax[i] += stepdx;
        w = 16 + (stepdx < 0 ? -stepdx : stepdx); h = 8; y = ay[i];
        x = stepdx > 0 ? ax[i] - stepdx : ax[i];
    }
    cur++;                                   /* frame_of(i) is now this pass's */
    if (stepdx > 0) alien16(i, alien_shape[kind(i)][anim], x, y, (uint8_t) w, (uint8_t) stepdx);
    else if (stepdx < 0) alien16(i, alien_shape[kind(i)][anim], x, y, (uint8_t) w, 0);
    else draw_alien(i);
    if (y + h > BUNK_Y && y < BUNK_Y + BUNK_H) bunker_clear(x, y, w, h);
    if (ay[i] + 8 > CAN_Y && !invaded) { invaded = 1; dying = 1; lives = 1; }   /* they have landed */
}

/* ---- the shot ----------------------------------------------------------- */
static void kill_alien(uint8_t i)
{
    uint8_t k;
    alive[i] = 0; nalive--;
    exp_t = 16; exp_i = i;
    k = alien_ink(i); sec_r = 255;
    shape16(boom_shape, 8, ax[i], ay[i], 16, 0, k);   /* in its own colour */
    sweep(CH_BOOM, 3, 600, -24, 12);
    add_score(alien_pts[kind(i)]);
}
static void move_shot(void)
{
    uint8_t i, r, b, k; int y, c;
    box(sx, sy, 1, 4, C_BLUE);
    sy -= 4;
    if (sy < FY0 + 2) { shot_on = 0; splat(sx - 3, FY0 + 1, 8); return; }   /* the roof */
    for (r = 4; r--; ) {                      /* the bunkers, from below: the lowest pixel the shot meets */
        y = sy + r;
        if ((b = bunker_at(sx, y)) != 0) { shot_on = 0; bite(b - 1, sx - 3, y - 4, shot_bite, 8); return; }
    }
    for (i = 0; i < 3; i++) if (b_on[i] && sx >= bx[i] && sx < bx[i] + 3 && sy < by[i] + 7 && sy + 4 > by[i]) {   /* a bomb: both go */
        b_on[i] = 0; box(bx[i], by[i], 3, 7, C_BLUE); shot_on = 0; splat(sx - 3, sy - 2, 8); return;
    }
    if (u_on && sx >= ux && sx < ux + 16 && sy < UFO_Y + 8 && sy + 4 > UFO_Y) {
        for (r = 0; r < 4; r++) {
            y = sy + r - UFO_Y;
            if (y >= 0 && y < 8 && (ufo_shape[y] & (0x8000u >> (sx - ux)))) {
                shot_on = 0; u_on = 0; keyoff(CH_UFO);
                u_hit_pts = ufo_pts[shots % 15]; u_hit_x = ux; u_hit_t = 90;
                shape16(boom_shape, 8, ux - 1, UFO_Y, 18, 1, C_LRED);
                sweep(CH_UFO, 4, 300, 12, 40);
                add_score(u_hit_pts);
                return;
            }
        }
    }
    for (i = 0; i < NAL; i++) {
        if (!alive[i]) continue;
        c = sx - ax[i];
        if (c < 0 || c > 15 || sy >= ay[i] + 8 || sy + 4 <= ay[i]) continue;
        k = frame_of(i);
        for (r = 0; r < 4; r++) {
            y = sy + r - ay[i];
            if (y >= 0 && y < 8 && (alien_shape[kind(i)][k][y] & (0x8000u >> c))) { shot_on = 0; kill_alien(i); return; }
        }
    }
    box(sx, sy, 1, 4, ink(sy));
}

/* ---- the bombs ---------------------------------------------------------- */
static uint8_t lowest(uint8_t col)            /* the bottom invader standing in a column, or 255 */
{
    uint8_t r;
    for (r = 0; r < 5; r++) if (alive[r * 11 + col]) return (uint8_t)(r * 11 + col);
    return 255;
}
static void drop_bomb(void)
{
    uint8_t t = b_next, i = 255, c, best = 255; int d, bd = 999;
    b_next = (uint8_t)((b_next + 1) % 3);
    if (b_on[t]) return;
    if (t == 0) {                             /* rolling: from the column above the cannon */
        for (c = 0; c < 11; c++) if ((i = lowest(c)) != 255) {
            d = ax[i] + 8 - (px + 8); if (d < 0) d = -d;
            if (d < bd) { bd = d; best = i; }
        }
        i = best;
    }
    else if (t == 1) { if (nalive == 1) return; i = lowest((uint8_t)(plunger_col[pl_i] - 1)); pl_i = (uint8_t)((pl_i + 1) & 15); }
    else { i = lowest((uint8_t)(squiggly_col[sq_i] - 1)); sq_i = (uint8_t)((sq_i + 1) % 15); }
    if (i == 255) return;
    b_on[t] = 1; b_fr[t] = 0; bx[t] = ax[i] + 6; by[t] = ay[i] + 8;
    b_reload = (uint8_t)(score < 200 ? 48 : score < 1000 ? 32 : score < 2000 ? 24 : score < 3000 ? 18 : 14);
}
static uint8_t hits_cannon(uint8_t t)
{
    uint8_t r, c; int y, col;
    if (by[t] + 7 <= CAN_Y || by[t] >= CAN_Y + 8) return 0;
    for (c = 0; c < 3; c++) {
        col = bx[t] + c - px;
        if (col < 0 || col > 15) continue;
        for (r = 0; r < 7; r++) {
            y = by[t] + r - CAN_Y;
            if (y >= 0 && y < 8 && ((bomb_shape[t][b_fr[t]][r] >> (2 - c)) & 1) && (cannon_shape[y] & (0x8000u >> col))) return 1;
        }
    }
    return 0;
}
static void move_bomb(uint8_t t)              /* each bomb moves every third frame, 4 px (5 when eight or fewer are left) */
{
    uint8_t s = nalive <= 8 ? 5 : 4, r, c, b; int y;
    box(bx[t], by[t], 3, 7, C_BLUE);
    by[t] += s; b_fr[t] = (uint8_t)((b_fr[t] + 1) & 3);
    for (r = (uint8_t)(7 - s); r < 7; r++) {      /* the bunkers, from above: the first row the bomb's new part meets */
        y = by[t] + r;
        for (c = 0; c < 3; c++) if ((b = bunker_at(bx[t] + c, y)) != 0) {
            b_on[t] = 0; bite(b - 1, bx[t] - 1, y - 1, bomb_bite, 6); return;
        }
    }
    if (hits_cannon(t)) { b_on[t] = 0; dying = 1; return; }
    if (by[t] + 7 >= GROUND_Y) { b_on[t] = 0; splat(bx[t] - 1, GROUND_Y - 3, 6); return; }   /* the ground: a bite out of the line */
    shape8(bomb_shape[t][b_fr[t]], 3, 7, bx[t], by[t], ink(by[t] + 6));
}

/* ---- the mystery ship -------------------------------------------------- */
static void ufo_step(void)
{
    int tx;
    if (u_hit_t) {                            /* its explosion, then what it was worth */
        tx = u_hit_x - 4;
        if (tx < FX0) tx = FX0;
        if (tx > FX1 - 24) tx = FX1 - 24;
        if (--u_hit_t == 60) { box(u_hit_x - 1, UFO_Y, 18, 8, C_BLUE); text(tx, UFO_Y, num(u_hit_pts, u_hit_pts < 100 ? 2 : 3), C_LRED, C_BLUE, F8); }
        else if (!u_hit_t) box(tx, UFO_Y, 24, 8, C_BLUE);
        return;
    }
    if (!u_on) {
        if (u_timer) { u_timer--; return; }
        u_timer = 1536;                       /* 25.6 seconds, the arcade's */
        if (nalive < 8) return;
        u_on = 1; ufo_ph = 0;
        udir = (int8_t)(shots & 1 ? -1 : 1);
        ux = udir > 0 ? FX0 + 1 : FX1 - 17;   /* drawn from ux-1 to ux+16: clear of the walls */
    }
    ux += udir;
    if (ux < FX0 + 1 || ux > FX1 - 17) { u_on = 0; keyoff(CH_UFO); box(ux - udir - 1, UFO_Y, 18, 8, C_BLUE); return; }
    draw_ufo();
}

/* ---- the cannon's end ------------------------------------------------- */
static void die_step(void)                    /* dying counts up: the two frames of the explosion for a second and a half */
{
    uint8_t i;
    if (dying == 1) { hush(); sweep(CH_DIE, 3, 700, -6, 90); if (u_on) { u_on = 0; box(ux - 1, UFO_Y, 18, 8, C_BLUE); } }
    if (dying < 96) { if (dying % 6 == 1) shape16(die_shape[(dying / 6) & 1], 8, px - 1, CAN_Y, 18, 1, C_LGREEN); dying++; return; }
    box(px - 1, CAN_Y, 18, 8, C_BLUE);
    for (i = 0; i < 3; i++) if (b_on[i]) { b_on[i] = 0; box(bx[i], by[i], 3, 7, C_BLUE); }
    if (shot_on) { shot_on = 0; box(sx, sy, 1, 4, C_BLUE); }
    lives--; draw_status();
    dying = 0;
    if (!lives) { over = 1; return; }
    px = FX0 + 1; draw_cannon(); b_reload = 60;
}

/* a frame of the game */
static void step(void)
{
    uint8_t held, i;
    snd_tick();
    for (i = 0; i < NSPLAT; i++) if (sp_t[i] && !--sp_t[i]) box(spx[i], spy[i], sp_w[i], 8, C_BLUE);
    if (dying) { die_step(); return; }
    if (wave_t) {                             /* the rack is gone: a breath, then the next, lower */
        if (!--wave_t) { wave++; new_wave(); }
        return;
    }
    held = keys_held();
    if ((held & HELD_LEFT) && px > FX0 + 1) px--;              /* a pixel a frame, the arcade's pace */
    if ((held & HELD_RIGHT) && px < FX1 - 17) px++;
    draw_cannon();
    if ((fire_req || (held & HELD_FIRE)) && !shot_on && !exp_t) {
        shot_on = 1; shots++; sx = px + 7; sy = CAN_Y - 4;
        box(sx, sy, 1, 4, ink(sy));
        sweep(CH_SHOT, 5, 760, -36, 14);
    }
    fire_req = 0;
    if (shot_on) move_shot();
    if (dying) return;
    if (++bturn == 3) bturn = 0;              /* the bombs take turns, a frame each */
    if (b_on[bturn]) move_bomb(bturn);
    if (dying) return;
    if (b_reload) b_reload--; else if (nalive) drop_bomb();
    ufo_step();
    if (exp_t) {                              /* an invader's explosion: the rack waits, as the arcade's did */
        if (!--exp_t) box(ax[exp_i], ay[exp_i], 16, 8, C_BLUE);
    }
    else if (nalive) ripple();
    if (!nalive && !exp_t && !wave_t && !dying) {
        for (i = 0; i < 3; i++) if (b_on[i]) { b_on[i] = 0; box(bx[i], by[i], 3, 7, C_BLUE); }
        if (u_on) { u_on = 0; keyoff(CH_UFO); box(ux - 1, UFO_Y, 18, 8, C_BLUE); }
        wave_t = 90;
    }
}

/* ---- the screens -------------------------------------------------------- */
static void header(const char *sub, uint8_t col)   /* a full-glass screen: the bars, the name, a line under it */
{
    glass();
    bars(12, GY + 8, 5, 6);
    text(72, GY + 8, "Invaders", C_WHITE, C_BLUE, F8X2);
    text(72, GY + 28, sub, col, C_BLUE, F8);
}
static const char *const PAUSE[2] = { "Resume", "End the game" };
static uint8_t pause_screen(void)             /* 1: go on */
{
    uint8_t k;
    hush();
    header("Paused", C_YELLOW);
    text(24, GY + 104, "SCORE", C_LBLUE, C_BLUE, F8); text(72, GY + 100, num(score, 5), C_WHITE, C_BLUE, F16);
    text(136, GY + 104, "WAVE", C_LBLUE, C_BLUE, F8); text(176, GY + 100, num(wave, 2), C_WHITE, C_BLUE, F16);
    text(208, GY + 104, "CANNONS", C_LBLUE, C_BLUE, F8); text(272, GY + 100, num(lives, 1), C_WHITE, C_BLUE, F16);
    text(24, GY + 128, "INVADERS LEFT", C_LBLUE, C_BLUE, F8); text(136, GY + 124, num(nalive, 2), C_WHITE, C_BLUE, F16);
    band_text(GB, "P resumes, ESC ends the game");
    k = list(12, GY + 46, W - 24, PAUSE, 2, 0);
    if (k != 0) return 0;
    draw_field();
    return 1;
}
static void play(void)
{
    uint8_t lf, fc, d, k;
    wave = 1; lives = 3; score = 0; over = 0; invaded = 0; extra = 0; dying = 0; wave_t = 0; shots = 0; beat = 0; pl_i = sq_i = 0;
    new_wave();
    lf = REG(SYS + 0x0D);
    while (!over) {
        fc = REG(SYS + 0x0D);
        if (fc == lf) { K_WAIT(); continue; }       /* the frame not yet over: rest the CPU till it is */
        d = (uint8_t)(fc - lf); lf = fc;
        if (d > 3) d = 3;
        while ((k = key_get()) != 0) {
            if (k == 0x1B) { over = 1; break; }
            if (k == 'p' || k == 'P') { if (!pause_screen()) { over = 1; break; } lf = REG(SYS + 0x0D); d = 0; }
            else if (k == 'm' || k == 'M') { sound_on ^= 1; if (!sound_on) hush(); draw_status(); }
            else if (k == ' ') fire_req = 1;
        }
        tick_top();
        while (d-- && !over) step();
    }
    hush();
}
static const char *const AGAIN[2] = { "Play again", "Leave" };
static uint8_t game_over(void)               /* 1: again */
{
    static char ini[4];
    uint8_t k, i, j, pos = 0;
    for (k = 0; k < 45; k++) { snd_tick(); wait_vblank(); }
    while (key_get()) ;
    box(FX0, FY0, FX1 - FX0, GB - FY0, C_BLUE);                 /* the field emptied: no bomb left showing */
    box(16, 34, FX1 - FX0 - 16, 186, C_LBLUE); box(18, 36, FX1 - FX0 - 20, 182, C_BLUE);   /* the field's middle, framed */
    center(16, FX1 - FX0 - 16, 42, "GAME OVER", C_LRED, F8X2);
    if (invaded) center(16, FX1 - FX0 - 16, 60, "THEY HAVE LANDED", C_WHITE, F8);
    for (i = 0; i < 5 && hs_s[i] >= score; i++) ;
    if (i < 5 && score) {
        center(16, FX1 - FX0 - 16, 74, "YOUR INITIALS", C_YELLOW, F8);
        band_text(GB, "LETTERS, RETURN KEEPS THEM");
        strcpy(ini, "___");
        for (;;) {
            center(16, FX1 - FX0 - 16, 86, ini, C_WHITE, F8X2);
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
        box(18, 72, FX1 - FX0 - 20, 32, C_BLUE);
    }
    center(16, FX1 - FX0 - 16, 80, "THE BEST FIVE", C_LBLUE, F8);
    for (j = 0; j < 5; j++) hs_line(68, 94 + j * 11, j, j == i ? C_YELLOW : C_WHITE);
    band_text(GB, "1-2 or cursor keys, RETURN picks");
    k = list(24, 160, FX1 - FX0 - 32, AGAIN, 2, 0);
    return k == 0;
}
static const char *const TITLE[2] = { "Play", "Leave" };
static uint8_t title(void)                   /* 1: play */
{
    static const char *const worth[4] = { "= ? MYSTERY", "= 30 POINTS", "= 20 POINTS", "= 10 POINTS" };
    uint8_t j, k;
    header("the arcade of 1978", C_YELLOW);
    text(24, 108, "SCORE ADVANCE TABLE", C_LBLUE, C_BLUE, F8);
    sec_r = UFO_LIGHTS; sec_m = 0x1248; sec_c = C_YELLOW;
    shape16(ufo_shape, 8, 24, 121, 16, 0, C_LRED);
    for (j = 0; j < 3; j++) { sec_r = EYE_ROW; sec_m = eye_mask[j]; sec_c = eye_col[j]; shape16(alien_shape[j][0], 8, 24, 136 + j * 14, 16, 0, alien_col[j]); }
    sec_r = 255;
    for (j = 0; j < 4; j++) text(48, 122 + j * 14, worth[j], j ? C_WHITE : C_LRED, C_BLUE, F8);
    text(200, 108, "THE BEST FIVE", C_LBLUE, C_BLUE, F8);
    for (j = 0; j < 5; j++) hs_line(200, 122 + j * 11, j, C_WHITE);
    text(24, 186, "< >  move    SPACE  fire", C_LBLUE, C_BLUE, F8);
    text(24, 198, "P  pause   M  sound   ESC  ends", C_LBLUE, C_BLUE, F8);
    band_text(GB, "1-2 or cursor keys, RETURN picks");
    k = list(12, GY + 44, W - 24, TITLE, 2, 0);
    return k == 0;
}

void main(void)
{
    uint8_t i, ctrl_was, l0_was, l1_was, bg_was;
    opl_ok = REG(OPL_ID) == 0x02;
    if (opl_ok) {
        for (i = 0; i < 0xF6; i++) opl(i, 0);
        opl(0x01, 0x20);
        patch(CH_BEAT, p_beat); patch(CH_SHOT, p_shot); patch(CH_BOOM, p_boom); patch(CH_DIE, p_die); patch(CH_UFO, p_ufo);
    }
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
