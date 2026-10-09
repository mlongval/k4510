/* JIM, the terminal ($DA00). See term.h for the registers and the repertoire. */
#include "term.h"
#include "codepage.h"                /* the K4510 code page: one table for JIM, the keyboard and the tools */
#include "mem.h"
#include "io.h"
#include "vicky.h"
#include "jimgfx.h"                 /* JIM's pictures: the Kitty graphics protocol (2026-09-17) */
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#define NPAR 16
typedef struct {
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
    /* UNUSED since 2026-10-01, kept so a save state keeps its size: a program's
     * band claim and heights lived here (FLAGS bit 3, $DA0F, $DA16) because
     * nothing owned the layout.  VICKY does now ($D0B0-$D0B7); those three
     * JIM addresses are doors onto her registers, and term_state_load moves an
     * old state's values across. */
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
    /* Two K4510 modes (2026-10-05), for programs that draw their screens through
     * JIM rather than into the text map: Doc wanted every program to write
     * through JIM, so that a second screen -- or a pty -- could carry them.
     * They are new at the END, so a save state from before them still loads
     * (term_state_load reads the shorter record and leaves them 0). */
    uint8_t paldirect;                 /* ESC [ ? 4510 h: 38;5;n and 48;5;n with n < 16 are the palette's entry n,
                                        * not xterm's colour n -- the machine's sixteen, all of them (SGR's ANSI
                                        * order reaches twelve: orange, brown, mid and light grey are not in it) */
    uint8_t dispctl;                   /* SGR 11 (the Linux console's "display control flag"), SGR 10 off: the
                                        * bytes $00-$1F and $7F draw their CP437 glyphs, all but the ones that act --
                                        * BS, HT, LF, VT, FF, CR, SO, SI and ESC */
    char osc[64]; uint8_t oscn;        /* an OSC being received, for JIM's own (ESC ] 4510 ; ... BEL); $FF: not ours */
} term_t;

/* Two terminals since 2026-10-05 (Doc: JIM shows "the current K/OS K4510
 * program, or ... one terminal connection"): TS[0] is the machine's, the one
 * the registers at $DA00 talk to, saved in a state; TS[1] is the second
 * screen, a session on the Linux beneath or beyond (core/io.c owns its pty),
 * drawn into a map of its own that VICKY shows in the console's place while
 * it is up.  Everything below works on T, the one in hand: TS[0] except while
 * the second screen's bytes or keys go through. */
static term_t TS[2];
static term_t *tp = &TS[0];
#define T (*tp)
static int vis;                                         /* the screen VICKY shows: 0 K/OS, 1 the terminal */
#define VISIBLE (tp == &TS[vis])
static uint8_t sync_on[2], sync_age[2];             /* ESC [ ? 2026: a synchronized update, per screen (mode(), below) */
static uint32_t band_sig;                               /* the bands as last drawn (bands_tick, at the end) */
static int scr_req = -1;                                /* a screen asked for (OSC, $DA18), for io to act on */

static uint8_t *apc; static size_t apc_n, apc_cap;      /* an APC being received (not in T: a save state does not carry a half-sent picture) */
static int host_session;                               /* a `!` session: a picture's t=f path is the Linux's, not the machine's */
static void apc_done(void);
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
/* ...except when both rows lie whole in RAM, which is every row of every real
 * map: then one memmove, the cell-by-cell copy kept for the row that wraps.
 * A row is whole when its last cell is where its first plus the width says --
 * cellp wraps or zeroes an address that runs off the end, so the two cannot
 * agree then.  Scrolling was a third of what a scrolling update cost JIM
 * (the JIM review, 2026-10-09: two cellp calls a cell). */
static void copy_row(int dst, int src)
{
    size_t span = (size_t)(T.cols - 1) * 4;
    uint8_t *d = cellp(0, dst), *s = cellp(0, src);
    if (cellp(T.cols - 1, dst) == d + span && cellp(T.cols - 1, src) == s + span) { memmove(d, s, span + 4); return; }
    for (int x = 0; x < T.cols; x++) memcpy(cellp(x, dst), cellp(x, src), 4);
}
/* Where the text window is on the glass, in pixels, for the pictures that live
 * among the cells (core/jimgfx.h).  The console is VICKY's layer 0: its cells
 * are 8 wide and 8 or 16 tall by that layer's size field, and the ROM scrolls
 * the HD console down by half its spare lines, which the pictures must follow. */
static void gfx_geom(jimgfx_geom_t *g)
{
    int H = vicky_cell_h(0), CW = vicky_cell_w(0);
    int sx = vicky_read(VR_LAYER(0) + VL_SCROLLX) | vicky_read(VR_LAYER(0) + VL_SCROLLX + 1) << 8;
    int sy = (int16_t)(vicky_read(VR_LAYER(0) + VL_SCROLLY) | vicky_read(VR_LAYER(0) + VL_SCROLLY + 1) << 8);
    g->cols = T.cols; g->rows = T.rows; g->cell_w = CW; g->cell_h = H;
    g->px0 = T.ox * CW - sx; g->py0 = T.oy * H - sy; g->cx = T.cx; g->cy = T.cy; g->host = host_session;
}
static void gfx_rows_moved(int top, int bot, int n)             /* n rows up (negative) or down: the pictures in those rows go with the text */
{
    jimgfx_geom_t g;
    if (!jimgfx_active()) return;
    gfx_geom(&g);
    jimgfx_scroll(g.py0 + top * g.cell_h, g.py0 + (bot + 1) * g.cell_h, n * g.cell_h);
}
static void scroll_up(int top, int bot, int n)
{
    if (n <= 0) return;
    if (n > bot - top + 1) n = bot - top + 1;
    gfx_rows_moved(top, bot, -n);
    for (int y = top; y + n <= bot; y++) copy_row(y, y + n);
    for (int y = bot - n + 1; y <= bot; y++) blank_span(y, 0, T.cols - 1);
}
static void scroll_down(int top, int bot, int n)
{
    if (n <= 0) return;
    if (n > bot - top + 1) n = bot - top + 1;
    gfx_rows_moved(top, bot, n);
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
    vicky_dirty = 1;
    if (!CUR_SHOWN) return;
    if (CUR_ATTR) { uint8_t *a = &k4510_ram[T.cur_at];
                    if (((*a >> 7) & 1) != CUR_ORIG) *a = (uint8_t)((*a & 0x7F) | (CUR_ORIG << 7)); }
    else if (VISIBLE) vicky_cursor(0, 0, 0);
    T.cur_on &= 6;
}
static void cur_draw(void)
{
    vicky_dirty = 1;
    cur_undraw();
    if (!T.shown) return;
    T.cur_at = (uint32_t)(cellp(T.cx, T.cy) - k4510_ram) + 1;
    if (CUR_STYLE == 0) { uint8_t *a = &k4510_ram[T.cur_at]; uint8_t orig = (uint8_t)((*a >> 7) & 1);
                          *a ^= 0x80; T.cur_on = (uint8_t)((T.cur_on & 6) | 9 | (orig << 4)); }
    else { if (VISIBLE) vicky_cursor(T.cur_at, CUR_STYLE, 1); T.cur_on |= 1; }
}
static FILE *termlog(void);
static void bands_tick(int force);
/* A palette loaded while the Terminal screen has text on it (Doc, 2026-10-07:
 * F12 -> AMBER over a shell): readable_fg chose each character's colour
 * against the palette it was printed under, and AMBER made the shell's
 * yellow on blue two near shades of amber.  So when the palette changes,
 * every character already on that screen is made readable again. */
