/* JIM, the terminal ($DA00) -- the Beeb's third page, given a job: a VT100 with the ANSI colour and VT220 editing
 * additions, in hardware -- the way a real 8-bit machine got a serious
 * terminal: a card, not a program. It draws on the VICKY text32 screen the
 * ROM console uses, inside the geometry the ROM gives it, so the console
 * and the terminal share one screen and one cursor. Anything that needs a
 * terminal writes its byte stream here: the ROM for the Tube (CP/M
 * programs set up for VT100/ANSI, BBC BASIC's console edition) and
 * TELNET for the BBSes (ANSI-BBS: CP437 glyphs, 16 colours).
 *
 *   $DA00 W  DATA    a byte of the stream
 *   $DA01 R  STATUS  bit7 a reply byte waits; bit0 the stream moved the cursor since CX/CY were written
 *   $DA02 R  REPLY   the next reply byte (pops): answers to ESC[6n / ESC[c, and translated keys
 *   $DA03 W  KEY     a K4510 key code (io.h): its terminal bytes go to REPLY
 *                    (arrows ESC[A.. or ESC OA.. in application mode, Home/End, PgUp/PgDn/Ins ESC[n~,
 *                    Del $7F, F1-F4 ESC OP.., F5-F12 ESC[15~.., everything else through unchanged)
 *   $DA04 W  CTRL    1 reset (modes, attributes, cursor home; the screen kept)  2 clear the screen and home
 *   $DA05-$DA0D RW   COLS ROWS OX OY CX CY FG BG STRIDE   the window: origin (OX,OY) cells, STRIDE cells per row
 *   $DA0E RW FLAGS   bit0 cursor shown (blinking)   bit1 read: application cursor keys (DECCKM)
 *                    bit2 PETSCII mode
 *                    bit3 THE STATUS BANDS BELONG TO THE PROGRAM.  While it is set, K/OS lays the
 *                    console around $DA0F/$DA16 instead of the user's F7 heights, and stops drawing
 *                    into the bands at all -- no clock, no MHz, and cls() leaves those rows alone.
 *                    The program draws them itself and MUST clear the bit before it exits, the way
 *                    PETSCII mode must be cleared: leave it set and the shell comes back to a screen
 *                    whose furniture nobody is maintaining.
 *   $DA0F RW BANDTOP rows in the top band while bit3 is set
 *   $DA16 RW BANDBOT rows in the bottom band while bit3 is set.  Write the two, set bit3, then call
 *                    VIDEO ($FF92): K/OS re-lays the console around them.  Clear bit3 and call VIDEO
 *                    again to hand them back.  (JIM stores these and never reads them; the ROM does.
 *                    They live here because this is where the console's geometry lives, and because
 *                    the frontend rewrites the user's heights every frame, so a guest request has
 *                    nowhere else to survive.)
 *   $DA10-$DA13 RW   BASE  28-bit address of the text32 map (reset: $030000)
 *   $DA14,$DA15 RW   DEFFG DEFBG   the colours SGR 0 / 39 / 49 return to
 * Sequences: the VT100 set (cursor, ED/EL, DECSTBM, DECSC/DECRC, IND/RI/NEL,
 * tabs, DECAWM/DECOM/DECCKM, DEC line drawing via ESC(0 and SO/SI, DSR, DA,
 * DECALN, RIS), ANSI SGR 0/1/4/5/7/22/24/27/30-37/39/40-47/49/90-97/100-107
 * and 38;5;n / 48;5;n for n < 16, VT220 ICH/DCH/IL/DL/ECH/SU/SD/CHA/VPA,
 * IRM, ESC[?25 cursor, ESC[s/u, DECSTR. Bytes $80-$FF are glyphs (CP437). */
#ifndef K4510_TERM_H
#define K4510_TERM_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define IO_TERM 0xDA00u
void    term_reset(void);          /* power-on: geometry defaults too */
uint8_t term_read(uint8_t reg);
void    term_write(uint8_t reg, uint8_t v);
void    term_tick(void);           /* once a frame: the cursor blink */
#ifdef __cplusplus
}
#endif
#endif
