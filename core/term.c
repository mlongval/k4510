/* JIM, the terminal ($DA00). See term.h for the registers and the repertoire. */
#include "term.h"
#include "mem.h"
#include "io.h"
#include "vicky.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

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
    uint8_t utf8, u_need, u_nraw, u_raw[4]; uint32_t u_cp;   /* UTF-8 mode (ESC % G .. ESC % @) and a sequence half-read */
    uint8_t tabs[32];                  /* tab stops, one bit per column */
    struct { uint8_t cx, cy, fg, bg, bold, rev, uline, g0, g1, shift, origin; } saved;
    /* the parser */
    uint8_t st;                        /* 0 ground 1 ESC 2 CSI 3 OSC 4 ESC( 5 ESC) 6 ESC# 7 OSC-ESC 8 ESC% */
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
/* Per cell, not one memcpy of the row: cellp wraps the START of a cell only,
 * so a row placed at the top of physical RAM ran the copy off the end of the
 * mapping (review 2026-09-12, 1). */
static void copy_row(int dst, int src) { for (int x = 0; x < T.cols; x++) memcpy(cellp(x, dst), cellp(x, src), 4); }
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
/* The block cursor is the cell's reverse bit inverted in place, so undrawing
 * must invert it back -- and that went wrong whenever something wrote the cell
 * directly while the cursor sat on it (the ROM's line editor blanks a cell it
 * believes cursor-free; a program's CursorOn had just put the cursor there).
 * The inversion parity was then off by one for every cell the cursor visited
 * after, each move leaving a reverse-video space behind: the blocks that
 * followed PMANDEL on the Dell (Doc, 2026-09-11).  Now cur_on bit 4 remembers
 * the ORIGINAL reverse bit, and undraw restores it only if the cell still
 * carries the cursor's own inversion; a rewritten cell is left as written. */
