/* JIM, the terminal ($DA00). See term.h for the registers and the repertoire. */
#include "term.h"
#include "mem.h"
#include "io.h"
#include "vicky.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define NPAR 16
static struct {
    uint8_t cols, rows, ox, oy, stride;
    uint32_t base;
    uint8_t cx, cy, fg, bg, deffg, defbg;
    uint8_t bold, rev, uline;          /* attributes */
    uint8_t top, bot;                  /* scroll region, 0-based inclusive */
    uint8_t wrap, origin, ckm, insert, shown, dirty, pending;   /* modes; pending = the VT100 last-column wrap */
    uint8_t lnm;                       /* ANSI mode 20: LF also returns the column.  The ROM console
                                        * ends its lines with a bare \n and expects column 0 back,
                                        * which is exactly what LNM is for. */
    uint8_t petscii, pet_lower;        /* PETSCII mode (FLAGS bit 2), and its case set ($0E / $8E) */
    /* The status bands, when a PROGRAM has taken them (FLAGS bit 3).  JIM does
     * not draw them and never reads these -- the ROM does, in video_init.  They
     * live here because this is where the console's geometry lives, and the
     * band heights are what the geometry is made of; and because the frontend
     * rewrites the USER's heights ($D52D/$D52E) every frame, so a guest has
     * nowhere else to put a request of its own that would survive. */
    uint8_t bandclaim, bandtop, bandbot;
    uint8_t g0, g1, shift;             /* charsets: 0 ASCII, 1 DEC line drawing; shift = SO */
    uint8_t tabs[32];                  /* tab stops, one bit per column */
    struct { uint8_t cx, cy, fg, bg, bold, rev, uline, g0, g1, shift, origin; } saved;
    /* the parser */
    uint8_t st;                        /* 0 ground 1 ESC 2 CSI 3 OSC 4 ESC( 5 ESC) 6 ESC# 7 OSC-ESC */
    uint16_t par[NPAR]; uint8_t npar, priv, inter;
    /* the reply FIFO */
    uint8_t rep[128]; uint8_t rh, rt;
    /* the cursor */
    uint8_t cur_on; uint32_t cur_at; uint32_t frames;
} T;

static const uint8_t apal[8]  = { 0, 2, 5, 7, 6, 4, 3, 1 };      /* ANSI order -> the C64 palette */
static const uint8_t apalb[8] = { 11, 10, 13, 7, 14, 4, 3, 1 };  /* the bright set */
static const uint8_t decgfx[32] = {                              /* DEC special graphics ` a b ... ~ -> CP437 */
    0x04, 0xB1, 0x20, 0x20, 0x20, 0x20, 0xF8, 0xF1, 0xB0, 0x20, 0xD9, 0xBF, 0xDA, 0xC0, 0xC5, 0xC4,
    0xC4, 0xC4, 0xC4, 0xC4, 0xC3, 0xB4, 0xC1, 0xC2, 0xB3, 0xF3, 0xF2, 0xE3, 0xF0, 0x9C, 0xFA, 0x20 };

/* ---- the screen ---------------------------------------------------------- */
static uint8_t *cellp(int x, int y)
{
    uint32_t a = (T.base + ((uint32_t)(y + T.oy) * T.stride + (uint32_t)(x + T.ox)) * 4) & K4510_PHYS_MASK;
    if (a > K4510_PHYS_MASK - 4) a = 0;
    return k4510_ram + a;
}
static void put_cell(int x, int y, uint8_t ch, uint8_t attr, uint8_t fg, uint8_t bg)
{
    uint8_t *c = cellp(x, y);
    c[0] = ch; c[1] = attr; c[2] = fg; c[3] = bg;
}
static void blank(int x, int y) { put_cell(x, y, ' ', 0, T.fg, T.bg); }
static void blank_span(int y, int x0, int x1) { for (int x = x0; x <= x1; x++) blank(x, y); }
static void copy_row(int dst, int src) { memcpy(cellp(0, dst), cellp(0, src), (size_t) T.cols * 4); }
static void scroll_up(int top, int bot, int n)
{
    if (n <= 0) return;
    if (n > bot - top + 1) n = bot - top + 1;
    for (int y = top; y + n <= bot; y++) copy_row(y, y + n);
    for (int y = bot - n + 1; y <= bot; y++) blank_span(y, 0, T.cols - 1);
}
static void scroll_down(int top, int bot, int n)
{
    if (n <= 0) return;
    if (n > bot - top + 1) n = bot - top + 1;
    for (int y = bot; y - n >= top; y--) copy_row(y, y - n);
    for (int y = top; y < top + n; y++) blank_span(y, 0, T.cols - 1);
}

