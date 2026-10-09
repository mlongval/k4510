/* vtconf -- JIM against the VT100/VT220 (and the xterm the far end assumes).
 *
 * Byte streams in, the cell grid, the cursor, the colours and the replies out,
 * checked against what a real terminal does.  The reference is DEC's VT220
 * manual (EK-VT220-RM), vttest's expectations and xterm's ctlseqs; where they
 * differ, xterm wins, because TERM says xterm-color and that is what nvim,
 * tmux and mosh-client write for.
 *
 * A case JIM gets wrong today is KNOWN: it is printed with its sequence, what
 * a terminal does and what JIM does, and does not fail the suite.  A KNOWN
 * case that starts passing DOES fail it ("now passes"), so that whoever fixed
 * it takes the mark off.  Anything else that fails is a regression.
 *
 * test/termtest checks the K4510's own additions (the registers, PETSCII,
 * the code page, pictures, ESC[?4510h, SGR 11); this is the standard part.
 * Written for the JIM review, 2026-10-09. */
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <locale.h>
#include <wchar.h>
#include "../core/xemu/emutools_basicdefs.h"
#include "../core/xemu/cpu65.h"
#include "../core/mem.h"
#include "../core/io.h"
#include "../core/term.h"

#define COLS 80
#define ROWS 24
#define FG 1                       /* the default colours: white on black, so a colour that changes is plain */
#define BG 0
#define R(r) io_read(IO_TERM + (r))
#define W(r, v) io_write(IO_TERM + (r), (uint8_t)(v))
static int npass, nknown, nfail;
static const char *cur_case;
static void send(const char *s) { while (*s) W(0, (uint8_t) *s++); }
static void sendn(const char *s, size_t n) { while (n--) W(0, (uint8_t) *s++); }
static uint8_t *cell(int x, int y) { return k4510_ram + 0x030000 + ((size_t) y * COLS + (size_t) x) * 4; }
static uint8_t ch(int x, int y) { return cell(x, y)[0]; }
static int cx(void) { return R(9); }
static int cy(void) { return R(10); }
static char rbuf[ROWS][COLS + 1];
static const char *row(int y)                  /* the row's text, trailing blanks off */
{
    char *o = rbuf[y];
    for (int x = 0; x < COLS; x++) o[x] = (char) ch(x, y);
    o[COLS] = 0;
    for (int x = COLS - 1; x >= 0 && o[x] == ' '; x--) o[x] = 0;
    return o;
}
static char repbuf[256];
static const char *reply(void) { int i = 0; while ((R(1) & 0x80) && i < 255) repbuf[i++] = (char) R(2); repbuf[i] = 0; return repbuf; }
static const char *vis(const char *s) { static char b[4][512]; static int k; char *o = b[k++ & 3]; int n = 0;   /* ESC as \e */
    for (; *s && n < 500; s++) { if (*s == 27) { o[n++] = '\\'; o[n++] = 'e'; } else if ((uint8_t) *s < 32) n += sprintf(o + n, "\\x%02X", (uint8_t) *s); else o[n++] = *s; }
    o[n] = 0; return o; }

/* A fresh terminal for every case: RIS, then the K4510's own leftovers off
 * (LNM, UTF-8), the cursor hidden so its reverse-bit block stays out of the cells. */
static void fresh(const char *name)
{
    cur_case = name;
    W(0x0E, 0);
    send("\033c\033[20l\033%@");
    term_host_session(0);
    W(4, 2);
    reply();
}
static void verdict(int known, int ok, const char *seq, const char *expect, const char *fmt, ...)
{
    char got[512]; va_list ap; va_start(ap, fmt); vsnprintf(got, sizeof got, fmt, ap); va_end(ap);
    if (ok && !known) { npass++; return; }
    if (!ok && known) { nknown++; printf("  known  %-34s %s\n         want: %s\n         JIM:  %s\n", cur_case, vis(seq), expect, got); return; }
    nfail++;
    if (ok) printf("  NOW PASSES (take the KNOWN mark off)  %s %s\n", cur_case, vis(seq));
    else    printf("  FAIL   %-34s %s\n         want: %s\n         JIM:  %s\n", cur_case, vis(seq), expect, got);
}
#define OK(cond, seq, expect, ...)    verdict(0, (cond), seq, expect, __VA_ARGS__)
#define KNOWN(cond, seq, expect, ...) verdict(1, (cond), seq, expect, __VA_ARGS__)
#define AT(x, y) (cx() == (x) && cy() == (y))
#define CUR "cursor (%d,%d)", cx(), cy()
#define CURF "cursor (%d,%d)"   /* after other values: "... " CURF, a, b, cx(), cy() */
static void fill80(int y, char c) { char b[16]; snprintf(b, sizeof b, "\033[%d;1H", y + 1); send(b); for (int i = 0; i < COLS; i++) { char s[2] = { c, 0 }; send(s); } }
static void label_rows(void) { for (int y = 0; y < ROWS; y++) { char b[32]; snprintf(b, sizeof b, "\033[%d;1HL%02d", y + 1, y); send(b); } }

