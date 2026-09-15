/* K4510: PAINT -- a paint program for the mouse, on a 640x480 canvas.
 *
 *   PAINT            a new picture (saved as PICTURE.PIC unless named)
 *   PAINT NAME.PIC   that picture: K4PC, the format BOOK shows
 *
 * The toolbar along the bottom holds 32 colours (DawnBringer's 32), ten
 * tools, four brush sizes and the file buttons; Tab hides it, to paint
 * there too.  The left button paints, the right one picks up the colour
 * under the pointer, and the wheel steps through the colours.
 *
 *   P pen   L line   B box   K block (a filled box)   O oval   D disc (filled)
 *   F fill  S spray  E eraser   I pick a colour   1-4 brush size   [ ] colour
 *   Ctrl-Z undo (again: redo)   Ctrl-N new   Ctrl-O open   Ctrl-S save
 *   Tab the toolbar   Esc leave (asks first if the picture is not saved)
 *
 * The canvas is VICKY layer 1, 8 bpp at $200000, with the text layer off,
 * as BOOK shows a picture.  The blitter draws the lines and fills, clipped
 * above the toolbar; the rows the toolbar covers wait in far memory.  A
 * picture is saved as K4PC runs (format 0) with its palette.
 */
#include "k4510.h"

#define TERM    0xDA00u
#define FS      0xD300u
#define BLT     0xD070u
#define BITMAP  0x00200000UL
#define LOADBUF 0x0D800000UL                 /* far memory nothing else uses (HEXED is below it) */
#define LOADMAX 0x00180000UL
#define UNDO    0x0DA00000UL
#define TMP     0x0DB00000UL
#define UNDER   0x0DC00000UL                 /* the canvas's rows under the toolbar */
#define SPRTAB  0x123000UL
#define SPRDATA 0x123100UL
#define MOUSEX  0xD108u
#define MOUSEY  0xD10Au
#define MOUSEB  0xD10Cu
#define MOUSEW  0xD10Du
#define W   640
#define H   480
#define TBY 448                              /* the toolbar's top line */
#define SHELL_RC (*(volatile uint8_t *)0x03FF)

static void rom_video(void) { ((void (*)(void))0xFF92)(); }
static unsigned char rom_args(void) { return ((unsigned char (*)(void))0xFF95)(); }

/* DawnBringer's 32 */
static const uint8_t db32[96] = {
    0x00,0x00,0x00, 0x22,0x20,0x34, 0x45,0x28,0x3c, 0x66,0x39,0x31, 0x8f,0x56,0x3b, 0xdf,0x71,0x26, 0xd9,0xa0,0x66, 0xee,0xc3,0x9a,
    0xfb,0xf2,0x36, 0x99,0xe5,0x50, 0x6a,0xbe,0x30, 0x37,0x94,0x6e, 0x4b,0x69,0x2f, 0x52,0x4b,0x24, 0x32,0x3c,0x39, 0x3f,0x3f,0x74,
    0x30,0x60,0x82, 0x5b,0x6e,0xe1, 0x63,0x9b,0xff, 0x5f,0xcd,0xe4, 0xcb,0xdb,0xfc, 0xff,0xff,0xff, 0x9b,0xad,0xb7, 0x84,0x7e,0x87,
    0x69,0x6a,0x6a, 0x59,0x56,0x52, 0x76,0x42,0x8a, 0xac,0x32,0x32, 0xd9,0x57,0x63, 0xd7,0x7b,0xba, 0x8f,0x97,0x4a, 0x8a,0x6f,0x30 };
/* The toolbar's own colours, above any a picture brings (a picture's palette stops at 240; 241 and 242 are the pointer's). */
#define C_DARK  243
#define C_BTN   244
#define C_SEL   245
#define C_WHITE 246
#define C_BLACK 247
static const uint8_t uicol[15] = { 0x22,0x20,0x34, 0x45,0x28,0x3c, 0x63,0x9b,0xff, 0xff,0xff,0xff, 0x00,0x00,0x00 };

