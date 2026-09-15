/* sdl/sidebars/river.c -- one of the sidebars' scenes (docs/SIDEBARS-PLAN.md);
 * until 2026-09-15 a part of sdl/savers.c. */
#include "canvas.h"

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
void s_river(cv_t *c, uint32_t t, int side)
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