static void t_cursor(void)
{
    const char *s;
    fresh("CUP");
    send(s = "\033[5;10H"); OK(AT(9, 4), s, "(9,4)", CUR);
    send(s = "\033[;5H");   OK(AT(4, 0), s, "(4,0): an empty parameter is the default", CUR);
    send(s = "\033[0;0H");  OK(AT(0, 0), s, "(0,0): 0 is the default", CUR);
    send(s = "\033[99;199H"); OK(AT(79, 23), s, "(79,23): clamped to the screen", CUR);
    send(s = "\033[5;5f");  OK(AT(4, 4), s, "(4,4): HVP is CUP", CUR);
    fresh("CUU/CUD/CUF/CUB");
    send(s = "\033[10;10H\033[0A"); OK(AT(9, 8), s, "(9,8): 0 counts as 1", CUR);
    send(s = "\033[3B");    OK(AT(9, 11), s, "(9,11)", CUR);
    send(s = "\033[99D");   OK(AT(0, 11), s, "(0,11): stops at the left edge", CUR);
    send(s = "\033[99C");   OK(AT(79, 11), s, "(79,11): stops at the right edge", CUR);
    send(s = "\033[99A");   OK(AT(79, 0), s, "(79,0): stops at the top", CUR);
    send(s = "\033[99B");   OK(AT(79, 23), s, "(79,23): stops at the bottom", CUR);
    fresh("CUU/CUD stop at the margins");
    send(s = "\033[5;10r\033[7;1H\033[20A"); KNOWN(AT(0, 4), s, "(0,4): CUU inside the region stops at its top margin (VT100, xterm)", CUR);
    send(s = "\033[5;10r\033[7;1H\033[20B"); KNOWN(AT(0, 9), s, "(0,9): CUD inside the region stops at its bottom margin", CUR);
    send(s = "\033[5;10r\033[3;1H\033[A");   OK(AT(0, 1), s, "(0,1): above the region the margin does not apply", CUR);
    send(s = "\033[5;10r\033[12;1H\033[30B"); OK(AT(0, 23), s, "(0,23): below the region, the screen's bottom", CUR);
    fresh("CNL/CPL/CHA/HPA/VPA/HPR/VPR");
    send(s = "\033[5;5H\033[2E"); OK(AT(0, 6), s, "(0,6)", CUR);
    send(s = "\033[5;5H\033[2F"); OK(AT(0, 2), s, "(0,2)", CUR);
    send(s = "\033[3;3H\033[20G"); OK(AT(19, 2), s, "(19,2)", CUR);
    send(s = "\033[3;3H\033[20`"); OK(AT(19, 2), s, "(19,2): HPA", CUR);
    send(s = "\033[3;3H\033[7d"); OK(AT(2, 6), s, "(2,6): VPA keeps the column", CUR);
    send(s = "\033[3;3H\033[3a\033[2e"); OK(AT(5, 4), s, "(5,4): HPR, VPR", CUR);
    fresh("C0 controls");
    send(s = "\bab\b\bX"); OK(!strcmp(row(0), "Xb") && AT(1, 0), s, "\"Xb\", (1,0): BS at column 0 stays", "\"%s\" " CURF, row(0), cx(), cy());
    send(s = "\033[2;5Hx\ny"); OK(ch(5, 2) == 'y' && AT(6, 2), s, "y at (5,2): LF keeps the column (LNM off)", "cursor (%d,%d)", cx(), cy());
    send(s = "\033[2;5H\013\014"); OK(AT(4, 3), s, "(4,3): VT and FF are LF", CUR);
    send(s = "\033[2;5H\033E"); OK(AT(0, 2), s, "(0,2): NEL", CUR);
    send(s = "\033[2;5H\033D"); OK(AT(4, 2), s, "(4,2): IND keeps the column", CUR);
    send(s = "\033[20h\033[2;5H\n"); OK(AT(0, 2), s, "(0,2): LNM on, LF is a new line", CUR);
    send("\033[20l");
    send("\033[10;1H"); sendn(s = "a\000\177\007b", 5); OK(!strcmp(row(9), "ab"), "a NUL DEL BEL b", "\"ab\": NUL, DEL, BEL draw nothing", "\"%s\"", row(9));
}

static void t_wrap(void)
{
    const char *s = "80 x 'a', then X";
    fresh("autowrap and the last column");
    fill80(0, 'a'); OK(AT(79, 0), s, "(79,0) after the 80th: the wrap waits", CUR);
    send("X"); OK(ch(79, 0) == 'a' && ch(0, 1) == 'X' && AT(1, 1), s, "X at (0,1), cursor (1,1)", "(79,0)='%c' (0,1)='%c' " CURF, ch(79, 0), ch(0, 1), cx(), cy());
    fresh("pending wrap and CR");
    fill80(0, 'a'); send("\rY"); OK(ch(0, 0) == 'Y' && AT(1, 0), "80 x 'a', CR, Y", "Y at (0,0): CR cancels the wrap", "(0,0)='%c' " CURF, ch(0, 0), cx(), cy());
    fresh("pending wrap and BS");
    fill80(0, 'a'); send("\bZ"); OK(ch(78, 0) == 'Z' && ch(79, 0) == 'a' && AT(79, 0), "80 x 'a', BS, Z", "Z at (78,0)", "(78,0)='%c' (79,0)='%c' " CURF, ch(78, 0), ch(79, 0), cx(), cy());
    fresh("pending wrap and LF");
    fill80(0, 'a'); send("\nQ"); OK(ch(79, 1) == 'Q' && ch(0, 1) == ' ' && AT(79, 1), "80 x 'a', LF, Q", "Q at (79,1): LF ends the wait, no wrap", "(79,1)='%c' (0,1)='%c' " CURF, ch(79, 1), ch(0, 1), cx(), cy());
    fresh("pending wrap and CUP");
    fill80(0, 'a'); send("\033[1;79HZW"); OK(ch(78, 0) == 'Z' && ch(79, 0) == 'W' && ch(0, 1) == ' ' && AT(79, 0), "80 x 'a', ESC[1;79H, ZW", "ZW at 78-79 and no wrap", "(0,1)='%c' " CURF, ch(0, 1), cx(), cy());
    fresh("pending wrap and CPR");
    fill80(0, 'a'); send("\033[6n"); OK(!strcmp(reply(), "\033[1;80R"), "80 x 'a', ESC[6n", "\\e[1;80R", "%s", vis(repbuf));
    fresh("DECAWM off");
    send("\033[?7l"); fill80(0, 'a'); send("bcdef"); OK(ch(79, 0) == 'f' && ch(0, 1) == ' ' && AT(79, 0), "ESC[?7l, 85 characters", "the last at (79,0), nothing wraps", "(79,0)='%c' (0,1)='%c' " CURF, ch(79, 0), ch(0, 1), cx(), cy());
    fresh("wrap at the bottom scrolls");
    label_rows(); fill80(23, 'a'); send("Z");
    OK(!strcmp(row(0), "L01") && ch(0, 22) == 'a' && ch(0, 23) == 'Z' && AT(1, 23), "row 24 full, then Z", "the screen up one, Z at (0,23)", "row0 \"%s\" (0,23)='%c' " CURF, row(0), ch(0, 23), cx(), cy());
    fresh("wrap at the region's bottom");
    label_rows(); send("\033[5;10r"); fill80(9, 'a'); send("Z");
    OK(!strcmp(row(3), "L03") && !strcmp(row(4), "L05") && ch(0, 9) == 'Z' && !strcmp(row(10), "L10"), "ESC[5;10r, row 10 full, Z", "rows 5-10 up one, the rest still", "row3 \"%s\" row4 \"%s\" row10 \"%s\"", row(3), row(4), row(10));
}

