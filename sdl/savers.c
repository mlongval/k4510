/* sdl/savers.c -- sidebar-savers: scenes for the space beside the picture.
 *
 * Doc, 2026-09-14, going to bed: "while i am sleeping can you make more
 * sidebar-savers -- a halloween themed one, a christmas themed one, a space
 * themed one, and ... something like frogger where a frog sprite jumps from
 * log to turtle to log to crocodile while those platforms flow down the
 * sidebar 'river' ... our frog (one per side) should never die nor fall off
 * the bottom nor reach the top ... and then one sidebarsaver of your choice,
 * a fantasy one by Claude for Claude.  make them so they really fill out the
 * side bar, dont be afraid."
 *
 * Each draws one sidebar, in machine pixels (sdl/main.c scales it to the
 * picture's pixel size), every frame: a sky or water filling it, and things
 * moving in it.  Sprites are pixel art from strings, drawn at 1-3x as the
 * sidebar is narrow or wide.  No libm: a sine table (Bhaskara's
 * approximation), and a hash for anything that must look random but stay put. */
#include "savers.h"
#include <string.h>

/* ---- the toolbox --------------------------------------------------------- */
typedef struct { uint32_t *px; int pitch, w, h; } cv_t;

static int sintab[1024];                          /* 256 sin, a full turn in 1024 */
static void sin_init(void)
{
    static int done; if (done) return; done = 1;
    for (int a = 0; a < 1024; a++) {
        int hh = a % 512; long long p = (long long) hh * (512 - hh);
        int r = (int)(4096LL * p / (5LL * 512 * 512 - 4 * p));
        sintab[a] = a < 512 ? r : -r;
    }
}
static int isin(int a) { return sintab[a & 1023]; }
static int icos(int a) { return sintab[(a + 256) & 1023]; }
static uint32_t hh(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static uint32_t RGB(int r, int g, int b) { return 0xFF000000u | (uint32_t) clampi(r, 0, 255) << 16 | (uint32_t) clampi(g, 0, 255) << 8 | (uint32_t) clampi(b, 0, 255); }
static uint32_t mix(uint32_t a, uint32_t b, int t)          /* t/256 of b over a */
{
    t = clampi(t, 0, 256);
    int r = (int)((a >> 16 & 255) * (256 - t) + (b >> 16 & 255) * t) >> 8;
    int g = (int)((a >> 8 & 255) * (256 - t) + (b >> 8 & 255) * t) >> 8;
    int bl = (int)((a & 255) * (256 - t) + (b & 255) * t) >> 8;
    return RGB(r, g, bl);
}
static void pset(cv_t *c, int x, int y, uint32_t v) { if ((unsigned) x < (unsigned) c->w && (unsigned) y < (unsigned) c->h) c->px[y * c->pitch + x] = v; }
static void blend(cv_t *c, int x, int y, uint32_t v, int t) { if ((unsigned) x < (unsigned) c->w && (unsigned) y < (unsigned) c->h) { uint32_t *p = &c->px[y * c->pitch + x]; *p = mix(*p, v, t); } }
static void rect(cv_t *c, int x, int y, int w, int h, uint32_t v)
{
    int x0 = clampi(x, 0, c->w), x1 = clampi(x + w, 0, c->w), y0 = clampi(y, 0, c->h), y1 = clampi(y + h, 0, c->h);
    for (int yy = y0; yy < y1; yy++) for (int xx = x0; xx < x1; xx++) c->px[yy * c->pitch + xx] = v;
}
static void rectb(cv_t *c, int x, int y, int w, int h, uint32_t v, int t)
{
    for (int yy = y; yy < y + h; yy++) for (int xx = x; xx < x + w; xx++) blend(c, xx, yy, v, t);
}
static void disc(cv_t *c, int cx, int cy, int r, uint32_t v)
{
    for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) if (dx * dx + dy * dy <= r * r + r) pset(c, cx + dx, cy + dy, v);
}
static void discb(cv_t *c, int cx, int cy, int r, uint32_t v, int t)
{
    for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) if (dx * dx + dy * dy <= r * r + r) blend(c, cx + dx, cy + dy, v, t);
}
static void glow(cv_t *c, int cx, int cy, int r, uint32_t v, int t)   /* a soft halo: stronger in the middle */
{
    for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) {
        int d2 = dx * dx + dy * dy; if (d2 > r * r) continue;
        blend(c, cx + dx, cy + dy, v, t * (r * r - d2) / (r * r));
    }
}
static void vgrad(cv_t *c, int y0, int y1, uint32_t a, uint32_t b)
{
    for (int y = clampi(y0, 0, c->h); y < clampi(y1, 0, c->h); y++) {
        uint32_t v = mix(a, b, y1 > y0 ? (y - y0) * 256 / (y1 - y0) : 0);
        for (int x = 0; x < c->w; x++) c->px[y * c->pitch + x] = v;
    }
}
static void line(cv_t *c, int x0, int y0, int x1, int y1, uint32_t v)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1, dy = -(y1 > y0 ? y1 - y0 : y0 - y1), sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (int n = 0; n < 4000; n++) {
        pset(c, x0, y0, v);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * e; if (e2 >= dy) { e += dy; x0 += sx; } if (e2 <= dx) { e += dx; y0 += sy; }
    }
}
static void lineb(cv_t *c, int x0, int y0, int x1, int y1, uint32_t v, int t)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1, dy = -(y1 > y0 ? y1 - y0 : y0 - y1), sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (int n = 0; n < 4000; n++) {
        blend(c, x0, y0, v, t);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * e; if (e2 >= dy) { e += dy; x0 += sx; } if (e2 <= dx) { e += dx; y0 += sy; }
    }
}
/* A sprite from strings: '.' is transparent, every other character a colour
 * from keys/cols; drawn k times over, mirrored with flip, blended with alpha
 * (256 = solid). */
