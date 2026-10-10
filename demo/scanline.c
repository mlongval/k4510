/* K4510: SCANLINE -- a test picture for the scanlines and the scaling.
 *
 * Doc, 2026-10-09: "make the darkness adjustable rather than two fixed
 * strengths ... he wants to test variants first".  This is the bench for
 * that: a picture with the things scanlines and scaling act on -- text in
 * four sizes, the sixteen colours as bars, a grey ramp and a sky ramp (the
 * photo-like gradients), a moving sprite -- and keys that step the darkness
 * and switch the way the screen is scaled while you look.
 *
 * It asks for a 640x480 screen with scanlines (MODE 640x480 -c: on a
 * 1080-line panel that is 2.25x, where integer, sharp-bilinear and native
 * all look different) and then writes VICKY's registers directly:
 *
 *   $D0E0  SCANDK    the darkness, percent (0 = the user's F12 default)
 *   $D0D0  GLASSCTL  bits4-5: how the glass is shown -- $30 integer,
 *                    $10 fit (sharp-bilinear), $20 native; bit6 scanlines
 *
 * Keys:  Left/Right or -/+   darkness by 10%     1..9, 0   10%..90%, 100%
 *        I  S  N             integer / sharp-bilinear / native
 *        U                   the user's own (bits4-5 = 0, SCANDK = 0)
 *        A                   auto: a step every two seconds
 *        Esc                 back to K/OS, the registers as they were
 *
 * The frontend reads the registers every frame, so each key shows at once.
 * On the way out the two registers are put back and K/OS restores the
 * shell's own screen (any MODE a program asked for ends with it).
 */
#include "k4510.h"

#define W 640
#define H 480
#define FB       0x200000UL              /* the bitmap, 640x480 at 8 bpp */
#define SPRTAB   0x250000UL
#define SPRDATA  0x250100UL
#define GLASSCTL REG(VICKY + 0xD0)
#define SCANDK   REG(VICKY + 0xE0)

#define GREY0    128                     /* 64 greys */
#define SKY0     192                     /* 64 steps of sky */
#define WHITE    1
#define BLACK    0

static uint8_t dark = 56, pres = 0x30, auto_on = 0;   /* pres: GLASSCTL bits4-5 */
static uint8_t saved_gc, saved_dk;

/* ---- drawing into the bitmap --------------------------------------------- */
static void hline(uint16_t x, uint16_t y, uint16_t n, uint8_t c) { dma_fill(c, FB + (uint32_t) y * W + x, n); }
static void box(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t c) { while (h--) hline(x, y++, w, c); }

/* a glyph from the 8x8 font at scale s: each set bit a run of s pixels, s rows */
static void glyph(uint16_t x, uint16_t y, uint8_t ch, uint8_t s, uint8_t c)
{
    uint8_t r, b, run; uint32_t g = FONT8 + (uint32_t) ch * 8;
    for (r = 0; r < 8; r++) {
        uint8_t bits = far_peek(g + r), k;
        for (b = 0, run = 0; b <= 8; b++) {
            if (b < 8 && (bits & (0x80 >> b))) { run++; continue; }
            if (run) { for (k = 0; k < s; k++) hline(x + (uint16_t)(b - run) * s, y + r * s + k, run * s, c); run = 0; }
        }
    }
}
static void text(uint16_t x, uint16_t y, const char *t, uint8_t s, uint8_t c)
{
    while (*t) { if (*t != ' ') glyph(x, y, (uint8_t) *t, s, c); x += 8 * s; t++; }
}
static void num(uint16_t x, uint16_t y, uint8_t v, uint8_t s, uint8_t c)
{
    char b[4]; uint8_t i = 0;
    if (v >= 100) b[i++] = '0' + v / 100;
    if (v >= 10) b[i++] = '0' + (v / 10) % 10;
    b[i++] = '0' + v % 10; b[i] = 0;
    text(x, y, b, s, c);
}