static void t_region(void)
{
    const char *s;
    fresh("DECSTBM homes the cursor");
    send(s = "\033[10;10H\033[5;10r"); OK(AT(0, 0), s, "(0,0)", CUR);
    send(s = "\033[10;5r"); OK(AT(0, 0), s, "an inverted region is ignored", CUR);
    fresh("LF below the region");
    label_rows(); send(s = "\033[5;10r\033[24;1H\n"); OK(!strcmp(row(0), "L00") && AT(0, 23), s, "no scroll at the screen's bottom outside the region", "row0 \"%s\" " CURF, row(0), cx(), cy());
    fresh("RI at the region's top");
    label_rows(); send(s = "\033[5;10r\033[5;1H\033M");
    OK(!strcmp(row(3), "L03") && row(4)[0] == 0 && !strcmp(row(5), "L04") && !strcmp(row(10), "L10") && AT(0, 4), s, "rows 5-10 down one, a blank at 5", "row4 \"%s\" row5 \"%s\" row10 \"%s\"", row(4), row(5), row(10));
    fresh("RI at row 1, above the region");
    label_rows(); send(s = "\033[5;10r\033[1;1H\033M"); OK(!strcmp(row(0), "L00") && AT(0, 0), s, "nothing moves", "row0 \"%s\"", row(0));
    fresh("IND in the region");
    label_rows(); send(s = "\033[5;10r\033[10;1H\033D"); OK(!strcmp(row(4), "L05") && row(9)[0] == 0 && !strcmp(row(10), "L10"), s, "rows 5-10 up one", "row4 \"%s\" row9 \"%s\"", row(4), row(9));
    fresh("IL in the region");
    label_rows(); send(s = "\033[5;10r\033[6;3H\033[2L");
    OK(!strcmp(row(4), "L04") && row(5)[0] == 0 && row(6)[0] == 0 && !strcmp(row(7), "L05") && !strcmp(row(9), "L07") && !strcmp(row(10), "L10") && AT(0, 5), s,
       "two blanks at 6-7, L08 L09 lost, row 11 kept, column 0", "rows 5..10: %s|%s|%s|%s|%s " CURF, row(5), row(6), row(7), row(9), row(10), cx(), cy());
    fresh("IL outside the region");
    label_rows(); send(s = "\033[5;10r\033[12;1H\033[L"); OK(!strcmp(row(11), "L11") && !strcmp(row(12), "L12"), s, "ignored", "row11 \"%s\"", row(11));
    fresh("IL more than the room");
    label_rows(); send(s = "\033[5;10r\033[8;1H\033[99L"); OK(!strcmp(row(6), "L06") && row(7)[0] == 0 && row(9)[0] == 0 && !strcmp(row(10), "L10"), s, "rows 8-10 blank, 11 kept", "row7 \"%s\" row10 \"%s\"", row(7), row(10));
    fresh("DL in the region");
    label_rows(); send(s = "\033[5;10r\033[6;1H\033[2M");
    OK(!strcmp(row(5), "L07") && !strcmp(row(7), "L09") && row(8)[0] == 0 && row(9)[0] == 0 && !strcmp(row(10), "L10"), s, "L07.. up to row 6, blanks at 9-10", "rows 6,8,9,10,11: %s|%s|%s|%s|%s", row(5), row(7), row(8), row(9), row(10));
    fresh("DL outside the region");
    label_rows(); send(s = "\033[5;10r\033[2;1H\033[M"); OK(!strcmp(row(1), "L01"), s, "ignored", "row1 \"%s\"", row(1));
    fresh("SU/SD");
    label_rows(); send(s = "\033[5;10r\033[7;7H\033[2S");
    OK(!strcmp(row(4), "L06") && row(8)[0] == 0 && !strcmp(row(10), "L10") && AT(6, 6), s, "the region up two, the cursor stays", "row4 \"%s\" row8 \"%s\" " CURF, row(4), row(8), cx(), cy());
    send(s = "\033[3T"); OK(row(4)[0] == 0 && row(6)[0] == 0 && !strcmp(row(7), "L06") && !strcmp(row(3), "L03"), s, "the region down three", "row4 \"%s\" row7 \"%s\"", row(4), row(7));
    fresh("SU without a region");
    label_rows(); send(s = "\033[S"); OK(!strcmp(row(0), "L01") && row(23)[0] == 0, s, "the screen up one", "row0 \"%s\"", row(0));
    fresh("background colour erase in a scroll");
    send(s = "\033[44m\033[S"); OK(cell(0, 23)[3] == 6, s, "the new line has the current background (bce)", "bg %d", cell(0, 23)[3]);
}

