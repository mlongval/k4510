/* sdl/savers.h -- the sidebar-savers that paint a whole scene (sdl/savers.c).
 * The gradient and the knot live in sdl/main.c; these draw into a picture
 * the size of one sidebar, in machine pixels, every frame. */
#ifndef K4510_SAVERS_H
#define K4510_SAVERS_H
#include <stdint.h>
#include <stddef.h>

enum { SAVER_HALLOWEEN, SAVER_CHRISTMAS, SAVER_SPACE, SAVER_RIVER, SAVER_DREAMFALL, SAVER_TETRIS, SAVER_ANTFARM, SAVER_MATRIX, SAVER_DOOM, SAVER_NAVIDROME, SAVER_COUNT };

/* which: SAVER_*; px: w x h ARGB pixels, pitch in pixels; ms: a clock in
 * milliseconds; side: 0 left, 1 right (each side its own scene and seed). */
void saver_draw(int which, uint32_t *px, int pitch, int w, int h, uint32_t ms, int side);
/* The machine's glyphs (sdl/main.c's font_panel: unscii-16, 8 bits a row,
 * frows rows a character), for the scenes that draw characters -- the Matrix
 * rain does.  A property of the machine, set once rather than passed through
 * every draw; unset, the scenes that want it fall back to something else. */
void saver_font(const uint8_t *font, int frows);
/* A scene's own option, from its OPTIONS.CFG (the ant farm's day = 30m). */
void   saver_option(int which, const char *key, const char *value);
/* What a scene keeps across a power cycle (STATE.DAT): a new buffer the caller
 * frees, and its length; 0 when it keeps nothing or has not started.  Restore
 * takes it back -- at once when the canvas is the size it was saved at, or when
 * the scene is next drawn at that size. */
size_t saver_state(int which, uint8_t **buf);
void   saver_restore(int which, const uint8_t *buf, size_t n);

/* the Navidrome sidebar's player (sdl/sidebars/navidrome.c): whether it is on the
 * glass, its music added to n samples of the machine's, and the next song */
void navi_active(int on);
void navi_mix(int16_t *out, int n);
void navi_next(void);
#endif