/* ---- the cursor ---------------------------------------------------------- *
 * cur_on packs three things so that the state file's JIM record keeps its
 * size: bit 0 the cursor is drawn now, bits 1-2 its style (0 block, 1
 * underline, 2 bar -- DECSCUSR, ESC [ n SP q, the escape vim sends), bit 3
 * it was drawn by flipping the cell's reverse bit.  A block is that reverse
 * bit, as it always was; the other two shapes are VICKY's (vicky_cursor),
 * which leaves the cell alone.  Doc, 2026-09-10: "inside VI I would like
 * the text cursor to change shape according to the mode". */
#define CUR_SHOWN (T.cur_on & 1)
#define CUR_ATTR  (T.cur_on & 8)
#define CUR_STYLE ((T.cur_on >> 1) & 3)
static void cur_undraw(void)
{
    if (!CUR_SHOWN) return;
    if (CUR_ATTR) k4510_ram[T.cur_at] ^= 0x80; else vicky_cursor(0, 0, 0);
    T.cur_on &= 6;
}
static void cur_draw(void)
{
    cur_undraw();
    if (!T.shown) return;
    T.cur_at = (uint32_t)(cellp(T.cx, T.cy) - k4510_ram) + 1;
    if (CUR_STYLE == 0) { k4510_ram[T.cur_at] ^= 0x80; T.cur_on |= 9; }
    else { vicky_cursor(T.cur_at, CUR_STYLE, 1); T.cur_on |= 1; }
}
void term_tick(void)
{
    if (!T.shown) return;
    T.frames++;
    if (T.frames & 16) { if (CUR_SHOWN) cur_undraw(); } else if (!CUR_SHOWN) cur_draw();
}

/* ---- the reply FIFO -------------------------------------------------------- */
static void reply(const char *s) { while (*s) { uint8_t n = (uint8_t)((T.rh + 1) & 127); if (n == T.rt) return; T.rep[T.rh] = (uint8_t) *s++; T.rh = n; } }
static void reply_num(char *p, int v) { char b[8]; int i = 0; do { b[i++] = (char)('0' + v % 10); v /= 10; } while (v); while (i) *p++ = b[--i]; *p = 0; }

/* ---- the state --------------------------------------------------------------- */
static void reset_tabs(void) { memset(T.tabs, 0, sizeof T.tabs); for (int x = 8; x < 256; x += 8) T.tabs[x >> 3] |= (uint8_t)(1 << (x & 7)); }
static void soft_reset(void)
{
    cur_undraw();
    T.fg = T.deffg; T.bg = T.defbg; T.bold = T.rev = T.uline = 0;
    T.top = 0; T.bot = (uint8_t)(T.rows - 1);
    T.wrap = 1; T.origin = 0; T.ckm = 0; T.insert = 0; T.pending = 0;
    T.g0 = T.g1 = 0; T.shift = 0;
    T.st = 0; T.npar = 0;
    reset_tabs();
    memset(&T.saved, 0, sizeof T.saved); T.saved.fg = T.fg; T.saved.bg = T.bg;
}
static void clamp_geometry(void)
{
    if (T.cols < 1) T.cols = 1;
    if (T.rows < 1) T.rows = 1;
    if (T.stride < T.cols + T.ox) T.stride = (uint8_t)(T.cols + T.ox);
    if (T.cx >= T.cols) T.cx = (uint8_t)(T.cols - 1);
    if (T.cy >= T.rows) T.cy = (uint8_t)(T.rows - 1);
    if (T.bot >= T.rows) T.bot = (uint8_t)(T.rows - 1);
    if (T.top > T.bot) T.top = 0;
}
void term_reset(void)
{
    vicky_cursor(0, 0, 0);
    memset(&T, 0, sizeof T);
    T.cols = 80; T.rows = 30; T.stride = 80; T.base = 0x030000u;
    T.deffg = 7; T.defbg = 6;                        /* the ROM's yellow on blue until it says otherwise */
    soft_reset();
    T.cx = T.cy = 0;
}