static void t_erase(void)
{
    const char *s;
    fresh("EL 0/1/2");
    fill80(0, 'a'); send(s = "\033[1;10H\033[K"); OK(ch(8, 0) == 'a' && ch(9, 0) == ' ' && ch(79, 0) == ' ', s, "columns 9-79 erased (the cursor's too)", "\"%s\"", row(0));
    fill80(0, 'a'); send(s = "\033[1;10H\033[1K"); OK(ch(9, 0) == ' ' && ch(0, 0) == ' ' && ch(10, 0) == 'a', s, "columns 0-9 erased", "\"%s\"", row(0));
    fill80(0, 'a'); send(s = "\033[1;10H\033[2K"); OK(row(0)[0] == 0 && AT(9, 0), s, "the line, the cursor kept", "\"%s\" " CURF, row(0), cx(), cy());
    fresh("ED 0/1/2");
    label_rows(); send(s = "\033[5;2H\033[J"); OK(!strcmp(row(3), "L03") && !strcmp(row(4), "L") && row(5)[0] == 0, s, "from the cursor on", "row4 \"%s\" row5 \"%s\"", row(4), row(5));
    label_rows(); send(s = "\033[5;2H\033[1J"); OK(row(3)[0] == 0 && ch(1, 4) == ' ' && ch(2, 4) == '4' && !strcmp(row(5), "L05"), s, "to the cursor, inclusive", "row4 \"%s\" row5 \"%s\"", row(4), row(5));
    label_rows(); send(s = "\033[5;2H\033[2J"); OK(row(0)[0] == 0 && row(23)[0] == 0 && AT(1, 4), s, "all, the cursor kept", "row0 \"%s\" " CURF, row(0), cx(), cy());
    fresh("ED 3");
    label_rows(); send(s = "\033[3J"); KNOWN(!strcmp(row(0), "L00"), s, "the screen untouched: 3 is the scrollback (xterm)", "row0 \"%s\"", row(0));
    fresh("ECH");
    fill80(0, 'a'); send(s = "\033[1;5H\033[3X"); OK(ch(3, 0) == 'a' && ch(4, 0) == ' ' && ch(6, 0) == ' ' && ch(7, 0) == 'a' && AT(4, 0), s, "three blanks, the cursor stays", "\"%s\" " CURF, row(0), cx(), cy());
    fill80(0, 'a'); send(s = "\033[1;78H\033[9X"); OK(ch(76, 0) == 'a' && ch(77, 0) == ' ' && ch(79, 0) == ' ', s, "clamped at the edge", "\"%s\"", row(0));
    fresh("ICH");
    send(s = "abcdef\033[1;3H\033[2@"); OK(!strcmp(row(0), "ab  cdef") && AT(2, 0), s, "\"ab  cdef\"", "\"%s\" " CURF, row(0), cx(), cy());
    fill80(1, 'a'); send(s = "\033[2;75H\033[99@"); OK(ch(73, 1) == 'a' && ch(74, 1) == ' ' && ch(79, 1) == ' ', s, "the rest of the line blank", "\"%s\"", row(1));
    fresh("DCH");
    send(s = "abcdef\033[1;2H\033[2P"); OK(!strcmp(row(0), "adef"), s, "\"adef\"", "\"%s\"", row(0));
    send(s = "\033[41m\033[1;1H\033[P"); OK(cell(79, 0)[3] == 2, s, "the cell brought in at the right has the current background", "bg %d", cell(79, 0)[3]);
    fresh("erase uses the current background");
    send(s = "\033[44m\033[2K"); OK(cell(40, 0)[3] == 6 && cell(40, 0)[2] == FG, s, "bg blue (6), fg the default", "fg %d bg %d", cell(40, 0)[2], cell(40, 0)[3]);
    fresh("IRM");
    send(s = "abc\033[1G\033[4hXY\033[4lZ"); OK(!strcmp(row(0), "XYZbc"), s, "\"XYZbc\": X and Y pushed abc right, Z replaced the a", "\"%s\"", row(0));
    fill80(1, 'a'); send("\033[2;80Hb\033[2;1H\033[4hQ\033[4l"); OK(ch(0, 1) == 'Q' && ch(79, 1) == 'a', "row full, insert Q at column 0", "the last character falls off", "(79,1)='%c'", ch(79, 1));
}

static void t_origin(void)
{
    const char *s;
    fresh("DECOM");
    send(s = "\033[5;10r\033[?6h"); OK(AT(0, 4), s, "(0,4): home is the region's top", CUR);
    send(s = "\033[2;3H"); OK(AT(2, 5), s, "(2,5): CUP is relative", CUR);
    send(s = "\033[99;1H"); OK(AT(0, 9), s, "(0,9): kept in the region", CUR);
    send(s = "\033[6n"); OK(!strcmp(reply(), "\033[6;1R"), s, "\\e[6;1R: CPR is relative too", "%s", vis(repbuf));
    send(s = "\033[?6l"); OK(AT(0, 0), s, "(0,0): reset homes to the screen", CUR);
    fresh("DECSC keeps DECOM");
    send(s = "\033[5;10r\033[?6h\0337\033[?6l\0338\033[1;1H"); OK(AT(0, 4), s, "(0,4): origin mode came back with DECRC", CUR);
}