enum { T_PEN, T_LINE, T_BOX, T_BLK, T_OVL, T_DSK, T_FILL, T_SPR, T_ERA, T_PICK };
static const char *const tname[10] = { "PEN", "LIN", "BOX", "BLK", "OVL", "DSK", "FIL", "SPR", "ERA", "PIK" };
static const char tkey[10] = { 'P', 'L', 'B', 'K', 'O', 'D', 'F', 'S', 'E', 'I' };
static const char *const bname[6] = { "UNDO", "NEW", "OPEN", "SAVE", "HIDE", "QUIT" };
static const int bx[6] = { 384, 426, 460, 502, 544, 586 };
static const uint8_t bw[6] = { 38, 30, 38, 38, 38, 38 };
static const uint8_t sizes[4] = { 1, 3, 6, 12 };

static uint8_t ink = 0, paper = 21, tool, bsz, tb_on = 1, modified, drawing;
static uint16_t clip_h = TBY, lim = TBY, pal_n = 32;
static char fname[64];
static uint8_t row_a[W], row_b[W];

static void fs_w32(uint8_t r, uint32_t v)
{
    REG(FS + r) = (uint8_t) v; REG(FS + r + 1) = (uint8_t)(v >> 8);
    REG(FS + r + 2) = (uint8_t)(v >> 16); REG(FS + r + 3) = (uint8_t)(v >> 24);
}
static uint32_t fs_r32(uint8_t r)
{
    return (uint32_t) REG(FS + r) | ((uint32_t) REG(FS + r + 1) << 8) | ((uint32_t) REG(FS + r + 2) << 16) | ((uint32_t) REG(FS + r + 3) << 24);
}
static uint8_t fs_cmd(uint8_t c) { REG(FS) = c; return REG(FS + 1); }
static int iabs(int v) { return v < 0 ? -v : v; }

/* ---- drawing: the blitter, clipped to the canvas (lim: the lines below it are the toolbar's) ---- */
static void blt_colour(uint8_t c) { REG(BLT) = c; REG(BLT + 1) = 0; REG(BLT + 2) = 0; REG(BLT + 3) = 0; }
static void blt_go(uint8_t op) { REG(BLT + 0x10) = op; REG(BLT + 0x12) = 1; }
static void line(int x0, int y0, int x1, int y1, uint8_t c)
{
    blt_colour(c);
    w32(BLT + 4, BITMAP); w16(BLT + 8, W); w16(BLT + 10, lim); w16(BLT + 14, W);
    w16(0xD084u, (uint16_t) x0); w16(0xD086u, (uint16_t) y0); w16(0xD088u, (uint16_t) x1); w16(0xD08Au, (uint16_t) y1);
    blt_go(6);
}
static void box_fill(int x, int y, int w, int h, uint8_t c)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > W) w = W - x;
    if (y + h > (int) lim) h = (int) lim - y;
    if (w <= 0 || h <= 0) return;
    blt_colour(c);
    w32(BLT + 4, BITMAP + (uint32_t) y * W + x); w16(BLT + 8, (uint16_t) w); w16(BLT + 10, (uint16_t) h); w16(BLT + 14, W);
    blt_go(2);
}
static void plot(int x, int y, uint8_t c)
{
    if (x >= 0 && x < W && y >= 0 && y < (int) lim) far_poke(BITMAP + (uint32_t) y * W + x, c);
}
static uint8_t pixel(int x, int y) { return far_peek(BITMAP + (uint32_t) y * W + x); }

static void stamp(int x, int y, uint8_t c)
{
    uint8_t s = sizes[bsz];
    if (s == 1) plot(x, y, c); else box_fill(x - s / 2, y - s / 2, s, s, c);
}
static void stroke(int x0, int y0, int x1, int y1, uint8_t c)
{
    int dx = x1 - x0, dy = y1 - y0, n, i, step;
    uint8_t s = sizes[bsz];
    if (s == 1) { line(x0, y0, x1, y1, c); return; }
    n = iabs(dx) > iabs(dy) ? iabs(dx) : iabs(dy);
    step = s / 2;
    n = n / step + 1;
    for (i = 0; i <= n; i++) stamp(x0 + (int)((long) dx * i / n), y0 + (int)((long) dy * i / n), c);
}