/* ---- cursor motion ------------------------------------------------------ */
static void move(int x, int y)
{
    int lo = T.origin ? T.top : 0, hi = T.origin ? T.bot : T.rows - 1;
    if (x < 0) x = 0;
    if (x >= T.cols) x = T.cols - 1;
    if (y < lo) y = lo;
    if (y > hi) y = hi;
    T.cx = (uint8_t) x; T.cy = (uint8_t) y; T.pending = 0;
}
static void index_down(void)
{
    if (T.cy == T.bot) scroll_up(T.top, T.bot, 1);
    else if (T.cy < T.rows - 1) T.cy++;
}
static void index_up(void)
{
    if (T.cy == T.top) scroll_down(T.top, T.bot, 1);
    else if (T.cy > 0) T.cy--;
}
static void linefeed(void) { index_down(); T.pending = 0; }

/* ---- printing --------------------------------------------------------------- */
static void print_char(uint8_t ch)
{
    uint8_t fg = T.fg, bg = T.bg, attr = 0;
    int cs = T.shift ? T.g1 : T.g0;
    if (cs == 1 && ch >= 0x60 && ch <= 0x7E) ch = decgfx[ch - 0x60];
    if (T.bold) { for (int i = 0; i < 8; i++) if (apal[i] == T.fg) { fg = apalb[i]; break; } }
    if (T.rev) { uint8_t t = fg; fg = bg; bg = t; }
    if (T.uline) attr |= 0x00;           /* text32 has no underline; kept for the day it does */
    if (T.pending) {                     /* the VT100 way: the wrap happens as the next character lands */
        if (T.wrap) { T.cx = 0; linefeed(); } else T.cx = (uint8_t)(T.cols - 1);
        T.pending = 0;
    }
    if (T.insert) { for (int x = T.cols - 1; x > T.cx; x--) memcpy(cellp(x, T.cy), cellp(x - 1, T.cy), 4); }
    put_cell(T.cx, T.cy, ch, attr, fg, bg);
    if (T.cx + 1 < T.cols) T.cx++; else T.pending = 1;
}

