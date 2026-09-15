/* sdl/sidebars/space.c -- one of the sidebars' scenes (docs/SIDEBARS-PLAN.md);
 * until 2026-09-15 a part of sdl/savers.c. */
#include "canvas.h"

/* ---- Space ----------------------------------------------------------------- */
void s_space(cv_t *c, uint32_t t, int side)
{
    int w = c->w, h = c->h, k = scale_of(w);
    uint32_t seed = 0x5ACEu + (uint32_t) side * 15485863u;
    rect(c, 0, 0, w, h, RGB(2, 2, 10));
    /* nebulae: big soft clouds drifting slowly down */
    static const uint32_t neb[4] = { 0xFF6A2A9Au, 0xFF2A4AAAu, 0xFFAA2A6Au, 0xFF2A8A9Au };
    for (int i = 0; i < 5; i++) {
        uint32_t r = hh(seed + 50u + (uint32_t) i);
        int per = h + 200, ny = (int)(((r >> 8) % (uint32_t) per + t / 90) % (uint32_t) per) - 100;
        glow(c, (int)(r % (uint32_t) w), ny, w / 3 + (int)(r >> 28) * 4, neb[i & 3], 70);
    }
    /* three layers of stars, slow to fast */
    for (int layer = 0; layer < 3; layer++) {
        int n = w * h / (500 - layer * 120), sp = 5 + layer * layer * 12;
        for (int i = 0; i < n; i++) {
            uint32_t r = hh(seed + (uint32_t)(layer * 100000 + i));
            int y = (int)(((r >> 5) % (uint32_t) h + t * (uint32_t) sp / 1000) % (uint32_t) h);
            int x = (int)((r >> 13) % (uint32_t) w);
            uint32_t col = (r & 7) == 0 ? RGB(160, 190, 255) : (r & 7) == 1 ? RGB(255, 220, 160) : RGB(200 + layer * 25, 200 + layer * 25, 210 + layer * 20);
            if (layer == 0) blend(c, x, y, col, 130); else pset(c, x, y, col);
            if (layer == 2 && (r & 15) == 3) { pset(c, x, y + 1, col); blend(c, x, y - 1, col, 120); }
        }
    }
    /* a ringed planet sliding down, very slowly */
    { int pr = w / 5 + 4, per = h + 4 * pr, py = (int)((t / 150 + (uint32_t)(side * h / 2)) % (uint32_t) per) - 2 * pr, px = side ? w * 3 / 5 : w * 2 / 5;
      int rx = pr * 2, ry = pr / 3;
      for (int a = 512; a < 1024; a++) { int x = px + rx * icos(a) / 256, y = py + ry * isin(a) / 256; pset(c, x, y, RGB(200, 180, 140)); pset(c, x, y + 1, RGB(150, 130, 100)); }  /* the far half of the ring */
      for (int dy = -pr; dy <= pr; dy++) for (int dx = -pr; dx <= pr; dx++) {
          if (dx * dx + dy * dy > pr * pr) continue;
          int band = ((dy + pr) * 7 / (2 * pr + 1)) & 1, shade = 255 - (dx + pr) * 90 / (2 * pr + 1);
          uint32_t base = band ? RGB(214, 176, 120) : RGB(190, 140, 90);
          pset(c, px + dx, py + dy, mix(RGB(20, 12, 8), base, shade));
      }
      for (int a = 0; a < 512; a++) { int x = px + rx * icos(a) / 256, y = py + ry * isin(a) / 256; pset(c, x, y, RGB(236, 214, 170)); pset(c, x, y + 1, RGB(180, 160, 120)); }  /* the near half */
    }
    /* a small red planet on the other track */
    { int pr = w / 10 + 2, per = h + 4 * pr, py = (int)((t / 95 + (uint32_t)(h / 3 + side * 211)) % (uint32_t) per) - 2 * pr, px = side ? w / 4 : w * 3 / 4;
      for (int dy = -pr; dy <= pr; dy++) for (int dx = -pr; dx <= pr; dx++) if (dx * dx + dy * dy <= pr * pr)
          pset(c, px + dx, py + dy, mix(RGB(40, 8, 4), RGB(210, 90, 60), 256 - (dx + dy + 2 * pr) * 100 / (4 * pr + 1))); }
    /* a comet, now and then */
    { uint32_t cyc = t / 9000, ph = t % 9000;
      if (ph < 1400) { uint32_t r = hh(seed + cyc);
          int x0 = (int)(r % (uint32_t) w), dir = (r >> 8) & 1 ? 1 : -1, len = 20 * k;
          int hx = x0 + dir * (int) ph * w / 2800, hy = (int) ph * h / 2200;
          for (int i = 0; i < len; i++) blend(c, hx - dir * i * 2 / 3, hy - i, RGB(200, 230, 255), 255 - i * 255 / len);
          disc(c, hx, hy, k, RGB(255, 255, 255)); } }
    /* a rocket climbing, its flame flickering */
    { int per = h + 40 * k, ry = h - (int)((t / 40 + (uint32_t)(side * 97)) % (uint32_t) per), rx = w / 2 + isin((int)(t / 30)) * (w / 5) / 256;
      rect(c, rx - 2 * k, ry, 4 * k, 12 * k, RGB(230, 230, 240)); rect(c, rx - k, ry - 3 * k, 2 * k, 3 * k, RGB(230, 230, 240)); pset(c, rx, ry - 4 * k, RGB(230, 230, 240));
      rect(c, rx - 2 * k, ry + 3 * k, 4 * k, 2 * k, RGB(220, 40, 40)); disc(c, rx, ry + 7 * k, k, RGB(80, 160, 255));
      rect(c, rx - 4 * k, ry + 9 * k, 2 * k, 3 * k, RGB(220, 40, 40)); rect(c, rx + 2 * k, ry + 9 * k, 2 * k, 3 * k, RGB(220, 40, 40));
      int fl = 3 + (int)(hh(t / 60) % 4);
      for (int i = 0; i < fl * k; i++) { int wdt = (fl * k - i) / 2 + 1; for (int dx = -wdt; dx <= wdt; dx++) blend(c, rx + dx, ry + 12 * k + i, i < k * 2 ? RGB(255, 250, 200) : RGB(255, 140, 30), 230 - i * 160 / (fl * k)); } }
    /* a UFO hovering, lights chasing round it */
    { int ux = w / 2 + isin((int)(t / 17) + side * 400) * (w / 3) / 256, uy = h / 2 + isin((int)(t / 23)) * (h / 5) / 256, uw = 9 * k;
      if ((t / 5000) % 3 == 1) for (int i = 0; i < 20 * k; i++) { int wdt = 3 * k + i / 2; for (int dx = -wdt; dx <= wdt; dx++) blend(c, ux + dx, uy + 2 * k + i, RGB(150, 255, 210), 45 - i * 30 / (20 * k)); }
      for (int dy = -2 * k; dy <= 2 * k; dy++) for (int dx = -uw; dx <= uw; dx++) if (dx * dx * 4 * k * k + dy * dy * uw * uw <= 4 * k * k * uw * uw) pset(c, ux + dx, uy + dy, dy < 0 ? RGB(190, 196, 210) : RGB(120, 126, 140));
      for (int dy = -4 * k; dy < -k; dy++) for (int dx = -3 * k; dx <= 3 * k; dx++) if (dx * dx + (dy + k) * (dy + k) <= 9 * k * k) pset(c, ux + dx, uy + dy, RGB(120, 220, 240));
      for (int i = 0; i < 5; i++) { int on = (int)((t / 150 + (uint32_t) i) % 5) == 0; pset(c, ux - uw + 2 + i * (2 * uw - 4) / 4, uy, on ? RGB(255, 80, 80) : RGB(255, 220, 90)); } }
}