static void spr(cv_t *c, int x, int y, const char *const *rows, int n, const char *keys, const uint32_t *cols, int k, int flip, int alpha)
{
    int w = (int) strlen(rows[0]);
    for (int r = 0; r < n; r++) for (int i = 0; i < w; i++) {
        char ch = rows[r][flip ? w - 1 - i : i]; if (ch == '.') continue;
        const char *p = strchr(keys, ch); if (!p) continue;
        uint32_t v = cols[p - keys];
        for (int dy = 0; dy < k; dy++) for (int dx = 0; dx < k; dx++) {
            if (alpha >= 256) pset(c, x + i * k + dx, y + r * k + dy, v); else blend(c, x + i * k + dx, y + r * k + dy, v, alpha);
        }
    }
}
static int scale_of(int w) { return w >= 200 ? 3 : w >= 100 ? 2 : 1; }
static void stars(cv_t *c, uint32_t seed, int n, int ymax, uint32_t t, uint32_t lit, uint32_t dim)
{
    for (int i = 0; i < n; i++) {
        uint32_t r = hh(seed + (uint32_t) i * 2654435761u);
        int x = (int)(r % (uint32_t) c->w), y = (int)((r >> 11) % (uint32_t)(ymax > 0 ? ymax : 1));
        int tw = isin((int)(t / 3 + (r >> 22)));
        pset(c, x, y, tw > 120 ? lit : dim);
        if (tw > 230 && (r & 3) == 0) { pset(c, x - 1, y, dim); pset(c, x + 1, y, dim); pset(c, x, y - 1, dim); pset(c, x, y + 1, dim); }
    }
}
static void branch(cv_t *c, int x, int y, int len, int ang, int depth, uint32_t v)
{
    int x2 = x + len * isin(ang) / 256, y2 = y - len * icos(ang) / 256;
    line(c, x, y, x2, y2, v);
    if (depth > 2) { line(c, x + 1, y, x2 + 1, y2, v); }
    if (depth <= 0 || len < 3) return;
    branch(c, x2, y2, len * 2 / 3, ang - 90 - depth * 8, depth - 1, v);
    branch(c, x2, y2, len * 3 / 4, ang + 70 + depth * 6, depth - 1, v);
}

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

static void s_halloween(cv_t *c, uint32_t t, int side)
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

/* ---- Christmas ------------------------------------------------------------- */
static const char *const tstar[] = { "....y....", "....y....", "...yyy...", "yyyyYyyyy", ".yyYYYyy.", "..yyYyy..", ".yy...yy.", "yy.....yy" };
static void s_christmas(cv_t *c, uint32_t t, int side)
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

/* ---- Space ----------------------------------------------------------------- */
static void s_space(cv_t *c, uint32_t t, int side)
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

