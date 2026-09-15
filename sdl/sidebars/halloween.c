/* sdl/sidebars/halloween.c -- one of the sidebars' scenes (docs/SIDEBARS-PLAN.md);
 * until 2026-09-15 a part of sdl/savers.c. */
#include "canvas.h"

/* ---- Halloween ----------------------------------------------------------- */
static const char *const bat_a[] = { "kk.......kk", ".kkk.k.kkk.", "..kkrkrkk..", "...kkkkk...", "....k.k...." };
static const char *const bat_b[] = { "...........", "....k.k....", "..kkrkrkk..", ".kkkkkkkkk.", "kk..k.k..kk" };
static const char *const ghost_a[] = {
    "....wwww....", "..wwwwwwww..", ".wwwwwwwwww.", ".wwkkwwkkww.", "wwwkkwwkkwww", "wwwwwwwwwwww",
    "wwwwwkkwwwww", "wwwwkkkkwwww", "wwwwwkkwwwww", "wwwwwwwwwwww", "wwwwwwwwwwww", "ww.www.www.w", "w...w...w..." };
static const char *const ghost_b[] = {
    "....wwww....", "..wwwwwwww..", ".wwwwwwwwww.", ".wwkkwwkkww.", "wwwkkwwkkwww", "wwwwwwwwwwww",
    "wwwwwkkwwwww", "wwwwkkkkwwww", "wwwwwkkwwwww", "wwwwwwwwwwww", "wwwwwwwwwwww", "w.www.www.ww", "..w...w...ww" };
static const char *const pumpkin[] = {
    ".....g.....", "....gg.....", "..ooOoOoo..", ".oOoooooOo.", "oOyyoOoyyOo", "oOyyoOoyyOo",
    "oOooOyOooOo", "oOyoyyyoyOo", ".oOyyyyyOo.", "..ooOoOoo.." };
static const char *const spider[] = { "k.k.k.k", ".kkkkk.", "kkrkrkk", ".kkkkk.", "k.k.k.k" };

