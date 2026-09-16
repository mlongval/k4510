/* sdl/sidebars/matrix.c -- digital rain, of the kind the film made famous
 * (Doc, 2026-09-16: "and a 'the Matrix' like sidebar").
 *
 * The characters are the machine's own: cv_t carries font_panel, unscii-16,
 * eight bits to a row and frows rows to a glyph, the same font the register
 * panel draws with.  So the rain is made of the letters this computer knows,
 * not of an imitation drawn here -- and when there is no font (the tests call
 * with none) it falls back to streaks of light, because every scene must draw
 * whatever it is handed.
 *
 * Like the other scenes it is a pure function of the clock: no state is kept
 * between frames.  Each column's speed, length, phase and depth come out of a
 * hash of its number, so a column falls the same way every time the same
 * millisecond comes round, and the two sides differ by their seed.
 *
 * The film's touches, which are what stop it looking like a screensaver from
 * 1996: the leading character is white-hot and the one behind it barely less
 * so; the tail fades green to black over its length; glyphs flicker -- a
 * character changes while it hangs there, which is the detail everyone
 * remembers; some columns are dimmer and slower than others, which gives the
 * strip depth; and now and then a column runs bright the whole way down.
 */
#include "canvas.h"

/* The glyphs it rains.  The film used mirrored katakana, which this font does
 * not have, so: digits, capitals and the CP437 shapes that read as cryptic --
 * arrows, blocks, maths.  A set that is not quite an alphabet is the point. */
static const unsigned char rain_set[] =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    ":=*+-<>|!?/\\^~$%&#@"
    "\x0F\x1E\x1F\x18\x19\x1A\x1B\xE0\xE1\xE2\xE3\xE4\xE5\xE8\xE9\xEA\xEB\xEC\xED\xEE\xEF"
    "\xF0\xF1\xF2\xF3\xF4\xF5\xF6\xF7\xFB\xFC\xFD";
#define RAIN_N ((int)(sizeof rain_set - 1))

/* One glyph, blended so a fading tail can be drawn over the black. */
static void rain_glyph(cv_t *c, int ch, int x, int y, int g, uint32_t col, int alpha)
{
    const uint8_t *gl;
    if (!c->font || c->frows <= 0 || alpha <= 0) return;
    gl = c->font + (unsigned) (ch & 0xFF) * (unsigned) c->frows;
    if (x + 8 * g <= 0 || x >= c->w || y + c->frows * g <= 0 || y >= c->h) return;   /* wholly off: skip the rows */
    for (int gy = 0; gy < c->frows; gy++) {
        uint8_t bits = gl[gy];
        if (!bits) continue;
        for (int gx = 0; gx < 8; gx++) {
            if (!(bits & (0x80 >> gx))) continue;
            for (int dy = 0; dy < g; dy++)
                for (int dx = 0; dx < g; dx++)
                    blend(c, x + gx * g + dx, y + gy * g + dy, col, alpha);   /* blend clips */
        }
    }
}