/* ---- River (Frogger) ------------------------------------------------------- */
/* Lanes of logs, turtles and crocodiles float down; the frog rides one and,
 * whenever it has drifted too low, leaps to a platform further up -- the
 * next lane if it can -- so it never falls off the bottom, never reaches the
 * top, and never gets wet. */
enum { P_LOG, P_TURTLES, P_CROC };
#define LANES 6
#define PLATS 24
typedef struct { int n, v, cx, lw, per; int off[PLATS], len[PLATS], type[PLATS]; } lane_t;
static struct { int w, h, nl; lane_t lane[LANES]; int init, l, i, d, jumping; uint32_t j0; int fx0, fy0, tl, ti, td; } rv[2];

static int plat_y(lane_t *L, int i, uint32_t t, int k) { return (int)(((uint32_t) L->off[i] + t * (uint32_t) L->v / 1000) % (uint32_t) L->per) - 90 * k; }

static const char *const frog_sit[] = {
    ".g.......g.", "ggg.....ggg", ".gGGGGGGGg.", "..GkGGGkG..", "..GGGGGGG..", ".gGGyyyGGg.",
    "gg.GyyyG.gg", "g..GGGGG..g", "..gGG.GGg..", ".gg.....gg." };
static const char *const frog_jump[] = {
    "g.........g", ".g.......g.", "..gGGGGGg..", "..GkGGGkG..", "..GGGGGGG..", "..GGyyyGG..",
    "...GyyyG...", "..gGGGGGg..", ".g.......g.", "g.........g" };
static const char *const turtle_a[] = {
    ".....hh.....", "....hhhh....", "ff..SSSS..ff", ".fSSsSSsSSf.", "..SsSSSSsS..", "..SSSssSSS..",
    "..SsSSSSsS..", ".fSSsSSsSSf.", "ff..SSSS..ff", ".....tt....." };
static const char *const turtle_b[] = {
    ".....hh.....", "....hhhh....", "....SSSS....", "ffSSsSSsSSff", "..SsSSSSsS..", "..SSSssSSS..",
    "..SsSSSSsS..", "ffSSsSSsSSff", "....SSSS....", ".....tt....." };