#define CUR_ORIG  ((T.cur_on >> 4) & 1)
static void cur_undraw(void)
{
    if (!CUR_SHOWN) return;
    if (CUR_ATTR) { uint8_t *a = &k4510_ram[T.cur_at];
                    if (((*a >> 7) & 1) != CUR_ORIG) *a = (uint8_t)((*a & 0x7F) | (CUR_ORIG << 7)); }
    else vicky_cursor(0, 0, 0);
    T.cur_on &= 6;
}
static void cur_draw(void)
{
    cur_undraw();
    if (!T.shown) return;
    T.cur_at = (uint32_t)(cellp(T.cx, T.cy) - k4510_ram) + 1;
    if (CUR_STYLE == 0) { uint8_t *a = &k4510_ram[T.cur_at]; uint8_t orig = (uint8_t)((*a >> 7) & 1);
                          *a ^= 0x80; T.cur_on = (uint8_t)((T.cur_on & 6) | 9 | (orig << 4)); }
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
    /* A Unix program's ANSI blue is the machine's own blue background (both
     * C64 colour 6), so Claude Code's inline code -- ESC[34m -- drew blue on
     * blue and vanished (the Dell, 2026-09-12).  In a UTF-8 session (a Unix
     * host) a character whose colour IS its background takes the bright one,
     * or white/black.  A BBS (CP437) keeps its exact colours: art may mean it. */
    if (T.utf8 && fg == bg) {
        for (int i = 0; i < 8; i++) if (apal[i] == fg) { fg = apalb[i]; break; }
        if (fg == bg) fg = (bg == 1) ? 0 : 1;
    }
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
static int ansi16_of(int r, int g, int b)                       /* 0-15: the xterm colour nearest r,g,b */
{
    static const uint8_t x[16][3] = { {0,0,0}, {205,0,0}, {0,205,0}, {205,205,0}, {0,0,238}, {205,0,205}, {0,205,205}, {229,229,229},
                                      {127,127,127}, {255,0,0}, {0,255,0}, {255,255,0}, {92,92,255}, {255,0,255}, {0,255,255}, {255,255,255} };
    int best = 0; long bd = -1;
    for (int i = 0; i < 16; i++) {
        long dr = r - x[i][0], dg = g - x[i][1], db = b - x[i][2], d = dr * dr + dg * dg + db * db;
        if (bd < 0 || d < bd) { bd = d; best = i; }
    }
    return best;
}
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
        else if ((v == 38 || v == 48) && i + 2 < T.npar && T.par[i + 1] == 5) {   /* 256 colours: the nearest of the 16 */
            int n = T.par[i + 2], a;
            if (n < 16) a = n;
            else if (n < 232) { n -= 16; a = ansi16_of(n / 36 ? 55 + 40 * (n / 36) : 0, (n / 6) % 6 ? 55 + 40 * ((n / 6) % 6) : 0, n % 6 ? 55 + 40 * (n % 6) : 0); }
            else { int g = 8 + 10 * (n - 232); a = ansi16_of(g, g, g); }
            uint8_t c = a < 8 ? apal[a] : apalb[a - 8];
            if (v == 38) T.fg = c; else T.bg = c;
            i += 2;
        }
        else if ((v == 38 || v == 48) && i + 4 < T.npar && T.par[i + 1] == 2) {   /* truecolour: the nearest of the 16.  Unread,
                                                                                 * its r;g;b were taken as SGR codes (34 = blue) */
            int a = ansi16_of(T.par[i + 2], T.par[i + 3], T.par[i + 4]);
            uint8_t c = a < 8 ? apal[a] : apalb[a - 8];
            if (v == 38) T.fg = c; else T.bg = c;
            i += 4;
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
    case 'c': if (!T.inter) {                                     /* DA1: a VT220 with 132 columns absent, selective erase, colour */
                  if (!T.priv) reply("\033[?62;1;6;22c");
                  else if (T.priv == '>') reply("\033[>1;10;0c");   /* DA2 (tmux asks): a VT220, firmware 10.  It once got the DA1 answer */
              } break;
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
    case '%': T.st = 8; return;                                    /* ESC % G / ESC % @: UTF-8 on / off */
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
 * (fonts are baked to CP437 by tools/mkcp437font.py at import), so there is no screen-code-ordered font
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

/* ---- UTF-8 ------------------------------------------------------------------- *
 * A Linux host talks UTF-8 -- the `!` shell, a TELNET to a Linux box, Claude
 * Code's bullets and box lines -- and JIM draws CP437.  With the mode on (ESC % G,
 * which the `!` shell and TELNET send), a sequence becomes its CP437 glyph: the
 * lines, blocks, arrows, card suits and accented letters CP437 has, a near
 * neighbour for the common ones it lacks (rounded corners, heavy lines, bullets,
 * dashes, quotes, ticks), '?' for the rest.  A wide character is '?' and a space
 * and a combining one nothing, so the far end's columns still line up.  A byte
 * that cannot be UTF-8 (a lead byte with no continuation after it) is CP437
 * after all and draws as itself.  That does NOT make CP437 art safe: C4 B3 (a
 * line, a bar) and DB B0 (a block, a shade) are valid UTF-8 by accident, so the
 * mode must stay off for a BBS -- TELNET turns it on only for a far end that
 * takes XTERM-COLOR.  Off by default, and only ESC % G or a `!` session turns it on, so a CP/M or BBC
 * BASIC session is never decoded.  Doc, 2026-09-12: reading Claude Code through
 * TELNET, every bullet was three glyphs of noise. */
static const uint16_t cp437_hi[128] = {                          /* $80-$FF */
    0x00C7,0x00FC,0x00E9,0x00E2,0x00E4,0x00E0,0x00E5,0x00E7,0x00EA,0x00EB,0x00E8,0x00EF,0x00EE,0x00EC,0x00C4,0x00C5,
    0x00C9,0x00E6,0x00C6,0x00F4,0x00F6,0x00F2,0x00FB,0x00F9,0x00FF,0x00D6,0x00DC,0x00A2,0x00A3,0x00A5,0x20A7,0x0192,
    0x00E1,0x00ED,0x00F3,0x00FA,0x00F1,0x00D1,0x00AA,0x00BA,0x00BF,0x2310,0x00AC,0x00BD,0x00BC,0x00A1,0x00AB,0x00BB,
    0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,0x2555,0x2563,0x2551,0x2557,0x255D,0x255C,0x255B,0x2510,
    0x2514,0x2534,0x252C,0x251C,0x2500,0x253C,0x255E,0x255F,0x255A,0x2554,0x2569,0x2566,0x2560,0x2550,0x256C,0x2567,
    0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256B,0x256A,0x2518,0x250C,0x2588,0x2584,0x258C,0x2590,0x2580,
    0x03B1,0x00DF,0x0393,0x03C0,0x03A3,0x03C3,0x00B5,0x03C4,0x03A6,0x0398,0x03A9,0x03B4,0x221E,0x03C6,0x03B5,0x2229,
    0x2261,0x00B1,0x2265,0x2264,0x2320,0x2321,0x00F7,0x2248,0x00B0,0x2219,0x00B7,0x221A,0x207F,0x00B2,0x25A0,0x00A0 };
static const uint16_t cp437_lo[32] = {                           /* $01-$1F: the ROM's pictures; [0] is $7F's house */
    0x2302,0x263A,0x263B,0x2665,0x2666,0x2663,0x2660,0x2022,0x25D8,0x25CB,0x25D9,0x2642,0x2640,0x266A,0x266B,0x263C,
    0x25BA,0x25C4,0x2195,0x203C,0x00B6,0x00A7,0x25AC,0x21A8,0x2191,0x2193,0x2192,0x2190,0x221F,0x2194,0x25B2,0x25BC };
static const struct { uint16_t u; uint8_t c; } cp437_near[] = { /* what CP437 lacks, drawn as its nearest */
    {0x00D7,'x'},{0x00A9,'C'},{0x00AE,'R'},{0x2122,'T'},{0x20AC,'E'},{0x00A6,0xB3},{0x00AF,0xC4},{0x00B4,'\''},{0x00A8,'"'},{0x00B8,','},
    {0x2010,'-'},{0x2011,'-'},{0x2012,'-'},{0x2013,'-'},{0x2014,'-'},{0x2015,0xC4},{0x2212,'-'},{0x2016,0xBA},
    {0x2018,'\''},{0x2019,'\''},{0x201A,','},{0x201C,'"'},{0x201D,'"'},{0x201E,'"'},{0x2032,'\''},{0x2033,'"'},
    {0x2039,'<'},{0x203A,'>'},{0x2026,'.'},{0x2024,'.'},{0x2027,0xFA},{0x22C5,0xFA},{0x02C6,'^'},{0x02DC,'~'},{0xFFFD,'?'},
    {0x25CF,0x07},{0x23FA,0x07},{0x2B24,0x07},{0x25C9,0x07},{0x25E6,0x09},{0x25B6,0x10},{0x25B8,0x10},{0x25B7,0x10},
    {0x25C0,0x11},{0x25C2,0x11},{0x25C1,0x11},{0x25B4,0x1E},{0x25BE,0x1F},{0x276F,'>'},{0x276E,'<'},{0x21D2,0x1A},
    {0x2794,0x1A},{0x279C,0x1A},{0x21B5,0x11},{0x23CE,0x11},{0x21B3,0xC0},
    {0x2501,0xC4},{0x2503,0xB3},{0x2504,0xC4},{0x2505,0xC4},{0x2506,0xB3},{0x2507,0xB3},{0x2508,0xC4},{0x2509,0xC4},
    {0x250A,0xB3},{0x250B,0xB3},{0x250D,0xDA},{0x250E,0xDA},{0x250F,0xDA},{0x2511,0xBF},{0x2512,0xBF},{0x2513,0xBF},
    {0x2515,0xC0},{0x2516,0xC0},{0x2517,0xC0},{0x2519,0xD9},{0x251A,0xD9},{0x251B,0xD9},{0x251D,0xC3},{0x2520,0xC3},
    {0x2523,0xC3},{0x2525,0xB4},{0x2528,0xB4},{0x252B,0xB4},{0x252F,0xC2},{0x2533,0xC2},{0x2537,0xC1},{0x253B,0xC1},
    {0x253F,0xC5},{0x254B,0xC5},{0x254C,0xC4},{0x254D,0xC4},{0x254E,0xB3},{0x254F,0xB3},{0x256D,0xDA},{0x256E,0xBF},
    {0x256F,0xD9},{0x2570,0xC0},{0x2574,0xC4},{0x2575,0xB3},{0x2576,0xC4},{0x2577,0xB3},{0x2578,0xC4},{0x2579,0xB3},
    {0x257A,0xC4},{0x257B,0xB3},{0x257C,0xC4},{0x257D,0xB3},{0x257E,0xC4},{0x257F,0xB3},{0x23BF,0xC0},{0x23BD,'_'},
    {0x2581,0xDC},{0x2582,0xDC},{0x2583,0xDC},{0x2585,0xDC},{0x2586,0xDB},{0x2587,0xDB},{0x2589,0xDB},{0x258A,0xDB},
    {0x258B,0xDD},{0x258D,0xDD},{0x258E,0xDD},{0x258F,0xDD},{0x2594,0xDF},{0x2595,0xDE},{0x2596,0xDC},{0x2597,0xDC},
    {0x2598,0xDF},{0x259D,0xDF},{0x2599,0xDB},{0x259B,0xDB},{0x259C,0xDB},{0x259F,0xDB},{0x259A,0xB1},{0x259E,0xB1},
    {0x25A1,0xFE},{0x25AA,0xFE},{0x25AB,0xFE},{0x25FB,0xFE},{0x25FC,0xFE},{0x25FD,0xFE},{0x25FE,0xFE},{0x2B1B,0xFE},{0x2B1C,0xFE},
    {0x2713,0xFB},{0x2714,0xFB},{0x2705,0xFB},{0x2715,'x'},{0x2716,'x'},{0x2717,'x'},{0x2718,'x'},{0x274C,'x'},
    {0x2605,'*'},{0x2606,'*'},{0x22C6,'*'},{0x2722,'*'},{0x2723,'*'},{0x2724,'*'},{0x2725,'*'},{0x2726,'*'},{0x2727,'*'},
    {0x2731,'*'},{0x2732,'*'},{0x2733,'*'},{0x2734,'*'},{0x2735,'*'},{0x2736,'*'},{0x2737,'*'},{0x2738,'*'},{0x2739,'*'},
    {0x273A,'*'},{0x273B,'*'},{0x273C,'*'},{0x273D,'*'} };
static uint8_t cp437_for(uint32_t u)                            /* 0: CP437 has nothing like it */
{
    unsigned i;
    for (i = 0; i < 128; i++) if (cp437_hi[i] == u) return (uint8_t)(0x80 + i);
    for (i = 1; i < 32; i++)  if (cp437_lo[i] == u) return (uint8_t) i;
    if (u == cp437_lo[0]) return 0x7F;
    for (i = 0; i < sizeof cp437_near / sizeof cp437_near[0]; i++) if (cp437_near[i].u == u) return cp437_near[i].c;
    if ((u >= 0xE000 && u <= 0xF8FF) || u >= 0xF0000 || (u >= 0x23F4 && u <= 0x23F7))
        return ' ';                                             /* icons: Nerd Font private-use glyphs, the play/pause
                                                                 * triangles -- a blank reads better than '?' (Doc, 2026-09-12) */
    if (u >= 0x2800 && u <= 0x28FF) {                           /* braille (btop's graphs): by how many dots */
        int d = 0; for (unsigned b = u & 0xFF; b; b >>= 1) d += b & 1;
        return d == 0 ? ' ' : d < 3 ? 0xB0 : d < 6 ? 0xB1 : 0xB2;
    }
    return 0;
}
static int uwidth(uint32_t u)                                   /* cells the far end reckons it takes */
{
    if (u < 0xA0) return u >= 0x80 ? 0 : 1;                     /* C1 controls: nothing */
    if ((u >= 0x0300 && u <= 0x036F) || (u >= 0x200B && u <= 0x200F) || (u >= 0x2028 && u <= 0x202E)
        || (u >= 0x2060 && u <= 0x2064) || (u >= 0xFE00 && u <= 0xFE0F) || u == 0xFEFF || u == 0x00AD
        || (u >= 0x1F3FB && u <= 0x1F3FF) || (u >= 0xE0000 && u <= 0xE01EF)) return 0;
    if ((u >= 0x1100 && u <= 0x115F) || (u >= 0x2E80 && u <= 0xA4CF && u != 0x303F) || (u >= 0xAC00 && u <= 0xD7A3)
        || (u >= 0xF900 && u <= 0xFAFF) || (u >= 0xFE30 && u <= 0xFE4F) || (u >= 0xFF00 && u <= 0xFF60)
        || (u >= 0xFFE0 && u <= 0xFFE6) || (u >= 0x1F300 && u <= 0x1F64F) || (u >= 0x1F680 && u <= 0x1F6FF)
        || (u >= 0x1F900 && u <= 0x1F9FF) || (u >= 0x20000 && u <= 0x3FFFD)) return 2;
    return 1;
}
static void utf8_mode(int on) { T.utf8 = (uint8_t)(on != 0); T.u_need = T.u_nraw = 0; }
static void utf8_spill(void)                                    /* not UTF-8 after all: the bytes were CP437 */
{
    uint8_t n = T.u_nraw, i;
    T.u_need = T.u_nraw = 0;
    for (i = 0; i < n; i++) print_char(T.u_raw[i]);
}
static int utf8_byte(uint8_t c)                                 /* 1: taken */
{
    if (T.u_need) {
        if ((c & 0xC0) == 0x80) {
            T.u_raw[T.u_nraw++] = c; T.u_cp = (T.u_cp << 6) | (c & 0x3F);
            if (--T.u_need == 0) {
                uint32_t u = T.u_cp; uint8_t n = T.u_nraw, g; int w;
                if ((n == 3 && u < 0x800) || (n == 4 && (u < 0x10000 || u > 0x10FFFF)) || (u >= 0xD800 && u <= 0xDFFF)) { utf8_spill(); return 1; }
                T.u_nraw = 0;
                if ((w = uwidth(u)) == 0) return 1;
                g = cp437_for(u);
                print_char(g ? g : '?');
                if (w == 2) print_char(' ');
            }
            return 1;
        }
        utf8_spill();                                           /* ... and c goes on to be itself */
    }
    if (c >= 0xC2 && c <= 0xF4) {
        T.u_need = (uint8_t)(c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1);
        T.u_cp = c & (c >= 0xF0 ? 0x07 : c >= 0xE0 ? 0x0F : 0x1F);
        T.u_raw[0] = c; T.u_nraw = 1;
        return 1;
    }
    return 0;                                                   /* ASCII, a control, a byte no sequence starts with */
}
/* A host session -- the `!` shell -- is a Unix program on a pty: it speaks
 * UTF-8, and its LF means "down a row, same column" (xterm-color's cud1 is ^J;
 * the pty's ONLCR already makes a program's \n a \r\n).  The ROM console wants
 * LNM, LF-returns-the-column, and sets it in video_init; left on under tmux,
 * every bare LF sent the cursor to column 0 and the ESC[nC after it skipped
 * cells tmux believed blank -- the stray characters at the left edge (Doc, the
 * Dell, 2026-09-12; found in a K4510_TERMLOG).  So a session turns LNM off and
 * gives it back after. */
static uint8_t host_lnm;
void term_host_session(int on)
{
    if (on) { host_lnm = T.lnm; T.lnm = 0; utf8_mode(1); }
    else    { T.lnm = host_lnm; utf8_mode(0); }
}

/* ---- the stream -------------------------------------------------------------- */
static void put_byte(uint8_t c)
{
    if (T.petscii && T.st == 0) { pet_byte(c); return; }
    if (T.utf8 && T.st == 0 && utf8_byte(c)) return;
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
    case 8: if (c == 'G' || c == '@') utf8_mode(c == 'G'); T.st = 0; return;
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
/* K4510_TERMLOG=file: every byte JIM receives, and every register write with
 * the cursor bookkeeping beside the REAL reverse bit under it -- the trace
 * that found the stray-cursor-block bug (Doc, the Dell, 2026-09-11). */
static FILE *termlog(void) { static FILE *lg; static int tried;
    if (!tried) { tried = 1; const char *f = getenv("K4510_TERMLOG"); if (f) lg = fopen(f, "wb"); } return lg; }
void term_write(uint8_t r, uint8_t v)
{
    if (r != 0x00 && r != 0x03) { FILE *lg = termlog(); if (lg) { fprintf(lg, "\n<r%02X<-%02X shown=%u cur_on=%u cx=%u cy=%u at=%06X bit=%u>",
        r, v, T.shown, T.cur_on, T.cx, T.cy, (unsigned) T.cur_at, (unsigned)((k4510_ram[T.cur_at] >> 7) & 1)); fflush(lg); } }
    switch (r) {
    case 0x00:
        { FILE *lg = termlog(); if (lg) {
              /* a wall-clock mark after any pause of 100 ms or more, so a log
               * says where the seconds went (a slow ssh login, 2026-09-12) */
              static long long last_ms; struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
              long long now = (long long) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
              if (now - last_ms >= 100) { time_t t = ts.tv_sec; struct tm tm; localtime_r(&t, &tm);
                  fprintf(lg, "\n<t %02d:%02d:%02d.%03d>", tm.tm_hour, tm.tm_min, tm.tm_sec, (int)(now % 1000)); }
              last_ms = now;
              fputc(v, lg); fflush(lg); } }
        cur_undraw(); put_byte(v); T.dirty = 1; cur_draw(); return;
    case 0x03: key(v); return;
    case 0x04:
        cur_undraw();
        if (v == 1) { uint8_t sh = T.shown; soft_reset(); T.shown = sh; T.cx = T.cy = 0; }   /* UTF-8 and LNM are left as they
                                                                                              * are: the ROM resets JIM (tube_term) AFTER
                                                                                              * the `!` session has switched them */
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
/* Around a save: the blink XORs the cell under the cursor in RAM, so a state
 * taken with it lit kept a reversed cell that nothing would put back (review
 * 2026-09-05, 2).  state_save parks it before the RAM goes out and puts it
 * back after; load already starts with the cursor off. */
int  term_cursor_park(void) { int was = CUR_SHOWN; cur_undraw(); return was; }
void term_cursor_unpark(int was) { if (was) cur_draw(); }
int  term_state_load(FILE *f)
{
    if (state_get(f, "JIM ", &T, sizeof T)) return -2;
    T.cur_on &= 6; T.rh &= 127; T.rt &= 127; clamp_geometry();   /* a hand-edited .k4s must not index out of bounds */
    vicky_cursor(0, 0, 0); return 0;
}