static uint16_t isqrt(uint32_t v)
{
    uint32_t r = 0, b = 1UL << 30;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1;
        b >>= 2;
    }
    return (uint16_t) r;
}
static int half_w(int rx, int ry, int y)      /* the ellipse's half-width y lines from its middle */
{
    if (!ry) return rx;
    return (int)((long) rx * isqrt((uint32_t)((long) ry * ry - (long) y * y)) / ry);
}
static void oval(int cx, int cy, int rx, int ry, uint8_t c, uint8_t filled)
{
    int y, w0, w1, lo;
    for (y = 0; y <= ry; y++) {
        w0 = half_w(rx, ry, y);
        if (filled) {
            box_fill(cx - w0, cy + y, 2 * w0 + 1, 1, c);
            if (y) box_fill(cx - w0, cy - y, 2 * w0 + 1, 1, c);
            continue;
        }
        w1 = y < ry ? half_w(rx, ry, y + 1) : -1;
        lo = w1 + 1 > w0 ? w0 : w1 + 1;
        box_fill(cx + lo, cy + y, w0 - lo + 1, 1, c); box_fill(cx - w0, cy + y, w0 - lo + 1, 1, c);
        if (y) { box_fill(cx + lo, cy - y, w0 - lo + 1, 1, c); box_fill(cx - w0, cy - y, w0 - lo + 1, 1, c); }
    }
}

/* Fill: the span a pixel is in, then the spans touching it above and below. */
static int16_t stx[512], sty[512];
static void flood(int x, int y, uint8_t c)
{
    uint8_t t;
    int l, r, ny, i, d;
    uint16_t sp = 0;
    t = pixel(x, y);
    if (t == c) return;
    stx[sp] = (int16_t) x; sty[sp] = (int16_t) y; sp++;
    while (sp) {
        sp--; x = stx[sp]; y = sty[sp];
        dma_copy(BITMAP + (uint32_t) y * W, (uint32_t)(uint16_t) row_a, W);
        if (row_a[x] != t) continue;
        l = x; while (l > 0 && row_a[l - 1] == t) l--;
        r = x; while (r < W - 1 && row_a[r + 1] == t) r++;
        box_fill(l, y, r - l + 1, 1, c);
        for (d = -1; d <= 1; d += 2) {
            ny = y + d;
            if (ny < 0 || ny >= (int) lim) continue;
            dma_copy(BITMAP + (uint32_t) ny * W, (uint32_t)(uint16_t) row_b, W);
            i = l;
            while (i <= r) {
                if (row_b[i] == t) {
                    if (sp < 512) { stx[sp] = (int16_t) i; sty[sp] = (int16_t) ny; sp++; }
                    while (i <= r && row_b[i] == t) i++;
                } else i++;
            }
        }
    }
}

static uint16_t seed = 7;
static uint8_t rnd(void) { seed = seed * 25173u + 13849u; return (uint8_t)(seed >> 8); }
static void spray(int x, int y, uint8_t c)
{
    int r = sizes[bsz] * 3 + 6, i, dx, dy;
    for (i = 0; i < 12; i++) {
        dx = (int)(rnd() % (2 * r + 1)) - r; dy = (int)(rnd() % (2 * r + 1)) - r;
        if (dx * dx + dy * dy <= r * r) plot(x + dx, y + dy, c);
    }
}

