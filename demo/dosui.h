/* demo/dosui.h -- MS-DOS EDIT's visual language, for the programs that share
 * it: EDIT, PROG and WORD (2026-10-02, Doc: "modify PROG so that its
 * interface is similar to this new EDIT ... a WORD version, using the same
 * visual language").  What is here is the furniture, not the text:
 *
 *   the colours   two schemes, by what a cell is: DOS EDIT's, or (-s) the
 *                 console's own -- palette entries, so the screen is cells
 *   the cells     a row built in rb and DMA'd out whole; single cells, boxes
 *                 with DOS's shadows
 *   the mouse     the pointer (sprite 0, off while the host draws its own:
 *                 $D110), and event(): a key or what the mouse just did
 *   the menus     the bar on row 0, drop-downs, Alt+letter, the mouse
 *   the dialogs   fields and buttons in a shadowed box; ask(); the Open
 *                 dialog with its directory list; a box of text
 *
 * The program defines draw() (the whole screen, used when a menu closes)
 * and sets ui_titles/ui_menus/ui_nmenu and ui_dirtab before using them.
 * #include "k4510.h" first, and "ed.h" before this when there is one (ed.h
 * has put(), cols, rows and full; a program without it gets them here). */

#ifndef K4510_ED_H
#define TERM   0xDA00u
#define NAMEMAX 64
static uint8_t cols, rows, full = 1, curshape;
static void put(char c) { REG(TERM) = (uint8_t)c; }
static void far_get(uint32_t p, void *d, unsigned n) { dma_copy(p, (uint32_t)(uint16_t)d, n); }
static void far_put(const void *s, uint32_t p, unsigned n) { dma_copy((uint32_t)(uint16_t)s, p, n); }
#endif

#define KSTAT   0xD101u                 /* bit0 shift, bit1 ctrl, bit2 alt; bit6 the byte read was a key code */
#define KUP     0x80
#define KDOWN   0x81
#define KLEFT   0x82
#define KRIGHT  0x83
#define KHOME   0x84
#define KEND    0x85
#define KPGUP   0x86
#define KPGDN   0x87
#define KINS    0x88
#define KDEL    0x89
#define KF(n)   (0x8F + (n))            /* F1 = $90 ... F12 = $9B; F12 is the machine's own */
#define KALT    0xC1                    /* Alt+A = $C1 ... Alt+Z = $DA (core/io.h KEY_ALT_A) */
#define MOUSEX  0xD108u
#define MOUSEY  0xD10Au
#define MOUSEB  0xD10Cu
#define MOUSEW  0xD10Du
#define MOUSEPTR 0xD110u                /* bit0: the host draws its own pointer, so we draw none; bit1 (written):
                                         * keep the host's pointer even while the mouse is captured */
#define SPRTAB  0x123000UL              /* the pointer: sprite 0, MOUSETEST's arrow */
#define SPRDATA 0x123100UL
#define KMOUSE  0xFF
#define C_NONE  0                       /* menu answers: nothing chosen */
#define C_SEP   0xFE                    /* a menu's separator line */

/* ---- the colours ----------------------------------------------------------
 * By what a cell is, not by colour, so the two schemes are two tables.  The
 * numbers are the machine's palette: the VIC-II sixteen it boots with, where
 * 6 is the blue, 15 the light grey and 3 the cyan of a DOS screen. */
enum { K_TEXT, K_SEL, K_FRAME, K_TITLE, K_MENU, K_MHOT, K_MSEL, K_MSELHOT, K_STATUS,
       K_SCROLL, K_DLG, K_DLGHOT, K_FIELD, K_BTN, K_BTNSEL, K_N };
static const uint8_t dos_f[K_N] = { 15,  6, 15,  6,  0,  1, 15,  1,  0,  0,  0,  1,  1,  0, 15 };
static const uint8_t dos_b[K_N] = {  6, 15,  6, 15, 15, 15,  0,  0,  3, 15, 15, 15,  0, 15,  0 };
static uint8_t kf[K_N], kb[K_N], sysc;
static void scheme(uint8_t sys)
{
    uint8_t i, d, b, hot;
    sysc = sys;
    if (!sys) { for (i = 0; i < K_N; i++) { kf[i] = dos_f[i]; kb[i] = dos_b[i]; } return; }
    d = REG(TERM + 0x14); b = REG(TERM + 0x15);        /* the console's own: JIM's defaults are the shell's colours */
    hot = (uint8_t)(d != 2 && b != 2 ? 2 : d != 0 && b != 0 ? 0 : 1);   /* the hot letters: red, else black, else white --
                                                                           * never one of the two colours they sit between */
    for (i = 0; i < K_N; i++) { kf[i] = b; kb[i] = d; }  /* the furniture: the console's colours, reversed */
    kf[K_TEXT] = d;  kb[K_TEXT] = b;
    kf[K_FRAME] = d; kb[K_FRAME] = b;
    kf[K_MSEL] = d;  kb[K_MSEL] = b;
    kf[K_BTNSEL] = d; kb[K_BTNSEL] = b;
    kf[K_FIELD] = d; kb[K_FIELD] = b;
    kf[K_MHOT] = hot; kf[K_DLGHOT] = hot;
    kf[K_MSELHOT] = (uint8_t)(b != 1 && d != 1 ? 1 : 0); kb[K_MSELHOT] = b;
}

