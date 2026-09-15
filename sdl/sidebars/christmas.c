/* sdl/sidebars/christmas.c -- one of the sidebars' scenes (docs/SIDEBARS-PLAN.md);
 * until 2026-09-15 a part of sdl/savers.c. */
#include "canvas.h"

/* ---- Christmas ------------------------------------------------------------- */
static const char *const tstar[] = { "....y....", "....y....", "...yyy...", "yyyyYyyyy", ".yyYYYyy.", "..yyYyy..", ".yy...yy.", "yy.....yy" };
void s_christmas(cv_t *c, uint32_t t, int side)
{
    int w = c->w, h = c->h, k = scale_of(w);
    uint32_t seed = 0xC4415u + (uint32_t) side * 104729u;
    vgrad(c, 0, h, RGB(4, 10, 38), RGB(24, 44, 92));
    stars(c, seed, w * h / 900, h * 3 / 5, t, RGB(230, 236, 255), RGB(90, 100, 150));
    /* a string of lights across the top, sagging, blinking in turn */
    static const uint32_t bulbs[4] = { 0xFFFF3030u, 0xFFFFD030u, 0xFF30A0FFu, 0xFF30E060u };
    for (int row = 0; row < 2; row++) {
        int y0 = 6 * k + row * (h / 3), sag = 10 * k;
        for (int x = 0; x < w; x++) { int dx = x - w / 2; pset(c, x, y0 + sag - sag * 4 * dx * dx / (w * w), RGB(20, 60, 20)); }
        for (int i = 0, x = 3 * k; x < w; x += 9 * k, i++) {
            int dx = x - w / 2, y = y0 + sag - sag * 4 * dx * dx / (w * w);
            int on = ((t / 350) + (uint32_t) i + (uint32_t) row) % 4 != 0;
            uint32_t col = bulbs[(i + row) & 3];
            if (on) glow(c, x, y + 2 * k, 4 * k, col, 120);
            rect(c, x - k / 2, y, k, k, RGB(30, 50, 30));
            disc(c, x, y + 2 * k, k, on ? col : mix(col, RGB(0, 0, 0), 150));
        }
    }
    /* the snowy ground */
    int g0 = h - h / 7;
    for (int x = 0; x < w; x++) { int top = g0 + isin(x * 7 + side * 250) * (h / 70) / 256; rect(c, x, top, 1, h - top, RGB(236, 242, 255)); rect(c, x, top + (h - top) / 2, 1, 2, RGB(200, 212, 240)); }
    /* the tree: tiers of green, lights, a garland, a star */
    int cx = side ? w * 3 / 5 : w * 2 / 5, top = h * 2 / 5, bot = g0 + 2 * k, tiers = 4, span = (bot - top);
    rect(c, cx - 3 * k, bot - 6 * k, 6 * k, 8 * k, RGB(90, 50, 20));
    for (int ti = 0; ti < tiers; ti++) {
        int ty = top + ti * span / (tiers + 1), by = top + (ti + 2) * span / (tiers + 1) - 4 * k;
        for (int y = ty; y < by; y++) {
            int half = (y - ty) * (w / 3 + ti * w / 20) / (by - ty) + 2;
            for (int x = cx - half; x <= cx + half; x++)
                pset(c, x, y, x < cx - half / 3 ? RGB(40, 130, 60) : x > cx + half / 2 ? RGB(14, 70, 32) : RGB(24, 100, 44));
        }
        for (int j = 0; j < 7; j++) {
            uint32_t r = hh(seed + (uint32_t)(ti * 40 + j));
            int ly = ty + 4 + (int)(r % (uint32_t)(by - ty - 4 > 1 ? by - ty - 4 : 1));
            int lh = (ly - ty) * (w / 3 + ti * w / 20) / (by - ty);
            int lx = cx - lh + (int)((r >> 9) % (uint32_t)(2 * lh + 1));
            int on = ((t / 280) + r) % 3 != 0;
            uint32_t col = bulbs[r >> 30];
            if (on) glow(c, lx, ly, 3 * k, col, 140);
            disc(c, lx, ly, k, on ? col : mix(col, RGB(0, 0, 0), 140));
        }
        lineb(c, cx - (w / 3 + ti * w / 20) * 2 / 3, (ty + by) / 2 + 4, cx + (w / 3 + ti * w / 20) / 2, ty + (by - ty) / 3, RGB(255, 215, 80), 200);
    }
    { static const char keys[] = "yY"; int tw = isin((int)(t / 2)); const uint32_t cols[] = { RGB(255, 220, 60), RGB(255, 255, 200) };
      glow(c, cx, top - 2 * k, 10 * k + tw * k / 128, RGB(255, 230, 120), 150);
      spr(c, cx - 4 * k, top - 6 * k, tstar, 8, keys, cols, k, 0, 256); }
    /* presents under the tree */
    static const uint32_t box[3] = { 0xFFD02828u, 0xFF2878D0u, 0xFFD0A020u };
    for (int i = 0; i < 3; i++) {
        int bw = (9 + i * 2) * k, bh = (7 + (i & 1) * 3) * k, bx = cx - 12 * k + i * 9 * k, by = bot - bh + 2 * k;
        rect(c, bx, by, bw, bh, box[i]); rect(c, bx + bw / 2 - k / 2, by, k, bh, RGB(250, 240, 200)); rect(c, bx, by + bh / 2 - k / 2, bw, k, RGB(250, 240, 200));
    }
    /* a snowman on the other side of the tree */
    if (w >= 50) {
        int sx = side ? w / 6 + 4 * k : w * 5 / 6 - 4 * k, sb = g0 + 3 * k;
        disc(c, sx, sb - 6 * k, 6 * k, RGB(245, 248, 255)); disc(c, sx, sb - 16 * k, 4 * k, RGB(245, 248, 255));
        rect(c, sx - 4 * k, sb - 22 * k, 8 * k, k, RGB(20, 20, 24)); rect(c, sx - 2 * k, sb - 26 * k, 4 * k, 4 * k, RGB(20, 20, 24));
        pset(c, sx - k, sb - 17 * k, RGB(20, 20, 24)); pset(c, sx + k, sb - 17 * k, RGB(20, 20, 24));
        rect(c, sx, sb - 16 * k, 3 * k, k, RGB(240, 120, 20));
        for (int i = 0; i < 3; i++) pset(c, sx, sb - 9 * k + i * 2 * k, RGB(30, 30, 40));
    }
    /* snow, over everything */
    int nf = w * h / 160;
    for (int i = 0; i < nf; i++) {
        uint32_t r = hh(seed * 3u + (uint32_t) i * 40503u);
        int sp = 12 + (int)(r % 34), per = h + 8;
        int y = (int)(((r >> 6) % (uint32_t) per + t * (uint32_t) sp / 1000) % (uint32_t) per) - 4;
        int x = (int)((r >> 14) % (uint32_t) w) + isin((int)(t / 8) + (int)(r >> 20)) * (2 + sp / 12) / 256;
        uint32_t wh = RGB(250, 252, 255);
        if (sp > 38) { pset(c, x, y, wh); pset(c, x - 1, y, wh); pset(c, x + 1, y, wh); pset(c, x, y - 1, wh); pset(c, x, y + 1, wh); }
        else if (sp > 26) { pset(c, x, y, wh); pset(c, x + 1, y, wh); pset(c, x, y + 1, wh); pset(c, x + 1, y + 1, wh); }
        else blend(c, x, y, wh, 200);
    }
}
