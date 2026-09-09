/* The side panel: the machine's registers, the next instructions, the banks,
 * VICKY and the audio, drawn as text beside the picture on a screen wider
 * than 4:3 (Doc, 2026-09-09: "the rest of the physical display could be used
 * for educational purposes -- live showing of processor registers, debugger").
 * Reads emulator state only; the machine never knows it is there.
 *
 * Laid out for a strip that is narrow and tall (Doc, 2026-09-09 evening:
 * "horizontal space is at a premium, vertical is abundant, reformat to take
 * advantage of this, please use larger font"): 26 columns, one fact per line,
 * and the glyph scale is chosen from the width so the columns fit. */
#ifndef K4510_PANEL_H
#define K4510_PANEL_H
#include <stdint.h>
typedef struct {
    double fps; const char *host; unsigned cpu_hz;
    int paused;            /* F8: the debugger's legend is shown and the state lines below it */
    int line;              /* the next scanline the machine will run (0 = a frame boundary) */
    unsigned trace_lines;  /* lines written to TRACE.TXT so far, 0 = not tracing */
    int trace_on;
    int dump_n;            /* the last dump written from the panel, 0 = none */
} panel_info;
#define PANEL_COLS 26
/* Draw into an ARGB buffer w x h (device pixels), glyphs scaled g times from
 * an 8-wide 1-bpp font of `rows` rows a glyph (8, or 16 for unscii-16). */
void panel_render(uint32_t *px, int pitch_px, int w, int h, int g, const uint8_t *font, int rows, const panel_info *info);
/* the glyph scale that gives PANEL_COLS columns in w pixels and at least 30 rows in h */
int  panel_scale(int w, int h, int rows);
/* one instruction as text, "1234 A900   LDA #$00"; returns its length in bytes */
int  panel_disasm(uint16_t pc, char *out, int outmax);
#endif
