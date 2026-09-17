/* sdl/sidebars/doom.c -- the first GAMEBAR: DOOM's side panel (docs/GAMEBARS.md).
 *
 * Doc, 2026-09-17: "Would like to have DOOM themed artwork that comes up if
 * sidebar(s) is just background.  Something like I would have seen in an
 * arcade.  Let's call them gamebars."
 *
 * A cabinet's side art is how you knew, across the room, what the machine
 * was.  This is that panel, stages 1 and 2 of the plan at once: painted, and
 * alive -- the fire licks, the embers rise, the pentagram turns, the eyes and
 * the logo breathe.  It is an ordinary sidebar, so it can be chosen like any
 * other; what makes it a gamebar is sdl/main.c, which shows it on its own
 * while DOOM has the Tube and the chosen sidebar is only a background.
 *
 * Nothing here is taken from the game.  Freedoom's sprites are BSD and could
 * have been used, but a panel pasted together from game sprites looks like a
 * screenshot, not like a painted cabinet; this is drawn with the toolbox
 * every other scene uses, in the machine's own hand.  A pure function of the
 * clock, as they all are. */
#include "canvas.h"

/* the logo's letters, 7 x 7 and two cells to a stroke, stacked down the panel
 * the way a cabinet's side carried its name */
static const char *const let_D[] = { "#####..", "######.", "##..###", "##...##", "##..###", "######.", "#####.." };
static const char *const let_O[] = { ".#####.", "#######", "##...##", "##...##", "##...##", "#######", ".#####." };
static const char *const let_M[] = { "##...##", "###.###", "#######", "##.#.##", "##...##", "##...##", "##...##" };
#define LET_W 7

static const char *const skull[] = {
    "h.........h",
    "hh.......hh",
    ".hhwwwwwhh.",
    ".wwwwwwwww.",
    "wwwwwwwwwww",
    "wweewwweeww",
    "wweewwweeww",
    "wwwwwkwwwww",
    ".wwwkkkwww.",
    "..wwwwwww..",
    "..wkwkwkw..",
    "..wkwkwkw.." };

/* one letter, s machine pixels a cell: a black drop shadow, then the
 * face shaded steel at the top to fire at the foot, the way the logo always
 * was, with a bright top edge on every cell that has nothing above it */
static void letter(cv_t *c, int x, int y, const char *const *rows, int s, int heat)
{
    for (int pass = 0; pass < 2; pass++)
        for (int r = 0; r < 7; r++) for (int i = 0; i < LET_W; i++) {
            if (rows[r][i] != '#') continue;
            if (!pass) { rect(c, x + i * s + (s + 2) / 3, y + r * s + (s + 2) / 3, s, s, RGB(0, 0, 0)); continue; }
            for (int dy = 0; dy < s; dy++) {
                int f = (r * s + dy) * 256 / (7 * s);                          /* 0 at the top of the letter, 256 at its foot */
                uint32_t v = mix(RGB(196, 204, 220), RGB(236, 84 + heat / 4, 18), f);
                if (dy == 0 && (r == 0 || rows[r - 1][i] != '#')) v = RGB(250, 250, 255);
                rect(c, x + i * s, y + r * s + dy, s, 1, v);
            }
        }
}

