/* demo/jimcell.h -- a screen of cells, drawn through JIM's stream.
 *
 * For programs that think in cells -- a glyph, a foreground, a background at
 * (x, y) -- and used to write them straight into VICKY's text32 map.  Since
 * 2026-10-05 (the jim-everywhere branch; Doc: "Have everything DEFAULT to
 * writing via JIM") they send them to JIM as a terminal is sent text: the
 * cursor moved only when the next cell is not where the last one left it,
 * the colours only when they change.  So a program's screen can go wherever
 * JIM's stream goes -- a second screen, one day a pty.
 *
 * The stream is VT100 plus JIM's two K4510 modes (core/term.h), set by
 * jc_start(): ESC[?4510h, so 38;5;n is the palette's own entry n (SGR's ANSI
 * order reaches twelve of the sixteen), and SGR 11, so the glyphs below $20
 * (arrows, the card suits) draw rather than act -- the nine that act anyway
 * go as UTF-8 (jc_ch).  Autowrap is off: a cell in
 * the last column of the last row does not scroll the screen.  jc_end()
 * hands JIM back to the shell as it found it, cleared.
 *
 * A program that must read its screen back (menus over text, shadows) keeps
 * its own copy: demo/dosui.h does.  Positions are in JIM's window, which is
 * where the console is: (0, 0) its top-left cell.
 *
 * #include "k4510.h" first.  jc_cols is the window's width (jc_start sets
 * it from JIM; a program that clips to fewer columns may lower it). */
#ifndef K4510_JIMCELL_H
#define K4510_JIMCELL_H

#define JC_TERM 0xDA00u
static uint8_t jc_cols = 80, jc_rows = 30;
static uint8_t jc_x = 0xFF, jc_y, jc_f = 0xFF, jc_b = 0xFF;   /* JIM's cursor and colours as last sent; $FF: not known */

static void jc_put(char c) { REG(JC_TERM) = (uint8_t)c; }
static void jc_str(const char *t) { while (*t) jc_put(*t++); }
static void jc_num(uint8_t n)
{
    if (n >= 100) jc_put((char)('0' + n / 100));
    if (n >= 10) jc_put((char)('0' + n / 10 % 10));
    jc_put((char)('0' + n % 10));
}
static void jc_at(uint8_t x, uint8_t y)                /* JIM's cursor to (x, y), if it is not there already */
{
    if (x == jc_x && y == jc_y) return;
    jc_str("\x1b["); jc_num((uint8_t)(y + 1)); jc_put(';'); jc_num((uint8_t)(x + 1)); jc_put('H');
    jc_x = x; jc_y = y;
}
static void jc_col(uint8_t f, uint8_t b)               /* the palette's entries f on b, if they are not set already */
{
    if (f == jc_f && b == jc_b) return;
    jc_str("\x1b[38;5;"); jc_num(f); jc_str(";48;5;"); jc_num(b); jc_put('m');
    jc_f = f; jc_b = b;
}
/* The nine glyphs whose bytes act even under SGR 11 -- BS HT LF VT FF CR SO
 * SI ($08-$0F) and ESC ($1B) -- go as their Unicode characters, in UTF-8 for
 * that one character (ESC % G ... ESC % @): JIM draws a UTF-8 character as
 * its CP437 glyph.  FONTED shows every glyph of the font, so none may be lost. */
static const uint16_t jc_uni[9] = { 0x25D8, 0x25CB, 0x25D9, 0x2642, 0x2640, 0x266A, 0x266B, 0x263C, 0x2190 };
static void jc_ch(uint8_t c)                           /* a glyph at JIM's cursor, which moves on */
{
    if ((c >= 0x08 && c <= 0x0F) || c == 0x1B) {
        uint16_t u = jc_uni[c == 0x1B ? 8 : c - 8];
        jc_str("\x1b%G");
        jc_put((char)(0xE0 | (u >> 12))); jc_put((char)(0x80 | ((u >> 6) & 0x3F))); jc_put((char)(0x80 | (u & 0x3F)));
        jc_str("\x1b%@");
    } else jc_put((char)c);
    jc_x = jc_x < jc_cols - 1 ? (uint8_t)(jc_x + 1) : 0xFF;   /* the last column: where the cursor is now is JIM's business */
}
static void jc_cell(uint8_t x, uint8_t y, uint8_t g, uint8_t f, uint8_t b) { jc_at(x, y); jc_col(f, b); jc_ch(g); }
static void jc_cursor(uint8_t on) { jc_str(on ? "\x1b[?25h" : "\x1b[?25l"); }
static void jc_start(void)
{
    jc_cols = REG(JC_TERM + 5); jc_rows = REG(JC_TERM + 6);   /* the window's size: read, as a terminal's is asked */
    if (!jc_cols) jc_cols = 80;
    if (!jc_rows) jc_rows = 30;
    /* DECSTR (modes and colours to their defaults, the screen kept); ?4510;
     * SGR 11; no autowrap */
    jc_str("\x1b[!p\x1b[?4510h\x1b[11m\x1b[?7l");
    jc_x = jc_f = jc_b = 0xFF;
}
static void jc_end(void)
{
    /* the block cursor; DECSTR clears ?4510 and SGR 11 and puts autowrap
     * back; the screen cleared and the cursor home (the shell shows it) */
    jc_str("\x1b[2 q\x1b[!p\x1b[2J\x1b[H");
}
#endif