static int s2_ready;
static uint8_t readable_fg(uint8_t fg, uint8_t bg);
static void s2_recolour(void)
{
    static uint32_t gen = 0xFFFFFFFFu;
    if (gen == vicky_palette_gen()) return;
    int first = gen == 0xFFFFFFFFu;
    gen = vicky_palette_gen();
    if (first || !s2_ready) return;
    term_t *keep = tp; tp = &TS[1];
    for (int y = 0; y < T.rows; y++) for (int x = 0; x < T.cols; x++) {
        uint8_t *c = cellp(x, y);
        if (c[0] != ' ' || c[1]) c[2] = readable_fg(c[2], c[3]);
    }
    tp = keep; vicky_dirty = 1;
}
void term_tick(void)
{
    /* K4510_TERMLOG is buffered and flushed here, once a second.  Flushed per
     * byte it was one write() each, and on the Dell's persistence -- mounted
     * `sync` -- that is 242 bytes/s: a tmux redraw stalled the machine for
     * minutes, which is what "ssh crashed" was (2026-09-12).  A crash now loses
     * at most the last second of the log. */
    { static unsigned n; if (++n % 60 == 0) { FILE *lg = termlog(); if (lg) fflush(lg); } }
    bands_tick(0);                              /* the bands are JIM's (below) */
    s2_recolour();                              /* a new palette: the Terminal's text readable again */
    for (int s = 0; s < 2; s++) if (sync_on[s] && ++sync_age[s] >= 30) sync_on[s] = 0;   /* half a second: show it anyway */
    tp = &TS[vis];                              /* the cursor blinks on the screen that is up */
    if (T.shown) {
        T.frames++;
        if (T.frames & 16) { if (CUR_SHOWN) cur_undraw(); } else if (!CUR_SHOWN) cur_draw();
    }
    tp = &TS[0];
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
    T.paldirect = T.dispctl = 0;
    sync_on[tp - TS] = 0;                       /* DECSTR ends a held update too */
    T.st = 0; T.npar = 0;
    reset_tabs();
    memset(&T.saved, 0, sizeof T.saved); T.saved.fg = T.fg; T.saved.bg = T.bg;
}
static void clamp_geometry(void)
{
    /* The window lives inside the console VICKY lays out (2026-10-01): JIM can
     * be moved anywhere in it, never over a status band.  Before a text grid
     * is declared ($D0B4 = 0) there is no layout to keep to. */
    { uint8_t oy, rows, bot; vicky_layout(&oy, &rows, &bot);
      if (vicky_read(VR_TROWS) && rows) {
          /* keep the size asked for (up to the console's) and move the window
           * to fit, rather than shrink it: the ROM writes ROWS before OY, so
           * at that moment OY can still be the last layout's */
          if (T.rows > rows) T.rows = rows;
          if (T.oy < oy) T.oy = oy;
          if (T.oy + T.rows > oy + rows) T.oy = (uint8_t)(oy + rows - T.rows);
      } }
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
    jimgfx_reset();
    memset(&T, 0, sizeof T);
    T.cols = 80; T.rows = 30; T.stride = 80; T.base = 0x030000u;
    T.deffg = 7; T.defbg = 6;                        /* the ROM's yellow on blue until it says otherwise */
    soft_reset();
    T.cx = T.cy = 0;
    vis = 0; vicky_screen_map(0); band_sig = 0;      /* a reset shows the machine; a second screen's session goes on behind */
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
/* Readable colour from a Unix host (2026-10-06).  Doc is protan -- "the red
 * looks muddy, like a brown" -- and ANSI red is the VIC-II's 880000, 1.3:1 on
 * the blue background for anyone and 1.2:1 for him.  Contrast here is WCAG's
 * ratio, taken as normal eyes see the pair and as protan eyes do (Machado
 * 2009, full strength; the lower of the two counts), and a character under
 * 4.5:1 takes the lighter or darker entry of its own hue if that reads
 * better; under 2.5:1 even so, white or black.  So red on blue draws light
 * red, and a palette with a clearer light red (CLEAR.PAL) gets it read. */
#define CONTRAST_GOOD  4.5f
#define CONTRAST_FLOOR 2.5f
static const uint8_t pal_lighter[16] = { 11, 1, 10, 3, 4, 13, 14, 7, 8, 8, 10, 12, 15, 13, 14, 1 };
static const uint8_t pal_darker[16]  = { 0, 15, 2, 3, 4, 5, 6, 7, 9, 9, 2, 0, 11, 5, 6, 12 };
static float lum_n[16], lum_p[16];
static uint32_t lum_gen = 0xFFFFFFFFu;
static float srgb_lin(uint32_t c) { float v = (float)(c & 255) / 255.0f; return v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f); }
static void lum_update(void)
{
    if (lum_gen == vicky_palette_gen()) return;
    lum_gen = vicky_palette_gen();
    for (int i = 0; i < 16; i++) {
        uint32_t c = vicky_palette_rgb(i);
        float r = srgb_lin(c >> 16), g = srgb_lin(c >> 8), b = srgb_lin(c);
        lum_n[i] = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        lum_p[i] = 0.1140f * r + 0.7827f * g + 0.1034f * b;   /* Machado's protan matrix, then the same weights */
    }
}
static float ratio_of(float a, float b) { return a > b ? (a + 0.05f) / (b + 0.05f) : (b + 0.05f) / (a + 0.05f); }
static float pal_ratio(int f, int b) { float n = ratio_of(lum_n[f], lum_n[b]), p = ratio_of(lum_p[f], lum_p[b]); return n < p ? n : p; }
static uint8_t readable_fg(uint8_t fg, uint8_t bg)
{
    if (fg > 15 || bg > 15) return fg;
    lum_update();
    float best = pal_ratio(fg, bg);
    if (best >= CONTRAST_GOOD) return fg;
    uint8_t pick = fg, alt[2] = { pal_lighter[fg], pal_darker[fg] };
    for (int i = 0; i < 2; i++) { float r = pal_ratio(alt[i], bg); if (r > best) { best = r; pick = alt[i]; } }
    if (best >= CONTRAST_FLOOR) return pick;
    return pal_ratio(1, bg) >= pal_ratio(0, bg) ? 1 : 0;
}