void s_matrix(cv_t *c, uint32_t t, int side)
{
    int w = c->w, h = c->h;
    uint32_t seed = 0x4D54u + (uint32_t) side * 2654435761u;

    rect(c, 0, 0, w, h, RGB(0, 6, 2));                       /* the black it falls through */

    /* No font: streaks instead of characters, so the scene still rains. */
    if (!c->font || c->frows <= 0) {
        int k = scale_of(w), cols = w / (2 * k); if (cols < 1) cols = 1;
        for (int i = 0; i < cols; i++) {
            uint32_t r = hh(seed + (uint32_t) i);
            int x = i * (w / cols) + (int)(r % 3u), len = 8 * k + (int)((r >> 5) % 30u);
            int sp = 8 + (int)((r >> 9) % 28u), per = h + len * 2;   /* slowed three times, as the rain proper */
            int y = (int)(((t * (uint32_t) sp) / 1000u + (r >> 15)) % (uint32_t) per) - len;
            for (int j = 0; j < len; j++) {
                int a = 255 - j * 255 / len;
                blend(c, x, y + len - j, RGB(60, 255, 90), a);
                if (k > 1) blend(c, x + 1, y + len - j, RGB(30, 180, 60), a * 2 / 3);
            }
            disc(c, x, y + len, k > 1 ? 1 : 0, RGB(220, 255, 230));
        }
        return;
    }

    {   /* the rain proper */
        /* The glyphs stay at 1x until the strip is wide: a sidebar is 120
         * pixels at its usual size, and big characters there give two lonely
         * columns instead of a wall of code.  8 pixels a column fills it. */
        int g = w >= 320 ? 2 : 1;
        int cw = 8 * g, chh = c->frows * g;
        if (cw > w) { g = 1; cw = 8; chh = c->frows; }         /* a very narrow strip: smallest glyphs */
        if (chh < 1) chh = 1;
        int cols = w / cw; if (cols < 1) cols = 1;
        int rows = h / chh + 2;
        int xoff = (w - cols * cw) / 2;                        /* centred, so the edges are even */

        for (int i = 0; i < cols; i++) {
            uint32_t r = hh(seed + (uint32_t) i * 2246822519u);
            /* Tails scale with the strip: a fixed length looks like confetti
             * down a tall sidebar.  Half its height to nearly all of it. */
            int len   = rows / 2 + (int)((r >> 3) % (uint32_t)(rows / 2 + 1));
            /* Glyphs a second, roughly, and slowed twice on the day it was
             * written (Doc, 2026-09-16: "too fast needs to be slowed down by
             * 40%", then "Matrix is better but still too fast.  Clip another
             * 40% please"), then 30% again.  35 + r%95 became 21 + r%57, then
             * 13 + r%34, and is now 9 + r%24 -- 9 to 32 glyphs a second, about
             * a quarter of the speed it was first written at.
             * The flicker is deliberately left at its old rate: in the film
             * the glyphs change fast while the column falls slowly, and
             * slowing both together only makes the whole strip look tired. */
            int speed = 9 + (int)((r >> 8) % 24u);
            int depth = (int)((r >> 17) % 3u);                 /* 0 near and bright, 2 far and dim */
            /* The period must cover the whole travel: the head starts at -len,
             * fully above the strip, and has to reach rows, fully below it.
             * Shortening it to crowd the drops together looks like a good idea
             * and is not -- with per = rows + len/3 the head bottoms out around
             * rows/3 and the lower two thirds of the sidebar stay black, which
             * is exactly what it did on the first try.  Density comes from the
             * two drops below, not from cutting the journey short. */
            int per   = rows + len + 2;
            int phase = (int)((r >> 21) % (uint32_t) per);
            int x     = xoff + i * cw;

            if (depth) { speed = speed * (4 - depth) / 4; }    /* the far ones fall slower */

            /* a column that runs bright all the way down, once in a while */
            uint32_t cyc = (t / 7000u) + (uint32_t) i * 31u;
            int blaze = (hh(seed + cyc) % 23u) == 0;

            int head = (int)(((t * (uint32_t) speed) / 1000u + (uint32_t) phase) % (uint32_t) per) - len;
            /* Two drops a column, half a period apart, so a tall strip is
             * never bare between one and the next. */
            int heads[2]; heads[0] = head;
            heads[1] = head - per / 2; if (heads[1] < -len) heads[1] += per;

            for (int d = 0; d < 2; d++)
            for (int j = 0; j < len; j++) {
                int row = heads[d] - j;
                if (row < 0 || row >= rows) continue;
                int y = row * chh;

                /* the glyph: flickers while it hangs -- the detail that sells it */
                uint32_t fl = hh((uint32_t)(i * 7919 + row * 104729) + t / (60u + (r & 63u)));
                int ch = rain_set[fl % (uint32_t) RAIN_N];

                uint32_t col;
                int alpha;
                if (j == 0) { col = RGB(225, 255, 235); alpha = 255; }               /* white-hot head */
                else if (j == 1) { col = RGB(150, 255, 175); alpha = 245; }          /* just behind it */
                else {
                    int f = 255 - (j - 1) * 255 / (len - 1 > 0 ? len - 1 : 1);       /* fading tail */
                    col = RGB(20 + f / 6, 90 + f * 150 / 255, 35 + f / 4);
                    alpha = 40 + f * 190 / 255;
                }
                if (blaze && j) alpha = alpha < 200 ? alpha + 55 : 255;
                if (depth == 1) alpha = alpha * 7 / 10;
                if (depth == 2) alpha = alpha * 9 / 20;
                rain_glyph(c, ch, x, y, g, col, alpha);
            }

            /* a soft glow under the head, so it looks lit rather than drawn */
            if (g > 1 && head >= 0 && head < rows)
                glow(c, x + cw / 2, head * chh + chh / 2, cw, RGB(40, 255, 90), 26 - depth * 7);
        }
    }
}