static void river_build(int side, int w, int h, int k)
{
    int bank = 3 * k, nl = clampi((w - 2 * bank) / (15 * k), 2, LANES);
    rv[side].w = w; rv[side].h = h; rv[side].nl = nl; rv[side].init = 0;
    for (int l = 0; l < nl; l++) {
        lane_t *L = &rv[side].lane[l];
        L->lw = (w - 2 * bank) / nl; L->cx = bank + L->lw * l + L->lw / 2;
        L->v = 12 + 5 * (int)((l * 7 + side * 3) % 4);
        L->per = h + 180 * k; L->n = 0;
        uint32_t s = hh(0x7109u + (uint32_t)(side * 31 + l));
        int y = (int)(s % 40);
        while (L->n < PLATS) {
            uint32_t r = hh(s + (uint32_t) L->n * 977u);
            int type = (int)(r % 3), len = type == P_LOG ? (34 + (int)(r >> 8) % 30) * k : type == P_TURTLES ? (2 + (int)((r >> 9) & 1)) * 11 * k : 52 * k;
            if (y + len > L->per - 10 * k) break;
            L->off[L->n] = y; L->len[L->n] = len; L->type[L->n] = type; L->n++;
            y += len + (6 + (int)((r >> 12) % 14)) * k;     /* short gaps: there is always somewhere to land */
        }
    }
}
static void draw_plat(cv_t *c, lane_t *L, int i, int y, uint32_t t, int k)
{
    int len = L->len[i], cx = L->cx, wdt = L->lw * 3 / 5;
    if (L->type[i] == P_LOG) {
        rect(c, cx - wdt / 2, y + 2 * k, wdt, len - 4 * k, RGB(120, 72, 32));
        for (int s = -wdt / 2 + k; s < wdt / 2; s += 3 * k) rect(c, cx + s, y + 3 * k, k, len - 6 * k, RGB(96, 56, 24));
        rect(c, cx - wdt / 2, y + 2 * k, k, len - 4 * k, RGB(150, 96, 48));
        disc(c, cx, y + 2 * k, wdt / 2, RGB(200, 160, 100)); disc(c, cx, y + 2 * k, wdt / 3, RGB(170, 130, 80)); disc(c, cx, y + 2 * k, wdt / 6, RGB(200, 160, 100));
        disc(c, cx, y + len - 2 * k, wdt / 2, RGB(112, 66, 30));
        disc(c, cx + wdt / 5, y + len / 2, k + 1, RGB(80, 46, 20));
    } else if (L->type[i] == P_TURTLES) {
        static const char keys[] = "hfSst"; const uint32_t cols[] = { RGB(90, 170, 70), RGB(70, 150, 60), RGB(40, 110, 50), RGB(150, 190, 80), RGB(70, 150, 60) };
        for (int j = 0; j * 11 * k < len; j++)
            spr(c, cx - 6 * k, y + j * 11 * k, ((t / 300 + (uint32_t)(i + j)) & 1) ? turtle_a : turtle_b, 10, keys, cols, k, 0, 256);
    } else {                                          /* a crocodile, head down-river, snapping */
        int bw = wdt * 2 / 3, open = ((t / 700 + (uint32_t) i) % 3) == 0;
        for (int yy = 0; yy < len; yy++) {
            int half = yy < len / 4 ? 1 + bw / 2 * yy / (len / 4) : yy > len - len / 5 ? bw / 2 - (yy - (len - len / 5)) * bw / (2 * (len / 5) + 1) / 2 : bw / 2;
            for (int dx = -half; dx <= half; dx++) pset(c, cx + dx, y + yy, dx < -half / 2 ? RGB(60, 120, 50) : RGB(40, 96, 40));
            if (yy % (3 * k) == 0 && yy > len / 5 && yy < len - len / 4) rect(c, cx - k / 2, y + yy, k, k, RGB(90, 150, 60));
        }
        rect(c, cx - bw / 2 - 2 * k, y + len / 3, 2 * k, 3 * k, RGB(40, 96, 40)); rect(c, cx + bw / 2, y + len / 3, 2 * k, 3 * k, RGB(40, 96, 40));
        rect(c, cx - bw / 2 - 2 * k, y + len * 2 / 3, 2 * k, 3 * k, RGB(40, 96, 40)); rect(c, cx + bw / 2, y + len * 2 / 3, 2 * k, 3 * k, RGB(40, 96, 40));
        int hy = y + len - len / 5;
        rect(c, cx - bw / 2 + k, hy - k, k, k, RGB(240, 220, 60)); rect(c, cx + bw / 2 - 2 * k, hy - k, k, k, RGB(240, 220, 60));
        if (open) { rect(c, cx - bw / 4, hy + 2 * k, bw / 2, len / 5, RGB(180, 40, 40)); for (int yy = hy + 2 * k; yy < y + len; yy += 2 * k) { pset(c, cx - bw / 4, yy, RGB(250, 250, 240)); pset(c, cx + bw / 4, yy, RGB(250, 250, 240)); } }
    }
}
static void s_river(cv_t *c, uint32_t t, int side)
{
    int w = c->w, h = c->h, k = scale_of(w);
    if (rv[side].w != w || rv[side].h != h) river_build(side, w, h, k);
    int bank = 3 * k, nl = rv[side].nl;
    /* the water, and ripples flowing with it */
    rect(c, 0, 0, w, h, RGB(24, 76, 150));
    for (int i = 0; i < w * h / 90; i++) {
        uint32_t r = hh(0x3A7Eu + (uint32_t) side * 999u + (uint32_t) i);
        int sp = 18 + (int)(r % 20), y = (int)(((r >> 6) % (uint32_t) h + t * (uint32_t) sp / 1000) % (uint32_t) h), x = (int)((r >> 14) % (uint32_t) w);
        int len = 2 + (int)(r >> 28) % 4;
        for (int j = 0; j < len; j++) pset(c, x, y + j, (r & 3) ? RGB(60, 120, 196) : RGB(120, 176, 230));
    }
    /* the banks, reeds sliding past */
    for (int s2 = 0; s2 < 2; s2++) {
        int x0 = s2 ? w - bank : 0;
        rect(c, x0, 0, bank, h, RGB(46, 120, 44)); rect(c, s2 ? x0 : bank - 1, 0, 1, h, RGB(20, 70, 24));
        for (int y = (int)((t * 16 / 1000) % (uint32_t)(12 * k)) - 12 * k; y < h; y += 12 * k) { rect(c, x0 + k / 2, y, k, 4 * k, RGB(120, 170, 60)); pset(c, x0 + k / 2, y - 1, RGB(160, 110, 50)); }
    }
    /* the platforms */
    for (int l = 0; l < nl; l++) {
        lane_t *L = &rv[side].lane[l];
        for (int i = 0; i < L->n; i++) { int y = plat_y(L, i, t, k); if (y < h && y + L->len[i] > 0) draw_plat(c, L, i, y, t, k); }
    }
    /* the frog */
    int fh = 10 * k, lo = h * 3 / 5, zone0 = h * 3 / 10, zone1 = h * 9 / 20, aim = h * 3 / 8;
    if (!rv[side].init) {                             /* start on whatever is nearest the middle */
        int best = 1 << 30;
        for (int l = 0; l < nl; l++) { lane_t *L = &rv[side].lane[l];
            for (int i = 0; i < L->n; i++) { int y = plat_y(L, i, t, k), a = y + 2 * k, b = y + L->len[i] - fh - 2 * k;
                if (b < a) continue;
                int p = clampi(aim, a, b), s = (p > aim ? p - aim : aim - p) + (y < -80 * k ? 100000 : 0);
                if (s < best) { best = s; rv[side].l = l; rv[side].i = i; rv[side].d = p - y; } } }
        rv[side].init = 1; rv[side].jumping = 0;
    }
    lane_t *L = &rv[side].lane[rv[side].l];
    int fx, fy, frame = 0;
    if (!rv[side].jumping) {
        fx = L->cx; fy = plat_y(L, rv[side].i, t, k) + rv[side].d;
        if (fy > lo) {                               /* too low: find somewhere up the river */
            int best = 1 << 30, bl = -1, bi = 0, bd = 0;
            for (int l = 0; l < nl; l++) { lane_t *M = &rv[side].lane[l];
                for (int i = 0; i < M->n; i++) {
                    if (l == rv[side].l && i == rv[side].i) continue;
                    int y = plat_y(M, i, t, k), a = clampi(y + 2 * k, zone0, zone1), b = clampi(y + M->len[i] - fh - 2 * k, zone0, zone1);
                    if (y + 2 * k > zone1 || y + M->len[i] - fh - 2 * k < zone0) continue;
                    int p = clampi(aim, a, b), dl = l > rv[side].l ? l - rv[side].l : rv[side].l - l;
                    int s = (p > aim ? p - aim : aim - p) + (dl == 1 ? 0 : dl == 0 ? 60 : 30 * dl);
                    if (s < best) { best = s; bl = l; bi = i; bd = p - y; } } }
            if (bl >= 0) { rv[side].jumping = 1; rv[side].j0 = t; rv[side].fx0 = fx; rv[side].fy0 = fy; rv[side].tl = bl; rv[side].ti = bi; rv[side].td = bd; }
            else if (rv[side].d > 2 * k) rv[side].d -= k;   /* nothing in reach: hop up the one it is on */
        }
    }
    if (rv[side].jumping) {
        lane_t *T = &rv[side].lane[rv[side].tl];
        int tx = T->cx, ty = plat_y(T, rv[side].ti, t, k) + rv[side].td, dur = 520, e = (int)(t - rv[side].j0);
        if (e >= dur) { rv[side].jumping = 0; rv[side].l = rv[side].tl; rv[side].i = rv[side].ti; rv[side].d = rv[side].td; fx = tx; fy = ty; }
        else {
            fx = rv[side].fx0 + (tx - rv[side].fx0) * e / dur; fy = rv[side].fy0 + (ty - rv[side].fy0) * e / dur;
            int hop = isin(e * 512 / dur) * 6 * k / 256;
            discb(c, fx, fy + fh / 2 + hop, 4 * k, RGB(0, 20, 50), 110);   /* its shadow on the water */
            fy -= hop; frame = 1;
        }
    }
    { static const char keys[] = "gGky"; const uint32_t cols[] = { RGB(40, 150, 40), RGB(80, 200, 60), RGB(10, 10, 10), RGB(230, 230, 110) };
      spr(c, fx - 5 * k, fy, frame ? frog_jump : frog_sit, 10, keys, cols, k, 0, 256); }
}