/* The machine's own programs under another palette (Doc, 2026-10-07: EDIT,
 * PROG "and the rest ... with the monochrome palettes. some color
 * combinations are very low contrast and nigh unreadable").  They name
 * palette entries (ESC[?4510h) chosen on the VIC-II sixteen; a ramp palette
 * (AMBER, GREEN, GREY) turns some of those pairs into two near shades of one
 * colour.  So a pair keeps the contrast it was designed with: what it had on
 * the VIC-II, up to 4.5:1.  A character that falls short takes the entry
 * nearest its own in brightness that reaches it, on the same side of the
 * background where one does.  A pair meant to be dim (DOS's shadows) was dim
 * on the VIC-II too, and stays as dim as it was. */
static const uint32_t vic16[16] = {
    0x000000, 0xFFFFFF, 0x880000, 0xAAFFEE, 0xCC44CC, 0x00CC55, 0x0000AA, 0xEEEE77,
    0xDD8855, 0x664400, 0xFF7777, 0x333333, 0x777777, 0xAAFF66, 0x0088FF, 0xBBBBBB,
};
static uint8_t intent_fg[16][16];
static uint32_t intent_gen = 0xFFFFFFFFu;
static float lum_of(uint32_t c, int protan)
{
    float r = srgb_lin(c >> 16), g = srgb_lin(c >> 8), b = srgb_lin(c);
    return protan ? 0.1140f * r + 0.7827f * g + 0.1034f * b : 0.2126f * r + 0.7152f * g + 0.0722f * b;
}
static uint8_t intended_fg(uint8_t fg, uint8_t bg)
{
    if (fg > 15 || bg > 15 || fg == bg) return fg;
    lum_update();
    if (intent_gen != vicky_palette_gen()) {
        intent_gen = vicky_palette_gen();
        for (int b = 0; b < 16; b++) for (int f = 0; f < 16; f++) {
            float rn = ratio_of(lum_of(vic16[f], 0), lum_of(vic16[b], 0)), rp = ratio_of(lum_of(vic16[f], 1), lum_of(vic16[b], 1));
            float want = rn < rp ? rn : rp;
            if (want > CONTRAST_GOOD) want = CONTRAST_GOOD;
            uint8_t pick = (uint8_t) f;
            if (f != b && pal_ratio(f, b) < want * 0.9f) {
                int up = lum_of(vic16[f], 0) >= lum_of(vic16[b], 0);            /* lighter than its background, as designed */
                float best = 1e9f, most = -1; int bi = -1, mi = f;
                for (int i = 0; i < 16; i++) {
                    float r = pal_ratio(i, b);
                    if (r > most) { most = r; mi = i; }
                    if (r < want) continue;
                    float d = fabsf(lum_n[i] - lum_n[f]) + (((lum_n[i] >= lum_n[b]) != up) ? 2.0f : 0.0f);   /* the other side only if need be */
                    if (d < best) { best = d; bi = i; }
                }
                pick = (uint8_t)(bi >= 0 ? bi : mi);
            }
            intent_fg[f][b] = pick;
        }
    }
    return intent_fg[fg][bg];
}