/* ---- the canvas as a whole: undo, and the rows under the toolbar -------- */
static uint32_t strip(void) { return tb_on ? UNDER : BITMAP + (uint32_t) TBY * W; }
static uint32_t row_at(uint16_t y) { return y < TBY ? BITMAP + (uint32_t) y * W : strip() + (uint32_t)(y - TBY) * W; }
static void snap(uint32_t dst)
{
    dma_copy(BITMAP, dst, (uint32_t) TBY * W);
    dma_copy(strip(), dst + (uint32_t) TBY * W, (uint32_t)(H - TBY) * W);
}
static void put_back(uint32_t src)
{
    dma_copy(src, BITMAP, (uint32_t) TBY * W);
    dma_copy(src + (uint32_t) TBY * W, strip(), (uint32_t)(H - TBY) * W);
}
static void undo(void) { snap(TMP); put_back(UNDO); dma_copy(TMP, UNDO, (uint32_t) W * H); }
static void clear_canvas(void)
{
    uint16_t l = lim;
    lim = clip_h; box_fill(0, 0, W, clip_h, paper); lim = l;
    if (tb_on) dma_fill(paper, UNDER, (uint32_t)(H - TBY) * W);
}

/* ---- the toolbar -------------------------------------------------------- */
static uint8_t gl[8];
static void text(int x, int y, const char *s, uint8_t fg, uint8_t bg)
{
    uint8_t r, b, i;
    for (; *s; s++, x += 8)
        for (r = 0; r < 8; r++) {
            b = far_peek(FONT8 + (uint16_t)(uint8_t) *s * 8 + r);
            for (i = 0; i < 8; i++) gl[i] = (b & (0x80 >> i)) ? fg : bg;
            dma_copy((uint32_t)(uint16_t) gl, BITMAP + (uint32_t)(y + r) * W + x, 8);
        }
}
static void draw_toolbar(void)
{
    uint8_t i;
    static char s[2];
    if (!tb_on) return;
    lim = H;
    box_fill(0, TBY, W, H - TBY, C_DARK);
    for (i = 0; i < 32; i++) {
        int x = 2 + (i & 15) * 16, y = TBY + 2 + (i >> 4) * 14;
        if (i == ink) box_fill(x - 1, y - 1, 16, 14, C_WHITE);
        box_fill(x, y, 14, 12, i);
    }
    box_fill(261, TBY + 2, 32, 26, C_WHITE); box_fill(263, TBY + 4, 28, 22, ink);
    for (i = 0; i < 10; i++) {
        int x = 300 + i * 34;
        uint8_t bg = i == tool ? C_SEL : C_BTN;
        box_fill(x, TBY + 2, 32, 12, bg);
        text(x + 4, TBY + 4, tname[i], i == tool ? C_BLACK : C_WHITE, bg);
    }
    for (i = 0; i < 4; i++) {
        int x = 300 + i * 20;
        uint8_t bg = i == bsz ? C_SEL : C_BTN;
        box_fill(x, TBY + 17, 18, 12, bg);
        s[0] = (char)('1' + i); s[1] = 0;
        text(x + 5, TBY + 19, s, i == bsz ? C_BLACK : C_WHITE, bg);
    }
    for (i = 0; i < 6; i++) { box_fill(bx[i], TBY + 17, bw[i], 12, C_BTN); text(bx[i] + 3, TBY + 19, bname[i], C_WHITE, C_BTN); }
    lim = clip_h;
}
static void tb_show(void)
{
    if (tb_on) return;
    dma_copy(BITMAP + (uint32_t) TBY * W, UNDER, (uint32_t)(H - TBY) * W);
    tb_on = 1; clip_h = lim = TBY;
    draw_toolbar();
}
static void tb_hide(void)
{
    if (!tb_on) return;
    dma_copy(UNDER, BITMAP + (uint32_t) TBY * W, (uint32_t)(H - TBY) * W);
    tb_on = 0; clip_h = lim = H;
}
static void msg(const char *s)               /* a line over the lower half of the toolbar, until it is next drawn */
{
    tb_show();
    lim = H;
    box_fill(300, TBY + 16, W - 300, H - TBY - 16, C_DARK);
    text(302, TBY + 19, s, C_WHITE, C_DARK);
    lim = clip_h;
}
static uint8_t ask(const char *q, char *buf, uint8_t max)
{
    static char l[48];
    uint8_t n = (uint8_t) strlen(buf), k;
    for (;;) {
        strcpy(l, q); strcat(l, buf); strcat(l, "_");
        msg(l);
        while ((k = key_get()) == 0) ;
        if (k == 13) { draw_toolbar(); return n != 0; }
        if (k == 0x1B) { draw_toolbar(); return 0; }
        if ((k == 8 || k == 0x7F || k == 0x14) && n) buf[--n] = 0;
        else if (k >= 0x20 && k < 0x7F && n < max) { buf[n++] = (char) k; buf[n] = 0; }
    }
}

