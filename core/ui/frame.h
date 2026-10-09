/* The frame's colour (Doc, 2026-10-07): the border and the status bands are
 * one colour, F12 -> Video -> Frame colour.  Shared by the emulator
 * (sdl/main.c) and the Personality Chooser (sdl/chooser.c), so both paint
 * the same frame from the same settings and palette. */
#ifndef K4510_FRAME_H
#define K4510_FRAME_H
#include <stdint.h>
extern const uint32_t frame_vic[16];          /* the VIC-II sixteen, 0x00RRGGBB */
uint32_t frame_rgb(void);                     /* the border's colour, and the bands' */
double   frame_ratio(uint32_t a, uint32_t b); /* contrast, the worse of normal and protan eyes */
uint32_t frame_text_rgb(uint32_t bg);         /* near-white or near-black, whichever reads better on bg */
#endif