static void t_tabs(void)
{
    const char *s;
    fresh("HT");
    send(s = "\tx\ty"); OK(ch(8, 0) == 'x' && ch(16, 0) == 'y', s, "x at 8, y at 16", "\"%s\"", row(0));
    send(s = "\033[1;76H\t\tz"); OK(ch(79, 0) == 'z', s, "z at 79: HT stops at the last column", "\"%s\"", row(0));
    fresh("HTS/TBC");
    send(s = "\033[1;4H\033H\033[1;1H\tx"); OK(ch(3, 0) == 'x', s, "x at 3", "\"%s\"", row(0));
    send(s = "\033[1;4H\033[0g\033[2;1H\tx"); OK(ch(8, 1) == 'x', s, "x at 8: the stop at 3 is gone", "\"%s\"", row(1));
    send(s = "\033[3g\033[3;1H\tx"); OK(ch(79, 2) == 'x', s, "x at 79: no stops left", "\"%s\"", row(2));
    fresh("CBT/CHT");
    send(s = "\033[1;21H\033[Z"); OK(AT(16, 0), s, "(16,0)", CUR);
    send(s = "\033[1;4H\033[Z"); OK(AT(0, 0), s, "(0,0)", CUR);
    send(s = "\033[1;1H\033[2I"); KNOWN(AT(16, 0), s, "(16,0): CHT, forward two stops", CUR);
}

static void t_sgr(void)
{
    const char *s;
    fresh("SGR colours");
    send(s = "\033[31;42mA"); OK(cell(0, 0)[2] == 2 && cell(0, 0)[3] == 5, s, "red (2) on green (5)", "fg %d bg %d", cell(0, 0)[2], cell(0, 0)[3]);
    send(s = "\033[mB"); OK(cell(1, 0)[2] == FG && cell(1, 0)[3] == BG, s, "ESC[m is SGR 0", "fg %d bg %d", cell(1, 0)[2], cell(1, 0)[3]);
    send(s = "\033[31;42m\033[39mC"); OK(cell(2, 0)[2] == FG && cell(2, 0)[3] == 5, s, "39: the default foreground only", "fg %d bg %d", cell(2, 0)[2], cell(2, 0)[3]);
    send(s = "\033[49mD"); OK(cell(3, 0)[3] == BG, s, "49: the default background", "bg %d", cell(3, 0)[3]);
    send(s = "\033[0;1;31mE"); OK(cell(4, 0)[2] == 10, s, "bold red is light red (10)", "fg %d", cell(4, 0)[2]);
    send(s = "\033[22mF"); OK(cell(5, 0)[2] == 2, s, "22: red again", "fg %d", cell(5, 0)[2]);
    send(s = "\033[0;7;31;42mG"); OK(cell(6, 0)[2] == 5 && cell(6, 0)[3] == 2, s, "reverse: green on red", "fg %d bg %d", cell(6, 0)[2], cell(6, 0)[3]);
    send(s = "\033[27mH"); OK(cell(7, 0)[2] == 2 && cell(7, 0)[3] == 5, s, "27: red on green", "fg %d bg %d", cell(7, 0)[2], cell(7, 0)[3]);
    send(s = "\033[0;91;104mI"); OK(cell(8, 0)[2] == 10 && cell(8, 0)[3] == 14, s, "90-97 / 100-107: the bright set", "fg %d bg %d", cell(8, 0)[2], cell(8, 0)[3]);
    send(s = "\033[0;38;5;1;48;5;196mJ"); OK(cell(9, 0)[2] == 2 && cell(9, 0)[3] == 10, s, "38;5;1 red, 48;5;196 bright red", "fg %d bg %d", cell(9, 0)[2], cell(9, 0)[3]);
    send(s = "\033[0;38;2;0;205;0mK"); OK(cell(10, 0)[2] == 5, s, "38;2: the nearest, green", "fg %d", cell(10, 0)[2]);
    send(s = "\033[0;3;9;2;8;53mL"); OK(ch(11, 0) == 'L' && cx() == 12, s, "unknown attributes ignored, nothing printed", "\"%s\"", row(0));
    fresh("SGR with sub-parameters");
    send(s = "\033[38:2::255:0:0mA"); KNOWN(!strcmp(row(0), "A") && cell(0, 0)[2] == 10, s, "\"A\" in bright red: ITU T.416 colons (xterm, kitty, tmux's RGB)", "\"%s\" fg %d", row(0), cell(0, 0)[2]);
    fresh("SGR 4:3 (undercurl)");
    send(s = "\033[4:3mA\033[4:0m"); KNOWN(!strcmp(row(0), "A"), s, "\"A\": nvim's undercurl, tmux's Smulx", "\"%s\"", row(0));
    fresh("SGR 58 (underline colour)");
    send(s = "\033[58:2::255:0:0mA\033[59m"); KNOWN(!strcmp(row(0), "A"), s, "\"A\"", "\"%s\"", row(0));
    send(s = "\033[2;1H\033[58;5;196mB\033[59m"); OK(!strcmp(row(1), "B"), s, "\"B\": the semicolon form", "\"%s\"", row(1));
}

static void t_save(void)
{
    const char *s;
    fresh("DECSC/DECRC");
    send(s = "\033[5;6H\033[1;31;42m\033(0\0337\033[m\033(B\033[1;1H\0338q");
    OK(ch(5, 4) == 0xC4 && cell(5, 4)[2] == 10 && cell(5, 4)[3] == 5, s, "a line glyph (C4), light red on green, at (5,4)", "'%c'=%02X fg %d bg %d", ch(5, 4), ch(5, 4), cell(5, 4)[2], cell(5, 4)[3]);
    fresh("DECRC without DECSC");
    send(s = "\033[31m\033[5;5H\0338X"); OK(ch(0, 0) == 'X' && cell(0, 0)[2] == FG, s, "home, default attributes", "(0,0)='%c' fg %d", ch(0, 0), cell(0, 0)[2]);
    fresh("CSI s / CSI u");
    send(s = "\033[3;4H\033[s\033[9;9H\033[u"); OK(AT(3, 2), s, "(3,2)", CUR);
    fresh("DECSC keeps SO/G1");
    send(s = "\033)0\016\0337\017\0338q\017"); OK(ch(0, 0) == 0xC4, s, "C4: shifted out to G1 again", "%02X", ch(0, 0));
}