static void print_char(uint8_t ch)
{
    uint8_t fg = T.fg, bg = T.bg, attr = 0;
    int cs = T.shift ? T.g1 : T.g0;
    if (cs == 1 && ch >= 0x60 && ch <= 0x7E) ch = decgfx[ch - 0x60];
    if (T.bold) { for (int i = 0; i < 8; i++) if (apal[i] == T.fg) { fg = apalb[i]; break; } }
    if (T.rev) { uint8_t t = fg; fg = bg; bg = t; }
    /* A Unix program's ANSI blue is the machine's own blue background (both
     * C64 colour 6), so Claude Code's inline code -- ESC[34m -- drew blue on
     * blue and vanished (the Dell, 2026-09-12); its red is not much better.
     * In a UTF-8 session (a Unix host) a character too close to its
     * background is made readable (readable_fg, above).  A BBS (CP437) keeps
     * its exact colours: art may mean it. */
    if (T.utf8 && ch != ' ') fg = readable_fg(fg, bg);
    else if (T.paldirect && ch != ' ') fg = intended_fg(fg, bg);   /* the machine's programs: as readable as designed */
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
        else if (v == 10) T.dispctl = 0;
        else if (v == 11) T.dispctl = 1;
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
            if (n < 16 && T.paldirect) { if (v == 38) T.fg = (uint8_t) n; else T.bg = (uint8_t) n; i += 2; continue; }
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
/* Synchronized update, ESC [ ? 2026 h ... l (2026-10-06): the terminals'
 * own convention (kitty, foot, WezTerm, tmux) for "hold the screen while I
 * redraw it".  PROG scrolling a long file redrew the whole window through
 * JIM's stream, a frame's worth of bytes, and the picture showed it half
 * done.  While it is set on the screen that is up, VICKY leaves the lines
 * it has not yet drawn as the last frame had them (term_hold); half a
 * second without the l, and the screen is shown anyway.  Not in T: a save
 * state does not carry a redraw in progress. */
int term_hold(void) { return sync_on[vis]; }
static void mode(int on)
{
    for (int i = 0; i < T.npar; i++) {
        int v = T.par[i];
        if (T.priv) {
            if (v == 1) T.ckm = (uint8_t) on;
            else if (v == 6) { T.origin = (uint8_t) on; move(0, 0); }
            else if (v == 7) T.wrap = (uint8_t) on;
            else if (v == 25) T.shown = (uint8_t) on;
            else if (v == 4510) T.paldirect = (uint8_t) on;
            else if (v == 2026) { int s = (int)(tp - TS), was = sync_on[s];
                                  sync_on[s] = (uint8_t) on; sync_age[s] = 0;
                                  if (was && !on && s == vis) vicky_commit(); }   /* the finished picture, shown whole at once */
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
        if (jimgfx_active()) { jimgfx_geom_t g; gfx_geom(&g);        /* the pictures in what was erased go too, as Kitty's do */
            if (m == 0) jimgfx_clear_rows(g.py0 + (T.cy + (T.cx ? 1 : 0)) * g.cell_h, 1 << 20);
            else if (m == 1) jimgfx_clear_rows(0, g.py0 + T.cy * g.cell_h);
            else jimgfx_clear(); }
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
    case 't': {                                                   /* the window in pixels and in cells: how `icat` and its kind size a picture */
        int m = P(0, 0); jimgfx_geom_t g; char b[40];
        gfx_geom(&g);
        if (m == 14) { snprintf(b, sizeof b, "\033[4;%d;%dt", T.rows * g.cell_h, T.cols * g.cell_w); reply(b); }
        else if (m == 16) { snprintf(b, sizeof b, "\033[6;%d;%dt", g.cell_h, g.cell_w); reply(b); }
        else if (m == 18) { snprintf(b, sizeof b, "\033[8;%d;%dt", T.rows, T.cols); reply(b); }
        break; }
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
    case ']': T.st = 3; T.oscn = 0; return;
    case '_': T.st = 9; apc_n = 0; return;                          /* APC: Kitty's graphics come in one (core/jimgfx.h) */
    case 'P': case '^': case 'X': T.st = 3; T.oscn = 0xFF; return;   /* DCS, PM, SOS: skipped like an OSC */
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
 * (the one font, unscii, is CP437), so there is no screen-code-ordered font
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
/* The code page (docs/K4510-CODEPAGE.md): strict CP437, the machine's default,
 * or the K4510 page, which gives 26 of CP437's Greek and maths places to Western
 * Europe's letters.  $DA17 reads and sets it; setting it copies that page's two
 * fonts into the live slots, so the screen follows at once.  Not part of T: a
 * save state does not carry it, the frontend's setting does (Doc, 2026-09-15:
 * "keep plain as default but keep modified as option"). */
static int page_k;                                              /* 1: the K4510 page */
static int page_req = -1;                                       /* the guest chose one: the frontend saves it */
const uint16_t *term_page_table(void) { return page_k ? k4510_cp : cp437_cp; }
void term_set_page(int k)
{
    vicky_dirty = 1;                            /* the fonts are copied in */
    uint32_t f8 = k ? K4510_FONT8_K_PHYS : K4510_FONT8_437_PHYS, f16 = k ? K4510_FONT16_K_PHYS : K4510_FONT16_437_PHYS;
    page_k = k != 0;
    if (k4510_ram[f8 + 0x41 * 8 + 3])   memcpy(k4510_ram + K4510_FONT8_PHYS, k4510_ram + f8, 2048);    /* an empty slot: the font stays */
    if (k4510_ram[f16 + 0x41 * 16 + 6]) memcpy(k4510_ram + K4510_FONT16_PHYS, k4510_ram + f16, 4096);
}
int term_get_page(void) { return page_k; }
int term_page_request(void) { int r = page_req; page_req = -1; return r; }
#define cp437_hi (term_page_table() + 0x80)                      /* $80-$FF: the page in use (core/codepage.h) */
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
/* The other direction: a CP437 byte the machine typed, as UTF-8 for a Unix
 * host.  In a `!` session the ROM sends an accented letter raw (é = $82), and
 * Linux, ssh and ubuntu-s1 took that for broken UTF-8 and dropped it -- dead
 * keys "did nothing" (the Dell, 2026-09-12).  Returns the length (1-3). */
int term_cp437_utf8(uint8_t b, char *out)
{
    uint32_t u = b < 0x80 ? b : cp437_hi[b - 0x80];
    if (u < 0x80)  { out[0] = (char) u; return 1; }
    if (u < 0x800) { out[0] = (char)(0xC0 | (u >> 6)); out[1] = (char)(0x80 | (u & 0x3F)); return 2; }
    out[0] = (char)(0xE0 | (u >> 12)); out[1] = (char)(0x80 | ((u >> 6) & 0x3F)); out[2] = (char)(0x80 | (u & 0x3F)); return 3;
}
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
    host_session = on != 0;
    if (on) { host_lnm = T.lnm; T.lnm = 0; utf8_mode(1); T.paldirect = T.dispctl = 0; }   /* a Linux program means xterm's colours */
    else    { T.lnm = host_lnm; utf8_mode(0); }
}

/* ---- the stream -------------------------------------------------------------- */
/* An APC has ended.  If it was a picture to show, JIM makes room the way it
 * would for that many lines of text -- scrolling, and the pictures already up
 * scroll too -- and only then is it drawn; the cursor ends on the picture's
 * last row, just past it, unless the program asked for it to stay (C=1). */
static void apc_done(void)
{
    if (tp != &TS[0]) { apc_n = 0; return; }   /* pictures are the machine's layers: not drawn for the second screen's session */
    jimgfx_geom_t g; jimgfx_todo_t todo;
    gfx_geom(&g);
    jimgfx_apc(apc, apc_n, &g, &todo); apc_n = 0;
    if (todo.reply[0]) reply(todo.reply);
    if (!todo.place) return;
    { int room = T.bot - T.cy + 1;
      if (todo.rows > room && T.cy >= T.top && T.cy <= T.bot) { int n = todo.rows - room; if (n > T.cy - T.top) n = T.cy - T.top; scroll_up(T.top, T.bot, n); T.cy = (uint8_t)(T.cy - n); } }
    gfx_geom(&g);
    jimgfx_draw(&todo, &g);
    if (!todo.keep_cursor) { int nx = T.cx + todo.cols, ny = T.cy + todo.rows - 1; T.cx = (uint8_t)(nx > T.cols - 1 ? T.cols - 1 : nx); T.cy = (uint8_t)(ny > T.bot ? T.bot : ny); T.pending = 0; }
}
/* ---- JIM's own OSCs ------------------------------------------------------------
 * ESC ] 4510 ; kos BEL      show K/OS (the first screen): tmux's binding,
 *                           `bind K run-shell "printf '\\033]4510;kos\\007' > #{client_tty}"`
 * ESC ] 4510 ; term BEL     show the terminal (the second screen)
 * ESC ] 4510 ; note ; text BEL   a line for the bottom band's left end, from
 *                           either screen ("" clears it): the bands are JIM's,
 *                           and this is how a program puts something there
 * Every other OSC (titles, colours, hyperlinks) is skipped, as before. */
static char band_note[64];
static void osc_done(void)
{
    const char *o = T.osc;
    if (T.oscn == 0xFF) return;
    T.osc[T.oscn] = 0;
    if (strncmp(o, "4510;", 5)) return;
    o += 5;
    if (!strcmp(o, "kos") || !strcmp(o, "1")) scr_req = 0;
    else if (!strcmp(o, "term") || !strcmp(o, "2")) scr_req = 1;
    else if (!strncmp(o, "note;", 5)) { snprintf(band_note, sizeof band_note, "%s", o + 5); }
}
int term_screen_request(void) { int r = scr_req; scr_req = -1; return r; }

static void put_byte(uint8_t c)
{
    if (T.petscii && T.st == 0) { pet_byte(c); return; }
    if (T.utf8 && T.st == 0 && utf8_byte(c)) return;
    switch (T.st) {
    case 0:
        if (c >= 0x20 && c != 0x7F) { print_char(c); return; }
        if (T.dispctl && (c == 0x7F || (c < 0x20 && !((c >= 0x08 && c <= 0x0F) || c == 0x1B)))) { print_char(c); return; }   /* DEL's glyph too */
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
        if (c == 7) { T.st = 0; osc_done(); }
        else if (c == 0x1B) T.st = 7;
        else if (T.oscn < sizeof T.osc - 1) T.osc[T.oscn++] = (char) c;
        else T.oscn = 0xFF;                                         /* too long to be ours */
        return;
    case 7: if (c == '\\') { T.st = 0; osc_done(); } else T.st = 3; return;
    case 4: T.g0 = (c == '0') ? 1 : 0; T.st = 0; return;
    case 5: T.g1 = (c == '0') ? 1 : 0; T.st = 0; return;
    case 6: if (c == '8') { for (int y = 0; y < T.rows; y++) for (int x = 0; x < T.cols; x++) put_cell(x, y, 'E', 0, T.fg, T.bg); } T.st = 0; return;
    case 8: if (c == 'G' || c == '@') utf8_mode(c == 'G'); T.st = 0; return;
    case 9:                                                            /* inside an APC: kept whole until its ST */
        if (c == 0x1B) { T.st = 10; return; }
        if (apc_n + 1 > apc_cap) { size_t nc = apc_cap ? apc_cap * 2 : 8192; uint8_t *nb = nc <= (64u << 20) ? realloc(apc, nc) : NULL; if (!nb) { T.st = 3; apc_n = 0; return; } apc = nb; apc_cap = nc; }   /* absurd: skip the rest as an OSC is skipped */
        apc[apc_n++] = c; return;
    case 10: T.st = 0; if (c == '\\') apc_done(); else apc_n = 0; return;
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
        if (k >= KEY_ALT_A && k < KEY_ALT_A + 26) { b[0] = 0x1B; b[1] = (char)('a' + (k - KEY_ALT_A)); b[2] = 0; reply(b); return; }   /* Alt as Meta, xterm's way */
        return;                                                     /* an unknown special key: nothing */
    }
}

/* ---- the registers ----------------------------------------------------------- */
uint8_t term_read(uint8_t r)
{
    switch (r) {
    case 0x18: return (uint8_t) vis;                       /* SCREEN: which is up */
    case 0x01: return (uint8_t)((T.rh != T.rt ? 0x80 : 0) | (T.dirty ? 1 : 0));
    case 0x02: { uint8_t v = 0; if (T.rh != T.rt) { v = T.rep[T.rt]; T.rt = (uint8_t)((T.rt + 1) & 127); } return v; }
    case 0x05: return T.cols;  case 0x06: return T.rows;
    case 0x07: return T.ox;    case 0x08: return T.oy;
    case 0x09: return T.cx;    case 0x0A: return T.cy;
    case 0x0B: return T.fg;    case 0x0C: return T.bg;
    case 0x0D: return T.stride;
    case 0x0E: return (uint8_t)((T.shown ? 1 : 0) | (T.ckm ? 2 : 0) | (T.petscii ? 4 : 0) | ((vicky_read(VR_BANDCTL) & VB_PROGRAM) ? 8 : 0));
    case 0x0F: return vicky_read(VR_BANDTOP);
    case 0x10: case 0x11: case 0x12: case 0x13: return (uint8_t)(T.base >> (8 * (r - 0x10)));
    case 0x14: return T.deffg; case 0x15: return T.defbg;
    case 0x16: return vicky_read(VR_BANDBOT);
    case 0x17: return (uint8_t) page_k;                         /* CODEPAGE: 0 CP437, 1 the K4510 page */
    default: return 0;
    }
}
/* K4510_TERMLOG=file: every byte JIM receives, and every register write with
 * the cursor bookkeeping beside the REAL reverse bit under it -- the trace
 * that found the stray-cursor-block bug (Doc, the Dell, 2026-09-11). */
static FILE *termlog(void) { static FILE *lg; static int tried;
    if (!tried) { tried = 1; const char *f = getenv("K4510_TERMLOG"); if (f) { lg = fopen(f, "wb"); if (lg) setvbuf(lg, NULL, _IOFBF, 1 << 16); } }
    return lg; }
void term_write(uint8_t r, uint8_t v)
{
    vicky_dirty = 1;
    if (r != 0x00 && r != 0x03) { FILE *lg = termlog(); if (lg) { fprintf(lg, "\n<r%02X<-%02X shown=%u cur_on=%u cx=%u cy=%u at=%06X bit=%u>",
        r, v, T.shown, T.cur_on, T.cx, T.cy, (unsigned) T.cur_at, (unsigned)((k4510_ram[T.cur_at] >> 7) & 1)); } }
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
              fputc(v, lg); } }                         /* flushed once a second in term_tick */
        cur_undraw(); put_byte(v); T.dirty = 1; cur_draw(); return;
    case 0x03: key(v); return;
    case 0x04:
        cur_undraw();
        if (v == 1) { uint8_t sh = T.shown; soft_reset(); T.shown = sh; T.cx = T.cy = 0; }   /* UTF-8 and LNM are left as they
                                                                                              * are: the ROM resets JIM (tube_term) AFTER
                                                                                              * the `!` session has switched them */
        if (v == 2) { for (int y = 0; y < T.rows; y++) blank_span(y, 0, T.cols - 1); T.cx = T.cy = 0; T.pending = 0; jimgfx_clear(); }
        cur_draw(); return;
    case 0x05: cur_undraw(); if (T.cols != v) jimgfx_reset(); T.cols = v; clamp_geometry(); T.bot = (uint8_t)(T.rows - 1); T.top = 0; cur_draw(); return;
    case 0x06: cur_undraw(); if (T.rows != v) jimgfx_reset(); T.rows = v; clamp_geometry(); T.bot = (uint8_t)(T.rows - 1); T.top = 0; cur_draw(); return;
    case 0x07: cur_undraw(); T.ox = v; clamp_geometry(); cur_draw(); return;
    case 0x08: cur_undraw(); T.oy = v; clamp_geometry(); cur_draw(); return;
    case 0x09: cur_undraw(); T.cx = v; clamp_geometry(); T.pending = 0; T.dirty = 0; cur_draw(); return;
    case 0x0A: cur_undraw(); T.cy = v; clamp_geometry(); T.pending = 0; T.dirty = 0; cur_draw(); return;
    case 0x0B: T.fg = v; return;
    case 0x0C: T.bg = v; return;
    case 0x0D: cur_undraw(); T.stride = v; clamp_geometry(); cur_draw(); return;
    case 0x0E: cur_undraw(); T.shown = v & 1;
               if (((v >> 2) & 1) != T.petscii) { T.petscii = (v >> 2) & 1; T.pet_lower = 0; }
               vicky_write(VR_BANDCTL, (uint8_t)((vicky_read(VR_BANDCTL) & ~VB_PROGRAM) | ((v & 8) ? VB_PROGRAM : 0)));   /* the claim is VICKY's */
               cur_draw(); return;
    case 0x0F: vicky_write(VR_BANDTOP, v); return;
    case 0x10: case 0x11: case 0x12: case 0x13:
        cur_undraw(); T.base = (T.base & ~(0xFFu << (8 * (r - 0x10)))) | ((uint32_t) v << (8 * (r - 0x10))); T.base &= K4510_PHYS_MASK; cur_draw(); return;
    case 0x14: T.deffg = v; return;
    case 0x15: T.defbg = v; return;
    case 0x16: vicky_write(VR_BANDBOT, v); return;
    case 0x17: term_set_page(v & 1); page_req = page_k; return;
    case 0x18: scr_req = v & 1; return;                 /* SCREEN: the TERMINAL command; io starts the session (s2_pump) */
    default: return;
    }
}

