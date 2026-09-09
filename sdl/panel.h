/* The side panel: the machine's registers, the next instructions, the banks,
 * VICKY and the audio, drawn as text beside the picture on a screen wider
 * than 4:3 (Doc, 2026-09-09: "the rest of the physical display could be used
 * for educational purposes -- live showing of processor registers, debugger").
 * Reads emulator state only; the machine never knows it is there. */
#ifndef K4510_PANEL_H
#define K4510_PANEL_H
#include <stdint.h>
typedef struct { double fps; const char *host; unsigned cpu_hz; } panel_info;
/* Draw into an ARGB buffer w x h (logical pixels), glyphs scaled g times from an 8x8 1-bpp font. */
void panel_render(uint32_t *px, int pitch_px, int w, int h, int g, const uint8_t *font, const panel_info *info);
#endif