static void t_charsets(void)
{
    const char *s;
    fresh("DEC special graphics");
    send(s = "\033(0`afgjklmnqtuvwxyz{|}~\033(B");
    static const uint8_t want[] = { 0x04, 0xB1, 0xF8, 0xF1, 0xD9, 0xBF, 0xDA, 0xC0, 0xC5, 0xC4, 0xC3, 0xB4, 0xC1, 0xC2, 0xB3, 0xF3, 0xF2, 0xE3, 0xF0, 0x9C, 0xFA };
    int bad = -1; for (int i = 0; i < (int) sizeof want; i++) if (ch(i, 0) != want[i]) { bad = i; break; }
    OK(bad < 0, s, "each as its CP437 glyph", "the %dth wrong (%02X)", bad, bad < 0 ? 0 : ch(bad, 0));
    send(s = "\033[2;1Hq\033(0q\033(Bq"); OK(ch(0, 1) == 'q' && ch(1, 1) == 0xC4 && ch(2, 1) == 'q', s, "q, a line, q", "%02X %02X %02X", ch(0, 1), ch(1, 1), ch(2, 1));
    send(s = "\033[3;1H\033)0\016x\017x"); OK(ch(0, 2) == 0xB3 && ch(1, 2) == 'x', s, "SO: G1, SI: G0", "%02X %02X", ch(0, 2), ch(1, 2));
    fresh("G2/G3 designation");
    send(s = "\033*0\033+BX"); KNOWN(!strcmp(row(0), "X"), s, "\"X\": ESC * F and ESC + F take their final byte", "\"%s\"", row(0));
    fresh("ESC SP F (S7C1T)");
    send(s = "\033 FX"); KNOWN(!strcmp(row(0), "X"), s, "\"X\": an escape with an intermediate takes its final byte", "\"%s\"", row(0));
}

static void t_utf8(void)
{
    const char *s;
    fresh("UTF-8 decoding");
    send("\033%G");
    send(s = "\xC3\xA9\xE2\x94\x80\xF0\x9F\x98\x80x"); OK(ch(0, 0) == 0x82 && ch(1, 0) == 0xC4 && ch(4, 0) == 'x' && cx() == 5, s, "e-acute, a line, a wide emoji (2 cells), x", "%02X %02X .. x at %d", ch(0, 0), ch(1, 0), cx() - 1);
    send("\r\n"); send(s = "a\x80" "b"); OK(cx() == 3, s, "three cells: a lone continuation is one replacement (xterm U+FFFD)", "%d cells", cx());
    send("\r\n"); send(s = "\xC0\x80" "b"); OK(cx() == 3, s, "three cells: an overlong is two replacements", "%d cells", cx());
    send("\r\n"); send(s = "\xE2\x94" "A"); KNOWN(cx() == 2, s, "two cells: a cut sequence is ONE replacement, then A (Unicode's maximal subpart)", "%d cells (the two bytes drawn as CP437)", cx());
    send("\r\n"); send(s = "\xF0\x9F\x98" "A"); KNOWN(cx() == 2, s, "two cells", "%d cells", cx());
    send("\r\n"); send(s = "e\xCC\x81" "x"); OK(cx() == 2, s, "two cells: a combining accent takes none", "%d cells", cx());
    fresh("UTF-8 wide character at the last column");
    send("\033%G\033[1;80H"); send(s = "\xE4\xB8\xAD"); KNOWN(ch(79, 0) == ' ' && ch(0, 1) != ' ' && AT(2, 1), s, "it wraps first: at (0,1)-(1,1), cursor (2,1)", "(79,0)=%02X (0,1)=%02X " CURF, ch(79, 0), ch(0, 1), cx(), cy());
    fresh("UTF-8 width of emoji");
    send("\033%G"); send(s = "\xE2\x9C\x85x"); KNOWN(ch(2, 0) == 'x', s, "x at 2: U+2705 is wide (Unicode 9+, glibc, nvim, tmux)", "x at %d", cx() - 1);
    send("\r\n"); send(s = "\xF0\x9F\xA7\xAA" "x"); OK(cx() == 3, s, "x at 2: U+1F9EA is wide", "x at %d", cx() - 1);
    send("\r\n"); send(s = "\xF0\x9F\xAB\xA0" "x"); KNOWN(cx() == 3, s, "x at 2: U+1FAE0 (Unicode 14) is wide", "x at %d", cx() - 1);
    fresh("UTF-8 and DEC graphics");
    send("\033%G"); send(s = "\033(0q\033(B"); OK(ch(0, 0) == 0xC4, s, "C4: the line drawing set works in UTF-8 too", "%02X", ch(0, 0));
}

/* The width JIM gives every character, against glibc's wcwidth: tmux, nvim's
 * own tables and mosh all count columns by Unicode's widths, so a character
 * JIM counts differently moves everything after it on the line. */