int term_cell_h(void) { jimgfx_geom_t g; gfx_geom(&g); return g.cell_h; }

/* ---- save states (core/state.h) ------------------------------------------ */
#include "state.h"
void term_state_save(FILE *f) { state_put(f, "JIM ", &TS[0], sizeof TS[0]); }   /* the machine's terminal; the second screen is a live session, not state */
/* Around a save: the blink XORs the cell under the cursor in RAM, so a state
 * taken with it lit kept a reversed cell that nothing would put back (review
 * 2026-09-05, 2).  state_save parks it before the RAM goes out and puts it
 * back after; load already starts with the cursor off. */
int  term_cursor_park(void) { int was = CUR_SHOWN; cur_undraw(); return was; }
void term_cursor_unpark(int was) { if (was) cur_draw(); }
int  term_state_load(FILE *f)
{
    /* A state from before paldirect/dispctl has a shorter record: read what it
     * has, and the two modes start off. */
    { char t[4]; uint32_t len;
      if (fread(t, 1, 4, f) != 4 || fread(&len, 4, 1, f) != 1 || memcmp(t, "JIM ", 4)) return -2;
      if (len != sizeof T && len != offsetof(__typeof__(T), paldirect)) return -2;
      memset(&T, 0, sizeof T);
      if (fread(&T, 1, len, f) != len) return -2; }
    if (T.bandclaim || T.bandtop || T.bandbot) {                /* a state from before VICKY owned the layout */
        vicky_write(VR_BANDTOP, T.bandtop); vicky_write(VR_BANDBOT, T.bandbot);
        if (T.bandclaim) vicky_write(VR_BANDCTL, (uint8_t)(vicky_read(VR_BANDCTL) | VB_PROGRAM));
        T.bandclaim = T.bandtop = T.bandbot = 0;
    }
    T.cur_on &= 6; T.rh &= 127; T.rt &= 127; clamp_geometry();   /* a hand-edited .k4s must not index out of bounds */
    if (T.npar > NPAR) T.npar = NPAR;                            /* ...nor may the parser's own counters (review 2026-09-17): */
    if (T.u_need > 3 || T.u_nraw + T.u_need > 4) T.u_need = T.u_nraw = 0;   /* u_raw[] is 4, a sequence at most 4 */
    T.cur_at &= K4510_PHYS_MASK;
    vicky_cursor(0, 0, 0); return 0;
}

