/* sdl/sidebars/tetris.c -- one of the sidebars' scenes (docs/SIDEBARS-PLAN.md);
 * until 2026-09-15 a part of sdl/savers.c. */
#include "canvas.h"

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
void s_tetris(cv_t *c, uint32_t t, int side)
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
    for (int n = 0; t - z->last >= 50 && n < 60; n++) { tstep(z); z->last += 50; }   /* a step every 50 ms (Doc: 30% slower than 35) */
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