void s_halloween(cv_t *c, uint32_t t, int side)
{
    int w = c->w, h = c->h, k = scale_of(w);
    uint32_t seed = 0x4A11u + (uint32_t) side * 7919u;
    vgrad(c, 0, h * 2 / 3, RGB(14, 4, 30), RGB(60, 18, 58));
    vgrad(c, h * 2 / 3, h, RGB(60, 18, 58), RGB(130, 50, 18));
    stars(c, seed, w * h / 700, h * 2 / 3, t, RGB(220, 210, 240), RGB(90, 70, 120));
    /* the moon, its glow, its craters, and clouds drifting over it */
    int mr = w / 4 + 3, mx = side ? w * 2 / 5 : w * 3 / 5, my = h / 9 + mr / 2;
    glow(c, mx, my, mr * 2, RGB(255, 190, 110), 110);
    disc(c, mx, my, mr, RGB(246, 234, 182));
    disc(c, mx - mr / 3, my - mr / 4, mr / 5, RGB(222, 206, 150));
    disc(c, mx + mr / 4, my + mr / 3, mr / 6, RGB(226, 210, 156));
    disc(c, mx + mr / 3, my - mr / 3, mr / 9 + 1, RGB(222, 206, 150));
    for (int i = 0; i < 4; i++) {
        int cy = my - mr + i * mr * 2 / 3, span = w + 60, len = 30 + i * 9;
        int cx = (int)((t / (uint32_t)(70 + i * 23) + (uint32_t) i * 97) % (uint32_t) span) - 40;
        for (int dx = 0; dx < len; dx++) { int th = 2 + (isin(dx * 40) + 256) * 2 / 512; rectb(c, cx + dx, cy + (dx / 9) % 2, 1, th, RGB(25, 12, 38), 170); }
    }
    /* a spider on its thread, from the top */
    int sx = side ? w - w / 6 : w / 6, sy = h / 5 + (isin((int)(t / 14)) + 256) * (h / 6) / 512;
    line(c, sx, 0, sx, sy, RGB(170, 170, 185));
    { static const char keys[] = "kr"; const uint32_t cols[] = { RGB(10, 8, 12), RGB(230, 40, 30) };
      spr(c, sx - 3 * k, sy, spider, 5, keys, cols, k, 0, 256); }
    /* ghosts drifting up */
    for (int i = 0; i < 3; i++) {
        int period = h + 60 * k, gy = h - (int)((t / (uint32_t)(38 + i * 15) + (uint32_t)(i * h / 3)) % (uint32_t) period);
        int gx = w / 2 - 6 * k + isin((int)(t / 9) + i * 330) * (w / 3) / 256;
        static const char keys[] = "wk"; const uint32_t cols[] = { RGB(236, 240, 255), RGB(30, 20, 50) };
        spr(c, gx, gy, (t / 400 + (uint32_t) i) & 1 ? ghost_a : ghost_b, 13, keys, cols, k, i & 1, 190);
    }
    /* bats */
    int nb = h / 110 + 3;
    for (int i = 0; i < nb; i++) {
        uint32_t r = hh(seed + 1000u + (uint32_t) i);
        int sp = 22 + (int)(r % 30), per = h + 40;
        int by = (int)((r >> 8) % (uint32_t) per) - (int)((t * (uint32_t) sp / 1000) % (uint32_t) per);
        if (by < -20) by += per;
        int bx = w / 2 - 5 * k + isin((int)(t * (uint32_t)(3 + i % 3) / 20 + r)) * (w / 2 - 7 * k) / 256;
        static const char keys[] = "kr"; const uint32_t cols[] = { RGB(12, 6, 16), RGB(240, 60, 40) };
        spr(c, bx, by, ((t / 110 + (uint32_t) i) & 1) ? bat_a : bat_b, 5, keys, cols, k, 0, 256);
    }
    /* the ground: a black hill, a bare tree, tombstones, jack-o'-lanterns */
    int g0 = h - h / 6;
    for (int x = 0; x < w; x++) { int top = g0 + isin(x * 9 + side * 300) * (h / 50) / 256; rect(c, x, top, 1, h - top, RGB(8, 4, 10)); }
    int tx = side ? w / 4 : w * 3 / 4;
    branch(c, tx, g0 + 2, h / 9, side ? 20 : -20, 5, RGB(8, 4, 10));
    for (int i = 0; i < 2; i++) {
        int bx = (side ? w * 3 / 5 : w / 6) + i * 14 * k, bw = 8 * k, bh = 11 * k, by = g0 - bh + 3 * k + i * 2 * k;
        rect(c, bx, by + 3 * k, bw, bh - 3 * k, RGB(96, 96, 110)); disc(c, bx + bw / 2, by + 3 * k, bw / 2, RGB(96, 96, 110));
        rect(c, bx + bw / 2 - k / 2, by + 3 * k, k, 5 * k, RGB(60, 60, 70)); rect(c, bx + bw / 2 - 2 * k, by + 4 * k, 4 * k, k, RGB(60, 60, 70));
    }
    for (int i = 0; i < 3; i++) {
        int px = (i * w / 3) + (side ? 4 : 2) * k, py = h - 10 * k - (i == 1 ? 4 * k : k);
        int fl = (int)(hh((t / 90) * 3u + (uint32_t) i + (uint32_t) side * 17u) & 255);
        uint32_t lit = mix(RGB(255, 230, 90), RGB(255, 140, 20), fl);
        glow(c, px + 5 * k, py + 6 * k, 9 * k, RGB(255, 150, 40), 70 + fl / 6);
        static const char keys[] = "goOy"; const uint32_t cols[] = { RGB(40, 120, 30), RGB(236, 110, 20), RGB(190, 80, 10), lit };
        spr(c, px, py, pumpkin, 10, keys, cols, k, i & 1, 256);
    }
}
