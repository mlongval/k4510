/* sdl/sidebars/antfarm.c -- one of the sidebars' scenes (docs/SIDEBARS-PLAN.md);
 * until 2026-09-15 a part of sdl/savers.c. */
#include "canvas.h"

/* ---- Ant farm -------------------------------------------------------------- */
/* Doc's brainshot, 2026-09-15: "antfarm sidebars?".  A cross-section behind
 * glass: sky and grass, a sand mound over the entrance, and soil in layers.
 * The tunnels are remembered, so the colony grows: ants wander their tunnels,
 * dig into the soil at the ends (down and sideways, rarely up), carry each
 * grain to the surface by the shortest way, and go back down.  Now and then a
 * chamber opens; the first deep one is the queen's, with her eggs, and one
 * becomes the store the ants carry crumbs down to.  When the soil is two-fifths
 * dug the tunnels fill back in with sand and a new colony starts. */
#define AG_COLS 128
#define AG_ROWS 280
#define ANTS 16
enum { AT_SOIL, AT_TUNNEL, AT_CHAMBER, AT_QUEEN, AT_STORE };
typedef struct { int x, y, px, py, st, dir, pref, fails, stuck, attop; } ant_t;   /* st 5: digging a second way in */   /* cell, previous cell, state, the way it likes to dig */
typedef struct {
    int w, h, g, cols, rows, surf, entry, nants, dug, soil, chambers, queen_x, queen_y, store_x, store_y, mound, refill, stepn, food, lastdig, markdug;
    int entry2, mound2;                                          /* the second way in, once the colony has grown (Doc, 2026-09-15) */
    uint32_t last, rng; uint8_t cell[AG_ROWS][AG_COLS]; uint16_t dist[AG_ROWS][AG_COLS], sdist[AG_ROWS][AG_COLS]; ant_t ant[ANTS];
} af_t;
static af_t af[2];
static uint32_t arand(af_t *a) { a->rng = a->rng * 1103515245u + 12345u; return a->rng >> 9; }
static int open_cell(af_t *a, int x, int y) { return x >= 0 && x < a->cols && y >= a->surf && y < a->rows && a->cell[y][x] != AT_SOIL; }
static void af_bfs(af_t *a, uint16_t d[AG_ROWS][AG_COLS], int from_store)   /* steps, through the tunnels */
{
    static int qx[AG_ROWS * AG_COLS], qy[AG_ROWS * AG_COLS];
    int h = 0, n = 0;
    for (int y = 0; y < a->rows; y++) for (int x = 0; x < a->cols; x++) d[y][x] = 0xFFFF;
    if (from_store) { if (a->store_x < 0) return; d[a->store_y][a->store_x] = 0; qx[n] = a->store_x; qy[n] = a->store_y; n++; }
    else for (int x = 0; x < a->cols; x++) if (open_cell(a, x, a->surf)) { d[a->surf][x] = 0; qx[n] = x; qy[n] = a->surf; n++; }
    while (h < n) {
        int x = qx[h], y = qy[h]; h++;
        static const int dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };
        for (int k = 0; k < 4; k++) { int nx = x + dx[k], ny = y + dy[k];
            if (open_cell(a, nx, ny) && d[ny][nx] == 0xFFFF) { d[ny][nx] = (uint16_t)(d[y][x] + 1); qx[n] = nx; qy[n] = ny; n++; } }
    }
}
static void af_dist(af_t *a) { af_bfs(a, a->dist, 0); af_bfs(a, a->sdist, 1); }   /* to the surface; to the store */
static void af_blob(af_t *a, int cx, int cy, int r, int kind)
{
    for (int y = cy - r; y <= cy + r; y++) for (int x = cx - r - 1; x <= cx + r + 1; x++) {
        int dx = x - cx, dy = y - cy;
        if (dx * dx + dy * dy * 2 > r * r * 2 + 1 || x < 1 || x >= a->cols - 1 || y <= a->surf || y >= a->rows - 1) continue;
        if (a->cell[y][x] == AT_SOIL) a->dug++;
        a->cell[y][x] = (uint8_t) kind;
    }
}
static void af_start(af_t *a)
{
    memset(a->cell, 0, sizeof a->cell); a->dug = 0; a->chambers = 0; a->queen_x = a->store_x = -1; a->mound = 0; a->refill = 0; a->food = 0;
    a->entry2 = -1; a->mound2 = 0;
    a->entry = a->cols / 3 + (int)(arand(a) % (uint32_t)(a->cols / 3));
    for (int y = a->surf; y < a->surf + 6; y++) { a->cell[y][a->entry] = AT_TUNNEL; a->dug++; }
    a->lastdig = a->stepn; a->markdug = a->dug;
    for (int i = 0; i < a->nants; i++) { ant_t *n = &a->ant[i]; n->x = n->px = a->entry; n->y = n->py = a->surf + (i % 5); n->st = 0; n->dir = (i & 1) ? 1 : -1; n->pref = (i % 4) == 3 ? i % 2 : 2; n->fails = n->stuck = 0; }
    af_dist(a);
}
/* The day, for the ant farm (Doc, 2026-09-15: "sun cross sky and ant activity
 * track daylight"): the host's local time -- or, K4510_SAVER_DAY=seconds, a day
 * that long starting at dawn, to watch a whole one.  0.0 is midnight, 0.5 noon. */