static void t_widths(void)
{
    if (!setlocale(LC_CTYPE, "C.UTF-8") && !setlocale(LC_CTYPE, "en_US.UTF-8")) { printf("  (no UTF-8 locale here: the width sweep skipped)\n"); return; }
    fresh("widths against wcwidth");
    send("\033%G");
    long n = 0, diff = 0, cdiff = 0, narrow_wide = 0, wide_narrow = 0; char ex[8][24]; int nex = 0;
    for (uint32_t u = 0xA0; u < 0x30000; u++) {
        if (u >= 0xD800 && u <= 0xDFFF) continue;
        int w = wcwidth((wchar_t) u);
        if (w < 0) continue;
        char b[8]; int l;
        if (u < 0x800) { b[0] = (char)(0xC0 | (u >> 6)); b[1] = (char)(0x80 | (u & 0x3F)); l = 2; }
        else if (u < 0x10000) { b[0] = (char)(0xE0 | (u >> 12)); b[1] = (char)(0x80 | ((u >> 6) & 0x3F)); b[2] = (char)(0x80 | (u & 0x3F)); l = 3; }
        else { b[0] = (char)(0xF0 | (u >> 18)); b[1] = (char)(0x80 | ((u >> 12) & 0x3F)); b[2] = (char)(0x80 | ((u >> 6) & 0x3F)); b[3] = (char)(0x80 | (u & 0x3F)); l = 4; }
        send("\033[1;10H"); sendn(b, (size_t) l);
        int jw = cx() - 9; n++;
        int common = (u >= 0x2000 && u < 0x2C00) || (u >= 0x1F000 && u < 0x1FB00);   /* symbols, arrows, boxes, emoji */
        if (jw != w) { diff++; if (w == 2) wide_narrow++; else if (jw == 2) narrow_wide++;
                       if (common) { cdiff++; if (nex < 8) snprintf(ex[nex++], sizeof ex[0], "U+%04X %d/%d", u, jw, w); } }
    }
    printf("  info   %ld characters U+00A0..U+2FFFF: JIM's width differs from glibc's wcwidth on %ld"
           " (%ld wide that JIM counts narrow, %ld the other way)\n"
           "         %ld of them among the symbols and emoji a terminal meets (U+2000-2BFF, U+1F000-1FAFF), e.g.", n, diff, wide_narrow, narrow_wide, cdiff);
    for (int i = 0; i < nex; i++) printf(" %s", ex[i]);
    printf("  (JIM/wcwidth)\n");
}

static void t_replies(void)
{
    const char *s;
    fresh("DSR/CPR/DA");
    send(s = "\033[5n"); OK(!strcmp(reply(), "\033[0n"), s, "\\e[0n", "%s", vis(repbuf));
    send(s = "\033[3;7H\033[6n"); OK(!strcmp(reply(), "\033[3;7R"), s, "\\e[3;7R", "%s", vis(repbuf));
    send(s = "\033[c"); OK(!strncmp(reply(), "\033[?62;", 6), s, "\\e[?62;...c (a VT220)", "%s", vis(repbuf));
    send(s = "\033[0c"); OK(!strncmp(reply(), "\033[?62;", 6), s, "the same for 0", "%s", vis(repbuf));
    send(s = "\033Z"); OK(!strncmp(reply(), "\033[?62;", 6), s, "DECID: the DA answer", "%s", vis(repbuf));
    send(s = "\033[>c"); OK(!strncmp(reply(), "\033[>1;", 5), s, "\\e[>1;...c (DA2: a VT220)", "%s", vis(repbuf));
    send(s = "\033[=c"); OK(reply()[0] == 0 || !strncmp(repbuf, "\033P!|", 4), s, "nothing, or DECRPTUI (DA3)", "%s", vis(repbuf));
    fresh("DECRQM");
    send(s = "\033[?2026$p"); KNOWN(!strcmp(reply(), "\033[?2026;2$y"), s, "\\e[?2026;2$y: nvim asks this, and without an answer never sends ?2026",
                                 "%s", repbuf[0] ? vis(repbuf) : "no answer");
    send(s = "\033[?7$p"); KNOWN(!strcmp(reply(), "\033[?7;1$y"), s, "\\e[?7;1$y (DECAWM set)", "%s", repbuf[0] ? vis(repbuf) : "no answer");
}

static void t_resets(void)
{
    const char *s;
    fresh("RIS");
    send("\033[5;10r\033[?6h\033[4h\033[?7l\033[31m\033(0\033[3g\033[10;10Habc");
    send(s = "\033cx\ty");
    OK(ch(0, 0) == 'x' && ch(8, 0) == 'y' && cell(0, 0)[2] == FG && row(9)[0] == 0, s, "cleared, home, attributes, charset and tabs reset", "\"%s\" fg %d", row(0), cell(0, 0)[2]);
    send(s = "\033[24;1H\n"); OK(AT(0, 23) && row(0)[0] == 0, s, "no region: LF at row 24 scrolls the whole screen (row 1 gone)", "row0 \"%s\"", row(0));
    fresh("DECSTR");
    send("\033[5;10r\033[?6h\033[4h\033[31m\0337\033[6;10Habc\033(0\033[10;1H");
    send(s = "\033[!p\033[10;10Hq");
    OK(!strcmp(row(9), "         qbc") && cell(9, 9)[2] == FG, s, "the screen kept; attributes, charset, insert, origin mode reset", "row9 \"%s\" fg %d", row(9), cell(9, 9)[2]);
    send(s = "\0338X"); OK(ch(0, 0) == 'X', s, "the saved cursor reset to home", "(0,0)='%c' " CURF, ch(0, 0), cx(), cy());
    fresh("DECSTR shows the cursor");
    send(s = "\033[?25l\033[!p"); KNOWN(R(0x0E) & 1, s, "DECTCEM set: the cursor shown again (VT220)", "hidden");
    W(0x0E, 0);
    fresh("DECALN");
    send(s = "\033[5;10r\033[10;10H\033#8"); KNOWN(ch(0, 0) == 'E' && ch(79, 23) == 'E' && AT(0, 0), s, "the screen of E, the margins reset, the cursor home", "(0,0)='%c' " CURF, ch(0, 0), cx(), cy());
    send(s = "\033[24;1H\n"); KNOWN(ch(0, 22) == 'E' && ch(0, 23) == ' ', s, "and the region gone: LF at 24 scrolls", "(0,23)='%c'", ch(0, 23));
}