/* ---- the cells -------------------------------------------------------------
 * Written as text32 cells straight into the console's map, because the
 * colours are palette entries and JIM's escapes reach only eight of them.
 * JIM keeps the cursor: it blinks it, shapes it, and stays out of the way. */
static uint8_t ox, oy, stride, kmod, kcode, running = 1;
static uint32_t scr;
static uint8_t rb[4 * 184];                           /* one row of cells, DMA'd out whole */
static uint32_t rowaddr(uint8_t y) { return scr + ((uint32_t)(uint8_t)(oy + y) * stride + ox) * 4; }
static void cel(uint8_t x, uint8_t ch, uint8_t k)
{
    uint8_t *p = rb + ((unsigned)x << 2);
    p[0] = ch; p[1] = 0; p[2] = kf[k]; p[3] = kb[k];
}
static void celc(uint8_t x, uint8_t ch, uint8_t f, uint8_t b)
{
    uint8_t *p = rb + ((unsigned)x << 2);
    p[0] = ch; p[1] = 0; p[2] = f; p[3] = b;
}
static void flush(uint8_t y, uint8_t n) { dma_copy((uint32_t)(uint16_t)rb, rowaddr(y), (unsigned)n << 2); }
static void pc(uint8_t x, uint8_t y, uint8_t ch, uint8_t f, uint8_t b)     /* one cell, straight to the screen */
{
    uint32_t a = rowaddr(y) + ((unsigned)x << 2);
    far_poke16(a, ch); far_poke16(a + 2, (uint16_t)f | ((uint16_t)b << 8));
}
static void pk(uint8_t x, uint8_t y, uint8_t ch, uint8_t k) { pc(x, y, ch, kf[k], kb[k]); }
static uint8_t slen(const char *s) { uint8_t n = 0; while (s[n]) n++; return n; }
static void pstr(uint8_t x, uint8_t y, const char *s, uint8_t k) { while (*s) pk(x++, y, (uint8_t)*s++, k); }
static void shade(uint8_t x, uint8_t y)               /* a shadow cell: what is there, dimmed on black, as DOS did */
{
    if (x >= cols || y >= rows) return;
    pc(x, y, far_peek(rowaddr(y) + ((unsigned)x << 2)), 12, 0);
}
static void box(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t k, const char *title)   /* a framed box and its shadow */
{
    uint8_t i, j;
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            uint8_t c = ' ';
            if (!j || j == h - 1) c = 0xC4;
            if (i == 0 || i == w - 1) c = 0xB3;
            if (!j && !i) c = 0xDA; else if (!j && i == w - 1) c = 0xBF;
            else if (j == h - 1 && !i) c = 0xC0; else if (j == h - 1 && i == w - 1) c = 0xD9;
            pk((uint8_t)(x + i), (uint8_t)(y + j), c, k);
        }
        shade((uint8_t)(x + w), (uint8_t)(y + j + 1)); shade((uint8_t)(x + w + 1), (uint8_t)(y + j + 1));
    }
    for (i = 2; i < w + 2; i++) shade((uint8_t)(x + i), (uint8_t)(y + h));
    if (title) { i = slen(title); pk((uint8_t)(x + (w - i) / 2 - 1), y, ' ', k); pstr((uint8_t)(x + (w - i) / 2), y, title, k); pk((uint8_t)(x + (w - i) / 2 + i), y, ' ', k); }
}
/* A window's top border with a title in it, at row y (the title reversed,
 * as DOS lit the window in front) -- built in rb and flushed. */
static void frame_top(uint8_t y, const char *t, uint8_t lit)
{
    uint8_t x, n = slen(t), a;
    cel(0, 0xDA, K_FRAME);
    for (x = 1; x < cols - 1; x++) cel(x, 0xC4, K_FRAME);
    cel((uint8_t)(cols - 1), 0xBF, K_FRAME);
    if (n > cols - 6) n = (uint8_t)(cols - 6);
    a = (uint8_t)((cols - n - 2) / 2);
    cel(a, ' ', lit ? K_TITLE : K_FRAME);
    for (x = 0; x < n; x++) cel((uint8_t)(a + 1 + x), (uint8_t)t[x], lit ? K_TITLE : K_FRAME);
    cel((uint8_t)(a + 1 + n), ' ', lit ? K_TITLE : K_FRAME);
    flush(y, cols);
}
/* A vertical scroll bar in column x, rows y0..y1: arrows at the ends, the
 * thumb at t of the track.  Answers the track's length. */
