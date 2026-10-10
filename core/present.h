/* present.h -- how a glass is shown on a panel (2026-10-09).
 *
 * Doc's three ways, for any resolution a program asks for:
 *   PRES_INTEGER  the largest whole scale that fits, centred: every machine
 *                 pixel the same square of panel pixels, a border round it.
 *   PRES_FIT      sharp-bilinear: as large as fits, keeping the aspect; hard
 *                 pixels to the whole multiple below, then one smooth step to
 *                 fill (sdl/main.c draws it so).  present_rect() gives the
 *                 rect; present_whole() the multiple the hard step uses.
 *   PRES_NATIVE   as large as fits, keeping the aspect, any scale, hard
 *                 pixels (nearest): 2.25x makes some pixels two lines tall
 *                 and some three.
 *
 * Which one applies is present_mode(): the user's F12 choice (integer, or
 * sharp-bilinear) unless GLASSCTL bits4-5 say how; 0 there leaves it to the
 * user.  K/OS's own MODE writes 0, so the user's switch reaches the shell's
 * screens too; MODE -i, -m/-f and -a write the other three.
 *
 * Pure arithmetic, no SDL: test/vickytest.c checks it against the rule above.
 * At a whole scale all three give the same rect, which is how a fitted
 * picture that happens to divide the panel stays pixel for pixel. */
#ifndef K4510_PRESENT_H
#define K4510_PRESENT_H
#include "vicky.h"

enum { PRES_INTEGER, PRES_FIT, PRES_NATIVE };

typedef struct { int x, y, w, h; double scale; int whole; } present_rect_t;

/* The scale a glass of gw x gh gets on a panel of pw x ph, by mode, and the
 * rect it lands in, centred.  Integer never shrinks below 1x (a panel too
 * small for the glass shows the top-left of it, as SDL's integer scale does);
 * the others never shrink below what fits. */
static inline present_rect_t present_rect(int pw, int ph, int gw, int gh, int mode)
{
    present_rect_t r = { 0, 0, 0, 0, 1.0, 1 };
    double sx, sy, sc;
    if (gw <= 0 || gh <= 0 || pw <= 0 || ph <= 0) return r;
    sx = (double) pw / gw; sy = (double) ph / gh;
    sc = sx < sy ? sx : sy;
    if (mode == PRES_INTEGER) { sc = (double)(int) sc; if (sc < 1.0) sc = 1.0; }
    r.scale = sc; r.whole = (int) sc; if (r.whole < 1) r.whole = 1;
    r.w = (int)(gw * sc); r.h = (int)(gh * sc);
    r.x = (pw - r.w) / 2; r.y = (ph - r.h) / 2;
    return r;
}

/* The mode in force: the user's setting (0 integer, 1 sharp-bilinear, as
 * settings.h's SMOOTH_ enum) unless GLASSCTL bits4-5 say how -- for any
 * glass, CTRL's own included (MODE 0 -a is a thing); 0 there is the user's.
 * K/OS writes the bits back as the program leaves them when it exits, so a
 * program's choice lasts as long as the program. */
static inline int present_mode(int user_fit, int glassctl)
{
    switch (glassctl & VG_PRES_MASK) {
    case VG_PRES_FIT:     return PRES_FIT;
    case VG_PRES_NATIVE:  return PRES_NATIVE;
    case VG_PRES_INTEGER: return PRES_INTEGER;
    default:              return user_fit ? PRES_FIT : PRES_INTEGER;
    }
}

/* Scanline darkness in force, percent: the register's, else the user's. */
static inline int present_scan_dark(int reg_scandk, int user_dark)
{
    int d = reg_scandk ? reg_scandk : user_dark;
    return d > 100 ? 100 : d < 0 ? 0 : d;
}
#endif