/* ---- CSI ---------------------------------------------------------------------- */
static int P(int i, int dflt) { return (i < T.npar && T.par[i]) ? T.par[i] : dflt; }
static void sgr(void)
{
    if (!T.npar) { T.par[0] = 0; T.npar = 1; }
    for (int i = 0; i < T.npar; i++) {
        int v = T.par[i];
        if (v == 0) { T.fg = T.deffg; T.bg = T.defbg; T.bold = T.rev = T.uline = 0; }
        else if (v == 1) T.bold = 1;
        else if (v == 4) T.uline = 1;
        else if (v == 7) T.rev = 1;
        else if (v == 22) T.bold = 0;
        else if (v == 24) T.uline = 0;
        else if (v == 27) T.rev = 0;
        else if (v >= 30 && v <= 37) T.fg = apal[v - 30];
        else if (v == 39) T.fg = T.deffg;
        else if (v >= 40 && v <= 47) T.bg = apal[v - 40];
        else if (v == 49) T.bg = T.defbg;
        else if (v >= 90 && v <= 97) T.fg = apalb[v - 90];
        else if (v >= 100 && v <= 107) T.bg = apalb[v - 100];
        else if ((v == 38 || v == 48) && i + 2 < T.npar && T.par[i + 1] == 5) {
            int n = T.par[i + 2]; uint8_t c = n < 8 ? apal[n] : n < 16 ? apalb[n - 8] : (uint8_t)(n & 15);
            if (v == 38) T.fg = c; else T.bg = c;
            i += 2;
        }
    }
}
static void mode(int on)
{
    for (int i = 0; i < T.npar; i++) {
        int v = T.par[i];
        if (T.priv) {
            if (v == 1) T.ckm = (uint8_t) on;
            else if (v == 6) { T.origin = (uint8_t) on; move(0, 0); }
            else if (v == 7) T.wrap = (uint8_t) on;
            else if (v == 25) T.shown = (uint8_t) on;
        } else if (v == 4) T.insert = (uint8_t) on;
        else if (v == 20) T.lnm = (uint8_t) on;          /* LNM */
    }
}
static void csi(uint8_t c)
{
    int n = P(0, 1);
    switch (c) {
    case 'A': move(T.cx, T.cy - n); break;
    case 'B': case 'e': move(T.cx, T.cy + n); break;
    case 'C': case 'a': move(T.cx + n, T.cy); break;
    case 'D': move(T.cx - n, T.cy); break;
    case 'E': move(0, T.cy + n); break;
    case 'F': move(0, T.cy - n); break;
    case 'G': case '`': move(n - 1, T.cy); break;
    case 'd': move(T.cx, (T.origin ? T.top : 0) + n - 1); break;
    case 'H': case 'f': move(P(1, 1) - 1, (T.origin ? T.top : 0) + n - 1); break;
    case 'J': {
        int m = P(0, 0); T.pending = 0;
        if (m == 0) { blank_span(T.cy, T.cx, T.cols - 1); for (int y = T.cy + 1; y < T.rows; y++) blank_span(y, 0, T.cols - 1); }
        else if (m == 1) { for (int y = 0; y < T.cy; y++) blank_span(y, 0, T.cols - 1); blank_span(T.cy, 0, T.cx); }
        else { for (int y = 0; y < T.rows; y++) blank_span(y, 0, T.cols - 1); }
        break; }
    case 'K': {
        int m = P(0, 0); T.pending = 0;
        if (m == 0) blank_span(T.cy, T.cx, T.cols - 1);
        else if (m == 1) blank_span(T.cy, 0, T.cx);
        else blank_span(T.cy, 0, T.cols - 1);
        break; }
    case 'L': if (T.cy >= T.top && T.cy <= T.bot) scroll_down(T.cy, T.bot, n); T.cx = 0; T.pending = 0; break;
    case 'M': if (T.cy >= T.top && T.cy <= T.bot) scroll_up(T.cy, T.bot, n); T.cx = 0; T.pending = 0; break;
    case 'S': scroll_up(T.top, T.bot, n); break;
    case 'T': scroll_down(T.top, T.bot, n); break;
    case '@': { if (n > T.cols - T.cx) n = T.cols - T.cx;
        for (int x = T.cols - 1; x >= T.cx + n; x--) memcpy(cellp(x, T.cy), cellp(x - n, T.cy), 4);
        blank_span(T.cy, T.cx, T.cx + n - 1); T.pending = 0; break; }
    case 'P': { if (n > T.cols - T.cx) n = T.cols - T.cx;
        for (int x = T.cx; x + n < T.cols; x++) memcpy(cellp(x, T.cy), cellp(x + n, T.cy), 4);
        blank_span(T.cy, T.cols - n, T.cols - 1); T.pending = 0; break; }
    case 'X': { if (n > T.cols - T.cx) n = T.cols - T.cx; blank_span(T.cy, T.cx, T.cx + n - 1); T.pending = 0; break; }
    case 'g': if (P(0, 0) == 3) memset(T.tabs, 0, sizeof T.tabs); else T.tabs[T.cx >> 3] &= (uint8_t) ~(1 << (T.cx & 7)); break;
    case 'h': mode(1); break;
    case 'l': mode(0); break;
    case 'm': if (!T.priv) sgr(); break;
    case 'n': {
        int m = P(0, 0);
        if (m == 5) reply("\033[0n");
        else if (m == 6) { char b[16]; strcpy(b, "\033["); reply_num(b + 2, (T.origin ? T.cy - T.top : T.cy) + 1); strcat(b, ";"); reply_num(b + strlen(b), T.cx + 1); strcat(b, "R"); reply(b); }
        break; }
    case 'c': if (!T.inter) reply("\033[?62;1;6;22c"); break;      /* a VT220 with 132 columns absent, selective erase, colour */
    case 'r': {
        int t = P(0, 1), b = P(1, T.rows);
        if (t < 1) t = 1;
        if (b > T.rows) b = T.rows;
        if (t < b) { T.top = (uint8_t)(t - 1); T.bot = (uint8_t)(b - 1); move(0, T.origin ? T.top : 0); }
        break; }
    case 's': T.saved.cx = T.cx; T.saved.cy = T.cy; break;
    case 'u': move(T.saved.cx, T.saved.cy); break;
    case 'p': if (T.inter == '!') { uint8_t sh = T.shown; soft_reset(); T.shown = sh; } break;   /* DECSTR */
    case 'q': if (T.inter == ' ') {                                 /* DECSCUSR: 0-2 block, 3-4 underline, 5-6 bar */
                  int st = n >= 5 ? 2 : n >= 3 ? 1 : 0;
                  if (st != CUR_STYLE) { cur_undraw(); T.cur_on = (uint8_t)((T.cur_on & 9) | (st << 1)); if (T.shown) cur_draw(); } }
              break;
    case 'Z': { int x = T.cx; while (n-- > 0) { do x--; while (x > 0 && !(T.tabs[x >> 3] & (1 << (x & 7)))); } move(x, T.cy); break; }
    default: break;
    }
}