static double af_dayfrac(uint32_t t)
{
    static int checked; static double daylen;
    if (!checked) { const char *e = getenv("K4510_SAVER_DAY"); daylen = e ? atof(e) : 0; checked = 1; }
    if (daylen > 0) return fmod(t / 1000.0 / daylen + 0.25, 1.0);
    time_t now = time(NULL); struct tm lt; localtime_r(&now, &lt);
    return (lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec) / 86400.0;
}
static double af_sunh(double f) { return sin((f - 0.25) * 2 * 3.14159265); }   /* the sun's height: 1 at noon, below 0 at night */
static int af_light(double f) { return clampi((int)((af_sunh(f) + 0.15) * 3 * 256), 0, 256); }   /* 0 night .. 256 day, twilight between */
static void af_step(af_t *a, int light)
{
    a->stepn++;
#ifdef AF_DEBUG
    if (a->stepn % 667 == 0) { int st[6] = { 0 }; for (int i = 0; i < a->nants; i++) st[a->ant[i].st]++;
        fprintf(stderr, "step %6d light %3d dug %4d/%d wander %d carry %d surface %d food %d refill %d chambers %d queen %d entry2 %d\n",
                a->stepn, light, a->dug, a->soil, st[0], st[1] + st[3], st[2], st[4], a->refill, a->chambers, a->queen_x >= 0, a->entry2); }
#endif
    if (a->refill) {                                           /* the sand comes back: dug cells fill from the bottom up */
        int n = 0;
        for (int y = a->rows - 1; y > a->surf && n < a->cols / 2; y--) for (int x = 0; x < a->cols && n < a->cols / 2; x++)
            if (a->cell[y][x] != AT_SOIL && (arand(a) & 3) == 0) { a->cell[y][x] = AT_SOIL; a->dug--; n++; }
        if (a->mound > 0 && (a->stepn & 3) == 0) a->mound--;
        if (a->dug <= a->cols / 4) af_start(a);
        return;
    }
    if (a->dug * 10 > a->soil * 3) { a->refill = 1; return; }       /* three-tenths dug: new sand */
    if (light < 128) { a->lastdig = a->stepn; a->markdug = a->dug; }   /* asleep is not finished: the night does not count */
    if (a->stepn - a->lastdig >= 3000) {                             /* every 4.5 minutes: grown by less than 25? then it is done too */
        if (a->dug - a->markdug < 25) { a->refill = 1; return; }
        a->lastdig = a->stepn; a->markdug = a->dug;
    }
    if ((a->stepn % 30) == 0) af_dist(a);
    for (int i = 0; i < a->nants; i++) {
        ant_t *n = &a->ant[i]; n->px = n->x; n->py = n->y;
        static const int dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };
        if ((int)(arand(a) % 100) >= 12 + light * 88 / 256) continue;   /* busy by day, a little at night */
#ifdef AF_DEBUG
        if (n->st != 2 && n->y >= a->surf && a->dist[n->y][n->x] <= 1) n->attop++; else n->attop = 0;
        if (n->attop == 25) fprintf(stderr, "  ant %d at the door 25 steps: st %d stuck %d fails %d at %d,%d dist %d nopen?\n",
                                    i, n->st, n->stuck, n->fails, n->x, n->y - a->surf, a->dist[n->y][n->x]);
#endif
        if (n->st == 0) {                                      /* out to the tips of the tunnels, and dig there */
            /* Only at a tip (a cell with one way out) -- or, now and then, a new
             * branch off a corridor -- and only into soil whose other three
             * sides are soil: long thin tunnels going down, not a maze of
             * corridors packed side by side (the first try filled the top
             * fifth that way and stalled, the tips out of reach). */
            int nopen = 0;
            for (int k2 = 0; k2 < 4; k2++) if (open_cell(a, n->x + dx[k2], n->y + dy[k2])) nopen++;
            int tip = nopen <= 1 && n->y > a->surf + 1;
            if (n->stuck > 0) n->stuck--;
            if ((tip && !n->stuck) || (arand(a) % 60) == 0) {
                int d = (arand(a) % 10) < 7 ? n->pref : (arand(a) % 20) == 0 ? 3 : (int)(arand(a) % 3), nx = n->x + dx[d], ny = n->y + dy[d], ok = 0;
                if (nx >= 1 && nx < a->cols - 1 && ny > a->surf && ny < a->rows - 1 && a->cell[ny][nx] == AT_SOIL) {
                    ok = 1;
                    for (int k2 = 0; k2 < 4; k2++) { int ax = nx + dx[k2], ay = ny + dy[k2];
                        if (ax == n->x && ay == n->y) continue;
                        if (open_cell(a, ax, ay)) { ok = 0; break; } }
                }
                if (ok) {
                    if ((arand(a) % 25) == 0) n->pref = (arand(a) % 3) ? 2 : (int)(arand(a) % 2);   /* mostly down */
                    a->cell[ny][nx] = AT_TUNNEL; a->dug++; n->x = nx; n->y = ny; n->st = 1; n->fails = 0;
                    int depth = ny - a->surf, span = a->rows - a->surf;
                    if ((a->queen_x < 0 && depth > span / 6)                     /* the queen's, as soon as a tunnel is deep enough */
                        || (a->chambers < 1 + span / 40 && depth > span / 4 && (arand(a) % 25) == 0)) {   /* a chamber opens */
                        int kind = a->queen_x < 0 && depth > span / 6 ? AT_QUEEN : a->store_x < 0 ? AT_STORE : AT_CHAMBER;
                        af_blob(a, nx, ny, kind == AT_QUEEN ? 4 : 3, kind); a->chambers++;
                        if (kind == AT_QUEEN) { a->queen_x = nx; a->queen_y = ny; } else if (kind == AT_STORE) { a->store_x = nx; a->store_y = ny; }
                        af_dist(a);
                    }
                    continue;
                }
                if (tip && ++n->fails > 10) { n->fails = 0; n->stuck = 40; n->pref = (int)(arand(a) % 3); }   /* a dead end: back up, try another */
            }
            if (n->stuck && a->dist[n->y][n->x] <= 1) {             /* backed up as far as the door: turn round and try */
                n->stuck = 0; n->pref = (int)(arand(a) % 3);         /* another branch, instead of shaking on the doorstep */
            }                                                        /* between up and down (Doc, 2026-09-15) */
            /* walk: outward along the tunnels to a tip, a random branch at each
             * fork; back toward the surface while 'stuck'; now and then any way */
            int here = a->dist[n->y][n->x] == 0xFFFF ? 0 : a->dist[n->y][n->x], nc = 0, cxs[4], cys[4];
            for (int k2 = 0; k2 < 4; k2++) { int nx2 = n->x + dx[k2], ny2 = n->y + dy[k2];
                if (!open_cell(a, nx2, ny2)) continue;
                int dd = a->dist[ny2][nx2] == 0xFFFF ? here + 1 : a->dist[ny2][nx2];
                if (n->stuck ? dd < here : dd > here) { cxs[nc] = nx2; cys[nc] = ny2; nc++; } }
            if (!nc && !tip && !n->stuck) n->stuck = 30;         /* the far wall of a chamber: nothing to dig -- back up */
            if (!nc || (arand(a) % 10) == 0) {
                nc = 0;
                for (int k2 = 0; k2 < 4; k2++) { int nx2 = n->x + dx[k2], ny2 = n->y + dy[k2]; if (open_cell(a, nx2, ny2)) { cxs[nc] = nx2; cys[nc] = ny2; nc++; } }
            }
            if (nc) { int j = (int)(arand(a) % (uint32_t) nc); n->x = cxs[j]; n->y = cys[j]; }
        } else if (n->st == 1 || n->st == 3) {                 /* carrying up: always the step nearer the surface */
            if (n->y <= a->surf) {
                if (n->st == 1) { if (a->entry2 >= 0 && abs(n->x - a->entry2) < abs(n->x - a->entry)) { if (a->mound2 < a->g * 6) a->mound2++; }
                                  else if (a->mound < a->g * 6) a->mound++; }
                n->st = 2; n->dir = (arand(a) & 1) ? 1 : -1; continue; }
            int best = a->dist[n->y][n->x], bx = n->x, by = n->y;
            for (int d = 0; d < 4; d++) { int nx = n->x + dx[d], ny = n->y + dy[d]; if (open_cell(a, nx, ny) && a->dist[ny][nx] < best) { best = a->dist[ny][nx]; bx = nx; by = ny; } }
            if (bx == n->x && by == n->y) { int d = (int)(arand(a) % 4); if (open_cell(a, n->x + dx[d], n->y + dy[d])) { bx = n->x + dx[d]; by = n->y + dy[d]; } }
            n->x = bx; n->y = by;
        } else if (n->st == 2) {                               /* on the surface: a stroll, perhaps a crumb, then back in */
            int home = a->entry2 >= 0 && abs(n->x - a->entry2) < abs(n->x - a->entry) ? a->entry2 : a->entry;
            if (light < 64) n->dir = home > n->x ? 1 : -1;            /* dusk: home, by the nearer way */
            if (a->entry2 < 0 && a->dug > a->soil / 25 && abs(n->x - a->entry) > a->cols / 4 && n->x > 1 && n->x < a->cols - 2 && (arand(a) % 20) == 0) {
                a->entry2 = n->x; n->st = 5; n->y = a->surf;          /* a second way in: begun from outside */
                if (a->cell[a->surf][n->x] == AT_SOIL) { a->cell[a->surf][n->x] = AT_TUNNEL; a->dug++; }
                continue;
            }
            n->x += n->dir;                                   /* kept between the edges: a random turn after the bounce */
            if (n->x < 1) { n->x = 1; n->dir = 1; } else if (n->x > a->cols - 2) { n->x = a->cols - 2; n->dir = -1; }   /* walked some off */
            if (light >= 64 && (arand(a) % 40) == 0) n->dir = -n->dir;
            if ((n->x == a->entry || (a->entry2 >= 0 && n->x == a->entry2)) && (light < 64 || (arand(a) % 3) == 0)) {   /* -1 is no second way in, not column -1 */ n->y = a->surf; n->st = (a->store_x >= 0 && (arand(a) % 4) == 0) ? 4 : 0; }
        } else if (n->st == 5) {                               /* the second way in: down three, then along to the first, till it meets a tunnel */
            int nx = n->x, ny = n->y;
            if (n->y - a->surf < 3) ny++; else nx += a->entry > n->x ? 1 : -1;
            if (nx < 1 || nx >= a->cols - 1) { n->st = 0; continue; }
            if (a->cell[ny][nx] == AT_SOIL) { a->cell[ny][nx] = AT_TUNNEL; a->dug++; if (a->mound2 < a->g * 6) a->mound2++; }
            n->x = nx; n->y = ny;
            for (int d = 0; d < 4; d++) { int ax = nx + dx[d], ay = ny + dy[d];
                if ((ax != n->px || ay != n->py) && open_cell(a, ax, ay)) { n->st = 0; af_dist(a); break; } }   /* through: joined */
        } else {                                               /* st 4: a crumb down to the store, by the tunnels' own shortest way */
            if (a->store_x < 0 || a->sdist[n->y][n->x] <= 1) { if (a->store_x >= 0) a->food++; n->st = 0; continue; }
            int bx = n->x, by = n->y, bd = a->sdist[n->y][n->x];
            for (int d = 0; d < 4; d++) { int nx = n->x + dx[d], ny = n->y + dy[d];
                if (open_cell(a, nx, ny) && a->sdist[ny][nx] < bd) { bd = a->sdist[ny][nx]; bx = nx; by = ny; } }
            if (bx == n->x && by == n->y) { int d = (int)(arand(a) % 4); if (open_cell(a, n->x + dx[d], n->y + dy[d])) { bx = n->x + dx[d]; by = n->y + dy[d]; } }
            n->x = bx; n->y = by;
        }
    }
}
static void draw_ant(cv_t *c, int x, int y, int k, int frame, int carry, int big)
{
    uint32_t body = RGB(30, 18, 12), leg = RGB(60, 40, 30);
    int s = big ? 2 : 1;
    rect(c, x, y, 2 * k * s, 2 * k * s, body); rect(c, x + 2 * k * s, y, k * s + 1, 2 * k * s - 1, body); rect(c, x + 3 * k * s + 1, y, 2 * k * s, 2 * k * s, body);
    for (int l = 0; l < 3; l++) { int lx = x + k * s + l * 2 * k * s, off = ((l + frame) & 1) ? k : 0; rect(c, lx, y - k + off - 1, 1, k + 1, leg); rect(c, lx, y + 2 * k * s, 1, k + 1 - off, leg); }
    if (carry) rect(c, x + k * s, y - k - 1, 2 * k, k + 1, carry == 2 ? RGB(120, 200, 80) : RGB(236, 214, 150));
}
void s_antfarm(cv_t *c, uint32_t t, int side)
{
    int w = c->w, h = c->h, k = scale_of(w);
    af_t *a = &af[side];
    if (a->w != w || a->h != h) {
        memset(a, 0, sizeof *a); a->w = w; a->h = h; a->g = 2 * k;
        a->cols = w / a->g; if (a->cols > AG_COLS) a->cols = AG_COLS;
        a->rows = h / a->g; if (a->rows > AG_ROWS) a->rows = AG_ROWS;
        a->surf = a->rows / 10; a->soil = (a->rows - a->surf) * a->cols;
        a->nants = clampi(6 + w / 20, 6, ANTS); a->rng = 0xA27Fu + (uint32_t) side * 7717u; a->last = t;
        af_start(a);
    }
    double dayf = af_dayfrac(t); int light = af_light(dayf);
    if (t - a->last > 2000) a->last = t;
    for (int n = 0; t - a->last >= 90 && n < 40; n++) { af_step(a, light); a->last += 90; }
    int g = a->g, sy = a->surf * g, frac = (int)((t - a->last) * 256 / 90);
    /* the sky by the time of day: blue, dark blue and stars, warm at dawn and dusk; the sun
     * across it from left (6:00) to right (18:00), the moon the same way by night */
    { int tw = clampi(128 - abs(light - 128), 0, 128);            /* twilight: strongest half way */
      uint32_t top = mix(RGB(8, 12, 34), RGB(120, 180, 240), light), bot = mix(mix(RGB(30, 40, 78), RGB(200, 230, 250), light), RGB(250, 140, 80), tw);
      vgrad(c, 0, sy, top, bot);
      if (light < 160) for (int i = 0; i < 24 + w / 6; i++) {    /* stars, twinkling */
          uint32_t r = hh((uint32_t)(i * 7919 + side * 31)); int x = (int)(r % (uint32_t) w), y = (int)((r >> 12) % (uint32_t)(sy > 1 ? sy - 1 : 1));
          blend(c, x, y, RGB(255, 255, 230), clampi((160 - light) * ((int)((r >> 20) & 3) + 1) / 4 + isin((int)(t / 8 + r)) / 8, 0, 255)); }
      double sh = af_sunh(dayf), mh = af_sunh(fmod(dayf + 0.5, 1.0));
      int arc = sy * 4 / 5;
      if (sh > -0.1) { int sx = (int)(fmod(dayf - 0.25 + 1.0, 1.0) * 2 * w), syy = sy - (int)(sh * arc);
          glow(c, sx, syy, sy / 2 + 2, RGB(255, 240, 160), 200); disc(c, sx, syy, sy / 5 + 1, mix(RGB(255, 150, 80), RGB(255, 236, 140), light)); }
      if (mh > -0.1) { int mx = (int)(fmod(dayf + 0.25, 1.0) * 2 * w), myy = sy - (int)(mh * arc), mr = sy / 6 + 1;
          glow(c, mx, myy, mr * 2, RGB(200, 210, 255), 60); disc(c, mx, myy, mr, RGB(230, 232, 220)); disc(c, mx + mr / 2, myy - mr / 3, mr * 3 / 4, top); } }
    /* the soil in layers, speckled; the tunnels and chambers dug through it */
    for (int y = sy; y < h; y++) {
        int depth = (y - sy) * 256 / (h - sy + 1);
        uint32_t base = depth < 90 ? mix(RGB(196, 150, 96), RGB(170, 120, 70), depth * 256 / 90) : mix(RGB(170, 120, 70), RGB(120, 70, 44), (depth - 90) * 256 / 166);
        int band = isin(y * 13 + side * 90) > 200;
        for (int x = 0; x < w; x++) {
            int cx = x / g, cy = y / g; uint8_t v = (cx < a->cols && cy < a->rows) ? a->cell[cy][cx] : AT_SOIL;
            uint32_t col;
            if (v == AT_SOIL) { uint32_t r = hh((uint32_t)(x * 7919 + y * 104729 + side)); col = (r & 15) == 0 ? mix(base, RGB(250, 230, 180), 90) : (r & 15) == 1 ? mix(base, RGB(40, 20, 10), 70) : base; if (band) col = mix(col, RGB(90, 60, 40), 40); }
            else if (v == AT_TUNNEL) col = RGB(52, 32, 20);
            else if (v == AT_QUEEN) col = RGB(78, 48, 30);
            else col = RGB(66, 40, 26);
            c->px[y * c->pitch + x] = col;
        }
    }
    for (int y = sy; y < h; y++) for (int x = 0; x < w; x++) {   /* a lighter lip where tunnel meets soil above */
        int cx = x / g, cy = y / g;
        if (cy > 0 && cy < a->rows && cx < a->cols && a->cell[cy][cx] != AT_SOIL && a->cell[cy - 1][cx] == AT_SOIL && y % g == 0) pset(c, x, y, RGB(140, 96, 60));
    }
    /* grass along the top of the soil, and the mound over the entrance */
    for (int x = 0; x < w; x++) { int gh = 2 + (int)(hh((uint32_t)(x * 31 + side)) % (uint32_t)(2 * k + 1)); rect(c, x, sy - gh, 1, gh, (x & 1) ? RGB(60, 150, 50) : RGB(80, 176, 60)); }
    { int ex = a->entry * g + g / 2, mh = a->mound / 2 + k;
      for (int dy = 0; dy < mh; dy++) { int half = (mh - dy) * 2 + g / 2; rect(c, ex - half, sy - dy - 1, 2 * half, 1, mix(RGB(210, 170, 110), RGB(180, 130, 80), dy * 256 / (mh + 1))); }
      rect(c, ex - g / 2, sy - 2, g, 3, RGB(52, 32, 20)); }
    if (a->entry2 >= 0) { int ex = a->entry2 * g + g / 2, mh = a->mound2 / 2 + k;   /* the second way in has its own */
      for (int dy = 0; dy < mh; dy++) { int half = (mh - dy) * 2 + g / 2; rect(c, ex - half, sy - dy - 1, 2 * half, 1, mix(RGB(210, 170, 110), RGB(180, 130, 80), dy * 256 / (mh + 1))); }
      rect(c, ex - g / 2, sy - 2, g, 3, RGB(52, 32, 20)); }
    /* the queen and her eggs; the store's crumbs */
    if (a->queen_x >= 0) {
        for (int i = 0; i < 7; i++) { uint32_t r = hh((uint32_t)(i * 97 + side)); int ex = a->queen_x * g + ((int)(r % 7) - 3) * g, ey = a->queen_y * g + ((int)((r >> 8) % 5) - 2) * g;
            if (ey / g < a->rows && ex / g < a->cols && a->cell[ey / g][ex / g] != AT_SOIL) disc(c, ex, ey, k, RGB(250, 246, 230)); }
        glow(c, a->queen_x * g, a->queen_y * g + k, 3 * g, RGB(255, 210, 150), 70);   /* the queen: big, and lit, so she is found */
        draw_ant(c, a->queen_x * g - 3 * k, a->queen_y * g, k, (int)(t / 400) & 1, 0, 1);
    }
    if (a->store_x >= 0) for (int i = 0; i < clampi(a->food, 0, 14); i++) { uint32_t r = hh((uint32_t)(i * 131 + side * 7)); int fx = a->store_x * g + ((int)(r % 5) - 2) * g, fy = a->store_y * g + ((int)((r >> 8) % 3) - 1) * g;
        if (fy / g < a->rows && fx / g < a->cols && a->cell[fy / g][fx / g] != AT_SOIL) disc(c, fx, fy, k, RGB(120, 200, 80)); }
    /* the ants, each moving smoothly from its last cell to this one */
    for (int i = 0; i < a->nants; i++) {
        ant_t *n = &a->ant[i];
        int x = (n->px * g * (256 - frac) + n->x * g * frac) / 256, y = (n->py * g * (256 - frac) + n->y * g * frac) / 256;
        if (n->st == 2) y = sy - 2 * k - 2;
        draw_ant(c, x - 2 * k, y - k, k, (int)(t / 120 + (uint32_t) i) & 1, n->st == 1 || n->st == 3 || n->st == 5 ? 1 : n->st == 4 ? 2 : 0, 0);
    }
    if (light < 200) { int dim = (200 - light) * 100 / 200;           /* night over the glass too, a little */
      for (int y = sy; y < h; y++) for (int x = 0; x < w; x++) blend(c, x, y, RGB(10, 14, 40), dim); }
    /* the glass: a pale edge down each side */
    for (int y = 0; y < h; y++) { blend(c, 0, y, RGB(230, 240, 250), 120); blend(c, w - 1, y, RGB(230, 240, 250), 120); }
}