/* ---- Dreamfall: Claude's own ------------------------------------------------ */
/* A floating island at dusk, a waterfall pouring off its edge the whole
 * height of the sidebar into mist and a rainbow, aurora overhead, paper
 * lanterns rising, fireflies, and now and then a small dragon gliding by.
 * What I would like to have beside me while I read. */
static void s_dreamfall(cv_t *c, uint32_t t, int side)
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
            uint32_t r = hh((uint32_t)((dx + 50) * 7919) + (uint32_t)((y - (int)(t / 4)) / (2 * k)) * 104729u);
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


/* ---- Tetris ---------------------------------------------------------------- */
/* Doc: "a colorful endless tetris style sidebar save".  A well ten wide and
 * as tall as the sidebar, the seven pieces in their colours, played by a
 * small AI that scores every rotation and column (height, holes, bumpiness,
 * lines) and now and then drops one somewhere random, so the stack rises
 * and falls; full lines flash and go, and a stack that reaches the top turns
 * grey row by row and a new game starts. */
#define TCOLS 10
#define TROWS 220
static const int8_t tbase[7][4][2] = {
    { {0,1},{1,1},{2,1},{3,1} }, { {1,0},{2,0},{1,1},{2,1} }, { {1,0},{0,1},{1,1},{2,1} },
    { {1,0},{2,0},{0,1},{1,1} }, { {0,0},{1,0},{1,1},{2,1} }, { {0,0},{0,1},{1,1},{2,1} }, { {2,0},{0,1},{1,1},{2,1} } };