/* ---- ESC -------------------------------------------------------------------- */
static void save_cursor(void)
{
    T.saved.cx = T.cx; T.saved.cy = T.cy; T.saved.fg = T.fg; T.saved.bg = T.bg; T.saved.bold = T.bold;
    T.saved.rev = T.rev; T.saved.uline = T.uline; T.saved.g0 = T.g0; T.saved.g1 = T.g1; T.saved.shift = T.shift; T.saved.origin = T.origin;
}
static void restore_cursor(void)
{
    T.fg = T.saved.fg; T.bg = T.saved.bg; T.bold = T.saved.bold; T.rev = T.saved.rev; T.uline = T.saved.uline;
    T.g0 = T.saved.g0; T.g1 = T.saved.g1; T.shift = T.saved.shift; T.origin = T.saved.origin;
    move(T.saved.cx, T.saved.cy);
}
static void esc(uint8_t c)
{
    switch (c) {
    case '[': T.st = 2; T.npar = 0; T.priv = 0; T.inter = 0; memset(T.par, 0, sizeof T.par); return;
    case ']': T.st = 3; return;
    case 'P': case '^': case '_': case 'X': T.st = 3; return;      /* DCS, PM, APC, SOS: skipped like an OSC */
    case '(': T.st = 4; return;
    case ')': T.st = 5; return;
    case '#': T.st = 6; return;
    case '7': save_cursor(); break;
    case '8': restore_cursor(); break;
    case 'D': linefeed(); break;
    case 'E': T.cx = 0; linefeed(); break;
    case 'M': index_up(); T.pending = 0; break;
    case 'H': T.tabs[T.cx >> 3] |= (uint8_t)(1 << (T.cx & 7)); break;
    case 'c': { uint8_t sh = T.shown; soft_reset(); T.shown = sh; T.cx = T.cy = 0; for (int y = 0; y < T.rows; y++) blank_span(y, 0, T.cols - 1); break; }
    case 'Z': reply("\033[?62;1;6;22c"); break;
    case '=': case '>': case 'N': case 'O': break;                  /* keypad modes, single shifts: nothing here */
    default: break;
    }
    T.st = 0;
}

/* ---- PETSCII ----------------------------------------------------------------- *
 * The other way an 8-bit machine talked to its screen.  Not a protocol: a set of
 * control codes and a character set, so this is a second dispatch beside the ANSI
 * one rather than a second renderer -- colours land in the same T.fg/T.bg, reverse
 * in the same T.rev, and printing goes through the same print_char.
 *
 * The sixteen colour codes, in the C64's own palette order (0 black .. 15 light
 * grey), which is the palette VICKY boots with. */
static const uint8_t pet_col[16] = {
    /* $90 */ 0, /* $05 */ 1, /* $1C */ 2, /* $9F */ 3, /* $9C */ 4, /* $1E */ 5,
    /* $1F */ 6, /* $9E */ 7, /* $81 */ 8, /* $95 */ 9, /* $96 */ 10, /* $97 */ 11,
    /* $98 */ 12, /* $99 */ 13, /* $9A */ 14, /* $9B */ 15
};
static int pet_colour(uint8_t c)          /* -> index into pet_col, or -1 */
{
    switch (c) {
    case 0x90: return 0;  case 0x05: return 1;  case 0x1C: return 2;  case 0x9F: return 3;
    case 0x9C: return 4;  case 0x1E: return 5;  case 0x1F: return 6;  case 0x9E: return 7;
    case 0x81: return 8;  case 0x95: return 9;  case 0x96: return 10; case 0x97: return 11;
    case 0x98: return 12; case 0x99: return 13; case 0x9A: return 14; case 0x9B: return 15;
    default: return -1;
    }
}
/* PETSCII -> the code the text32 renderer looks the glyph up by.
 *
 * NOT a screen code.  The machine's font is always ASCII/CP437-ordered: a
 * 4096-byte chargen is permuted into ASCII order on the way in
 * (petscii_to_ascii() in sdl/main.c), so there is no screen-code-ordered font
 * in RAM to index.  An earlier version of this did the textbook PETSCII ->
 * screen code arithmetic and rendered letters where graphics belonged, which
 * is exactly what that mistake looks like.
 *
 * So: letters and punctuation land on their ASCII codes, and the line-drawing
 * half lands on CP437 -- which the machine really does have, because the same
 * chargen loader lifts those glyphs out of the PETSCII set into their CP437
 * positions.  The PETSCII codes below are the pairs of that table.
 *
 * What is NOT here: the rest of PETSCII's graphics repertoire (the diagonals,
 * the quarter-blocks, the card suits).  The machine's font has no glyph at any
 * code for them, so they come out as spaces rather than as some other
 * character that happens to live there.  Giving PETSCII its full set means
 * loading the chargen a second time in screen-code order and switching to it
 * with the mode -- see docs/TODO.md. */