/* ---- the file ----------------------------------------------------------- */
static uint8_t obuf[512];
static uint16_t on;
static uint8_t oerr;
static void flush(void)
{
    if (!on) return;
    fs_w32(8, (uint16_t) obuf); fs_w32(12, on);
    if (fs_cmd(4)) oerr = 1;
    on = 0;
}
static void emit(uint8_t b) { obuf[on++] = b; if (on == sizeof obuf) flush(); }
static void save_pic(void)
{
    static const uint8_t head[4] = { 'K', '4', 'P', 'C' };
    uint16_t y, x, i;
    uint8_t cnt, v;
    fs_w32(4, (uint16_t) fname);
    if (fs_cmd(2)) { msg("could not write that name"); return; }
    on = 0; oerr = 0;
    for (i = 0; i < 4; i++) emit(head[i]);
    emit(W & 255); emit(W >> 8); emit(H & 255); emit(H >> 8);
    emit((uint8_t) pal_n); emit((uint8_t)(pal_n >> 8)); emit(0);
    for (i = 0; i < 5; i++) emit(0);
    for (i = 0; i < pal_n; i++) { REG(V_PALIDX) = (uint8_t) i; emit(REG(V_PALR)); emit(REG(V_PALG)); emit(REG(V_PALB)); }
    for (y = 0; y < H; y++) {
        dma_copy(row_at(y), (uint32_t)(uint16_t) row_a, W);
        for (x = 0; x < W; x += cnt) {
            v = row_a[x]; cnt = 1;
            while (x + cnt < W && cnt < 255 && row_a[x + cnt] == v) cnt++;
            emit(cnt); emit(v);
        }
    }
    flush(); fs_cmd(5);
    if (oerr) msg("the disk would not take it all");
    else { modified = 0; msg("saved"); }
}
static void load_pic(void)
{
    static uint8_t h[16], buf[256];
    uint16_t w, ht, nc, i, n;
    uint32_t o, end, p, y, x, take, cnt;
    uint8_t st;
    fs_w32(4, (uint16_t) fname); fs_w32(8, LOADBUF); fs_w32(12, LOADMAX);
    st = fs_cmd(9);
    if (st == 1) { msg("a new picture"); return; }
    if (st) { msg("could not read it (1.5 MB at most)"); return; }
    end = LOADBUF + fs_r32(12);
    dma_copy(LOADBUF, (uint32_t)(uint16_t) h, 16);
    if (h[0] != 'K' || h[1] != '4' || h[2] != 'P' || h[3] != 'C') { msg("that is not a K4PC picture"); return; }
    w = h[4] | (h[5] << 8); ht = h[6] | (h[7] << 8); nc = h[8] | (h[9] << 8);
    if (nc > 240) nc = 240;                                  /* 241 and 242 are the pointer's */
    o = LOADBUF + 16;
    for (i = 0; i < nc; i++, o += 3) {
        dma_copy(o, (uint32_t)(uint16_t) buf, 3);
        pal((uint8_t) i, buf[0], buf[1], buf[2]);
    }
    pal_n = nc < 32 ? 32 : nc;
    clear_canvas();
    if (!w) return;
    if (h[10] == 1) {
        for (y = 0; y < ht && y < H; y++) dma_copy(o + y * w, row_at((uint16_t) y), w < W ? w : W);
    } else {
        p = 0;
        while (o + 1 < end) {
            n = end - o > sizeof buf ? sizeof buf : (uint16_t)(end - o);
            dma_copy(o, (uint32_t)(uint16_t) buf, n);
            for (i = 0; i + 1 < n; i += 2) {
                cnt = buf[i];
                while (cnt) {
                    y = p / w; x = p % w; take = w - x; if (take > cnt) take = cnt;
                    if (y < H && x < W) dma_fill(buf[i + 1], row_at((uint16_t) y) + x, x + take > W ? W - x : take);
                    p += take; cnt -= take;
                }
            }
            o += n & ~1u;
        }
    }
}