static uint8_t vtrack(uint8_t y0, uint8_t y1) { return (uint8_t)(y1 - y0 - 1); }
static void vbar_at(uint8_t x, uint8_t y0, uint8_t y1, uint8_t t)
{
    uint8_t y;
    pk(x, y0, 0x1E, K_SCROLL);
    for (y = (uint8_t)(y0 + 1); y < y1; y++) pk(x, y, (uint8_t)(y - y0 - 1 == t ? 0xDB : 0xB0), K_SCROLL);
    pk(x, y1, 0x1F, K_SCROLL);
}
/* the thumb's place for position p of n, on a track of len cells */
static uint8_t thumb(unsigned long p, unsigned long n, uint8_t len)
{
    if (len < 2 || n < 2) return 0;
    if (p >= n) p = n - 1;
    return (uint8_t)(p * (len - 1) / (n - 1));
}
static void status_line(const char *s, const char *right)   /* the last row: s, and right at its right end */
{
    uint8_t x, n = slen(right);
    for (x = 0; x < cols; x++) cel(x, ' ', K_STATUS);
    for (x = 0; s[x] && x < cols - n - 3; x++) cel((uint8_t)(x + 1), (uint8_t)s[x], K_STATUS);
    for (x = 0; x < n; x++) cel((uint8_t)(cols - 1 - n + x), (uint8_t)right[x], K_STATUS);
    flush((uint8_t)(rows - 1), cols);
}
static void ui_init(void)                             /* the console's window, as JIM has it */
{
    cols = REG(TERM + 5); rows = REG(TERM + 6); ox = REG(TERM + 7); oy = REG(TERM + 8); stride = REG(TERM + 0x0D);
    if (!cols) cols = 80;
    if (!rows) rows = 30;
    if (!stride) stride = cols;
    if (cols > 184) cols = 184;
    scr = (uint32_t)REG(TERM + 0x10) | ((uint32_t)REG(TERM + 0x11) << 8) | ((uint32_t)REG(TERM + 0x12) << 16) | ((uint32_t)REG(TERM + 0x13) << 24);
}
static void cursor_shape(uint8_t want)                /* DECSCUSR: '2' a block, '4' an underline */
{
    if (want != curshape) { curshape = want; put(27); put('['); put((char)want); put(' '); put('q'); }
}
static void cursor_at(uint8_t x, uint8_t y) { REG(TERM + 9) = x; REG(TERM + 10) = y; }   /* writing them redraws JIM's cursor */
static void cursor_show(uint8_t on) { if (on) REG(TERM + 0x0E) |= 1; else REG(TERM + 0x0E) &= (uint8_t)~1; }

/* ---- keys and the mouse ----------------------------------------------------
 * The machine draws no pointer; the host may (F12's "Mouse pointer"), and
 * says so at $D110.  These programs ask it to keep its pointer up even when
 * a click has captured the mouse (a game wants it gone then; an editor does
 * not), so the pointer is the one the rest of the machine shows and does not
 * change when the mouse is captured.  With the setting off there is no host
 * pointer, and this draws one: sprite 0, the same arrow.  event() waits for
 * a key or for the mouse to do something, a frame at a time. */
