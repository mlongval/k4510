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
 * approximation), and a hash for anything that must look random but stay put.
 *
 * Since 2026-09-15 each scene is its own file in sdl/sidebars/, with the
 * toolbox in sdl/sidebars/canvas.h; this file only picks one. */
#include "savers.h"
#include "sidebars/canvas.h"

void saver_draw(int which, uint32_t *px, int pitch, int w, int h, uint32_t ms, int side)
{
    cv_t c = { px, pitch, w, h };
    sb_sin_init();
    if (w < 4 || h < 4) return;
    switch (which) {
    case SAVER_HALLOWEEN: s_halloween(&c, ms, side & 1); break;
    case SAVER_CHRISTMAS: s_christmas(&c, ms, side & 1); break;
    case SAVER_SPACE:     s_space(&c, ms, side & 1); break;
    case SAVER_RIVER:     s_river(&c, ms, side & 1); break;
    case SAVER_TETRIS:    s_tetris(&c, ms, side & 1); break;
    case SAVER_ANTFARM:   s_antfarm(&c, ms, side & 1); break;
    default:              s_dreamfall(&c, ms, side & 1); break;
    }
}