static uint8_t pet_gfx(uint8_t g)          /* g = the code within a graphics range, 0x40-0x7F */
{
    switch (g) {
    case 0x40: return 0xC4;   /* horizontal   */
    case 0x5D: return 0xB3;   /* vertical     */
    case 0x70: return 0xDA;   /* top left     */
    case 0x6E: return 0xBF;   /* top right    */
    case 0x6D: return 0xC0;   /* bottom left  */
    case 0x7D: return 0xD9;   /* bottom right */
    case 0x6B: return 0xC3;   /* tee right    */
    case 0x73: return 0xB4;   /* tee left     */
    case 0x5B: return 0xC5;   /* cross        */
    case 0x71: return 0xC1;   /* tee up       */
    case 0x72: return 0xC2;   /* tee down     */
    case 0x66: return 0xB1;   /* shaded block */
    default:   return ' ';    /* no glyph in this font: a space, not a lie */
    }
}
static uint8_t pet_glyph(uint8_t c, uint8_t lower)
{
    if (c >= 0x20 && c <= 0x3F) return c;                       /* space, digits, punctuation */
    if (c == 0x40) return '@';
    if (c >= 0x41 && c <= 0x5A)                                 /* the case sets: $0E / $8E */
        return lower ? (uint8_t)(c + 0x20) : c;
    if (c >= 0xC1 && c <= 0xDA)                                 /* the other half of the pair */
        return lower ? (uint8_t)(c - 0x80) : (uint8_t)(c - 0xA0);
    switch (c) {
    case 0x5B: return '[';  case 0x5D: return ']';
    case 0x5C: return 0x9C;                                     /* pound, CP437 */
    case 0x5E: return 0x18;                                     /* up arrow    */
    case 0x5F: return 0x1B;                                     /* left arrow  */
    case 0xA0: return ' ';                                      /* shifted space */
    default: break;
    }
    if (c >= 0x60 && c <= 0x7F) return pet_gfx(c);
    if (c >= 0xA0 && c <= 0xBF) return pet_gfx((uint8_t)(c - 0x40));
    if (c >= 0xC0)              return pet_gfx((uint8_t)(c - 0x80));
    return ' ';
}
static void pet_byte(uint8_t c)
{
    int col, y;
    if (c >= 0x20 && c != 0x7F && !(c >= 0x80 && c <= 0x9F)) { print_char(pet_glyph(c, T.pet_lower)); return; }
    if ((col = pet_colour(c)) >= 0) { T.fg = (uint8_t) pet_col[col]; return; }
    switch (c) {
    case 0x93: for (y = 0; y < T.rows; y++) blank_span(y, 0, T.cols - 1);
               T.pending = 0; move(0, 0); return;           /* CLR */
    case 0x13: move(0, 0); return;                         /* HOME */
    case 0x11: move(T.cx, T.cy + 1); return;               /* cursor down */
    case 0x91: move(T.cx, T.cy ? T.cy - 1 : 0); return;    /* cursor up */
    case 0x1D: move(T.cx + 1, T.cy); return;               /* cursor right */
    case 0x9D: move(T.cx ? T.cx - 1 : 0, T.cy); return;    /* cursor left */
    case 0x12: T.rev = 1; return;                          /* RVS ON */
    case 0x92: T.rev = 0; return;                          /* RVS OFF */
    case 0x0E: T.pet_lower = 1; return;                    /* lower/upper case set */
    case 0x8E: T.pet_lower = 0; return;                    /* upper/graphics set */
    case 0x0D: case 0x0A: T.cx = 0; linefeed(); return;    /* RETURN is both, on a CBM; LF too, so a
                                                            * program may drive this through CHROUT */
    case 0x14: if (T.cx) { move(T.cx - 1, T.cy); print_char(' '); move(T.cx - 1, T.cy); } return;  /* DEL */
    default: return;                                       /* everything else: swallowed */
    }
}