static void palette(void)
{
    uint8_t i;
    for (i = 0; i < 64; i++) pal(GREY0 + i, i * 4, i * 4, i * 4);
    for (i = 0; i < 64; i++) pal(SKY0 + i, 40 + i * 3, 90 + i * 2, 200 - i * 2);   /* a deep blue to a pale horizon */
}

static void picture(void)
{
    uint8_t i; uint16_t x;
    dma_fill(BLACK, FB, (uint32_t) W * H);
    /* text in four sizes: 1x on the left, the larger to the right */
    text(8, 8,  "SCANLINE  the K4510's test picture", 1, WHITE);
    text(8, 20, "Text at 8, 16, 24 and 32 pixels; the bars", 1, 15);
    text(8, 30, "are the VIC-II sixteen; grey and sky ramps;", 1, 15);
    text(8, 40, "a sprite.  Watch the row gaps as it scales.", 1, 15);
    text(8, 56, "Hamburgefons 0123", 2, WHITE);
    text(8, 76, "Hamburg 01", 3, 7);
    text(8, 104, "K4510", 4, 3);
    /* the sixteen, 40 wide and 48 high, at 144 */
    for (i = 0; i < 16; i++) box(i * 40, 144, 40, 48, i);
    /* a grey ramp: one row drawn, then copied */
    for (x = 0; x < W; x++) far_poke(FB + 200UL * W + x, (uint8_t)(GREY0 + x / 10));
    for (i = 1; i < 56; i++) dma_copy(FB + 200UL * W, FB + (200UL + i) * W, W);
    /* the sky: a row a step */
    for (i = 0; i < 64; i++) hline(0, 264 + i, W, (uint8_t)(SKY0 + 63 - i));
    /* fine detail the scanlines will beat against: one-pixel lines */
    for (i = 0; i < 24; i++) hline(0, 336 + i * 2, W, i & 1 ? 12 : 15);
    for (x = 0; x < W; x += 2) { uint8_t r; for (r = 0; r < 24; r++) far_poke(FB + (384UL + r) * W + x, 1); }
    text(8, 416, "Left/Right -/+ 1-9 0  darkness    I S N  integer / sharp-bilinear / native", 1, 15);
    text(8, 426, "U  the user's own (F12)    A  auto sweep    Esc  back to K/OS", 1, 15);
}

static void status(void)
{
    box(0, 444, W, 36, BLACK);
    text(8, 448, "DARKNESS", 2, 15); num(160, 448, dark, 2, WHITE); text(208, 448, "%", 2, WHITE);
    text(272, 448, pres == 0x30 ? "INTEGER" : pres == 0x10 ? "SHARP-BILINEAR" : pres == 0x20 ? "NATIVE" : "USER'S", 2, pres ? 7 : 13);
    if (auto_on) text(544, 448, "AUTO", 2, 10);
    text(8, 468, "SCANDK $D0E0 =", 1, 11); num(128, 468, dark, 1, 11);
    text(176, 468, "GLASSCTL $D0D0 bits4-5 =", 1, 11); num(376, 468, pres >> 4, 1, 11);
}

static void apply(void)
{
    SCANDK = pres ? dark : 0;                              /* the user's own: both registers 0 */
    GLASSCTL = (uint8_t)((GLASSCTL & 0x4F) | pres | 0x40);
    status();
}

