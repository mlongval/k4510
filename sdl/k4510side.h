/* sdl/k4510side.h -- the K4510's sidebars for other programs: the C64, the
 * Amiga and the X16 personalities draw the same scenes beside their picture
 * (Doc, 2026-10-09: "shift all emulators to be shifted left to maximise the
 * sidebar area to the right, there is a lot of cool stuff that can be done
 * there").  libk4510side.so is sdl/savers.c and its scenes, built on their
 * own; a program dlopen()s it (K4510_SIDEBAR_LIB says where) and hands it a
 * buffer of pixels.  No SDL in it, so the SDL2 emulators and the SDL3 one
 * use the same library.
 *
 *   k4510side_init("antfarm day=30m state=/home/k4510/personalities/sidebar")
 *       the scene by its F12 name (halloween christmas space river dreamfall
 *       tetris antfarm matrix), then its options; state= is a directory
 *       where a scene that keeps something (the ant farm's colony) keeps it,
 *       read now and written by k4510side_quit.  0, or -1 for no such scene.
 *   k4510side_draw(px, w, h, pitch, ms)
 *       the whole scene into w x h ARGB8888 pixels, pitch in pixels; ms is a
 *       clock in milliseconds.  The scenes are drawn in MACHINE pixels, as
 *       the K4510 draws them before scaling up: hand it a buffer some 240-400
 *       pixels tall (the free area divided by a whole number) and scale that
 *       up with nearest-neighbour, not the panel's full 1080 lines.
 *   k4510side_quit()   saves what the scene keeps.
 *
 * The Navidrome sidebar is not in it: it plays music into the K4510's own
 * sound.  Asked for by name, it is "no such scene". */
#ifndef K4510SIDE_H
#define K4510SIDE_H
#include <stdint.h>

#define K4510SIDE_API 1        /* bumped if these four change */

/* only these four leave the library: the scenes' own names (s_space ...)
 * stay inside, whatever the program that loads it calls its own */
#ifdef K4510SIDE_BUILD
#define K4510SIDE_EXPORT __attribute__((visibility("default")))
#else
#define K4510SIDE_EXPORT
#endif
K4510SIDE_EXPORT int  k4510side_init(const char *config);
K4510SIDE_EXPORT void k4510side_draw(uint32_t *px, int w, int h, int pitch, uint32_t ms);
K4510SIDE_EXPORT void k4510side_quit(void);
K4510SIDE_EXPORT int  k4510side_api(void);      /* K4510SIDE_API, to check a library found at run time */
#endif
