/* The frame's colour, from sdl/main.c (2026-10-07): following the palette it
 * is that palette entry, as the border always was; not following, it is the
 * VIC-II colour of that number whatever the palette, and the bands' text is
 * near-black or near-white, whichever reads better on it for normal and
 * protan eyes alike.  In core/ui since 2026-10-09 so the Personality Chooser
 * (sdl/chooser.c) reads it the same way. */
#include <math.h>
#include "frame.h"
#include "settings.h"
#include "../vicky.h"

const uint32_t frame_vic[16] = {
    0x000000, 0xFFFFFF, 0x880000, 0xAAFFEE, 0xCC44CC, 0x00CC55, 0x0000AA, 0xEEEE77,
    0xDD8855, 0x664400, 0xFF7777, 0x333333, 0x777777, 0xAAFF66, 0x0088FF, 0xBBBBBB,
};
static double frame_lin(uint32_t c, int sh) { double v = ((c >> sh) & 255) / 255.0; return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4); }
static double frame_lum(uint32_t c, int protan)
{
    double r = frame_lin(c, 16), g = frame_lin(c, 8), b = frame_lin(c, 0);
    return protan ? 0.1140 * r + 0.7827 * g + 0.1034 * b : 0.2126 * r + 0.7152 * g + 0.0722 * b;
}
double frame_ratio(uint32_t a, uint32_t b)
{
    double m = 99;
    for (int p = 0; p < 2; p++) { double x = frame_lum(a, p), y = frame_lum(b, p), r = x > y ? (x + 0.05) / (y + 0.05) : (y + 0.05) / (x + 0.05); if (r < m) m = r; }
    return m;
}
uint32_t frame_rgb(void)
{
    int i = settings_get(SET_VIDEO_BORDER_COLOUR) & 15;
    return settings_get(SET_VIDEO_FRAME_FOLLOW) ? vicky_palette_rgb(i) & 0xFFFFFF : frame_vic[i];
}
uint32_t frame_text_rgb(uint32_t bg)
{
    return frame_ratio(0xF2F2F2, bg) >= frame_ratio(0x111111, bg) ? 0xF2F2F2 : 0x111111;
}