/* ---- the bands: JIM's since 2026-10-05 ---------------------------------------
 * Doc: "the status bands are PART OF and OWNED BY JIM ... the same way as ...
 * the status bar at the bottom of TMUX or NVIM".  K/OS lays the console out
 * between them (VICKY's $D0B0-$D0BF) and draws nothing in them; JIM fills
 * BANDMAP here, once a frame if anything on them changed: the top band has
 * which screen is up at its left and the clock at its right, the bottom band
 * a program's note (ESC ] 4510 ; note ; text BEL) and the host's battery.
 * While a program has claimed them (BANDCTL bit1) they are the program's and
 * JIM keeps out, as K/OS did.  The rows between a band and the console are
 * blank in the shell's colours.  The clock is the host's, as the RTC is,
 * in the order and the hours $D52F asks for -- the same text the ROM drew. */
#define BAND_FG0 1                              /* white on dark grey, unless it does not read there (bands_tick) */
static void bcell(int col, int row, uint8_t ch, uint8_t f, uint8_t b)
{
    uint32_t a = vicky_text_cell(col, row);
    k4510_ram[a] = ch; k4510_ram[a + 1] = 0; k4510_ram[a + 2] = f; k4510_ram[a + 3] = b;
}
static void bstr(int col, int row, int end, const char *s, uint8_t f, uint8_t b)
{
    while (*s && col < end) bcell(col++, row, (uint8_t) *s++, f, b);
}
static int band_left = 1;                         /* where the bottom band's left-hand notes start, after REMOTE */
int term_band_left(void) { return band_left; }
static void bfill(int row, int cols, uint8_t f, uint8_t b) { for (int c = 0; c < cols; c++) bcell(c, row, ' ', f, b); }
static int screen2_shown(void);                  /* below */
static void bands_tick(int force)
{
    uint8_t oy, rows, bot, cols, fmt = io_clockfmt(), f = BAND_FG0, b = (uint8_t)(io_frame & 15);   /* the frame's colour (F12) */
    int claimed;
    if (!vicky_bands(&oy, &rows, &bot, &cols, &claimed) || claimed || (!oy && !bot) || !cols) { band_sig = 0; return; }
    /* White on the grey, unless it reads under 4.5:1 there, for normal or
     * protan eyes (readable_fg's measure): then the entry that reads best.
     * The ramps (amber, green, grey) and CLEAR's light grey take black --
     * CLEAR's white on AAAAAA was 2.3:1, which the old brightness test
     * passed (Doc, 2026-10-06: "should the foreground for the bars be
     * something else?"). */
    lum_update();
    if (!io_frame_follow) f = (uint8_t)(b == 1 ? 0 : 1);    /* the frontend draws the band lines in the frame's own colours:
                                                             * f is only a different entry, for it to tell apart */
    else if (pal_ratio(f, b) < CONTRAST_GOOD) {
        float bc = -1; for (int i = 0; i < 16; i++) { float c = pal_ratio(i, b); if (c > bc) { bc = c; f = (uint8_t) i; } }
    }
    time_t now = time(NULL); struct tm m; localtime_r(&now, &m);
    uint32_t sig = 2166136261u;
    #define MIX(v) (sig = (sig ^ (uint32_t)(v)) * 16777619u)
    MIX(oy); MIX(rows); MIX(bot); MIX(cols); MIX(fmt); MIX(io_battery); MIX(io_batt_min); MIX(io_frame); MIX(io_frame_follow); MIX(io_remote); MIX(io_net); MIX(io_net_q); MIX(f); MIX(b);
    MIX(TS[0].deffg); MIX(TS[0].defbg); MIX(vis); MIX(screen2_shown()); MIX(vicky_palette_gen());
    MIX(m.tm_min); MIX(m.tm_hour); MIX(m.tm_mday); MIX(m.tm_mon); MIX(m.tm_year);
    for (const char *q = band_note; *q; q++) MIX(*q);
    for (const char *q = io_title(); *q; q++) MIX(*q);
    #undef MIX
    if (!sig) sig = 1;
    if (sig == band_sig && !force) return;
    band_sig = sig; vicky_dirty = 1;
    int last = oy + rows + bot - 1;
    for (int r = 1; r < oy; r++) bfill(r, cols, TS[0].deffg, TS[0].defbg);            /* the spacers */
    for (int r = oy + rows; r < last; r++) bfill(r, cols, TS[0].deffg, TS[0].defbg);
    if (oy) {
        static const char sep[3] = { '.', '-', '/' };
        static const uint8_t ord[3][3] = { { 0, 1, 2 }, { 2, 1, 0 }, { 1, 0, 2 } };
        char clk[24]; int n, k = (fmt >> 1) & 3, h = m.tm_hour;
        if (k > 2) k = 0;
        if (!(fmt & 1)) { h %= 12; if (!h) h = 12; }                                  /* 12-hour: 0 and 12 both read 12 */
        n = snprintf(clk, sizeof clk, "%02d:%02d ", h, m.tm_min);
        if (!(fmt & 1)) n += snprintf(clk + n, sizeof clk - n, "%s ", m.tm_hour >= 12 ? "PM" : "AM");
        for (int j = 0; j < 3; j++) {
            int w = ord[k][j];
            n += snprintf(clk + n, sizeof clk - n, w == 2 ? "%04d" : "%02d", w == 2 ? m.tm_year + 1900 : w ? m.tm_mon + 1 : m.tm_mday);
            if (j < 2) clk[n++] = sep[k];
        }
        clk[n] = 0;
        bfill(0, cols, f, b);
        /* What runs on the screen that is up, plain, as the frontend drew it
         * before the bands were JIM's: K/OS at the prompt, "LOGO
         * SQUARES.LGO", the trail of who started whom; on the second screen,
         * Terminal.  (2026-10-05 it was a tmux-like tab list, " 1 K/OS  2
         * TERMINAL " with the one up in reverse -- Doc: "a bit too heavy".) */
        {
            const char *t = vis == 1 ? "Terminal" : io_title(); char tab[168];
            int max = cols - n - 3;                                                     /* the title's cells */
            if (max > 4) {
                int len = (int) strlen(t);
                if (len > max) snprintf(tab, sizeof tab, " \xAE%s", t + len - (max - 1));   /* the end is the news */
                else snprintf(tab, sizeof tab, " %s", t);
                bstr(0, 0, cols, tab, f, b);
            }
        }
        if (n < cols) bstr(cols - n, 0, cols, clk, f, b);                                /* right-anchored, as the ROM drew it */
    }
    if (bot) {
        bfill(last, cols, f, b);
        int right = cols;                                                                /* the first cell the right-hand things take */
        if (io_battery != 0xFF) {                                                        /* "nn%" and up (on mains) or down, */
            char bt[8], tm[12]; int n = snprintf(bt, sizeof bt, "%d%%", io_battery & 0x7F), t = 0;   /* then "(7:16)" with Battery time on */
            if (io_batt_min != 0xFFFF) t = snprintf(tm, sizeof tm, " (%d:%02d)", io_batt_min / 60, io_batt_min % 60);   /* (Doc, 2026-10-07: "77% down-arrow (7:16)") */
            int at = cols - 1 - t - 1 - n;                                               /* the "nn%" cell; the arrow follows it */
            bstr(at, last, cols, bt, f, b);
            bcell(at + n, last, (io_battery & 0x80) ? 0x18 : 0x19, f, b);
            if (t) bstr(at + n + 1, last, cols, tm, f, b);
            right = at;
        }
        if (io_net != 0xFF) {                                                            /* the network, left of the battery */
            char nt[16]; int n;
            switch (io_net) {
            case NET_WIFI:  n = snprintf(nt, sizeof nt, "Wi-Fi %d%%", io_net_q); break;
            case NET_WIRED: n = snprintf(nt, sizeof nt, "LAN"); break;
            case NET_OTHER: n = snprintf(nt, sizeof nt, "Net"); break;
            default:        n = snprintf(nt, sizeof nt, "offline"); break;
            }
            if (right - n - 2 > cols / 2) { bstr(right - n - 2, last, right, nt, f, b); right = right - n - 2; }
        }
        int left = 1;
        if (io_remote) {                                                                 /* REMOTE, reversed, first: nobody watches unseen */
            char rm[40]; int n = snprintf(rm, sizeof rm, " REMOTE ");
            bstr(0, last, right, rm, b, f);
            n = snprintf(rm, sizeof rm, "%s%s%s ", io_remote & REMOTE_KEYS ? " keys" : "", io_remote & REMOTE_LOGIN ? " login" : "",
                         io_remote & REMOTE_VIEW ? " viewed" : "");
            bstr(8, last, right, rm, f, b);
            left = 8 + n + 1;
        }
        band_left = left;
        { int end = right - 1;                                                           /* the note, left, up to them */
          if (band_note[0] && end > left) bstr(left, last, end, band_note, f, b); }
    }
}
void term_bands_redraw(void) { band_sig = 0; }