/* ---- the stream -------------------------------------------------------------- */
static void put_byte(uint8_t c)
{
    if (T.petscii && T.st == 0) { pet_byte(c); return; }
    switch (T.st) {
    case 0:
        if (c >= 0x20 && c != 0x7F) { print_char(c); return; }
        switch (c) {
        case 0x1B: T.st = 1; return;
        case '\r': T.cx = 0; T.pending = 0; return;
        case '\n': case 0x0B: case 0x0C: linefeed(); if (T.lnm) T.cx = 0; return;
        case 8: if (T.cx) T.cx--; T.pending = 0; return;
        case 9: { int x = T.cx + 1; while (x < T.cols - 1 && !(T.tabs[x >> 3] & (1 << (x & 7)))) x++; move(x, T.cy); return; }
        case 0x0E: T.shift = 1; return;
        case 0x0F: T.shift = 0; return;
        default: return;                                            /* BEL, NUL, DEL and the rest: swallowed */
        }
    case 1: esc(c); return;
    case 2:
        if (c >= '0' && c <= '9') { if (T.npar == 0) T.npar = 1; if (T.npar <= NPAR) { uint16_t *p = &T.par[T.npar - 1]; *p = (uint16_t)(*p < 1000 ? *p * 10 + (c - '0') : *p); } return; }
        if (c == ';') { if (T.npar == 0) T.npar = 1; if (T.npar < NPAR) T.npar++; return; }
        if (c == '?' || c == '>' || c == '=') { T.priv = c; return; }
        if (c >= 0x20 && c <= 0x2F) { T.inter = c; return; }
        if (c == 0x1B) { T.st = 1; return; }
        if (c < 0x20) { T.st = 0; put_byte(c); T.st = 2; return; } /* a control inside a CSI acts at once, in the ground state, and the CSI goes on */
        T.st = 0; csi(c); return;
    case 3:                                                         /* an OSC/DCS string: to BEL or ESC \ */
        if (c == 7) T.st = 0; else if (c == 0x1B) T.st = 7; return;
    case 7: T.st = (c == '\\') ? 0 : 3; return;
    case 4: T.g0 = (c == '0') ? 1 : 0; T.st = 0; return;
    case 5: T.g1 = (c == '0') ? 1 : 0; T.st = 0; return;
    case 6: if (c == '8') { for (int y = 0; y < T.rows; y++) for (int x = 0; x < T.cols; x++) put_cell(x, y, 'E', 0, T.fg, T.bg); } T.st = 0; return;
    }
}

/* ---- keys ---------------------------------------------------------------------- */
static void key(uint8_t k)
{
    char b[8]; b[0] = k; b[1] = 0;
    if (k < 0x80) { reply(b); return; }
    switch (k) {
    case KEY_UP: case KEY_DOWN: case KEY_RIGHT: case KEY_LEFT:
        b[0] = 0x1B; b[1] = T.ckm ? 'O' : '['; b[2] = "ABDC"[k - KEY_UP]; b[3] = 0; reply(b); return;
    case KEY_HOME: reply(T.ckm ? "\033OH" : "\033[H"); return;
    case KEY_END:  reply(T.ckm ? "\033OF" : "\033[F"); return;
    case KEY_INS:  reply("\033[2~"); return;
    case KEY_DEL:  reply("\177"); return;
    case KEY_PGUP: reply("\033[5~"); return;
    case KEY_PGDN: reply("\033[6~"); return;
    default:
        if (k >= KEY_F1 && k <= KEY_F1 + 3) { b[0] = 0x1B; b[1] = 'O'; b[2] = (char)('P' + (k - KEY_F1)); b[3] = 0; reply(b); return; }
        if (k >= KEY_F1 + 4 && k <= KEY_F1 + 11) {
            static const uint8_t fn[8] = { 15, 17, 18, 19, 20, 21, 23, 24 };
            strcpy(b, "\033["); reply_num(b + 2, fn[k - KEY_F1 - 4]); strcat(b, "~"); reply(b); return;
        }
        return;                                                     /* an unknown special key: nothing */
    }
}