/* ---- the pointer: MOUSETEST's arrow, in colours 241 and 242 ------------ */
static const uint8_t arrow[12] = { 0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xF0, 0xD8, 0x98, 0x0C, 0x0C };
static uint8_t at(int8_t x, int8_t y) { return (x >= 0 && x < 8 && y >= 0 && y < 12 && ((arrow[y] << x) & 0x80)) ? 1 : 0; }
static void make_pointer(void)
{
    uint32_t d = SPRDATA;
    int8_t x, y, dx, dy, px;
    uint8_t v[2], k, edge;
    for (y = 0; y < 16; y++) for (x = 0; x < 16; x += 2) {
        for (k = 0; k < 2; k++) {
            px = (int8_t)(x + k); edge = 0;
            if (at(px, y)) { v[k] = 1; continue; }
            for (dy = -1; dy <= 1; dy++) for (dx = -1; dx <= 1; dx++) if (at((int8_t)(px + dx), (int8_t)(y + dy))) edge = 1;
            v[k] = edge ? 2 : 0;
        }
        far_poke(d++, (uint8_t)((v[0] << 4) | v[1]));
    }
    pal(241, 255, 255, 255); pal(242, 0, 0, 0);
    far_poke(SPRTAB + 4, (uint8_t) SPRDATA); far_poke(SPRTAB + 5, (uint8_t)(SPRDATA >> 8)); far_poke(SPRTAB + 6, (uint8_t)(SPRDATA >> 16)); far_poke(SPRTAB + 7, 0);
    far_poke(SPRTAB + 8, 0x31);                              /* on, 4 bpp, over every layer */
    far_poke(SPRTAB + 9, 0x05);                              /* 16 x 16 */
    far_poke(SPRTAB + 10, 15);                               /* 15 << 4 | pixel: 241, 242 */
    w32(V_SPRTAB, SPRTAB); REG(V_SPRCTL) = 1;
}

/* ---- painting ----------------------------------------------------------- */
static int sx, sy, lx, ly;
static void shape(int x0, int y0, int x1, int y1)
{
    int l = x0 < x1 ? x0 : x1, t = y0 < y1 ? y0 : y1, w = iabs(x1 - x0) + 1, h = iabs(y1 - y0) + 1, k;
    uint8_t s = sizes[bsz];
    switch (tool) {
    case T_LINE: stroke(x0, y0, x1, y1, ink); break;
    case T_BOX:
        box_fill(l, t, w, s, ink); box_fill(l, t + h - s, w, s, ink);
        box_fill(l, t, s, h, ink); box_fill(l + w - s, t, s, h, ink);
        break;
    case T_BLK: box_fill(l, t, w, h, ink); break;
    case T_OVL: for (k = 0; k < s && k <= (w - 1) / 2 && k <= (h - 1) / 2; k++) oval(l + (w - 1) / 2, t + (h - 1) / 2, (w - 1) / 2 - k, (h - 1) / 2 - k, ink, 0); break;
    case T_DSK: oval(l + (w - 1) / 2, t + (h - 1) / 2, (w - 1) / 2, (h - 1) / 2, ink, 1); break;
    }
}
static void begin(int x, int y)
{
    if (tool == T_PICK) { ink = pixel(x, y); draw_toolbar(); return; }
    snap(UNDO); modified = 1;
    sx = lx = x; sy = ly = y;
    switch (tool) {
    case T_FILL: flood(x, y, ink); return;
    case T_PEN:  stamp(x, y, ink); break;
    case T_ERA:  stamp(x, y, paper); break;
    case T_SPR:  spray(x, y, ink); break;
    }
    drawing = 1;
}
static void drag(int x, int y)
{
    switch (tool) {
    case T_PEN: stroke(lx, ly, x, y, ink); break;
    case T_ERA: stroke(lx, ly, x, y, paper); break;
    case T_SPR: spray(x, y, ink); break;
    default:    if (x != lx || y != ly) { put_back(UNDO); shape(sx, sy, x, y); } break;
    }
    lx = x; ly = y;
}