/* ---- the sprite: a 16x16 ball, 4 bpp ------------------------------------- */
static void sprite(void)
{
    static const uint8_t ball[16] = { 0x03, 0xC0, 0x0F, 0xF0, 0x3F, 0xFC, 0x3F, 0xFC, 0x7F, 0xFE, 0x7F, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF };
    uint8_t y, x;
    for (y = 0; y < 16; y++) {
        uint16_t row = y < 8 ? ((uint16_t) ball[y * 2] << 8 | ball[y * 2 + 1]) : ((uint16_t) ball[(15 - y) * 2] << 8 | ball[(15 - y) * 2 + 1]);
        for (x = 0; x < 16; x += 2) {
            uint8_t a = (row & (0x8000 >> x)) ? ((y < 5 && x > 4 && x < 10) ? 1 : 2) : 0;   /* red, a white highlight */
            uint8_t b = (row & (0x8000 >> (x + 1))) ? ((y < 5 && x + 1 > 4 && x + 1 < 10) ? 1 : 2) : 0;
            far_poke(SPRDATA + y * 8 + x / 2, (uint8_t)(a << 4 | b));
        }
    }
    far_poke(SPRTAB + 4, (uint8_t) SPRDATA); far_poke(SPRTAB + 5, (uint8_t)(SPRDATA >> 8)); far_poke(SPRTAB + 6, (uint8_t)(SPRDATA >> 16)); far_poke(SPRTAB + 7, 0);
    far_poke(SPRTAB + 8, 0x31);                           /* enable, 4 bpp, after layer 3 */
    far_poke(SPRTAB + 9, 0x05);                           /* 16 x 16 */
    far_poke(SPRTAB + 10, 0);                             /* the VIC-II colours */
    w32(V_SPRTAB, SPRTAB); REG(V_SPRCTL) = 1;
}
static void sprite_at(uint16_t x, uint16_t y)
{
    far_poke(SPRTAB + 0, (uint8_t) x); far_poke(SPRTAB + 1, (uint8_t)(x >> 8));
    far_poke(SPRTAB + 2, (uint8_t) y); far_poke(SPRTAB + 3, (uint8_t)(y >> 8));
}

void main(void)
{
    uint8_t k, held, held_was = 0, frames = 0; uint16_t sx = 500, sy = 60; int8_t dx = 1;
    rom_shell("MODE 640x480 -c");                         /* 640x480 with scanlines: a software size on most panels */
    saved_gc = GLASSCTL; saved_dk = SCANDK;
    palette();
    REG(V_BGCOL) = 11;                                    /* dark grey under it all, so the black and blue bars show; K/OS puts its own back */
    REG(V_LAYER(0)) = 0;                                  /* the console off; K/OS puts it back */
    {   uint16_t L = V_LAYER(1);
        REG(L + 1) = 0; w16(L + 2, 0); w16(L + 4, 0); w16(L + 6, W); w32(L + 8, FB); w32(L + 12, 0);
        REG(L) = 1 | (0 << 1) | (3 << 3);                 /* bitmap, 8 bpp */
    }
    picture(); sprite(); apply();
    for (;;) {
        wait_vblank();
        held = keys_held();
        k = key_get();
        if (k == 0x1B) break;
        if (k >= 'a' && k <= 'z') k -= 32;
        if (k >= '1' && k <= '9') { dark = (uint8_t)((k - '0') * 10); auto_on = 0; apply(); }
        else if (k == '0') { dark = 100; auto_on = 0; apply(); }
        else if (k == '+' || k == '=' || ((held & HELD_RIGHT) && !(held_was & HELD_RIGHT))) { dark = (uint8_t)(dark >= 100 ? 100 : dark + 10); auto_on = 0; apply(); }
        else if (k == '-' || ((held & HELD_LEFT) && !(held_was & HELD_LEFT))) { dark = (uint8_t)(dark <= 10 ? 10 : dark - 10); auto_on = 0; apply(); }
        else if (k == 'I') { pres = 0x30; apply(); }
        else if (k == 'S') { pres = 0x10; apply(); }
        else if (k == 'N') { pres = 0x20; apply(); }
        else if (k == 'U') { pres = 0; apply(); }
        else if (k == 'A') { auto_on = !auto_on; frames = 0; if (auto_on) { dark = 10; apply(); } else status(); }
        held_was = held;
        if (auto_on && ++frames >= 120) { frames = 0; dark = (uint8_t)(dark >= 100 ? 10 : dark + 10); apply(); }
        sx += dx; if (sx >= 616 || sx <= 320) dx = -dx;
        sprite_at(sx, sy);
    }
    REG(V_SPRCTL) = 0;
    GLASSCTL = saved_gc; SCANDK = saved_dk;               /* as they were; K/OS restores the shell's mode */
}
