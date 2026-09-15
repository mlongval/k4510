/* sdl/sidebars/dreamfall.c -- one of the sidebars' scenes (docs/SIDEBARS-PLAN.md);
 * until 2026-09-15 a part of sdl/savers.c. */
#include "canvas.h"

/* ---- Dreamfall: Claude's own ------------------------------------------------ */
/* A floating island at dusk, a waterfall pouring off its edge the whole
 * height of the sidebar into mist and a rainbow, aurora overhead, paper
 * lanterns rising, fireflies, and now and then a small dragon gliding by.
 * What I would like to have beside me while I read. */
void s_dreamfall(cv_t *c, uint32_t t, int side)
{
    int w = c->w, h = c->h, k = scale_of(w);
    uint32_t seed = 0xD4EAu + (uint32_t) side * 7777u;
    vgrad(c, 0, h / 2, RGB(12, 10, 44), RGB(60, 34, 100));
    vgrad(c, h / 2, h, RGB(60, 34, 100), RGB(210, 120, 120));
    stars(c, seed, w * h / 800, h / 2, t, RGB(230, 230, 255), RGB(110, 100, 160));
    /* aurora: two ribbons, rippling */
    for (int band = 0; band < 2; band++) {
        uint32_t col = band ? RGB(80, 240, 180) : RGB(120, 160, 255);
        int base = h / 12 + band * h / 14, depth = h / 10;
        for (int x = 0; x < w; x++) {
            int yc = base + isin(x * 7 + (int)(t / 12) + band * 300 + side * 200) * (h / 28) / 256;
            for (int dy = 0; dy < depth; dy++) blend(c, x, yc + dy, col, 150 * (depth - dy) / depth * (100 + isin(x * 13 + (int)(t / 7))) / 356);
        }
    }
    /* the island, bobbing */
    int iw = w * 2 / 3, cx = w / 2 + isin((int)(t / 40)) * 3 / 256, cy = h / 4 + isin((int)(t / 25)) * 3 * k / 256;
    for (int dy = 0; dy < iw / 2; dy++) {                     /* the rock beneath, narrowing to a point */
        int half = (iw / 2) * (iw / 2 - dy) / (iw / 2);
        for (int dx = -half; dx <= half; dx++) {
            uint32_t r = hh((uint32_t)(dx * 131 + dy * 7 + 1000));
            pset(c, cx + dx, cy + dy, (r & 7) == 0 ? RGB(70, 60, 70) : dx < -half / 3 ? RGB(120, 96, 90) : RGB(92, 74, 76));
        }
    }
    for (int dx = -iw / 2; dx <= iw / 2; dx++) { int th = 3 * k + isin(dx * 20) * k / 256; rect(c, cx + dx, cy - th, 1, th, RGB(60, 170, 80)); pset(c, cx + dx, cy - th, RGB(120, 220, 110)); }
    /* a tree and a little tower with a warm window */
    int trx = cx - iw / 4; rect(c, trx - k / 2, cy - 12 * k, k, 10 * k, RGB(80, 50, 30)); disc(c, trx, cy - 14 * k, 5 * k, RGB(40, 130, 70)); disc(c, trx - 2 * k, cy - 16 * k, 3 * k, RGB(60, 160, 90));
    int twx = cx + iw / 8; rect(c, twx - 3 * k, cy - 20 * k, 6 * k, 18 * k, RGB(170, 160, 180));
    for (int i = 0; i < 7 * k; i++) rect(c, twx - 4 * k + i * 4 * k / (7 * k) , cy - 20 * k - i, 8 * k - i * 8 * k / (7 * k), 1, RGB(110, 60, 150));
    { int fl = (int)(hh(t / 120 + (uint32_t) side) & 63); glow(c, twx, cy - 13 * k, 5 * k, RGB(255, 200, 100), 120 + fl); rect(c, twx - k, cy - 15 * k, 2 * k, 3 * k, RGB(255, 210, 120)); }
    /* the waterfall, the whole way down */
    int wx = cx + iw / 3, wtop = cy, wbot = h - h / 10;
    for (int y = wtop; y < wbot; y++) {
        int ww = 3 * k + (y - wtop) * 4 * k / (wbot - wtop + 1);
        for (int dx = -ww; dx <= ww; dx++) {
            uint32_t r = hh((uint32_t)((dx + 50) * 7919) + (uint32_t)((y - (int)(t * 7 / 40)) / (2 * k)) * 104729u);
            blend(c, wx + dx, y, (r & 3) == 0 ? RGB(250, 252, 255) : RGB(150, 200, 245), 200);
        }
    }
    /* the pool, the mist, a rainbow */
    rect(c, 0, wbot, w, h - wbot, RGB(40, 50, 110));
    for (int y = wbot; y < h; y += 2) rectb(c, 0, y, w, 1, RGB(120, 110, 180), 60);
    for (int ring = 0; ring < 6; ring++) {
        static const uint32_t rb[6] = { 0xFFFF4040u, 0xFFFFA040u, 0xFFFFF060u, 0xFF60E060u, 0xFF4080FFu, 0xFFA060FFu };
        int rr = w / 2 - ring * k;
        for (int a = 256 + 60; a < 768 - 60; a += 1) { int x = wx + rr * icos(a) / 256, y = wbot + rr * isin(a) / 256 * -1; blend(c, x, y, rb[ring], 70); blend(c, x, y + 1, rb[ring], 50); }
    }
    for (int i = 0; i < 14; i++) { uint32_t r = hh(seed + 300u + (uint32_t) i); int mx = wx + (int)(r % 40) - 20, my = wbot - (int)((r >> 8) % 12) + isin((int)(t / 5) + (int)(r >> 20)) * 3 / 256; glow(c, mx, my, 5 * k + (int)(r >> 28) % 4, RGB(240, 240, 255), 110); }
    /* lanterns rising */
    for (int i = 0; i < 6; i++) {
        uint32_t r = hh(seed + 500u + (uint32_t) i);
        int sp = 9 + (int)(r % 8), per = h + 30 * k, ly = h - (int)(((r >> 6) % (uint32_t) per + t * (uint32_t) sp / 1000) % (uint32_t) per);
        int lx = (int)((r >> 14) % (uint32_t)(w - 8 * k)) + 4 * k + isin((int)(t / 14) + (int)(r >> 22)) * 4 * k / 256;
        int fl = (int)(hh(t / 100 + (uint32_t) i) & 63);
        glow(c, lx, ly + 4 * k, 7 * k, RGB(255, 170, 70), 110 + fl);
        rect(c, lx - 2 * k, ly, 4 * k, 7 * k, mix(RGB(255, 150, 60), RGB(255, 220, 140), fl * 4));
        rect(c, lx - 2 * k, ly, 4 * k, k, RGB(120, 40, 20)); rect(c, lx - 2 * k, ly + 6 * k, 4 * k, k, RGB(120, 40, 20));
    }
    /* fireflies */
    for (int i = 0; i < 16; i++) {
        uint32_t r = hh(seed + 800u + (uint32_t) i);
        int x = (int)(r % (uint32_t) w) + isin((int)(t / (uint32_t)(20 + i % 7)) + (int)(r >> 8)) * (w / 6) / 256;
        int y = h / 2 + (int)((r >> 12) % (uint32_t)(h / 2 - h / 10)) + isin((int)(t / (uint32_t)(27 + i % 5)) + (int)(r >> 20)) * 12 / 256;
        if (isin((int)(t / 4) + (int)(r >> 24)) > 60) { glow(c, x, y, 3 * k, RGB(230, 255, 120), 150); pset(c, x, y, RGB(255, 255, 200)); }
    }
    /* a small dragon, gliding across now and then */
    { uint32_t cyc = t / 21000, ph = t % 21000;
      if (ph < 7000) {
          int dir = ((cyc + (uint32_t) side) & 1) ? 1 : -1, span = w + 60 * k;
          int dx0 = dir > 0 ? -30 * k + (int) ph * span / 7000 : w + 30 * k - (int) ph * span / 7000;
          int dy0 = h / 3 + h / 4 - (int) ph * (h / 4) / 7000 + isin((int)(ph / 8)) * 6 * k / 256;
          uint32_t body = RGB(60, 30, 80), wing = RGB(90, 50, 120);
          for (int i = -10 * k; i <= 10 * k; i++) { int th = (10 * k - (i < 0 ? -i : i)) / (3 * k) + k; rect(c, dx0 + i, dy0 - th / 2, 1, th, body); }
          for (int i = 0; i < 12 * k; i++) pset(c, dx0 - dir * (10 * k + i), dy0 + isin(i * 40 + (int)(ph / 3)) * 2 * k / 256, body);
          disc(c, dx0 + dir * 11 * k, dy0 - k, 2 * k, body); pset(c, dx0 + dir * 12 * k, dy0 - 2 * k, RGB(255, 220, 80));
          int flap = isin((int)(ph / 2)) * 9 * k / 256;
          for (int i = 0; i < 9 * k; i++) { int yy = dy0 - flap * i / (9 * k) - k; line(c, dx0 - 4 * k + i * dir / 3, dy0 - k, dx0 - dir * 2 * k + i * dir, yy, wing); }
      } }
}
