/* sdl/savers.h -- the sidebar-savers that paint a whole scene (sdl/savers.c).
 * The gradient and the knot live in sdl/main.c; these draw into a picture
 * the size of one sidebar, in machine pixels, every frame. */
#ifndef K4510_SAVERS_H
#define K4510_SAVERS_H
#include <stdint.h>

enum { SAVER_HALLOWEEN, SAVER_CHRISTMAS, SAVER_SPACE, SAVER_RIVER, SAVER_DREAMFALL, SAVER_TETRIS, SAVER_COUNT };

/* which: SAVER_*; px: w x h ARGB pixels, pitch in pixels; ms: a clock in
 * milliseconds; side: 0 left, 1 right (each side its own scene and seed). */
void saver_draw(int which, uint32_t *px, int pitch, int w, int h, uint32_t ms, int side);

#endif