/* ---- the second screen ---------------------------------------------------------
 * A terminal of JIM's own (TS[1]) in a map of its own, the console's size and
 * place; core/io.c runs the session on a pty and hands the bytes and keys
 * through here.  It follows the console's geometry: a MODE change resizes it,
 * and io tells the pty. */
#define ALT_MAP 0x0FD40000u                      /* free far memory the ROM keeps (SWAPSCR ends at $0FD1BC70) */
static int screen2_shown(void) { return s2_ready; }
static void s2_blank(void)
{
    uint32_t n = (uint32_t) TS[0].stride * 67;   /* every row the console's map can have, margins and all */
    for (uint32_t i = 0; i < n; i++) { uint8_t *c = &k4510_ram[ALT_MAP + i * 4]; c[0] = ' '; c[1] = 0; c[2] = TS[1].deffg; c[3] = TS[1].defbg; }
}
int term2_fit(int *cols, int *rows)               /* the console's geometry, if it moved: 1 and the new size */
{
    term_t *a = &TS[1], *k = &TS[0];
    if (!s2_ready) return 0;
    if (a->cols == k->cols && a->rows == k->rows && a->ox == k->ox && a->oy == k->oy && a->stride == k->stride) return 0;
    tp = a; cur_undraw();
    T.cols = k->cols; T.rows = k->rows; T.ox = k->ox; T.oy = k->oy; T.stride = k->stride;
    T.deffg = k->deffg; T.defbg = k->defbg;
    clamp_geometry(); T.top = 0; T.bot = (uint8_t)(T.rows - 1); T.cx = T.cy = 0; T.pending = 0;
    s2_blank();
    if (VISIBLE && T.shown) cur_draw();
    tp = &TS[0];
    if (cols) *cols = a->cols;
    if (rows) *rows = a->rows;
    return 1;
}
void term2_open(void)
{
    if (s2_ready) return;
    memset(&TS[1], 0, sizeof TS[1]);
    TS[1].base = ALT_MAP; TS[1].stride = 1;      /* term2_fit takes the console's */
    TS[1].deffg = TS[0].deffg; TS[1].defbg = TS[0].defbg;
    tp = &TS[1]; soft_reset(); T.shown = 1; T.lnm = 0; utf8_mode(1); tp = &TS[0];   /* a Unix session: UTF-8, LF only moves down */
    s2_ready = 1;
    term2_fit(NULL, NULL);
}
int term2_size(int *cols, int *rows) { if (cols) *cols = TS[1].cols; if (rows) *rows = TS[1].rows; return s2_ready; }
int term_screen(void) { return vis; }
void term_screen_show(int n)
{
    n = n ? 1 : 0;
    if (n == vis) return;
    if (n) term2_open();
    tp = &TS[vis]; cur_undraw(); tp = &TS[0];    /* the screen going: its cursor off the glass */
    vis = n;
    vicky_screen_map(n ? ALT_MAP : 0);
    tp = &TS[vis]; if (T.shown) cur_draw(); tp = &TS[0];
    band_sig = 0;
}
void term2_feed(const uint8_t *b, size_t n)
{
    vicky_dirty = 1;
    if (!s2_ready || !n) return;
    tp = &TS[1];
    cur_undraw();
    while (n--) put_byte(*b++);
    if (T.shown) cur_draw();
    tp = &TS[0];
}
size_t term2_replies(uint8_t *out, size_t max)    /* what the second screen's JIM says back: DSR, DA, and the keys */
{
    size_t n = 0;
    while (n < max && TS[1].rh != TS[1].rt) { out[n++] = TS[1].rep[TS[1].rt]; TS[1].rt = (uint8_t)((TS[1].rt + 1) & 127); }
    return n;
}
void term2_key(uint8_t k) { if (!s2_ready) return; tp = &TS[1]; key(k); tp = &TS[0]; }
void term2_say(const char *s) { term2_feed((const uint8_t *) s, strlen(s)); }
void term2_wipe(void)
{
    if (!s2_ready) return;
    tp = &TS[1]; cur_undraw(); s2_blank(); T.cx = T.cy = 0; T.pending = 0; if (VISIBLE && T.shown) cur_draw(); tp = &TS[0];
    vicky_dirty = 1;
}