static uint8_t mev, mrow, mcol, mheld, dragging, chh = 8;   /* mev: 1 press, 2 drag, 3 release, 4 wheel */
static int8_t mwheel;
static const uint8_t arrowspr[128] = {   /* 16x16, 4 bpp: the arrow (1) with a black edge (2) round it */
    0x12, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x11, 0x12, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x11, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x11, 0x11, 0x12, 0x20, 0x00, 0x00, 0x00, 0x00, 0x11, 0x11, 0x11, 0x22, 0x00, 0x00, 0x00, 0x00,
    0x11, 0x11, 0x11, 0x12, 0x00, 0x00, 0x00, 0x00, 0x11, 0x11, 0x22, 0x22, 0x00, 0x00, 0x00, 0x00,
    0x11, 0x21, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x21, 0x12, 0x20, 0x00, 0x00, 0x00, 0x00,
    0x22, 0x22, 0x11, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x11, 0x20, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x02, 0x22, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static void ptr_on(void)
{
    uint8_t i;
    for (i = 0; i < 128; i++) far_poke(SPRDATA + i, arrowspr[i]);
    pal(17, 255, 255, 255); pal(18, 0, 0, 0);
    far_poke(SPRTAB + 4, (uint8_t)SPRDATA); far_poke(SPRTAB + 5, (uint8_t)(SPRDATA >> 8)); far_poke(SPRTAB + 6, (uint8_t)(SPRDATA >> 16)); far_poke(SPRTAB + 7, 0);
    far_poke(SPRTAB + 8, 0x31); far_poke(SPRTAB + 9, 0x05); far_poke(SPRTAB + 10, 1);
    REG(MOUSEPTR) = 2;                                 /* the host's pointer, captured or not */
    w32(V_SPRTAB, SPRTAB); REG(V_SPRCTL) = (uint8_t)!(REG(MOUSEPTR) & 1);
    chh = (uint8_t)((REG(0xD010) & 0x60) ? 16 : 8);
}
static void ptr_off(void) { REG(V_SPRCTL) = 0; REG(MOUSEPTR) = 0; }
static uint8_t event(void)
{
    uint8_t k, b, r, c; unsigned x, y; int8_t w;
    for (;;) {
        k = rom_getin();
        if (k) { kmod = REG(KSTAT); kcode = (uint8_t)((kmod & 0x40) ? 1 : 0); return k; }
        wait_vblank();
        x = REG(MOUSEX) | ((unsigned)REG(MOUSEX + 1) << 8); y = REG(MOUSEY) | ((unsigned)REG(MOUSEY + 1) << 8);
        far_poke(SPRTAB + 0, (uint8_t)x); far_poke(SPRTAB + 1, (uint8_t)(x >> 8));
        far_poke(SPRTAB + 2, (uint8_t)y); far_poke(SPRTAB + 3, (uint8_t)(y >> 8));
        REG(V_SPRCTL) = (uint8_t)!(REG(MOUSEPTR) & 1);     /* our arrow only when the host shows none of its own */
        b = (uint8_t)(REG(MOUSEB) & 1); w = (int8_t)REG(MOUSEW);
        { int ty = (int) y + (int16_t)(REG(0xD014) | (REG(0xD015) << 8));   /* the HD modes scroll the console down */
          ty = ty < 0 ? 0 : ty / chh;
          r = (uint8_t)(ty < oy ? 0xFF : ty - oy); }                      /* a row of OUR window; $FF above it (a band) */
        c = (uint8_t)(x >> 3);
        kmod = REG(KSTAT); kcode = 2;
        if (w) { mwheel = w; mev = 4; return KMOUSE; }
        if (b && !mheld) { mheld = 1; mrow = r; mcol = c; mev = 1; return KMOUSE; }
        if (b && (r != mrow || c != mcol)) { mrow = r; mcol = c; mev = 2; return KMOUSE; }
        if (!b && mheld) { mheld = 0; mrow = r; mcol = c; mev = 3; return KMOUSE; }
    }
}
static uint8_t lower(uint8_t k) { return (uint8_t)((k >= 'A' && k <= 'Z') ? k + 32 : k); }

/* ---- the menus -------------------------------------------------------------
 * F10 opens the first; Alt and a title's letter opens that one.  Up and Down
 * choose, Left and Right go along the bar, Enter or an entry's letter runs
 * it, Esc closes.  The mouse opens a title and runs an entry.  An entry with
 * a mark shows a check when ui_marked(cmd) says so (the colour scheme). */
struct item { const char *label; uint8_t hot; uint8_t cmd; const char *keys; };
static const char *const *ui_titles;
static const struct item *const *ui_menus;
static uint8_t ui_nmenu;
static uint8_t (*ui_marked)(uint8_t cmd);
static const char *ui_name = "";                      /* the program's name, at the bar's right end */
static void draw(void);                               /* the program's: the whole screen */
static uint8_t mx(uint8_t m) { uint8_t x = 1, i; for (i = 0; i < m; i++) x = (uint8_t)(x + slen(ui_titles[i]) + 2); return x; }
static void menubar(int8_t sel)
{
    uint8_t i, x = 0, j, k, n = slen(ui_name);
    for (; x < cols; x++) cel(x, ' ', K_MENU);
    for (i = 0; i < ui_nmenu; i++) {
        x = mx(i);
        k = (int8_t)i == sel ? K_MSEL : K_MENU;
        cel(x, ' ', k);
        for (j = 0; ui_titles[i][j]; j++) cel((uint8_t)(x + 1 + j), (uint8_t)ui_titles[i][j], (uint8_t)(j ? k : (k == K_MSEL ? K_MSELHOT : K_MHOT)));
        cel((uint8_t)(x + 1 + j), ' ', k);
    }
    for (i = 0; i < n; i++) cel((uint8_t)(cols - 1 - n + i), (uint8_t)ui_name[i], K_MENU);
    flush(0, cols);
}
static uint8_t mwidth(const struct item *it, uint8_t *n)
{
    uint8_t w = 0, r;
    for (*n = 0; it[*n].label; (*n)++) { r = (uint8_t)(slen(it[*n].label) + slen(it[*n].keys) + 7); if (r > w) w = r; }
    return w;
}
static uint8_t menu_x(uint8_t m, uint8_t *w, uint8_t *n)
{
    uint8_t x = (uint8_t)(mx(m) - 1);
    *w = mwidth(ui_menus[m], n);
    if (x + *w + 2 > cols) x = (uint8_t)(cols - *w - 2);
    return x;
}
/* A menu open is drawn row by row over a copy of the screen taken when it
 * opened (far memory just past ui_dirtab's list): each row is put together
 * in rb -- what was there, the box, the items, the shadow -- and goes out
 * in one DMA.  Nothing is erased first, so nothing flickers; closing puts
 * the copy back.  mn_bot is the lowest row an open menu has covered. */
static uint32_t ui_dirtab;
static uint8_t mn_bot;
static uint32_t snap_at(uint8_t y) { return ui_dirtab + 0x8000UL + (uint32_t)y * cols * 4; }
static void snap_take(void) { uint8_t y; for (y = 0; y < rows; y++) dma_copy(rowaddr(y), snap_at(y), (unsigned)cols << 2); }
static void snap_row(uint8_t y) { dma_copy(snap_at(y), (uint32_t)(uint16_t)rb, (unsigned)cols << 2); }
static void shd(uint8_t x) { if (x < cols) { uint8_t *p = rb + ((unsigned)x << 2); p[2] = 12; p[3] = 0; } }   /* a shadow cell, as shade() */
static void gap(uint8_t x)                            /* a window frame's line under row 1, cut beside a menu as round a title */
{
    uint8_t *p = rb + ((unsigned)x << 2);
    if (x < cols && p[0] == 0xC4) p[0] = ' ';
}
static void menu_draw(uint8_t m, uint8_t sel)
{
    const struct item *it = ui_menus[m];
    uint8_t n, w, x = menu_x(m, &w, &n), y, r, j, k, c, bot, e;
    bot = (uint8_t)(n + 3); e = bot > mn_bot ? bot : mn_bot;
    menubar((int8_t)m);
    for (y = 1; y <= e && y < rows; y++) {
        snap_row(y);
        if (y == 1 || y == n + 2) {
            cel(x, y == 1 ? 0xDA : 0xC0, K_MENU);
            for (j = 1; j < w - 1; j++) cel((uint8_t)(x + j), 0xC4, K_MENU);
            cel((uint8_t)(x + w - 1), y == 1 ? 0xBF : 0xD9, K_MENU);
            if (y == 1) { gap((uint8_t)(x - 1)); gap((uint8_t)(x + w)); }   /* the frame's line stops short of the corners */
        } else if (y < n + 2) {
            r = (uint8_t)(y - 2);
            if (it[r].cmd == C_SEP) {
                cel(x, 0xC3, K_MENU); for (j = 1; j < w - 1; j++) cel((uint8_t)(x + j), 0xC4, K_MENU); cel((uint8_t)(x + w - 1), 0xB4, K_MENU);
            } else {
                k = r == sel ? K_MSEL : K_MENU;
                cel(x, 0xB3, K_MENU); cel((uint8_t)(x + w - 1), 0xB3, K_MENU);
                for (j = 1; j < w - 1; j++) cel((uint8_t)(x + j), ' ', k);
                if (ui_marked && ui_marked(it[r].cmd)) cel((uint8_t)(x + 1), 0xFB, k);   /* a check */
                for (j = 0; it[r].label[j]; j++) cel((uint8_t)(x + 2 + j), (uint8_t)it[r].label[j], (uint8_t)(j == it[r].hot ? (k == K_MSEL ? K_MSELHOT : K_MHOT) : k));
                for (j = 0, c = (uint8_t)(x + w - 2 - slen(it[r].keys)); it[r].keys[j]; j++) cel((uint8_t)(c + j), (uint8_t)it[r].keys[j], k);
            }
        }
        if (y >= 2 && y < bot) { shd((uint8_t)(x + w)); shd((uint8_t)(x + w + 1)); }
        else if (y == bot) for (j = 2; j < w + 2; j++) shd((uint8_t)(x + j));
        flush(y, cols);
    }
    mn_bot = bot;
}
static uint8_t title_at(uint8_t c)
{
    uint8_t i, x;
    for (i = 0; i < ui_nmenu; i++) { x = mx(i); if (c >= x && c < (uint8_t)(x + slen(ui_titles[i]) + 2)) return i; }
    return ui_nmenu;
}
static uint8_t title_of(uint8_t k)                    /* the menu an Alt+letter opens, or ui_nmenu */
{
    uint8_t i;
    for (i = 0; i < ui_nmenu; i++) if (lower((uint8_t)ui_titles[i][0]) == 'a' + (k - KALT)) return i;
    return ui_nmenu;
}
static uint8_t mclose(uint8_t c)                      /* the menu away before its command runs: a dialog must not open over it */
{
    uint8_t y;
    for (y = 0; y <= mn_bot && y < rows; y++) { snap_row(y); flush(y, cols); }
    full = 1; cursor_show(1);
    return c;
}
/* Drawn again only when the menu or the entry changes.  The mouse: a press
 * or a drag lights an entry, the release runs it (DOS's way, so the press
 * that opened a menu from the bar does not also choose in it); a press off
 * the menu closes it. */
static uint8_t menu(uint8_t m)                        /* menu m open; the command chosen, or C_NONE */
{
    const struct item *it;
    uint8_t sel = 0, n, k, w, x, i, dm = 0xFF, ds = 0xFF;
    cursor_show(0); snap_take(); mn_bot = 0;
    for (;;) {
        it = ui_menus[m];
        x = menu_x(m, &w, &n);
        while (it[sel].cmd == C_SEP) sel++;
        if (m != dm || sel != ds) { menu_draw(m, sel); dm = m; ds = sel; }
        k = event();
        if (kcode == 2) {
            if (mev == 4) continue;
            if (mrow == 0) { if (mev == 3) continue; i = title_at(mcol); if (i < ui_nmenu && i != m) { m = i; sel = 0; } else if (mev == 1) break; continue; }
            if (mrow >= 2 && mrow < 2 + n && mcol > x && mcol < x + w - 1) {
                i = (uint8_t)(mrow - 2);
                if (it[i].cmd == C_SEP) continue;
                sel = i;
                if (mev == 3) return mclose(it[sel].cmd);
                continue;
            }
            if (mev == 1) break;
            continue;
        }
        if (k == 0x1B || (k == KF(10) && kcode)) break;
        if (k == 0x0D) return mclose(it[sel].cmd);
        if (kcode && k >= KALT && k < KALT + 26) { i = title_of(k); if (i < ui_nmenu) { m = i; sel = 0; } continue; }
        if (!kcode) {                                 /* an entry's letter runs it */
            for (i = 0; i < n; i++)
                if (it[i].cmd != C_SEP && lower((uint8_t)it[i].label[it[i].hot]) == lower(k)) return mclose(it[i].cmd);
            continue;
        }
        if (k == KUP) { do sel = sel ? (uint8_t)(sel - 1) : (uint8_t)(n - 1); while (it[sel].cmd == C_SEP); }
        else if (k == KDOWN) { do sel = (uint8_t)(sel + 1 < n ? sel + 1 : 0); while (it[sel].cmd == C_SEP); }
        else if (k == KLEFT || k == KRIGHT) { m = (uint8_t)(k == KLEFT ? (m ? m - 1 : ui_nmenu - 1) : (m + 1 < ui_nmenu ? m + 1 : 0)); sel = 0; }
    }
    return mclose(C_NONE);
}

/* ---- dialogs ---------------------------------------------------------------
 * A box in the middle of the screen, its controls in a row: fields (a line
 * of text each) and buttons.  Tab and Shift-Tab go round them, Enter is the
 * first button, Esc the last; the mouse clicks any of them.  The answer is
 * the button's number, or 0xFF for Esc. */
#define DMAXF 2
#define DMAXB 3
static const char *dl_lab[DMAXF]; static char *dl_buf[DMAXF]; static uint8_t dl_max[DMAXF];
static const char *dl_btn[DMAXB];
static uint8_t dl_nf, dl_nb, dl_x, dl_y, dl_w, dl_h, dl_fx, dl_fw, dl_by, dl_bx[DMAXB], dl_foc;
static const char *dl_title, *dl_text;
static void dl_field(uint8_t i)
{
    uint8_t x, n = slen(dl_buf[i]), y = (uint8_t)(dl_y + 2 + i * 2), off = n >= dl_fw ? (uint8_t)(n - dl_fw + 1) : 0;
    for (x = 0; x < dl_fw; x++) pk((uint8_t)(dl_fx + x), y, (uint8_t)(off + x < n ? dl_buf[i][off + x] : ' '), K_FIELD);
}
static void dl_button(uint8_t i)
{
    uint8_t k = dl_foc == dl_nf + i ? K_BTNSEL : K_BTN, x = dl_bx[i];
    pk(x, dl_by, '<', k); pk((uint8_t)(x + 1), dl_by, ' ', k);
    pstr((uint8_t)(x + 2), dl_by, dl_btn[i], k);
    pk((uint8_t)(x + 2 + slen(dl_btn[i])), dl_by, ' ', k); pk((uint8_t)(x + 3 + slen(dl_btn[i])), dl_by, '>', k);
}
static void dl_place(void)                            /* the box, sized to what is in it, centred */
{
    uint8_t i, w = 0, bw = 0;
    for (i = 0; i < dl_nb; i++) bw = (uint8_t)(bw + slen(dl_btn[i]) + 6);
    dl_w = (uint8_t)(cols > 64 ? 60 : cols - 4);
    if (dl_text) { w = (uint8_t)(slen(dl_text) + 4); if (w > dl_w) dl_w = w > cols - 4 ? (uint8_t)(cols - 4) : w; }
    if (bw + 4 > dl_w) dl_w = (uint8_t)(bw + 4);
    dl_h = (uint8_t)(4 + dl_nf * 2 + (dl_text ? 2 : 0));
    dl_x = (uint8_t)((cols - dl_w) / 2); dl_y = (uint8_t)((rows - dl_h) / 2);
    dl_fx = 0;
    for (i = 0; i < dl_nf; i++) if (slen(dl_lab[i]) + 3 > dl_fx) dl_fx = (uint8_t)(slen(dl_lab[i]) + 3);
    dl_fw = (uint8_t)(dl_w - dl_fx - 2); dl_fx = (uint8_t)(dl_x + dl_fx);
    dl_by = (uint8_t)(dl_y + dl_h - 2);
    w = (uint8_t)(dl_x + (dl_w - bw) / 2 + 1);
    for (i = 0; i < dl_nb; i++) { dl_bx[i] = w; w = (uint8_t)(w + slen(dl_btn[i]) + 6); }
}
static void dl_draw(void)
{
    uint8_t i;
    box(dl_x, dl_y, dl_w, dl_h, K_DLG, dl_title);
    if (dl_text) pstr((uint8_t)(dl_x + 2), (uint8_t)(dl_y + 2), dl_text, K_DLG);
    for (i = 0; i < dl_nf; i++) { pstr((uint8_t)(dl_x + 2), (uint8_t)(dl_y + 2 + i * 2), dl_lab[i], K_DLG); dl_field(i); }
    for (i = 0; i < dl_nb; i++) dl_button(i);
}
static uint8_t dialog(void)
{
    uint8_t k, i, n, done = 0xFE;
    dl_place(); dl_draw();
    dl_foc = 0;
    while (done == 0xFE) {
        for (i = 0; i < dl_nb; i++) dl_button(i);
        if (dl_foc < dl_nf) {
            dl_field(dl_foc);
            n = slen(dl_buf[dl_foc]);
            cursor_at((uint8_t)(dl_fx + (n >= dl_fw ? dl_fw - 1 : n)), (uint8_t)(dl_y + 2 + dl_foc * 2));
            cursor_show(1);
        } else cursor_show(0);
        k = event();
        if (kcode == 2) {
            if (mev != 1) continue;
            for (i = 0; i < dl_nb; i++) if (mrow == dl_by && mcol >= dl_bx[i] && mcol < dl_bx[i] + slen(dl_btn[i]) + 4) done = i;
            for (i = 0; i < dl_nf; i++) if (mrow == dl_y + 2 + i * 2 && mcol >= dl_fx && mcol < dl_fx + dl_fw) dl_foc = i;
            continue;
        }
        if (k == 0x1B) { done = 0xFF; break; }
        if (k == 0x09) { dl_foc = (uint8_t)((kmod & 1) ? (dl_foc ? dl_foc - 1 : dl_nf + dl_nb - 1) : (dl_foc + 1) % (dl_nf + dl_nb)); continue; }
        if (k == 0x0D) { done = dl_foc >= dl_nf ? (uint8_t)(dl_foc - dl_nf) : 0; break; }
        if (dl_foc >= dl_nf) {                        /* on a button: the arrows go along the row */
            if (kcode && (k == KLEFT || k == KUP)) dl_foc = dl_foc > dl_nf ? (uint8_t)(dl_foc - 1) : (uint8_t)(dl_nf + dl_nb - 1);
            else if (kcode && (k == KRIGHT || k == KDOWN)) dl_foc = dl_foc + 1 < dl_nf + dl_nb ? (uint8_t)(dl_foc + 1) : dl_nf;
            continue;
        }
        n = slen(dl_buf[dl_foc]);
        if (k == 0x08 && !kcode) { if (n) dl_buf[dl_foc][n - 1] = 0; }
        else if (kcode && (k == KDOWN || k == KUP)) dl_foc = (uint8_t)(k == KDOWN ? (dl_foc + 1) % (dl_nf + dl_nb) : (dl_foc ? dl_foc - 1 : dl_nf + dl_nb - 1));
        else if (!kcode && k >= 0x20 && n < dl_max[dl_foc] - 1) { dl_buf[dl_foc][n] = (char)k; dl_buf[dl_foc][n + 1] = 0; }
    }
    cursor_show(1);
    full = 1;
    return done;
}
static uint8_t ask(const char *title, const char *text, const char *b0, const char *b1, const char *b2)
{
    dl_title = title; dl_text = text; dl_nf = 0;
    dl_btn[0] = b0; dl_btn[1] = b1; dl_btn[2] = b2;
    dl_nb = (uint8_t)(b2 ? 3 : b1 ? 2 : 1);
    return dialog();
}
static uint8_t form1(const char *title, const char *lab, char *buf, uint8_t max, const char *ok)
{
    dl_title = title; dl_text = 0; dl_nf = 1; dl_lab[0] = lab; dl_buf[0] = buf; dl_max[0] = max;
    dl_btn[0] = ok; dl_btn[1] = "Cancel"; dl_nb = 2;
    return (uint8_t)(dialog() == 0);
}
/* A box of lines (help, about): any key or a click closes it. */
static void text_box(const char *title, const char *const *lines)
{
    uint8_t r, w = 0, h, x, y, i;
    for (r = 0; lines[r]; r++) if (slen(lines[r]) > w) w = slen(lines[r]);
    w = (uint8_t)(w + 4); h = (uint8_t)(r + 4);
    if (w > cols - 2) w = (uint8_t)(cols - 2);
    if (h > rows - 1) h = (uint8_t)(rows - 1);
    x = (uint8_t)((cols - w) / 2); y = (uint8_t)((rows - h) / 2);
    box(x, y, w, h, K_DLG, title);
    for (r = 0; lines[r] && r < h - 4; r++) for (i = 0; lines[r][i] && i < w - 4; i++) pk((uint8_t)(x + 2 + i), (uint8_t)(y + 2 + r), (uint8_t)lines[r][i], K_DLG);
    cursor_show(0);
    while (event() == KMOUSE && mev != 1) ;
    cursor_show(1);
    full = 1;
}

/* The Open dialog: a name, and the directory to pick one from.  The list is
 * the current directory's, read through the file device into far memory at
 * ui_dirtab (32 bytes an entry, [0] 1 for a directory); choosing a
 * directory goes into it (the shell's current directory moves, as it did in
 * DOS), and choosing a file answers 1 with its name in out. */
#define DIRMAX 1000u
static unsigned dn, dsel, dtop;
static char cwd[NAMEMAX];
static uint8_t dent[32];
static uint8_t fs_do(uint8_t c) { REG(0xD300) = c; return REG(0xD301); }
static void dir_read(void)
{
    REG(0xD318) = NAMEMAX;                            /* GETCWD's room */
    w32(0xD308, (uint32_t)(uint16_t)cwd); fs_do(15);
    dn = 0; dsel = 0; dtop = 0;
    if (!(cwd[0] == '/' && !cwd[1])) { dent[0] = 1; dent[1] = '.'; dent[2] = '.'; dent[3] = 0; far_put(dent, ui_dirtab, 32); dn = 1; }
    w32(0xD308, (uint32_t)(uint16_t)(dent + 1));
    if (fs_do(6)) return;
    while (dn < DIRMAX) {
        w32(0xD308, (uint32_t)(uint16_t)(dent + 1));
        if (fs_do(7)) break;
        dent[31] = 0;
        dent[0] = (uint8_t)(REG(0xD310) == 0xFF && REG(0xD313) == 0xFF);   /* size $FFFFFFFF: a directory */
        far_put(dent, ui_dirtab + ((uint32_t)dn << 5), 32);
        dn++;
    }
}
static void dir_draw(uint8_t lx, uint8_t ly, uint8_t lw, uint8_t lh, uint8_t foc)
{
    uint8_t r, i, k; unsigned e;
    if (dsel < dtop) dtop = dsel;
    if (dsel >= dtop + lh) dtop = dsel - lh + 1;
    for (r = 0; r < lh; r++) {
        e = dtop + r;
        k = (uint8_t)(e == dsel && e < dn ? (foc ? K_MSEL : K_SEL) : K_FIELD);
        for (i = 0; i < lw; i++) pk((uint8_t)(lx + i), (uint8_t)(ly + r), ' ', k);
        if (e >= dn) continue;
        far_get(ui_dirtab + ((uint32_t)e << 5), dent, 32);
        for (i = 0; dent[1 + i] && i < lw - 3; i++) pk((uint8_t)(lx + 1 + i), (uint8_t)(ly + r), dent[1 + i], k);
        if (dent[0]) pk((uint8_t)(lx + 1 + i), (uint8_t)(ly + r), '/', k);
    }
}
static uint8_t open_dialog(char *out)
{
    uint8_t lx, ly, lw, lh, k, foc = 0, i, go_ = 0, n, y, ok = 0;
    out[0] = 0;
    dir_read();
    dl_title = "Open"; dl_text = 0; dl_nf = 1; dl_lab[0] = "File Name:"; dl_buf[0] = out; dl_max[0] = NAMEMAX;
    dl_btn[0] = "OK"; dl_btn[1] = "Cancel"; dl_nb = 2;
    dl_place();
    dl_h = (uint8_t)(rows - 6 < 20 ? rows - 6 : 20); dl_y = (uint8_t)((rows - dl_h) / 2);
    dl_by = (uint8_t)(dl_y + dl_h - 2);
    lx = (uint8_t)(dl_x + 2); ly = (uint8_t)(dl_y + 6); lw = (uint8_t)(dl_w - 4); lh = (uint8_t)(dl_h - 9);
    dl_foc = 0;
    dl_draw();
    for (;;) {
        for (i = 0; i < lw; i++) pk((uint8_t)(lx + i), (uint8_t)(dl_y + 4), ' ', K_DLG);
        for (i = 0; cwd[i] && i < lw; i++) pk((uint8_t)(lx + i), (uint8_t)(dl_y + 4), (uint8_t)cwd[i], K_DLG);
        dl_field(0); dl_button(0); dl_button(1);
        dir_draw(lx, ly, lw, lh, (uint8_t)(foc == 1));
        n = slen(out);
        if (!foc) { cursor_at((uint8_t)(dl_fx + (n >= dl_fw ? dl_fw - 1 : n)), (uint8_t)(dl_y + 2)); cursor_show(1); }
        else cursor_show(0);
        k = event();
        if (kcode == 2) {
            if (mev == 4) { if (mwheel > 0) dsel = dsel > 3 ? dsel - 3 : 0; else { dsel += 3; if (dsel >= dn) dsel = dn ? dn - 1 : 0; } continue; }
            if (mev != 1) continue;
            if (mrow == dl_by && mcol >= dl_bx[0] && mcol < dl_bx[0] + 6) go_ = 1;
            else if (mrow == dl_by && mcol >= dl_bx[1] && mcol < dl_bx[1] + 10) break;
            else if (mrow == dl_y + 2 && mcol >= dl_fx) foc = 0;
            else if (mrow >= ly && mrow < ly + lh && mcol >= lx && mcol < lx + lw && dtop + (mrow - ly) < dn) {
                y = (uint8_t)(mrow - ly);
                if (foc == 1 && dsel == dtop + y) go_ = 2; else { dsel = dtop + y; foc = 1; }   /* a second click on it: open */
            }
        } else if (k == 0x1B) break;
        else if (k == 0x09) foc = (uint8_t)!foc;
        else if (k == 0x0D) go_ = foc ? 2 : 1;
        else if (foc && kcode) {
            if (k == KUP && dsel) dsel--;
            else if (k == KDOWN && dsel + 1 < dn) dsel++;
            else if (k == KPGUP) dsel = dsel > lh ? dsel - lh : 0;
            else if (k == KPGDN) { dsel += lh; if (dsel >= dn) dsel = dn ? dn - 1 : 0; }
            else if (k == KHOME) dsel = 0;
            else if (k == KEND) dsel = dn ? dn - 1 : 0;
        } else if (!foc && !kcode) {
            if (k == 0x08) { if (n) out[n - 1] = 0; }
            else if (k >= 0x20 && n < NAMEMAX - 1) { out[n] = (char)k; out[n + 1] = 0; }
        }
        if (go_ == 2 && dn) {                         /* the list's entry: a directory is gone into, a file chosen */
            far_get(ui_dirtab + ((uint32_t)dsel << 5), dent, 32);
            if (dent[0]) { w32(0xD304, (uint32_t)(uint16_t)(dent + 1)); fs_do(11); dir_read(); go_ = 0; dl_draw(); continue; }
            for (i = 0; dent[1 + i] && i < NAMEMAX - 1; i++) out[i] = (char)dent[1 + i];
            out[i] = 0; go_ = 1;
        }
        if (go_ == 1 && out[0]) { ok = 1; break; }
        go_ = 0;
    }
    cursor_show(1);
    full = 1;
    return ok;
}