static const uint32_t tcol[8] = { 0, 0xFF00DCFFu, 0xFFFFDC00u, 0xFFAA46FFu, 0xFF3CDC50u, 0xFFFF3C3Cu, 0xFF3C6EFFu, 0xFFFF961Eu };
static int8_t trot[7][4][4][2];
static void tetris_init_shapes(void)
{
    static int done; if (done) return; done = 1;
    for (int p = 0; p < 7; p++) for (int r = 0; r < 4; r++) {
        int mx = 99, my = 99, xs[4], ys[4];
        for (int i = 0; i < 4; i++) {
            int x = tbase[p][i][0], y = tbase[p][i][1];
            for (int k = 0; k < r; k++) { int nx = -y, ny = x; x = nx; y = ny; }
            xs[i] = x; ys[i] = y; if (x < mx) mx = x; if (y < my) my = y;
        }
        for (int i = 0; i < 4; i++) { trot[p][r][i][0] = (int8_t)(xs[i] - mx); trot[p][r][i][1] = (int8_t)(ys[i] - my); }
    }
}
typedef struct {
    int w, h, rows, cell, ox, oy, piece, rot, x, y, tr, tx, next, clearing, over, overrow;
    uint32_t last, t0, rng; uint8_t b[TROWS][TCOLS], full[TROWS];
} tz_t;
static tz_t tz[2];
static uint32_t trand(tz_t *z) { z->rng = z->rng * 1664525u + 1013904223u; return z->rng >> 8; }
static int tfits(tz_t *z, int p, int r, int x, int y)
{
    for (int i = 0; i < 4; i++) {
        int cx = x + trot[p][r][i][0], cy = y + trot[p][r][i][1];
        if (cx < 0 || cx >= TCOLS || cy >= z->rows) return 0;
        if (cy >= 0 && z->b[cy][cx]) return 0;
    }
    return 1;
}
static int tdrop(tz_t *z, int p, int r, int x) { int y = 0; if (!tfits(z, p, r, x, 0)) return -1; while (tfits(z, p, r, x, y + 1)) y++; return y; }
static int tscore(tz_t *z, int p, int r, int x)          /* the board after dropping it there, judged */
{
    int y = tdrop(z, p, r, x); if (y < 0) return -1000000;
    static uint8_t b[TROWS][TCOLS]; memcpy(b, z->b, sizeof b);
    for (int i = 0; i < 4; i++) { int cy = y + trot[p][r][i][1]; if (cy < 0) return -1000000; b[cy][x + trot[p][r][i][0]] = 1; }
    int lines = 0, hts[TCOLS], holes = 0, agg = 0, bump = 0;
    for (int yy = 0; yy < z->rows; yy++) { int f = 1; for (int c = 0; c < TCOLS; c++) if (!b[yy][c]) { f = 0; break; } lines += f; }
    for (int c = 0; c < TCOLS; c++) {
        int top = z->rows; for (int yy = 0; yy < z->rows; yy++) if (b[yy][c]) { top = yy; break; }
        hts[c] = z->rows - top; agg += hts[c];
        for (int yy = top + 1; yy < z->rows; yy++) if (!b[yy][c]) holes++;
    }
    for (int c = 0; c + 1 < TCOLS; c++) bump += hts[c] > hts[c + 1] ? hts[c] - hts[c + 1] : hts[c + 1] - hts[c];
    return -51 * agg + 76 * lines * 10 - 36 * holes * 10 - 18 * bump;
}
static void tspawn(tz_t *z)
{
    z->piece = z->next; z->next = (int)(trand(z) % 7); z->rot = 0; z->x = TCOLS / 2 - 2; z->y = 0;
    if (!tfits(z, z->piece, 0, z->x, 0)) { z->over = 1; z->overrow = z->rows; z->piece = -1; return; }
    int best = -2000000, br = 0, bx = z->x, rnd = (trand(z) % 7) == 0;   /* now and then, somewhere random */
    for (int r = 0; r < 4; r++) for (int x = -3; x < TCOLS; x++) {
        if (!tfits(z, z->piece, r, x, 0)) continue;
        int s = rnd ? (int)(trand(z) % 1000) : tscore(z, z->piece, r, x);
        if (s > best) { best = s; br = r; bx = x; }
    }
    z->tr = br; z->tx = bx;
}
static void tstep(tz_t *z)
{
    if (z->over) {                                         /* grey from the bottom, then a new game */
        if (z->overrow > 0) { z->overrow--; for (int c = 0; c < TCOLS; c++) if (z->b[z->overrow][c]) z->b[z->overrow][c] = 8; }
        else { memset(z->b, 0, sizeof z->b); z->over = 0; tspawn(z); }
        return;
    }
    if (z->clearing) {
        if (--z->clearing == 0) {
            int dst = z->rows - 1;
            for (int y = z->rows - 1; y >= 0; y--) if (!z->full[y]) { if (dst != y) memcpy(z->b[dst], z->b[y], TCOLS); dst--; }
            for (; dst >= 0; dst--) memset(z->b[dst], 0, TCOLS);
            memset(z->full, 0, sizeof z->full); tspawn(z);
        }
        return;
    }
    if (z->piece < 0) { tspawn(z); return; }
    if (z->rot != z->tr && tfits(z, z->piece, (z->rot + 1) & 3, z->x, z->y)) { z->rot = (z->rot + 1) & 3; return; }
    if (z->x != z->tx) { int nx = z->x + (z->tx > z->x ? 1 : -1); if (tfits(z, z->piece, z->rot, nx, z->y)) { z->x = nx; return; } }
    if (tfits(z, z->piece, z->rot, z->x, z->y + 1)) { z->y++; return; }
    for (int i = 0; i < 4; i++) { int cy = z->y + trot[z->piece][z->rot][i][1]; if (cy >= 0) z->b[cy][z->x + trot[z->piece][z->rot][i][0]] = (uint8_t)(z->piece + 1); }
    int any = 0;
    for (int y = 0; y < z->rows; y++) { int f = 1; for (int c = 0; c < TCOLS; c++) if (!z->b[y][c]) { f = 0; break; } z->full[y] = (uint8_t) f; any |= f; }
    z->piece = -1;
    if (any) z->clearing = 8; else tspawn(z);
}
static void tcell(cv_t *c, int x, int y, int s, uint32_t col, int alpha)
{
    if (alpha < 256) { rectb(c, x, y, s, s, col, alpha); return; }
    rect(c, x, y, s, s, col);
    rect(c, x, y, s, 1, mix(col, 0xFFFFFFFFu, 110)); rect(c, x, y, 1, s, mix(col, 0xFFFFFFFFu, 110));
    rect(c, x, y + s - 1, s, 1, mix(col, 0xFF000000u, 120)); rect(c, x + s - 1, y, 1, s, mix(col, 0xFF000000u, 120));
    if (s >= 6) pset(c, x + 2, y + 2, mix(col, 0xFFFFFFFFu, 170));
}
static void s_tetris(cv_t *c, uint32_t t, int side)
{
    int w = c->w, h = c->h;
    tz_t *z = &tz[side];
    tetris_init_shapes();
    if (z->w != w || z->h != h) {
        memset(z, 0, sizeof *z); z->w = w; z->h = h;
        z->cell = (w - 6) / TCOLS; if (z->cell < 3) z->cell = 3;
        z->rows = (h - 6) / z->cell; if (z->rows > TROWS) z->rows = TROWS;
        z->ox = (w - TCOLS * z->cell) / 2; z->oy = h - 3 - z->rows * z->cell;
        z->rng = 0x7E7215u + (uint32_t) side * 977u; z->next = (int)(trand(z) % 7); z->piece = -1; z->last = t;
    }
    if (t - z->last > 2000) z->last = t;                 /* after a pause: carry on, not catch up */
    for (int n = 0; t - z->last >= 35 && n < 60; n++) { tstep(z); z->last += 35; }
    /* the backdrop: deep blue, colours drifting behind the well */
    vgrad(c, 0, h, RGB(8, 8, 28), RGB(24, 10, 40));
    for (int i = 0; i < 4; i++) glow(c, w / 2 + isin((int)(t / 30) + i * 256) * w / 3 / 256, h * (i + 1) / 5 + isin((int)(t / 41) + i * 180) * h / 12 / 256, w / 2, tcol[1 + (i * 2 + (int)(t / 6000)) % 7], 40);
    int cs = z->cell, wx = z->ox, wy = z->oy;
    rect(c, wx - 3, wy - 3, TCOLS * cs + 6, z->rows * cs + 6, RGB(90, 100, 140));
    rect(c, wx - 2, wy - 2, TCOLS * cs + 4, z->rows * cs + 4, RGB(40, 44, 70));
    rect(c, wx, wy, TCOLS * cs, z->rows * cs, RGB(6, 6, 18));
    for (int x = 1; x < TCOLS; x++) rect(c, wx + x * cs, wy, 1, z->rows * cs, RGB(16, 16, 36));
    for (int y = 1; y < z->rows; y++) rect(c, wx, wy + y * cs, TCOLS * cs, 1, RGB(16, 16, 36));
    for (int y = 0; y < z->rows; y++) for (int x = 0; x < TCOLS; x++) {
        uint8_t v = z->b[y][x]; if (!v) continue;
        uint32_t col = v == 8 ? RGB(110, 110, 120) : tcol[v];
        if (z->clearing && z->full[y]) col = (z->clearing & 2) ? 0xFFFFFFFFu : mix(col, 0xFFFFFFFFu, 160);
        tcell(c, wx + x * cs, wy + y * cs, cs, col, 256);
    }
    if (z->piece >= 0 && !z->clearing && !z->over) {
        int gy = z->y; while (tfits(z, z->piece, z->rot, z->x, gy + 1)) gy++;
        for (int i = 0; i < 4; i++) {                     /* the ghost, where it will land */
            int cx = z->x + trot[z->piece][z->rot][i][0], cy = gy + trot[z->piece][z->rot][i][1];
            if (cy >= 0) tcell(c, wx + cx * cs, wy + cy * cs, cs, tcol[z->piece + 1], 50);
        }
        for (int i = 0; i < 4; i++) {
            int cx = z->x + trot[z->piece][z->rot][i][0], cy = z->y + trot[z->piece][z->rot][i][1];
            if (cy >= 0) tcell(c, wx + cx * cs, wy + cy * cs, cs, tcol[z->piece + 1], 256);
        }
    }
}

void saver_draw(int which, uint32_t *px, int pitch, int w, int h, uint32_t ms, int side)
{
    cv_t c = { px, pitch, w, h };
    sin_init();
    if (w < 4 || h < 4) return;
    switch (which) {
    case SAVER_HALLOWEEN: s_halloween(&c, ms, side & 1); break;
    case SAVER_CHRISTMAS: s_christmas(&c, ms, side & 1); break;
    case SAVER_SPACE:     s_space(&c, ms, side & 1); break;
    case SAVER_RIVER:     s_river(&c, ms, side & 1); break;
    case SAVER_TETRIS:    s_tetris(&c, ms, side & 1); break;
    default:              s_dreamfall(&c, ms, side & 1); break;
    }
}