static uint8_t leave(void)                   /* nonzero: go */
{
    uint8_t k, b, bwas;
    int x, y;
    if (!modified) return 1;
    msg("not saved -- Esc or QUIT again leaves");
    bwas = REG(MOUSEB);
    for (;;) {
        k = key_get();
        if (k) { draw_toolbar(); return k == 0x1B; }
        b = REG(MOUSEB);
        if ((b & 1) && !(bwas & 1)) {
            x = REG(MOUSEX) | (REG(MOUSEX + 1) << 8); y = REG(MOUSEY) | (REG(MOUSEY + 1) << 8);
            draw_toolbar();
            return y >= TBY + 17 && x >= bx[5] && x < bx[5] + bw[5];
        }
        bwas = b;
    }
}
static uint8_t command(uint8_t c)            /* the buttons and their keys; nonzero: leave */
{
    switch (c) {
    case 0: undo(); modified = 1; break;
    case 1: snap(UNDO); clear_canvas(); modified = 0; break;
    case 2: if (ask("open: ", fname, 40)) { snap(UNDO); load_pic(); modified = 0; draw_toolbar(); } break;
    case 3: if (ask("save as: ", fname, 40)) save_pic(); break;
    case 4: if (tb_on) tb_hide(); else tb_show(); break;
    case 5: return leave();
    }
    return 0;
}
static uint8_t click_toolbar(int x, int y)
{
    uint8_t i;
    if (y < TBY + 2) return 0;
    if (x < 258) {
        if (x >= 2) { i = (uint8_t)((x - 2) / 16 + (y - TBY - 2 >= 14 ? 16 : 0)); if (i < 32) { ink = i; draw_toolbar(); } }
        return 0;
    }
    if (x >= 300 && y < TBY + 15) { i = (uint8_t)((x - 300) / 34); if (i < 10) { tool = i; draw_toolbar(); } return 0; }
    if (y >= TBY + 17) {
        if (x >= 300 && x < 380) { i = (uint8_t)((x - 300) / 20); if (i < 4) { bsz = i; draw_toolbar(); } return 0; }
        for (i = 0; i < 6; i++) if (x >= bx[i] && x < bx[i] + bw[i]) return command(i);
    }
    return 0;
}
static uint8_t keypress(uint8_t k)
{
    uint8_t i;
    if (k >= 'a' && k <= 'z') k = (uint8_t)(k - 32);
    for (i = 0; i < 10; i++) if (k == (uint8_t) tkey[i]) { tool = i; draw_toolbar(); return 0; }
    if (k >= '1' && k <= '4') { bsz = (uint8_t)(k - '1'); draw_toolbar(); return 0; }
    switch (k) {
    case '[':  ink = (uint8_t)((ink + 31) & 31); draw_toolbar(); return 0;
    case ']':  ink = (uint8_t)((ink + 1) & 31); draw_toolbar(); return 0;
    case 0x1A: return command(0);
    case 0x0E: return command(1);
    case 0x0F: return command(2);
    case 0x13: return command(3);
    case 9:    return command(4);
    case 0x1B: return command(5);
    }
    return 0;
}

