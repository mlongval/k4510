/* sdl/sidebars/canvas.h -- the sidebars' toolbox: a canvas of machine
 * pixels and what draws on it.  Every scene, sdl/sidebars/NAME.c, includes it;
 * sdl/savers.c picks the scene (docs/SIDEBARS-PLAN.md, step 3: one file each).
 * The functions are static, so each scene has its own copy and the compiler
 * inlines what it likes; the sine table is the one thing shared (canvas.c). */
#ifndef K4510_SIDEBARS_CANVAS_H
#define K4510_SIDEBARS_CANVAS_H
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#ifdef AF_DEBUG
#include <stdio.h>
#endif

/* ---- the toolbox --------------------------------------------------------- */
typedef struct { uint32_t *px; int pitch, w, h; } cv_t;

extern int sb_sintab[1024];                       /* 256 sin, a full turn in 1024 (canvas.c) */
void sb_sin_init(void);
static int isin(int a) { return sb_sintab[a & 1023]; }
static int icos(int a) { return sb_sintab[(a + 256) & 1023]; }
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

/* the scenes, one file each */
void s_halloween(cv_t *c, uint32_t t, int side);
void s_christmas(cv_t *c, uint32_t t, int side);
void s_space(cv_t *c, uint32_t t, int side);
void s_river(cv_t *c, uint32_t t, int side);
void s_dreamfall(cv_t *c, uint32_t t, int side);
void s_tetris(cv_t *c, uint32_t t, int side);
void s_antfarm(cv_t *c, uint32_t t, int side);
/* the ant farm's option and its colony across a power cycle (sdl/savers.c) */
void antfarm_option(const char *key, const char *value);
size_t antfarm_state(uint8_t **buf);
void antfarm_restore(const uint8_t *buf, size_t n);
#endif