static void t_modes(void)
{
    const char *s;
    fresh("DECSCUSR");
    W(0x0E, 1);
    send(s = "\033[5 q"); OK(!(cell(0, 0)[1] & 0x80) && cx() == 0, s, "a bar: drawn by VICKY, the cell left alone, nothing printed", "attr %02X", cell(0, 0)[1]);
    send(s = "\033[2 q"); OK(cell(0, 0)[1] & 0x80, s, "a block: the cell reversed", "attr %02X", cell(0, 0)[1]);
    send(s = "\033[0 q"); OK((cell(0, 0)[1] & 0x80) && cx() == 0, s, "0 is the block", "attr %02X", cell(0, 0)[1]);
    W(0x0E, 0);
    fresh("?2026 synchronized update");
    send(s = "\033[?2026h"); OK(term_hold(), s, "held", "not held");
    send(s = "\033[?2026l"); OK(!term_hold(), s, "shown", "still held");
    send("\033[?2026h"); for (int i = 0; i < 31; i++) term_tick();
    OK(!term_hold(), "ESC[?2026h, 31 frames", "let go after half a second", "still held");
    send(s = "\033[?2026h\033c"); OK(!term_hold(), s, "RIS ends it", "still held");
    send(s = "\033[?2026h\033[!p"); OK(!term_hold(), s, "DECSTR ends it", "still held");
    fresh("?1049 alternate screen");
    send(s = "main\033[?1049halt\033[?1049l"); KNOWN(!strcmp(row(0), "main") && AT(4, 0), s, "\"main\" back, the cursor where it was (xterm; nvim and tmux use it)", "\"%s\" " CURF, row(0), cx(), cy());
    fresh("?47 alternate screen");
    send(s = "main\0337\033[?47halt\033[2J\033[?47l\0338"); KNOWN(!strcmp(row(0), "main"), s, "\"main\" back (xterm-color's smcup/rmcup: what nvim and mosh send)", "\"%s\"", row(0));
    fresh("modes JIM does not keep");
    send(s = "\033[?1h\033[?1000h\033[?1006h\033[?2004h\033[?1004h\033[?12h\033[?5hX\033[?5l\033[?1l");
    OK(!strcmp(row(0), "X"), s, "\"X\": set and reset quietly", "\"%s\"", row(0));
}

static void t_parser(void)
{
    const char *s;
    fresh("a control inside a CSI");
    send(s = "ab\033[1\bD"); OK(AT(0, 0), s, "(0,0): BS acts, the CSI goes on", CUR);
    fresh("CAN aborts a sequence");
    send(s = "\033[3\030A"); KNOWN(!strcmp(row(0), "A"), s, "\"A\": CAN ends the CSI, A is printed", "\"%s\" " CURF, row(0), cx(), cy());
    fresh("ESC restarts a sequence");
    send(s = "\033[3\033[2;2HX"); OK(ch(1, 1) == 'X', s, "X at (1,1)", "\"%s\"", row(1));
    fresh("OSC, DCS, APC, PM, SOS swallowed");
    send(s = "a\033]0;title\007b\033]8;;http://x\033\\c\033]8;;\033\\d\033P1$r\033\\e\033^pm\033\\f\033Xsos\033\\g");
    OK(!strcmp(row(0), "abcdefg"), s, "\"abcdefg\"", "\"%s\"", row(0));
    fresh("an unknown CSI is swallowed");
    send(s = "a\033[>0qb\033[?1;2;3$xc\033[1 @d"); OK(!strcmp(row(0), "abcd"), s, "\"abcd\": XTVERSION, a DECCARA-like, SL", "\"%s\"", row(0));
    fresh("kitty keyboard queries");
    send(s = "\033[1;1H\033[s\033[5;5H\033[?u"); KNOWN(AT(4, 4), s, "(4,4): CSI ? u is the kitty query (nvim sends it), not a restore", CUR);
    send(s = "\033[5;5H\033[>1u\033[<u"); KNOWN(AT(4, 4) && row(4)[0] == 0, s, "nothing printed, the cursor kept", "row4 \"%s\" " CURF, row(4), cx(), cy());
    fresh("XTSAVE / XTRESTORE");
    label_rows(); send(s = "\033[10;10H\033[?1;6r\033[24;1H\n");
    KNOWN(!strcmp(row(0), "L01"), s, "CSI ? Pm r restores modes, it does not set a region: LF at 24 scrolls the screen", "row0 \"%s\"", row(0));
    fresh("REP");
    send(s = "x\033[3b"); KNOWN(!strcmp(row(0), "xxxx"), s, "\"xxxx\" (xterm, ECMA-48; xterm-256color's rep)", "\"%s\"", row(0));
    fresh("a long parameter list");
    send(s = "\033[0;1;2;3;4;5;6;7;8;9;10;11;12;13;14;15;16;31mR"); KNOWN(cell(0, 0)[2] == 2, s, "red: the 18th parameter still counts (xterm takes 30)", "fg %d", cell(0, 0)[2]);
}

int main(void)
{
    mem_init(); io_reset();
    W(5, COLS); W(6, ROWS); W(7, 0); W(8, 0); W(0x0D, COLS); W(0x14, FG); W(0x15, BG);
    t_cursor(); t_wrap(); t_region(); t_erase(); t_origin(); t_tabs(); t_sgr(); t_save();
    t_charsets(); t_utf8(); t_replies(); t_resets(); t_modes(); t_parser(); t_widths();
    printf("vtconf: %d pass, %d known failures, %d unexpected\n", npass, nknown, nfail);
    return nfail != 0;
}