void main(void)
{
    static uint8_t palsave[39 * 3];
    const char *p;
    uint8_t i, n = 0, ctrl_was, l0_was, bg_was, lf, b, bwas = 0, press, k, q = 0, named = 0;
    int x, y;
    int8_t wh;

    rom_args();
    p = *(const char **) 0xF0;
    while (*p == ' ') p++;
    if (*p == '"') { q = 1; p++; }
    while (*p && n < 40 && (q ? *p != '"' : *p != ' ')) fname[n++] = *p++;
    fname[n] = 0;
    if (n) named = 1; else strcpy(fname, "PICTURE.PIC");

    for (i = 0; i < 39; i++) {                               /* the colours PAINT takes (0-31, 241-247), to give back */
        REG(V_PALIDX) = (uint8_t)(i < 32 ? i : 209 + i);
        palsave[i * 3] = REG(V_PALR); palsave[i * 3 + 1] = REG(V_PALG); palsave[i * 3 + 2] = REG(V_PALB);
    }
    for (i = 0; i < 32; i++) pal(i, db32[i * 3], db32[i * 3 + 1], db32[i * 3 + 2]);
    for (i = 0; i < 5; i++) pal((uint8_t)(243 + i), uicol[i * 3], uicol[i * 3 + 1], uicol[i * 3 + 2]);
    make_pointer();

    ctrl_was = REG(VICKY); l0_was = REG(VICKY + 0x10); bg_was = REG(V_BGCOL);
    REG(V_BGCOL) = 0;                                        /* a bitmap's colour 0 is see-through: the ground behind it must be colour 0 too */
    for (i = 0x21; i <= 0x25; i++) REG(VICKY + i) = 0;
    REG(VICKY + 0x26) = W & 255; REG(VICKY + 0x27) = W >> 8;
    REG(VICKY + 0x28) = 0; REG(VICKY + 0x29) = 0; REG(VICKY + 0x2A) = 0x20; REG(VICKY + 0x2B) = 0;   /* $200000 */
    REG(BLT + 0x11) = 0;                                      /* no flips */
    clear_canvas();
    REG(VICKY) = (uint8_t)(ctrl_was & 0xD9);                  /* 640x480, no doubling, out of the HD family, as BOOK does */
    REG(VICKY + 0x10) = 0;                                    /* the text layer off */
    REG(VICKY + 0x20) = 0x19;                                 /* layer 1: on, bitmap, 8 bpp */
    if (named) load_pic();
    draw_toolbar();
    if (named) msg(fname);
    snap(UNDO);

    lf = REG(SYS + 0x0D);
    for (;;) {
        while (REG(SYS + 0x0D) == lf) ;
        lf = REG(SYS + 0x0D);
        x = REG(MOUSEX) | (REG(MOUSEX + 1) << 8); y = REG(MOUSEY) | (REG(MOUSEY + 1) << 8);
        b = REG(MOUSEB); wh = (int8_t) REG(MOUSEW);
        far_poke(SPRTAB + 0, (uint8_t) x); far_poke(SPRTAB + 1, (uint8_t)(x >> 8));
        far_poke(SPRTAB + 2, (uint8_t) y); far_poke(SPRTAB + 3, (uint8_t)(y >> 8));
        if (wh) { ink = (uint8_t)((ink + (wh > 0 ? 31 : 1)) & 31); draw_toolbar(); }
        press = (uint8_t)(b & ~bwas); bwas = b;
        if (drawing) {
            if (b & 1) drag(x, y); else drawing = 0;
        } else if (press & 1) {
            if (tb_on && y >= TBY) { if (click_toolbar(x, y)) break; }
            else begin(x, y);
        } else if ((press & 2) && !(tb_on && y >= TBY)) { ink = pixel(x, y); draw_toolbar(); }
        k = key_get();
        if (k && !drawing && keypress(k)) break;
    }

    REG(V_SPRCTL) = 0;
    REG(VICKY + 0x20) = 0; REG(VICKY + 0x10) = l0_was; REG(VICKY) = ctrl_was; REG(V_BGCOL) = bg_was;
    for (i = 0; i < 39; i++) pal((uint8_t)(i < 32 ? i : 209 + i), palsave[i * 3], palsave[i * 3 + 1], palsave[i * 3 + 2]);
    rom_video();
    REG(TERM + 4) = 2;                                        /* a clear screen for the shell */
}
