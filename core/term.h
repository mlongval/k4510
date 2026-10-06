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
 *   $DA05-$DA0D RW   COLS ROWS OX OY CX CY FG BG STRIDE   the window: origin (OX,OY) cells, STRIDE cells per row.
 *                    The window is kept inside the console VICKY lays out ($D0B5/$D0B6): moved, not
 *                    shrunk, if it would cross a status band (2026-10-01).
 *   $DA0E RW FLAGS   bit0 cursor shown (blinking)   bit1 read: application cursor keys (DECCKM)
 *                    bit2 PETSCII mode
 *                    bit3 the status bands are the program's -- a DOOR onto VICKY's BANDCTL bit1
 *                    ($D0B2), which is where the claim lives since 2026-10-01; see core/vicky.h.
 *   $DA0F RW BANDTOP a door onto VICKY's BANDTOP ($D0B0)
 *   $DA16 RW BANDBOT a door onto VICKY's BANDBOT ($D0B1).  The three doors are kept for programs
 *                    written before VICKY owned the layout; new ones use $D0B0-$D0B2.
 *   $DA17 RW CODEPAGE 0 strict CP437 (power-on), 1 the K4510 page: JIM's table and the fonts follow
 *   $DA18 RW SCREEN  which screen JIM shows: 0 K/OS, 1 the terminal (a session on the Linux beneath or
 *                    beyond, /SYSTEM/ETC/TERMINAL.CFG; the TERMINAL command writes 1).  Alt+1 / Alt+2,
 *                    F12 > Screen and ESC ] 4510 ; kos BEL from the session switch too (2026-10-05)
 *   $DA10-$DA13 RW   BASE  28-bit address of the text32 map (reset: $030000)
 *   $DA14,$DA15 RW   DEFFG DEFBG   the colours SGR 0 / 39 / 49 return to
 * Sequences: the VT100 set (cursor, ED/EL, DECSTBM, DECSC/DECRC, IND/RI/NEL,
 * tabs, DECAWM/DECOM/DECCKM, DEC line drawing via ESC(0 and SO/SI, DSR, DA,
 * DECALN, RIS), ANSI SGR 0/1/4/5/7/22/24/27/30-37/39/40-47/49/90-97/100-107
 * and 38;5;n / 48;5;n for n < 16, VT220 ICH/DCH/IL/DL/ECH/SU/SD/CHA/VPA,
 * IRM, ESC[?25 cursor, ESC[s/u, DECSTR, DECSCUSR (ESC [ n SP q: 0-2 block, 3-4 underline,
 * 5-6 bar -- the shape VI changes with its mode). Bytes $80-$FF are glyphs (CP437).
 * UTF-8: ESC % G on, ESC % @ off (CTRL 1 leaves it).  On, a UTF-8 sequence draws as its
 * CP437 glyph (or a near one, or '?'), and a byte that continues no sequence is
 * CP437 as before.  The `!` shell and TELNET turn it on for their sessions.
 * Two K4510 additions, for a program that draws its whole screen through JIM
 * (EDIT, PROG, WORD; demo/jimscr.h), 2026-10-05: ESC[?4510h makes 38;5;n and
 * 48;5;n with n < 16 the palette's own entry n (SGR's ANSI order reaches only
 * twelve of the sixteen), ESC[?4510l puts xterm's meaning back; SGR 11 draws
 * the bytes $00-$1F and $7F as their glyphs (all but BS HT LF VT FF CR SO SI ESC: the
 * Linux console's display-control flag), SGR 10 stops.  A reset clears both. */
#ifndef K4510_TERM_H
#define K4510_TERM_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define IO_TERM 0xDA00u
void    term_reset(void);          /* power-on: geometry defaults too */
uint8_t term_read(uint8_t reg);
void    term_write(uint8_t reg, uint8_t v);
void    term_tick(void);           /* once a frame: the cursor blink */
void    term_host_session(int on); /* the `!` shell's session: UTF-8 on and LNM off; off gives LNM back */
int     term_cp437_utf8(uint8_t b, char *out);   /* a CP437 byte as UTF-8 (1-3 bytes), for a Unix host */
/* The bands (JIM's since 2026-10-05) and the second screen (core/io.c runs its
 * session).  Screen 0 is the machine's terminal, the one at $DA00; screen 1 a
 * terminal of JIM's own, shown in the console's place while it is up. */
void    term_bands_redraw(void);                 /* draw the bands again at the next frame */
int     term_screen(void);                       /* 0 K/OS, 1 the terminal */
void    term_screen_show(int n);
int     term_screen_request(void);               /* ESC ] 4510 ; kos / term asked for a screen: 0 or 1, -1 none */
void    term2_open(void);
int     term2_fit(int *cols, int *rows);         /* follows the console's geometry: 1 if it changed */
int     term2_size(int *cols, int *rows);
void    term2_feed(const uint8_t *b, size_t n);  /* the session's output */
size_t  term2_replies(uint8_t *out, size_t max); /* what it answers, and the keys turned into bytes */
void    term2_key(uint8_t k);                    /* a K4510 key code (or a plain byte), as JIM translates them */
void    term2_say(const char *s);
const uint16_t *term_page_table(void);   /* the code page in use, 256 Unicode values (core/codepage.h) */
void    term_set_page(int k4510);      /* 0 strict CP437 (the default), 1 the K4510 page: table and fonts */
int     term_get_page(void);
int     term_page_request(void);       /* -1, or the page the guest chose through $DA17 since the last call */
int     term_cursor_park(void);    /* take the cursor out of RAM (a save state); returns whether it was lit */
void    term_cursor_unpark(int was);
#ifdef __cplusplus
}
#endif
int     term_cell_h(void);                      /* 8 or 16: the pty's window size says so in pixels, for programs that draw pictures */
#endif