/* ---- the registers ----------------------------------------------------------- */
uint8_t term_read(uint8_t r)
{
    switch (r) {
    case 0x01: return (uint8_t)((T.rh != T.rt ? 0x80 : 0) | (T.dirty ? 1 : 0));
    case 0x02: { uint8_t v = 0; if (T.rh != T.rt) { v = T.rep[T.rt]; T.rt = (uint8_t)((T.rt + 1) & 127); } return v; }
    case 0x05: return T.cols;  case 0x06: return T.rows;
    case 0x07: return T.ox;    case 0x08: return T.oy;
    case 0x09: return T.cx;    case 0x0A: return T.cy;
    case 0x0B: return T.fg;    case 0x0C: return T.bg;
    case 0x0D: return T.stride;
    case 0x0E: return (uint8_t)((T.shown ? 1 : 0) | (T.ckm ? 2 : 0) | (T.petscii ? 4 : 0) | (T.bandclaim ? 8 : 0));
    case 0x0F: return T.bandtop;
    case 0x10: case 0x11: case 0x12: case 0x13: return (uint8_t)(T.base >> (8 * (r - 0x10)));
    case 0x14: return T.deffg; case 0x15: return T.defbg;
    case 0x16: return T.bandbot;
    default: return 0;
    }
}
void term_write(uint8_t r, uint8_t v)
{
    switch (r) {
    case 0x00:
        { static FILE *lg; static int tried;                          /* K4510_TERMLOG=file: every byte JIM receives (debugging a program's output) */
          if (!tried) { tried = 1; const char *f = getenv("K4510_TERMLOG"); if (f) lg = fopen(f, "wb"); }
          if (lg) { fputc(v, lg); fflush(lg); } }
        cur_undraw(); put_byte(v); T.dirty = 1; cur_draw(); return;
    case 0x03: key(v); return;
    case 0x04:
        cur_undraw();
        if (v == 1) { uint8_t sh = T.shown; soft_reset(); T.shown = sh; T.cx = T.cy = 0; }
        if (v == 2) { for (int y = 0; y < T.rows; y++) blank_span(y, 0, T.cols - 1); T.cx = T.cy = 0; T.pending = 0; }
        cur_draw(); return;
    case 0x05: cur_undraw(); T.cols = v; clamp_geometry(); T.bot = (uint8_t)(T.rows - 1); T.top = 0; cur_draw(); return;
    case 0x06: cur_undraw(); T.rows = v; clamp_geometry(); T.bot = (uint8_t)(T.rows - 1); T.top = 0; cur_draw(); return;
    case 0x07: cur_undraw(); T.ox = v; clamp_geometry(); cur_draw(); return;
    case 0x08: cur_undraw(); T.oy = v; clamp_geometry(); cur_draw(); return;
    case 0x09: cur_undraw(); T.cx = v; clamp_geometry(); T.pending = 0; T.dirty = 0; cur_draw(); return;
    case 0x0A: cur_undraw(); T.cy = v; clamp_geometry(); T.pending = 0; T.dirty = 0; cur_draw(); return;
    case 0x0B: T.fg = v; return;
    case 0x0C: T.bg = v; return;
    case 0x0D: cur_undraw(); T.stride = v; clamp_geometry(); cur_draw(); return;
    case 0x0E: cur_undraw(); T.shown = v & 1;
               if (((v >> 2) & 1) != T.petscii) { T.petscii = (v >> 2) & 1; T.pet_lower = 0; }
               T.bandclaim = (v >> 3) & 1;
               cur_draw(); return;
    case 0x0F: T.bandtop = v; return;
    case 0x10: case 0x11: case 0x12: case 0x13:
        cur_undraw(); T.base = (T.base & ~(0xFFu << (8 * (r - 0x10)))) | ((uint32_t) v << (8 * (r - 0x10))); T.base &= K4510_PHYS_MASK; cur_draw(); return;
    case 0x14: T.deffg = v; return;
    case 0x15: T.defbg = v; return;
    case 0x16: T.bandbot = v; return;
    default: return;
    }
}

/* ---- save states (core/state.h) ------------------------------------------ */
#include "state.h"
void term_state_save(FILE *f) { state_put(f, "JIM ", &T, sizeof T); }
int  term_state_load(FILE *f) { if (state_get(f, "JIM ", &T, sizeof T)) return -2; T.cur_on &= 6; vicky_cursor(0, 0, 0); return 0; }
