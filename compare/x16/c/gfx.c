/* gfx.c -- a graphics suite: the same C on both machines, each drawing
 * by its own means (gfx.h), so no BASIC stands between the program and
 * the video chip.  Nine tests, each a fixed piece of work, a "pass":
 *
 *   CLEAR    the whole 320x240 bitmap filled once
 *   RECTS    64 filled rectangles, 1-128 x 1-96
 *   LINES    64 lines, ends anywhere
 *   PIXELS   1024 single pixels
 *   IMAGE    16 pictures of 32x32 from the program's memory
 *   SCROLL   the whole bitmap moved up one line
 *   TEXT     the 40x30 text layer written whole, character and colour
 *   SPRITES  32 sprites, each moved once
 *   PALETTE  240 palette entries written
 *
 * A test repeats its pass until two seconds of the machine's own 60 Hz
 * clock have gone (one pass at least) and reports the time of one pass.
 * Every pass of a test does the same work: the places, sizes and colours
 * are drawn from a table made before the clock starts, so the clock sees
 * only the drawing and the loop around it.  Same source for both machines;
 * see bench.h and gfx.h. */
#include "bench.h"
#include "gfx.h"

#define NT 9
static const char *const names[NT] = { "clear", "rects", "lines", "pixels", "image", "scroll", "text", "sprites", "palette" };
static uint16_t took[NT];
static uint32_t passes[NT];

#define N 1024
static uint16_t rx[N];
static uint8_t ry[N], rc[N];
static uint16_t rw[64];
static uint8_t rh[64];
static uint16_t ix[16];
static uint8_t iy[16];
static uint8_t tile[32 * 32], shape[16 * 16];
static char letters[40];
static uint8_t clear_c, frame;

static uint16_t seed = 0xACE1;
static uint16_t rnd(void) { seed ^= seed << 7; seed ^= seed >> 9; seed ^= seed << 8; return seed; }   /* xorshift16 */

static void p_clear(void) { g_rect(0, 0, GW, GH, (uint8_t) (1 + (clear_c++ & 15))); }
static void p_rects(void) { uint8_t i; for (i = 0; i < 64; i++) g_rect(rx[i], ry[i], rw[i], rh[i], rc[i]); }
static void p_lines(void) { uint8_t i; for (i = 0; i < 64; i++) g_line(rx[i], ry[i], rx[i + 64], ry[i + 64], rc[i]); }
static void p_pixels(void) { uint16_t i; for (i = 0; i < N; i++) g_pixel(rx[i], ry[i], rc[i]); }
static void p_image(void) { uint8_t i; for (i = 0; i < 16; i++) g_image(ix[i], iy[i], tile, 32, 32); }
static void p_scroll(void) { g_scroll(); }
static void p_text(void) { uint8_t r; for (r = 0; r < 30; r++) g_text_row(r, letters, (uint8_t) (1 + (r % 15))); }
static void p_sprites(void)
{
    uint8_t n, f = frame++ & 63;
    for (n = 0; n < 32; n++) g_sprite(n, 16 + (n & 7) * 36 + f, 40 + (n >> 3) * 40);
}
static void p_palette(void) { g_palette(); }
static void (*const pass[NT])(void) = { p_clear, p_rects, p_lines, p_pixels, p_image, p_scroll, p_text, p_sprites, p_palette };

static void setup(void)
{
    uint16_t i; uint8_t x, y;
    for (i = 0; i < N; i++) { rx[i] = rnd() % GW; ry[i] = (uint8_t) (rnd() % GH); rc[i] = (uint8_t) (1 + rnd() % 15); }
    for (i = 0; i < 64; i++) {
        rw[i] = 1 + (rnd() & 127); rh[i] = (uint8_t) (1 + rnd() % 96);
        if (rx[i] + rw[i] > GW) rw[i] = GW - rx[i];
        if (ry[i] + rh[i] > GH) rh[i] = (uint8_t) (GH - ry[i]);
    }
    for (i = 0; i < 16; i++) { ix[i] = rnd() % (GW - 31); iy[i] = (uint8_t) (rnd() % (GH - 31)); }
    for (y = 0; y < 32; y++)                /* a bevelled tile: light top and left, dark bottom and right */
        for (x = 0; x < 32; x++)
            tile[y * 32 + x] = (x < 2 || y < 2) ? 15 : (x > 29 || y > 29) ? 11 : (uint8_t) (1 + ((x ^ y) >> 2) % 15);
    for (y = 0; y < 16; y++)                /* a ball; 0 is see-through */
        for (x = 0; x < 16; x++) {
            int dx = 2 * x - 15, dy = 2 * y - 15;
            shape[y * 16 + x] = dx * dx + dy * dy < 225 ? (dx + dy < -8 ? 1 : 7) : 0;
        }
    for (i = 0; i < 40; i++) letters[i] = LETTER(i);
}

static void run(uint8_t k)
{
    uint16_t t0, t; uint32_t n = 0;
    t0 = ticks(); while (ticks() == t0) ;   /* start on a tick */
    t0 = ticks();
    do { pass[k](); n++; t = ticks() - t0; } while (t < 120);
    took[k] = t; passes[k] = n;
}

static const char label[] = "  the same c on both machines: gfx.c";
static char line[40];
static void card(void)                      /* one of each, to look at */
{
    uint8_t i;
    g_rect(0, 0, GW, GH, 0);
    for (i = 0; i < 16; i++) g_rect(i * 20, 0, 20, 24, i);
    for (i = 0; i < 16; i++) g_line(0, 239, i * 21, 30, (uint8_t) (1 + i % 15));
    for (i = 0; i < 4; i++) g_image(180 + i * 34, 120, tile, 32, 32);
    for (i = 0; i < 32; i++) g_sprite(i, 16 + (i & 7) * 36, 160 + (i >> 3) * 18);
    for (i = 0; i < 40; i++) line[i] = ' ';
    for (i = 0; i < 29; i++) g_text_row(i, line, 1);
    for (i = 0; label[i]; i++) line[i] = SCODE(label[i]);
    g_text_row(29, line, 1);
}

/* RESULT NAME s.sssssss S, then the passes: the time of one pass */
static void report_pass(const char *name, uint16_t t, uint32_t n)
{
    uint32_t q = (uint32_t) t * 500000UL / 3 / n;      /* tenths of a microsecond */
    char b[8]; uint8_t i;
    out("result "); out(name); outc(' ');
    dec(q / 10000000UL); outc('.');
    q %= 10000000UL;
    for (i = 7; i; i--) { b[i - 1] = (char) ('0' + q % 10); q /= 10; }
    b[7] = 0; out(b);
    out(" s, passes "); dec(n); outc('\n');
}

void main(void)
{
    uint8_t k;
    setup();
    g_init(shape);
    for (k = 0; k < 40; k++) line[k] = ' ';
    for (k = 0; k < 30; k++) g_text_row(k, line, 1);
    for (k = 0; k < NT; k++) run(k);
    card();
    g_record();
    g_done();
    out("graphics in c, a pass each, "); out(MACHINE); outc('\n');
    for (k = 0; k < NT; k++) report_pass(names[k], took[k], passes[k]);
    out("done\n");
    finish();
}