void s_doom(cv_t *c, uint32_t t, int side)
{
    int w = c->w, h = c->h, k = scale_of(w);
    uint32_t seed = 0xD003u + (uint32_t) side * 7919u;
    int breath = (isin((int)(t / 6)) + 256) / 2;                               /* 0..256, a slow pulse: six seconds a turn */

    /* the sky over hell: black-red, to blood, to the glow off the fire */
    vgrad(c, 0, h * 3 / 5, RGB(10, 2, 4), RGB(78, 8, 6));
    vgrad(c, h * 3 / 5, h, RGB(78, 8, 6), RGB(168, 46, 8));
    for (int i = 0; i < 5; i++) {                                               /* smoke, drifting across */
        int cy = h / 8 + i * h / 7, span = w + 80, len = 34 + i * 11;
        int cx = (int)((t / (uint32_t)(90 + i * 31) + (uint32_t) i * 131 + (uint32_t) side * 57) % (uint32_t) span) - 60;
        for (int dx = 0; dx < len; dx++) { int th = 2 + (isin(dx * 36 + i * 90) + 256) * 3 / 512; rectb(c, cx + dx, cy + (dx / 7) % 2, 1, th, RGB(4, 0, 2), 120); }
    }

    /* the name, down the panel */
    { int s = (w - 8 * k) / (LET_W + 1), smax = h / 2 / 32;
      if (s > smax) s = smax;
      if (s >= 1) {
          static const char *const *const word[4] = { let_D, let_O, let_O, let_M };
          int lx = (w - LET_W * s) / 2, ly = h / 28 + 2 * k;
          for (int i = 0; i < 4; i++) {
              glow(c, lx + LET_W * s / 2, ly + i * 8 * s + 7 * s / 2, 5 * s + 2, RGB(255, 40, 10), 40 + breath / 5);
              letter(c, lx, ly + i * 8 * s, word[i], s, breath);
          }
      } }

    /* the pentagram: two rings and the star, turning -- one way on the left
     * of the picture and the other on the right -- and a horned skull in it
     * whose eyes come and go with the same breath as the logo */
    { int cx = w / 2, cy = h * 63 / 100, R = w * 40 / 100;
      if (R > h / 7) R = h / 7;
      if (R >= 3) {
          int rot = (int)(t / 48) * (side ? -1 : 1);
          uint32_t fire = mix(RGB(200, 20, 8), RGB(255, 150, 30), breath);
          glow(c, cx, cy, R + R / 3, RGB(255, 30, 0), 50 + breath / 6);
          for (int a = 0; a < 1024; a += 2) {
              int sx = isin(a), sy = icos(a);
              blend(c, cx + R * sx / 256, cy - R * sy / 256, fire, 230);
              blend(c, cx + (R - 1 - k) * sx / 256, cy - (R - 1 - k) * sy / 256, fire, 140);
          }
          for (int i = 0; i < 5; i++) {                                          /* point i to point i+2: the star */
              int a0 = rot + 512 + i * 1024 / 5, a1 = rot + 512 + (i + 2) * 1024 / 5;
              int x0 = cx + R * isin(a0) / 256, y0 = cy - R * icos(a0) / 256, x1 = cx + R * isin(a1) / 256, y1 = cy - R * icos(a1) / 256;
              lineb(c, x0, y0, x1, y1, fire, 235);
              if (k > 1) lineb(c, x0 + 1, y0, x1 + 1, y1, fire, 150);
          }
          { static const char keys[] = "hwek";
            const uint32_t cols[] = { RGB(120, 96, 70), RGB(226, 218, 196), mix(RGB(90, 0, 0), RGB(255, 236, 80), breath), RGB(16, 4, 4) };
            int sk = R / 13 > 0 ? R / 13 : 1;                               /* the skull fills the star's inner pentagon */
            int ex = cx - 11 * sk / 2, ey = cy - 6 * sk;
            discb(c, cx, cy, 6 * sk, RGB(20, 0, 0), 150);                  /* a dark ground, so the star's lines do not cross the face */
            glow(c, ex + 3 * sk, ey + 6 * sk, 3 * sk, RGB(255, 200, 40), breath / 2);
            glow(c, ex + 8 * sk, ey + 6 * sk, 3 * sk, RGB(255, 200, 40), breath / 2);
            spr(c, ex, ey, skull, 12, keys, cols, sk, 0, 256); }
      } }

    /* the far range, black against the glow */
    { int g0 = h - h / 8;
      for (int layer = 0; layer < 2; layer++)
          for (int x = 0; x < w; x++) {
              int per = (layer ? 46 : 30) * k, p = (x + side * 17 + layer * 11 * k) % per, tri = p < per / 2 ? p : per - p;   /* broad peaks... */
              int p2 = (x * 3 + layer * 7) % (9 * k), nick = p2 < 4 * k ? p2 : 9 * k - p2;                                     /* ...with shoulders... */
              int top = g0 - layer * 5 * k - tri * (layer ? 1 : 2) - nick - (int)(hh(seed + (uint32_t)(x / (2 * k)) + (uint32_t) layer * 99u) % (uint32_t)(3 * k));   /* ...and rough */
              rect(c, x, top, 1, h - top, layer ? RGB(6, 1, 2) : RGB(44, 6, 5));
          } }

    /* the fire along the foot: each column's height is two sines that move
     * against each other and a flicker that holds for 70 ms; colour by how
     * far up the flame the pixel is */
    { int fh = h / 6 + 6;
      for (int x = 0; x < w; x++) {
          int f = 150 + isin(x * 23 + (int)(t / 3) + side * 200) * 60 / 256 + isin(x * 57 - (int)(t / 2)) * 40 / 256
                  + (int)(hh(seed + (uint32_t) x * 31u + (t / 70) * 7u) % 40u);                           /* 50..290 of 256 */
          int top = fh * f / 290;
          for (int d = 0; d < top; d++) {
              int u = d * 256 / (top > 0 ? top : 1);                                                       /* 0 at the root, 256 at the tip */
              uint32_t v = u < 70  ? mix(RGB(255, 250, 190), RGB(255, 200, 40), u * 256 / 70)
                         : u < 170 ? mix(RGB(255, 200, 40), RGB(240, 80, 10), (u - 70) * 256 / 100)
                                   : mix(RGB(240, 80, 10), RGB(90, 8, 4), (u - 170) * 256 / 86);
              blend(c, x, h - 1 - d, v, u < 200 ? 256 : 256 - (u - 200) * 4);
          }
      } }

    /* embers, climbing out of it and going dark as they go */
    { int n = h / 16 + 6, per = h * 3 / 4 + 1;
      for (int i = 0; i < n; i++) {
          uint32_t r = hh(seed + 500u + (uint32_t) i);
          int sp = 24 + (int)(r % 40), up = (int)((t * (uint32_t) sp / 1000 + (r >> 8)) % (uint32_t) per);
          int ex = (int)((r >> 4) % (uint32_t)(w > 0 ? w : 1)) + isin((int)(t / 5 + (r >> 12))) * (3 * k) / 256;
          int a = 256 - up * 256 / per;
          for (int dy = 0; dy < k; dy++) for (int dx = 0; dx < k; dx++)
              blend(c, ex + dx, h - h / 10 - up + dy, dx || dy ? RGB(255, 110, 20) : RGB(255, 210, 90), a);
      } }

    /* the cabinet's own edge: a steel girder on the outside, a thin bead
     * against the picture, rivets down the girder */
    { int gw = 3 * k, outer = side ? w - gw : 0, inner = side ? 0 : w - k;
      for (int i = 0; i < gw; i++) rect(c, outer + i, 0, 1, h, mix(RGB(58, 60, 66), RGB(128, 132, 142), side ? (gw - i) * 256 / gw : (i + 1) * 256 / gw));
      rect(c, inner, 0, k, h, RGB(40, 12, 10));
      for (int y = 6 * k; y < h; y += 26 * k) { disc(c, outer + gw / 2, y, k > 1 ? k - 1 : 0, RGB(176, 180, 190)); pset(c, outer + gw / 2, y + (k > 1 ? k - 1 : 0) + 1, RGB(30, 30, 34)); }
    }
}
