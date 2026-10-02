/* K4510: EDIT [-s] [name] -- the editor, in the manner of MS-DOS 5's EDIT.
 *
 * Doc, 2026-10-02: "a WYSIWYG text editor similar to the one on later
 * releases of MS-DOS with keyboard shortcuts and mouse controls and screen
 * borders", replacing the small EDIT, "use DOS EDIT colours, but offer a
 * command line option to use system colors".  So: a menu bar (F10, Alt and
 * the letter, or the mouse), the text in a framed window with its name in
 * the top border, scroll bars that take the mouse, dialog boxes with
 * shadows, and the status line along the bottom -- in EDIT's blue, grey and
 * cyan, or with -s in the console's own colours.
 *
 * The text engine is VI's and PROG's (demo/ed.h), so the three cannot
 * drift: lines of up to 255 characters in far memory (16384 of them), undo
 * as far back as the session goes, the register, search, renumber.  What is
 * EDIT's is the screen and the keys.  The screen is written as text32 cells
 * straight into the console's map, a row at a time by DMA, because the
 * colours are palette entries and JIM's escapes reach only eight of them;
 * JIM keeps the cursor (it blinks it, shapes it, and stays out of the way).
 *
 *   row 0            the menu bar           File Edit Search Options Help
 *   row 1            the window's top: its corners, the file's name
 *   rows 2..rows-3   the text, framed left, the vertical scroll bar right
 *   row rows-2       the window's foot: the horizontal scroll bar
 *   row rows-1       the status line: what just happened, line:column
 *
 * Far memory: $08000000 its lines, $08600000 its undo, $08C00000 the Open
 * dialog's directory.  $08000000-$0BFFFFFF was used by nothing (PROG has
 * $04-$07, BOOK $0C, SPLIT $0D, VI $0E-$0F). */
#include "k4510.h"
#include "ed.h"

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
#define SPRTAB  0x123000UL              /* the pointer: sprite 0, PROG's and MOUSETEST's arrow */
#define SPRDATA 0x123100UL
#define KMOUSE  0xFF
#define DIRTAB  0x08C00000UL            /* the Open dialog: 32 bytes an entry, [0] 1 for a directory */
#define DIRMAX  1000u

enum { C_NONE, C_NEW, C_OPEN, C_SAVE, C_SAVEAS, C_EXIT, C_UNDO, C_REDO, C_CUT, C_COPY, C_PASTE,
       C_CLEAR, C_SELALL, C_RENUM, C_FIND, C_NEXT, C_CHANGE, C_GOTO, C_DOS, C_SYS, C_HELP, C_ABOUT, C_SEP };

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

/* ---- the cells ---------------------------------------------------------- */
static uint8_t ox, oy, stride, th, tw, hoff, lasthoff = 0xFF, over, kmod, kcode, wantx, running = 1;
static uint32_t scr;
static unsigned lasttop = 0xFFFF, lastcy = 0xFFFF, tgline = 0xFFFFu;
static uint8_t rb[4 * 184];                           /* one row of cells, DMA'd out whole */
static uint32_t rowaddr(uint8_t y) { return scr + ((uint32_t)(uint8_t)(oy + y) * stride + ox) * 4; }
static void cel(uint8_t x, uint8_t ch, uint8_t k)
{
    uint8_t *p = rb + ((unsigned)x << 2);
    p[0] = ch; p[1] = 0; p[2] = kf[k]; p[3] = kb[k];
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

/* ---- keys and the mouse --------------------------------------------------
 * The machine draws no pointer, so EDIT draws PROG's: sprite 0.  event()
 * waits for a key or for the mouse to do something, a frame at a time. */
static uint8_t mev, mrow, mcol, mheld, dragging, chh = 8;   /* mev: 1 press, 2 drag, 3 release, 4 wheel */
static int8_t mwheel;
static const uint8_t arrow[12] = { 0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xF0, 0xD8, 0x98, 0x0C, 0x0C };
static uint8_t arrow_at(int8_t x, int8_t y) { return (uint8_t)(x >= 0 && x < 8 && y >= 0 && y < 12 && ((arrow[y] << x) & 0x80)); }
static void ptr_on(void)
{
    uint32_t d = SPRDATA; int8_t x, y, dx, dy; uint8_t v[2], k, edge;
    for (y = 0; y < 16; y++) for (x = 0; x < 16; x += 2) {
        for (k = 0; k < 2; k++) {
            if (arrow_at((int8_t)(x + k), y)) { v[k] = 1; continue; }
            edge = 0;
            for (dy = -1; dy <= 1; dy++) for (dx = -1; dx <= 1; dx++) if (arrow_at((int8_t)(x + k + dx), (int8_t)(y + dy))) edge = 1;
            v[k] = edge ? 2 : 0;
        }
        far_poke(d++, (uint8_t)((v[0] << 4) | v[1]));
    }
    pal(17, 255, 255, 255); pal(18, 0, 0, 0);
    far_poke(SPRTAB + 4, (uint8_t)SPRDATA); far_poke(SPRTAB + 5, (uint8_t)(SPRDATA >> 8)); far_poke(SPRTAB + 6, (uint8_t)(SPRDATA >> 16)); far_poke(SPRTAB + 7, 0);
    far_poke(SPRTAB + 8, 0x31); far_poke(SPRTAB + 9, 0x05); far_poke(SPRTAB + 10, 1);
    w32(V_SPRTAB, SPRTAB); REG(V_SPRCTL) = 1;
    chh = (uint8_t)((REG(0xD010) & 0x60) ? 16 : 8);
}
static void ptr_off(void) { REG(V_SPRCTL) = 0; }
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
        REG(V_SPRCTL) = (uint8_t)!(REG(0xD110) & 1);     /* our arrow only when the host shows none of its own ($D110) */
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

/* ---- the selection (PROG's) ----------------------------------------------- */
static uint8_t selon, selshown;
static unsigned sely, qy1, qy2;
static uint8_t selx, qx1, qx2;
static void sel_order(void)
{
    if (sely < cy || (sely == cy && selx <= cx)) { qy1 = sely; qx1 = selx; qy2 = cy; qx2 = cx; }
    else { qy1 = cy; qx1 = cx; qy2 = sely; qx2 = selx; }
}

/* ---- drawing ------------------------------------------------------------ */
static const char *const mtitle[] = { "File", "Edit", "Search", "Options", "Help" };
#define NMENU 5
static uint8_t mx(uint8_t m) { uint8_t x = 1, i; for (i = 0; i < m; i++) x = (uint8_t)(x + slen(mtitle[i]) + 2); return x; }
static void menubar(int8_t sel)
{
    uint8_t i, x = 0, j, k;
    for (; x < cols; x++) cel(x, ' ', K_MENU);
    for (i = 0; i < NMENU; i++) {
        x = mx(i);
        k = (int8_t)i == sel ? K_MSEL : K_MENU;
        cel(x, ' ', k);
        for (j = 0; mtitle[i][j]; j++) cel((uint8_t)(x + 1 + j), (uint8_t)mtitle[i][j], (uint8_t)(j ? k : (k == K_MSEL ? K_MSELHOT : K_MHOT)));
        cel((uint8_t)(x + 1 + j), ' ', k);
    }
    flush(0, cols);
}
static void topframe(void)
{
    const char *t = name[0] ? base_of(name) : (const char *)"Untitled";
    uint8_t x, n = slen(t), a;
    cel(0, 0xDA, K_FRAME);
    for (x = 1; x < cols - 1; x++) cel(x, 0xC4, K_FRAME);
    cel((uint8_t)(cols - 1), 0xBF, K_FRAME);
    if (n > cols - 6) n = (uint8_t)(cols - 6);
    a = (uint8_t)((cols - n - 2) / 2);
    cel(a, ' ', K_TITLE);
    for (x = 0; x < n; x++) cel((uint8_t)(a + 1 + x), (uint8_t)t[x], K_TITLE);
    cel((uint8_t)(a + 1 + n), ' ', K_TITLE);
    flush(1, cols);
}
static void text_row(uint8_t r)
{
    unsigned l = top + r, p; const uint8_t *s = ln; uint8_t c, w = 0, a = 255, b = 0;
    if (l < nlines) {
        if (l != cy) { far_get(SLOT(l), tmp, 256); s = tmp; }
        w = s[0];
        if (selon && l >= qy1 && l <= qy2) { a = l == qy1 ? qx1 : 0; b = l == qy2 ? qx2 : 255; }
    }
    cel(0, 0xB3, K_FRAME);
    for (c = 0; c < tw; c++) {
        p = (unsigned)hoff + c;
        cel((uint8_t)(c + 1), (uint8_t)(p < w ? s[1 + p] : ' '), (uint8_t)((p >= a && (p < b || b == 255)) ? K_SEL : K_TEXT));
    }
    flush((uint8_t)(2 + r), (uint8_t)(cols - 1));
}
static uint8_t vthumb(void) { unsigned n = th - 2; return (uint8_t)(nlines > 1 ? (unsigned long)cy * (n - 1) / (nlines - 1) : 0); }
static void vbar(void)
{
    uint8_t y, t = vthumb(), x = (uint8_t)(cols - 1);
    pk(x, 2, 0x1E, K_SCROLL);
    for (y = 0; y < th - 2; y++) pk(x, (uint8_t)(3 + y), (uint8_t)(y == t ? 0xDB : 0xB0), K_SCROLL);
    pk(x, (uint8_t)(rows - 3), 0x1F, K_SCROLL);
}
static uint8_t hthumb(void) { return (uint8_t)((unsigned)cx * (cols - 6) / 255); }
static void hbar(void)
{
    uint8_t x, t = hthumb();
    cel(0, 0xC0, K_FRAME);
    cel(1, 0x11, K_SCROLL);
    for (x = 0; x < cols - 5; x++) cel((uint8_t)(2 + x), (uint8_t)(x == t ? 0xDB : 0xB0), K_SCROLL);
    cel((uint8_t)(cols - 3), 0x10, K_SCROLL);
    cel((uint8_t)(cols - 2), 0xC4, K_FRAME);
    cel((uint8_t)(cols - 1), 0xD9, K_FRAME);
    flush((uint8_t)(rows - 2), cols);
}
static void status(void)
{
    uint8_t x = 0, i; char p[16]; unsigned long v;
    const char *s = *note ? note : (const char *)"K4510 Editor  <F1=Help>  <F10 or Alt=Menus>";
    for (; x < cols; x++) cel(x, ' ', K_STATUS);
    for (x = 0; s[x] && x < cols - 18; x++) cel((uint8_t)(x + 1), (uint8_t)s[x], K_STATUS);
    v = cy + 1; for (i = 0; i < 5; i++) { p[4 - i] = (char)('0' + v % 10); v /= 10; }
    p[5] = ':'; v = (unsigned long)cx + 1; for (i = 0; i < 3; i++) { p[8 - i] = (char)('0' + v % 10); v /= 10; }
    p[9] = 0;
    if (over) { cel((uint8_t)(cols - 16), 'O', K_STATUS); cel((uint8_t)(cols - 15), 'V', K_STATUS); cel((uint8_t)(cols - 14), 'R', K_STATUS); }
    for (i = 0; p[i]; i++) cel((uint8_t)(cols - 11 + i), (uint8_t)p[i], K_STATUS);
    flush((uint8_t)(rows - 1), cols);
}
/* the top band names the file (core/io.c's title stack, SYS+$44), as PROG */
static void band_file(void)
{
    const char *b = base_of(name); uint8_t i;
    REG(0xD544) = 0;
    for (i = 0; b[i]; i++) REG(0xD544) = (uint8_t)b[i];
}
static void cursor_to(void)
{
    uint8_t want = over ? '2' : '4';                   /* a block overwriting, an underline inserting: EDIT's */
    if (want != curshape) { curshape = want; put(27); put('['); put((char)want); put(' '); put('q'); }
    REG(TERM + 9) = (uint8_t)(1 + cx - hoff);           /* writing them redraws JIM's cursor at once */
    REG(TERM + 10) = (uint8_t)(2 + cy - top);
}
static void draw(void)
{
    uint8_t r;
    if (cy < top) top = cy;
    while (cy >= top + th) top++;
    if (cx < hoff) hoff = cx;
    if (cx >= (unsigned)hoff + tw) hoff = (uint8_t)(cx - tw + 1);
    if (top != lasttop || hoff != lasthoff) full = 1;
    if (selon || selshown) { full = 1; if (selon) sel_order(); }
    selshown = selon;
    if (full) { band_file(); menubar(-1); topframe(); for (r = 0; r < th; r++) text_row(r); full = 0; }
    else {
        if (lastcy != cy && lastcy >= top && lastcy < top + th) text_row((uint8_t)(lastcy - top));
        text_row((uint8_t)(cy - top));
    }
    vbar(); hbar(); status();
    lasttop = top; lastcy = cy; lasthoff = hoff;
    cursor_to();
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
            REG(TERM + 9) = (uint8_t)(dl_fx + (n >= dl_fw ? dl_fw - 1 : n)); REG(TERM + 10) = (uint8_t)(dl_y + 2 + dl_foc * 2);
            REG(TERM + 0x0E) |= 1;
        } else REG(TERM + 0x0E) &= (uint8_t)~1;
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
    REG(TERM + 0x0E) |= 1;
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

/* ---- editing (PROG's) ----------------------------------------------------- */
static void t_end(void) { if (tgline != 0xFFFFu) { u_end(); tgline = 0xFFFFu; } }
static void t_begin(void) { if (tgline != cy) { t_end(); u_begin(); u_line(cy); line_in(cy); tgline = cy; } }
static void go(unsigned n) { t_end(); goline(n); }
static void type_ch(uint8_t c)
{
    t_begin();
    if (over && cx < ln[0]) { ln[cx + 1] = c; cx++; dirty = 1; }
    else ins_ch(c);
    wantx = cx;
}
static void enter(void)                               /* split the line, keeping its indent */
{
    uint8_t ind = 0, i;
    t_end();
    while (ind < ln[0] && ln[ind + 1] == ' ') ind++;
    if (ind > cx) ind = cx;
    u_begin(); u_line(cy); line_in(cy); u_ins(cy + 1);
    split();
    for (i = 0; i < ind; i++) ins_ch(' ');
    u_end(); wantx = cx;
}
static void backspace(void)
{
    if (cx) { t_begin(); cx--; del_ch(); wantx = cx; return; }
    if (!cy) return;
    t_end(); line_out(cy);
    far_get(SLOT(cy - 1), tmp, 256);
    if ((unsigned)tmp[0] + ln[0] > 255) { note = "The lines would not fit on one"; return; }
    u_begin(); u_line(cy - 1); u_del(cy);
    join_prev();
    u_end(); wantx = cx;
}
static void delete_fwd(void)
{
    uint8_t i;
    if (cx < ln[0]) { t_begin(); del_ch(); return; }
    if (cy + 1 >= nlines) return;
    t_end(); line_out(cy);
    far_get(SLOT(cy + 1), tmp, 256);
    if ((unsigned)tmp[0] + ln[0] > 255) { note = "The lines would not fit on one"; return; }
    u_begin(); u_line(cy); u_del(cy + 1);
    far_get(SLOT(cy + 1), tmp, 256);
    for (i = 0; i < tmp[0]; i++) ln[ln[0] + 1 + i] = tmp[i + 1];
    ln[0] = (uint8_t)(ln[0] + tmp[0]);
    line_out(cy); close_at(cy + 1);
    u_end(); dirty = 1;
}
static uint8_t wordc(uint8_t c) { return (uint8_t)((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'); }
static void word_left(void)
{
    if (!cx) { if (cy) { go(cy - 1); cx = ln[0]; } return; }
    while (cx && !wordc(ln[cx])) cx--;
    while (cx && wordc(ln[cx])) cx--;
}
static void word_right(void)
{
    if (cx >= ln[0]) { if (cy + 1 < nlines) { go(cy + 1); cx = 0; } return; }
    while (cx < ln[0] && wordc(ln[cx + 1])) cx++;
    while (cx < ln[0] && !wordc(ln[cx + 1])) cx++;
}
static void sel_clear(void) { if (selon) { selon = 0; full = 1; } }
static void sel_start(void) { if (!selon) { selon = 1; sely = cy; selx = cx; } }
static uint8_t sel_delete(void)
{
    unsigned i; uint8_t n, j;
    sel_order(); selon = 0; full = 1;
    t_end(); line_out(cy);
    far_get(SLOT(qy2), tmp, 256);
    if (qx2 > tmp[0]) qx2 = tmp[0];
    n = (uint8_t)(tmp[0] - qx2);
    if ((unsigned)qx1 + n > 255) { note = "The lines would not fit on one"; return 0; }
    u_begin(); u_line(qy1);
    for (i = qy2; i > qy1; i--) u_del(i);
    far_get(SLOT(qy2), tmp, 256);
    far_get(SLOT(qy1), ln, 256);
    for (j = 0; j < n; j++) ln[qx1 + 1 + j] = tmp[qx2 + 1 + j];
    ln[0] = (uint8_t)(qx1 + n);
    far_put(ln, SLOT(qy1), 256);
    for (i = qy2; i > qy1; i--) close_at(i);
    u_end();
    cy = qy1; cx = qx1; line_in(cy); dirty = 1; wantx = cx;
    return 1;
}
static uint8_t sel_copy(void)
{
    unsigned i, n; uint8_t a, b, j;
    sel_order(); line_out(cy);
    n = qy2 - qy1 + 1;
    if (n >= REGMAX) { note = "Too much to copy"; return 0; }
    for (i = 0; i < n; i++) {
        far_get(SLOT(qy1 + i), tmp, 256);
        a = i ? 0 : qx1; b = (qy1 + i == qy2) ? qx2 : tmp[0];
        if (b > tmp[0]) b = tmp[0];
        if (a > b) a = b;
        for (j = 0; j < (uint8_t)(b - a); j++) tmp[1 + j] = tmp[1 + a + j];
        tmp[0] = (uint8_t)(b - a);
        far_put(tmp, RSLOT(i), 256);
    }
    reglines = n; reglinewise = 0;
    return 1;
}
static void put_chars(void)
{
    unsigned i, n = reglines; uint8_t j, x;
    t_end(); line_out(cy);
    far_get(RSLOT(0), tmp, 256);
    if (n == 1) {
        if ((unsigned)ln[0] + tmp[0] > 255) { note = "The line would be too long"; return; }
        u_begin(); u_line(cy);
        far_get(RSLOT(0), tmp, 256);
        for (j = 0; j < tmp[0]; j++) ins_ch(tmp[1 + j]);
        line_out(cy); u_end(); wantx = cx; full = 1;
        return;
    }
    if ((unsigned)cx + tmp[0] > 255) { note = "The line would be too long"; return; }
    far_get(RSLOT(n - 1), tmp, 256);
    if ((unsigned)tmp[0] + (ln[0] - cx) > 255) { note = "The line would be too long"; return; }
    u_begin(); u_line(cy);
    tmp[0] = (uint8_t)(ln[0] - cx);
    for (j = 0; j < tmp[0]; j++) tmp[1 + j] = ln[cx + 1 + j];
    far_put(tmp, RSLOT(n), 256);
    far_get(RSLOT(0), tmp, 256);
    for (j = 0; j < tmp[0]; j++) ln[cx + 1 + j] = tmp[1 + j];
    ln[0] = (uint8_t)(cx + tmp[0]);
    line_out(cy);
    for (i = 1; i < n; i++) { u_ins(cy + i); open_at(cy + i); dma_copy(RSLOT(i), SLOT(cy + i), 256); }
    cy += n - 1; line_in(cy); x = ln[0];
    far_get(RSLOT(n), tmp, 256);
    for (j = 0; j < tmp[0]; j++) ln[x + 1 + j] = tmp[1 + j];
    ln[0] = (uint8_t)(x + tmp[0]);
    line_out(cy);
    u_end(); cx = x; wantx = cx; dirty = 1; full = 1;
}
static void indent(uint8_t out)
{
    unsigned y, ya = cy, yb = cy; uint8_t j, k, w = ed_tabw;
    if (selon) { sel_order(); ya = qy1; yb = qy2; if (yb > ya && !qx2) yb--; }
    t_end(); line_out(cy);
    u_begin();
    for (y = ya; y <= yb; y++) {
        u_line(y);
        if (out) {
            for (k = 0; k < w && k < tmp[0] && tmp[1 + k] == ' '; k++) ;
            if (!k) continue;
            for (j = 0; (uint8_t)(j + k) < tmp[0]; j++) tmp[1 + j] = tmp[1 + j + k];
            tmp[0] = (uint8_t)(tmp[0] - k);
        } else {
            if (!tmp[0] || (unsigned)tmp[0] + w > 255) continue;
            for (j = tmp[0]; j; j--) tmp[j + w] = tmp[j];
            for (j = 1; j <= w; j++) tmp[j] = ' ';
            tmp[0] = (uint8_t)(tmp[0] + w);
        }
        far_put(tmp, SLOT(y), 256);
    }
    u_end(); dirty = 1; full = 1;
    line_in(cy);
    if (selon) { sely = ya; selx = 0; goline(yb); cx = ln[0]; }
    else if (cx > ln[0]) cx = ln[0];
    wantx = cx;
}
static void cut_line(void)
{
    t_end(); line_out(cy);
    reg_take(cy, 1, 1);
    u_begin();
    if (nlines == 1) { u_line(0); ln[0] = 0; line_out(0); }
    else { u_del(cy); close_at(cy); if (cy >= nlines) cy = nlines - 1; }
    u_end();
    line_in(cy); cx = 0; dirty = 1; full = 1;
    note = "Line cut -- Ctrl+V puts it back";
}

/* ---- files -------------------------------------------------------------- */
static char fbuf[NAMEMAX], sbuf[NAMEMAX], cbuf[NAMEMAX], gbuf[8];
static void fresh(void) { ujp = ujn = 0; useq = 0; tgline = 0xFFFFu; cx = 0; top = 0; hoff = 0; dirty = 0; full = 1; wantx = 0; selon = 0; }
static uint8_t save_as(void)
{
    uint8_t i;
    for (i = 0; name[i] && i < NAMEMAX - 1; i++) fbuf[i] = name[i];
    fbuf[i] = 0;
    if (!form1("Save As", "File Name:", fbuf, NAMEMAX, "OK") || !fbuf[0]) return 0;
    for (i = 0; fbuf[i]; i++) name[i] = fbuf[i];
    name[i] = 0;
    t_end(); save_file();
    full = 1;
    if (note[0] == 'w') { note = "Saved"; return 1; }
    note = "The file was not saved"; return 0;
}
static uint8_t save(void)
{
    if (!name[0]) return save_as();
    t_end(); save_file();
    if (note[0] == 'w') { note = "Saved"; return 1; }
    note = "The file was not saved"; return 0;
}
static uint8_t may_leave(void)                        /* 1 if the text in front may go */
{
    uint8_t a;
    if (!dirty) return 1;
    a = ask(0, "Loaded file is not saved.  Save it now?", "Yes", "No", "Cancel");
    if (a == 0) return save();
    return (uint8_t)(a == 1);
}
static void load_name(const char *nm)
{
    uint8_t i;
    for (i = 0; nm[i] && i < NAMEMAX - 1; i++) name[i] = nm[i];
    name[i] = 0;
    nlines = 1; cy = 0;
    load_file();
    fresh();
    if (note[0] == 'n') note = "A new file";
}

/* The Open dialog: a name, and the directory to pick one from.  The list is
 * the current directory's, read through the file device into far memory;
 * choosing a directory goes into it (the shell's current directory moves,
 * as it did in DOS), and choosing a file opens it. */
static unsigned dn, dsel, dtop;
static char cwd[NAMEMAX];
static uint8_t dent[32];
static uint8_t fs_do(uint8_t c) { REG(0xD300) = c; return REG(0xD301); }
static void dir_read(void)
{
    uint8_t i;
    REG(0xD318) = NAMEMAX;                            /* GETCWD's room */
    w32(0xD308, (uint32_t)(uint16_t)cwd); fs_do(15);
    dn = 0; dsel = 0; dtop = 0;
    if (!(cwd[0] == '/' && !cwd[1])) { dent[0] = 1; dent[1] = '.'; dent[2] = '.'; dent[3] = 0; far_put(dent, DIRTAB, 32); dn = 1; }
    w32(0xD308, (uint32_t)(uint16_t)(dent + 1));
    if (fs_do(6)) return;
    while (dn < DIRMAX) {
        w32(0xD308, (uint32_t)(uint16_t)(dent + 1));
        if (fs_do(7)) break;
        dent[31] = 0;
        dent[0] = (uint8_t)(REG(0xD310) == 0xFF && REG(0xD313) == 0xFF);   /* size $FFFFFFFF: a directory */
        for (i = 1; dent[i]; i++) ;
        far_put(dent, DIRTAB + ((uint32_t)dn << 5), 32);
        dn++;
    }
}
static void dir_draw(uint8_t lx, uint8_t ly, uint8_t lw, uint8_t lh, uint8_t foc)
{
    uint8_t r, i; unsigned e;
    if (dsel < dtop) dtop = dsel;
    if (dsel >= dtop + lh) dtop = dsel - lh + 1;
    for (r = 0; r < lh; r++) {
        e = dtop + r;
        for (i = 0; i < lw; i++) pk((uint8_t)(lx + i), (uint8_t)(ly + r), ' ', (uint8_t)(e == dsel && e < dn ? (foc ? K_MSEL : K_SEL) : K_FIELD));
        if (e >= dn) continue;
        far_get(DIRTAB + ((uint32_t)e << 5), dent, 32);
        for (i = 0; dent[1 + i] && i < lw - 3; i++) pk((uint8_t)(lx + 1 + i), (uint8_t)(ly + r), dent[1 + i], (uint8_t)(e == dsel ? (foc ? K_MSEL : K_SEL) : K_FIELD));
        if (dent[0]) pk((uint8_t)(lx + 1 + i), (uint8_t)(ly + r), '/', (uint8_t)(e == dsel ? (foc ? K_MSEL : K_SEL) : K_FIELD));
    }
}
static void open_dialog(void)
{
    uint8_t lx, ly, lw, lh, k, foc = 0, i, go_ = 0, n, y;
    if (!may_leave()) { full = 1; return; }
    fbuf[0] = 0;
    dir_read();
    dl_title = "Open"; dl_text = 0; dl_nf = 1; dl_lab[0] = "File Name:"; dl_buf[0] = fbuf; dl_max[0] = NAMEMAX;
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
        n = slen(fbuf);
        if (!foc) { REG(TERM + 9) = (uint8_t)(dl_fx + (n >= dl_fw ? dl_fw - 1 : n)); REG(TERM + 10) = (uint8_t)(dl_y + 2); REG(TERM + 0x0E) |= 1; }
        else REG(TERM + 0x0E) &= (uint8_t)~1;
        k = event();
        if (kcode == 2) {
            if (mev == 4) { if (mwheel > 0) dsel = dsel > 3 ? dsel - 3 : 0; else { dsel += 3; if (dsel >= dn) dsel = dn ? dn - 1 : 0; } continue; }
            if (mev != 1) continue;
            if (mrow == dl_by && mcol >= dl_bx[0] && mcol < dl_bx[0] + 6) { go_ = 1; }
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
            if (k == 0x08) { if (n) fbuf[n - 1] = 0; }
            else if (k >= 0x20 && n < NAMEMAX - 1) { fbuf[n] = (char)k; fbuf[n + 1] = 0; }
        }
        if (go_ == 2 && dn) {                         /* the list's entry: a directory is gone into, a file opened */
            far_get(DIRTAB + ((uint32_t)dsel << 5), dent, 32);
            if (dent[0]) { w32(0xD304, (uint32_t)(uint16_t)(dent + 1)); fs_do(11); dir_read(); go_ = 0; dl_draw(); continue; }
            for (i = 0; dent[1 + i] && i < NAMEMAX - 1; i++) fbuf[i] = (char)dent[1 + i];
            fbuf[i] = 0; go_ = 1;
        }
        if (go_ == 1) { if (fbuf[0]) { load_name(fbuf); break; } go_ = 0; }
        go_ = 0;
    }
    REG(TERM + 0x0E) |= 1;
    full = 1;
}

/* ---- search ------------------------------------------------------------- */
static void find_dlg(void)
{
    uint8_t i;
    for (i = 0; i < patlen; i++) sbuf[i] = pat[i];
    sbuf[i] = 0;
    if (!form1("Find", "Find What:", sbuf, NAMEMAX, "OK") || !sbuf[0]) return;
    t_end();
    for (patlen = 0; sbuf[patlen]; patlen++) pat[patlen] = sbuf[patlen];
    search(1);
    if (note[0] == 'n') note = "Match not found";
}
static void change_dlg(void)
{
    unsigned l;
    dl_title = "Change"; dl_text = 0; dl_nf = 2;
    dl_lab[0] = "Find What:"; dl_buf[0] = sbuf; dl_max[0] = NAMEMAX;
    dl_lab[1] = "Change To:"; dl_buf[1] = cbuf; dl_max[1] = NAMEMAX;
    dl_btn[0] = "Change All"; dl_btn[1] = "Cancel"; dl_nb = 2;
    if (dialog() != 0 || !sbuf[0]) return;
    for (soldl = 0; sbuf[soldl]; soldl++) sold[soldl] = sbuf[soldl];
    for (snewl = 0; cbuf[snewl]; snewl++) snew[snewl] = cbuf[snewl];
    t_end(); subs = 0; line_out(cy); u_begin();
    for (l = 0; l < nlines; l++) sub_line(l, 1);
    u_end(); line_in(cy);
    if (cx > ln[0]) cx = ln[0];
    for (patlen = 0; sbuf[patlen]; patlen++) pat[patlen] = sbuf[patlen];
    full = 1;
    nb_reset(); nb_n(subs); nb_s(subs == 1 ? " change made (Ctrl+Z undoes it)" : " changes made (Ctrl+Z undoes them)"); note = nbuf;
}
static void goto_dlg(void)
{
    unsigned n = 0; uint8_t i;
    gbuf[0] = 0;
    if (!form1("Go To Line", "Line:", gbuf, sizeof gbuf, "OK")) return;
    for (i = 0; gbuf[i] >= '0' && gbuf[i] <= '9'; i++) n = n * 10 + (unsigned)(gbuf[i] - '0');
    if (n) { go(n - 1); cx = 0; wantx = 0; }
}

/* ---- help --------------------------------------------------------------- */
static const char *const helptext[] = {
    "Moving        arrows, Home, End, PgUp, PgDn; with Ctrl: words, the ends",
    "Selecting     Shift with a moving key; drag, Shift+click; Ctrl+A all",
    "Clipboard     Ctrl+X Ctrl+C Ctrl+V, or Shift+Del Ctrl+Ins Shift+Ins",
    "              (with nothing selected: the line)",
    "Undo, redo    Ctrl+Z, Ctrl+Y           Insert   insert / overwrite",
    "Tab           spaces to the next stop; Tab, Shift+Tab indent a selection",
    "Files         Ctrl+N new, Ctrl+O open, Ctrl+S save, Ctrl+Q exit",
    "Search        Ctrl+F find, F3 again, Ctrl+G go to a line",
    "BASIC         Ctrl+R renumbers (10, 20, 30 and every GOTO)",
    "Menus         F10, or Alt and the letter; Esc closes",
    "Mouse         click the text, a menu, a scroll bar; the wheel scrolls",
    "",
    "EDIT -s FILE  starts in the console's own colours (Options)",
    0 };
static void help(void)
{
    uint8_t r, w = 0, h, x, y;
    for (r = 0; helptext[r]; r++) if (slen(helptext[r]) > w) w = slen(helptext[r]);
    w = (uint8_t)(w + 4); h = (uint8_t)(r + 4);
    if (w > cols - 2) w = (uint8_t)(cols - 2);
    x = (uint8_t)((cols - w) / 2); y = (uint8_t)((rows - h) / 2);
    box(x, y, w, h, K_DLG, "Keyboard");
    for (r = 0; helptext[r]; r++) { const char *s = helptext[r]; uint8_t i; for (i = 0; s[i] && i < w - 4; i++) pk((uint8_t)(x + 2 + i), (uint8_t)(y + 2 + r), (uint8_t)s[i], K_DLG); }
    REG(TERM + 0x0E) &= (uint8_t)~1;
    while (event() == KMOUSE && mev != 1) ;
    REG(TERM + 0x0E) |= 1;
    full = 1;
}

/* ---- the commands ---------------------------------------------------------
 * Every key and every menu entry is a command number, run by one switch, so
 * a menu can never do something its key does not (PROG's rule). */
static void run_cmd(uint8_t c)
{
    if (c != C_CUT && c != C_COPY && c != C_PASTE && c != C_CLEAR && c != C_NONE) sel_clear();
    switch (c) {
    case C_NEW:    if (may_leave()) { name[0] = 0; load_name(""); note = ""; } break;
    case C_OPEN:   open_dialog(); break;
    case C_SAVE:   save(); break;
    case C_SAVEAS: save_as(); break;
    case C_EXIT:   if (may_leave()) running = 0; break;
    case C_UNDO:   t_end(); u_apply(0); break;
    case C_REDO:   t_end(); u_apply(1); break;
    case C_CUT:    if (!selon) { cut_line(); break; }
                   if (sel_copy() && sel_delete()) note = "Cut -- Ctrl+V puts it back";
                   break;
    case C_COPY:   if (selon) { if (sel_copy()) note = "Copied"; break; }
                   t_end(); line_out(cy); reg_take(cy, 1, 1); note = "Line copied"; break;
    case C_PASTE:  if (selon && !sel_delete()) break;
                   t_end(); if (reglinewise || !reglines) do_put(0); else put_chars();
                   break;
    case C_CLEAR:  if (selon) sel_delete(); else delete_fwd(); break;
    case C_SELALL: t_end(); sely = 0; selx = 0; go(nlines - 1); cx = ln[0]; selon = 1; full = 1; break;
    case C_RENUM:  t_end(); do_renum(""); break;
    case C_FIND:   find_dlg(); break;
    case C_NEXT:   t_end(); search(1); if (note[0] == 'n') note = "Match not found"; break;
    case C_CHANGE: change_dlg(); break;
    case C_GOTO:   goto_dlg(); break;
    case C_DOS:    scheme(0); full = 1; break;
    case C_SYS:    scheme(1); full = 1; break;
    case C_HELP:   help(); break;
    case C_ABOUT:  ask("About", "K4510 Editor -- MS-DOS EDIT's manner, VI's engine", "OK", 0, 0); break;
    }
    wantx = cx;
}

/* ---- the menus -------------------------------------------------------------
 * F10 opens File; Alt and a title's letter opens that one.  Up and Down
 * choose, Left and Right go along the bar, Enter or an entry's letter runs
 * it, Esc closes.  The mouse opens a title and runs an entry. */
struct item { const char *label; uint8_t hot; uint8_t cmd; const char *keys; };
static const struct item m_file[]   = { { "New", 0, C_NEW, "Ctrl+N" }, { "Open...", 0, C_OPEN, "Ctrl+O" }, { "Save", 0, C_SAVE, "Ctrl+S" },
                                        { "Save As...", 5, C_SAVEAS, "" }, { "", 0, C_SEP, "" }, { "Exit", 1, C_EXIT, "Ctrl+Q" }, { 0, 0, 0, 0 } };
static const struct item m_edit[]   = { { "Undo", 0, C_UNDO, "Ctrl+Z" }, { "Redo", 0, C_REDO, "Ctrl+Y" }, { "", 0, C_SEP, "" },
                                        { "Cut", 2, C_CUT, "Shift+Del" }, { "Copy", 0, C_COPY, "Ctrl+Ins" }, { "Paste", 0, C_PASTE, "Shift+Ins" },
                                        { "Clear", 2, C_CLEAR, "Del" }, { "Select All", 7, C_SELALL, "Ctrl+A" }, { "", 0, C_SEP, "" },
                                        { "Renumber BASIC", 2, C_RENUM, "Ctrl+R" }, { 0, 0, 0, 0 } };
static const struct item m_search[] = { { "Find...", 0, C_FIND, "Ctrl+F" }, { "Repeat Last Find", 0, C_NEXT, "F3" },
                                        { "Change...", 0, C_CHANGE, "" }, { "Go To Line...", 0, C_GOTO, "Ctrl+G" }, { 0, 0, 0, 0 } };
static const struct item m_opt[]    = { { "DOS Colours", 0, C_DOS, "" }, { "System Colours", 0, C_SYS, "" }, { 0, 0, 0, 0 } };
static const struct item m_help[]   = { { "Keyboard", 0, C_HELP, "F1" }, { "About...", 0, C_ABOUT, "" }, { 0, 0, 0, 0 } };
static const struct item *const menus[] = { m_file, m_edit, m_search, m_opt, m_help };

static uint8_t mwidth(const struct item *it, uint8_t *n)
{
    uint8_t w = 0, r;
    for (*n = 0; it[*n].label; (*n)++) { r = (uint8_t)(slen(it[*n].label) + slen(it[*n].keys) + 7); if (r > w) w = r; }
    return w;
}
static void menu_draw(uint8_t m, uint8_t sel)
{
    const struct item *it = menus[m];
    uint8_t n, w = mwidth(it, &n), x = (uint8_t)(mx(m) - 1), r, j, k, kw;
    if (x + w + 2 > cols) x = (uint8_t)(cols - w - 2);
    menubar((int8_t)m);
    box(x, 1, w, (uint8_t)(n + 2), K_MENU, 0);
    for (r = 0; r < n; r++) {
        if (it[r].cmd == C_SEP) { pk(x, (uint8_t)(2 + r), 0xC3, K_MENU); for (j = 1; j < w - 1; j++) pk((uint8_t)(x + j), (uint8_t)(2 + r), 0xC4, K_MENU); pk((uint8_t)(x + w - 1), (uint8_t)(2 + r), 0xB4, K_MENU); continue; }
        k = r == sel ? K_MSEL : K_MENU;
        for (j = 1; j < w - 1; j++) pk((uint8_t)(x + j), (uint8_t)(2 + r), ' ', k);
        if ((it[r].cmd == C_DOS && !sysc) || (it[r].cmd == C_SYS && sysc)) pk((uint8_t)(x + 1), (uint8_t)(2 + r), 0xFB, k);   /* a check: the scheme in use */
        for (j = 0; it[r].label[j]; j++) pk((uint8_t)(x + 2 + j), (uint8_t)(2 + r), (uint8_t)it[r].label[j], (uint8_t)(j == it[r].hot ? (k == K_MSEL ? K_MSELHOT : K_MHOT) : k));
        kw = slen(it[r].keys);
        pstr((uint8_t)(x + w - 2 - kw), (uint8_t)(2 + r), it[r].keys, k);
    }
}
static uint8_t title_at(uint8_t c)
{
    uint8_t i, x;
    for (i = 0; i < NMENU; i++) { x = mx(i); if (c >= x && c < (uint8_t)(x + slen(mtitle[i]) + 2)) return i; }
    return NMENU;
}
static uint8_t menu_x(uint8_t m, uint8_t *w, uint8_t *n)
{
    uint8_t x = (uint8_t)(mx(m) - 1);
    *w = mwidth(menus[m], n);
    if (x + *w + 2 > cols) x = (uint8_t)(cols - *w - 2);
    return x;
}
static uint8_t mclose(uint8_t c)                      /* the menu away before its command runs: a dialog must not open over it */
{
    full = 1; draw(); REG(TERM + 0x0E) |= 1;
    return c;
}
static uint8_t menu(uint8_t m)                        /* menu m open; the command chosen, or C_NONE */
{
    uint8_t sel = 0, n, k, w, x, i;
    REG(TERM + 0x0E) &= (uint8_t)~1;                   /* no text cursor over a menu */
    for (;;) {
        x = menu_x(m, &w, &n);
        while (menus[m][sel].cmd == C_SEP) sel++;
        full = 1; draw(); REG(TERM + 0x0E) &= (uint8_t)~1;
        menu_draw(m, sel);
        k = event();
        if (kcode == 2) {
            if (mev == 4) continue;
            if (mrow == 0) { if (mev == 3) continue; i = title_at(mcol); if (i < NMENU) { m = i; sel = 0; } continue; }
            if (mrow >= 2 && mrow < 2 + n && mcol > x && mcol < x + w - 1) {
                sel = (uint8_t)(mrow - 2);
                if (mev != 2 && menus[m][sel].cmd != C_SEP) return mclose(menus[m][sel].cmd);
                continue;
            }
            if (mev == 1) break;
            continue;
        }
        if (k == 0x1B || (k == KF(10) && kcode)) break;
        if (k == 0x0D) return mclose(menus[m][sel].cmd);
        if (kcode && k >= KALT && k < KALT + 26) {       /* Alt and a title's letter: that menu */
            for (i = 0; i < NMENU; i++) if (lower((uint8_t)mtitle[i][0]) == 'a' + (k - KALT)) { m = i; sel = 0; }
            continue;
        }
        if (!kcode) {                                 /* an entry's letter runs it */
            for (i = 0; i < n; i++)
                if (menus[m][i].cmd != C_SEP && lower((uint8_t)menus[m][i].label[menus[m][i].hot]) == lower(k)) return mclose(menus[m][i].cmd);
            continue;
        }
        if (k == KUP) { do sel = sel ? (uint8_t)(sel - 1) : (uint8_t)(n - 1); while (menus[m][sel].cmd == C_SEP); }
        else if (k == KDOWN) { do sel = (uint8_t)(sel + 1 < n ? sel + 1 : 0); while (menus[m][sel].cmd == C_SEP); }
        else if (k == KLEFT || k == KRIGHT) { m = (uint8_t)(k == KLEFT ? (m ? m - 1 : NMENU - 1) : (m + 1 < NMENU ? m + 1 : 0)); sel = 0; }
    }
    return mclose(C_NONE);
}

/* ---- the mouse in the window ---------------------------------------------- */
static uint8_t vdrag, hdrag;
static void place(uint8_t r, uint8_t c)
{
    go(top + (r - 2));
    cx = (uint8_t)(hoff + (c ? c - 1 : 0)); if (cx > ln[0]) cx = ln[0];
    wantx = cx;
}
static void vset(uint8_t r)                           /* the thumb dragged to row r of the bar */
{
    unsigned n = th - 2, y = r < 3 ? 0 : r - 3;
    if (y >= n) y = n - 1;
    go(n > 1 ? (unsigned)((unsigned long)y * (nlines - 1) / (n - 1)) : 0);
    if (cx > ln[0]) cx = ln[0];
}
static void hset(uint8_t c)
{
    unsigned n = cols - 5, x = c < 2 ? 0 : c - 2;
    if (x >= n) x = n - 1;
    cx = (uint8_t)((unsigned long)x * 255 / (n - 1)); if (cx > ln[0]) cx = ln[0];
    wantx = cx;
}
static void do_mouse(void)
{
    uint8_t r = mrow, c = mcol, i, rmax = (uint8_t)(rows - 3); unsigned d;
    if (mev == 4) {                                   /* the wheel: three lines a notch */
        d = (unsigned)(mwheel < 0 ? -mwheel : mwheel) * 3;
        t_end();
        if (mwheel > 0) top = top > d ? top - d : 0;
        else { top += d; if (top + th > nlines) top = nlines > th ? nlines - th : 0; }
        if (cy < top) go(top); else if (cy >= top + th) go(top + th - 1);
        if (cx > ln[0]) cx = ln[0];
        full = 1; return;
    }
    if (mev == 3) { dragging = vdrag = hdrag = 0; return; }
    if (mev == 2) {
        if (vdrag) { vset(r); return; }
        if (hdrag) { hset(c); return; }
        if (!dragging) return;
        if (r == 0xFF || r < 2) go(top ? top - 1 : 0);
        else if (r > rmax) go(top + th);
        else place(r, c);
        selon = (uint8_t)(cy != sely || cx != selx);
        full = 1; return;
    }
    if (r == 0) { i = title_at(c); if (i < NMENU) run_cmd(menu(i)); return; }
    if (c == cols - 1 && r >= 2 && r <= rmax) {         /* the vertical scroll bar */
        sel_clear(); t_end();
        if (r == 2) { if (cy) go(cy - 1); }
        else if (r == rmax) { if (cy + 1 < nlines) go(cy + 1); }
        else if (r - 3 == vthumb()) vdrag = 1;
        else if (r - 3 < vthumb()) go(cy > (unsigned)th ? cy - th : 0);
        else go(cy + th);
        if (cx > ln[0]) cx = ln[0];
        return;
    }
    if (r == rows - 2 && c >= 1 && c <= cols - 3) {       /* the horizontal one */
        sel_clear();
        if (c == 1) { if (cx) cx--; }
        else if (c == cols - 3) { if (cx < ln[0]) cx++; }
        else if (c - 2 == hthumb()) hdrag = 1;
        else hset(c);
        wantx = cx;
        return;
    }
    if (r >= 2 && r <= rmax && c >= 1 && c < cols - 1) {
        t_end();
        if (kmod & 1) { sel_start(); place(r, c); }
        else { sel_clear(); place(r, c); sely = cy; selx = cx; }
        dragging = 1;
    }
}

/* ---- the keys ------------------------------------------------------------ */
static void do_key(uint8_t k)
{
    uint8_t ctrl = (uint8_t)(kmod & 2), shift = (uint8_t)(kmod & 1), i;
    if (kcode == 2) { do_mouse(); return; }
    if (!kcode) {
        switch (k) {
        case 0x0D: if (selon && !sel_delete()) return; enter(); return;
        case 0x08: if (selon) { sel_delete(); return; } backspace(); return;
        case 0x09:
            if (shift) { indent(1); return; }
            if (selon) { sel_order(); if (qy2 > qy1) { indent(0); return; } if (!sel_delete()) return; }
            t_begin(); ed_tab(); wantx = cx; return;
        case 0x1B: sel_clear(); return;
        case 0x01: run_cmd(C_SELALL); return;        /* ^A */
        case 0x0E: run_cmd(C_NEW); return;           /* ^N */
        case 0x0F: run_cmd(C_OPEN); return;          /* ^O */
        case 0x13: run_cmd(C_SAVE); return;          /* ^S */
        case 0x11: run_cmd(C_EXIT); return;          /* ^Q */
        case 0x1A: run_cmd(C_UNDO); return;          /* ^Z */
        case 0x19: run_cmd(C_REDO); return;          /* ^Y */
        case 0x18: run_cmd(C_CUT); return;           /* ^X */
        case 0x03: run_cmd(C_COPY); return;          /* ^C */
        case 0x16: run_cmd(C_PASTE); return;         /* ^V */
        case 0x06: run_cmd(C_FIND); return;          /* ^F */
        case 0x07: run_cmd(C_GOTO); return;          /* ^G */
        case 0x12: run_cmd(C_RENUM); return;         /* ^R: renumber a BASIC file, as the old EDIT did */
        }
        if ((k >= 0x20 && k < 0x7F) || k >= 0x80) { if (selon && !sel_delete()) return; type_ch(k); }
        return;
    }
    if (k >= KALT && k < KALT + 26) {                 /* Alt and a title's letter */
        for (i = 0; i < NMENU; i++) if (lower((uint8_t)mtitle[i][0]) == 'a' + (k - KALT)) { run_cmd(menu(i)); return; }
        return;
    }
    if (k == KDEL && shift) { run_cmd(C_CUT); return; }        /* DOS's own clipboard keys */
    if (k == KINS && ctrl) { run_cmd(C_COPY); return; }
    if (k == KINS && shift) { run_cmd(C_PASTE); return; }
    if (k >= KUP && k <= KPGDN) {
        t_end();                                      /* a move ends a run of typing: Ctrl+Z takes back what was typed since */
        if (shift) sel_start(); else sel_clear();
    }
    switch (k) {
    case KLEFT:  if (ctrl) word_left(); else if (cx) cx--; else if (cy) { go(cy - 1); cx = ln[0]; } wantx = cx; break;
    case KRIGHT: if (ctrl) word_right(); else if (cx < ln[0]) cx++; else if (cy + 1 < nlines) { go(cy + 1); cx = 0; } wantx = cx; break;
    case KUP:    if (cy) { go(cy - 1); cx = wantx < ln[0] ? wantx : ln[0]; } break;
    case KDOWN:  if (cy + 1 < nlines) { go(cy + 1); cx = wantx < ln[0] ? wantx : ln[0]; } break;
    case KHOME:  if (ctrl) go(0); cx = 0; wantx = 0; break;
    case KEND:   if (ctrl) go(nlines - 1); cx = ln[0]; wantx = cx; break;
    case KPGUP:  go(cy > (unsigned)(th - 1) ? cy - (th - 1) : 0); cx = wantx < ln[0] ? wantx : ln[0]; break;
    case KPGDN:  go(cy + th - 1); cx = wantx < ln[0] ? wantx : ln[0]; break;
    case KINS:   over = (uint8_t)!over; break;
    case KDEL:   run_cmd(C_CLEAR); break;
    case KF(1):  run_cmd(C_HELP); break;
    case KF(3):  run_cmd(C_NEXT); break;
    case KF(10): run_cmd(menu(0)); break;
    }
}

void main(void)
{
    uint8_t k, na = rom_args(), j = 0, sys = 0; const char *a = *(const char **)0xF0;
    for (;;) {                                        /* EDIT [-s] [name] */
        while (na && *a == ' ') { a++; na--; }
        if (na >= 2 && a[0] == '-' && (a[1] == 's' || a[1] == 'S') && (na == 2 || a[2] == ' ')) { sys = 1; a += 2; na -= 2; continue; }
        break;
    }
    while (j < na && j < NAMEMAX - 1 && a[j] != ' ') { name[j] = a[j]; j++; }
    name[j] = 0;
    cols = REG(TERM + 5); rows = REG(TERM + 6); ox = REG(TERM + 7); oy = REG(TERM + 8); stride = REG(TERM + 0x0D);
    if (!cols) cols = 80;
    if (!rows) rows = 30;
    if (!stride) stride = cols;
    scr = (uint32_t)REG(TERM + 0x10) | ((uint32_t)REG(TERM + 0x11) << 8) | ((uint32_t)REG(TERM + 0x12) << 16) | ((uint32_t)REG(TERM + 0x13) << 24);
    if (cols > 184) cols = 184;
    th = (uint8_t)(rows - 4); tw = (uint8_t)(cols - 2);
    scheme(sys);
    ed_maxlines = 16384u;
    ed_slots = 0x08000000UL; ed_undo = 0x08600000UL;
    ed_rc_tabw();                                     /* VI's set ts=N, one tab width for all three editors */
    load_file();
    fresh();
    note = name[0] ? (note[0] == 'n' ? "A new file" : "") : "";
    REG(TERM + 4) = 1; REG(TERM + 0x0E) |= 1;
    ptr_on();
    while (running) {
        draw();
        k = event();
        if (kcode != 2 || mev == 1) note = "";
        do_key(k);
    }
    ptr_off();
    put(27); put('['); put('2'); put(' '); put('q');     /* the block back for the shell */
    REG(TERM + 0x0E) = 0; REG(TERM + 4) = 1; REG(TERM + 4) = 2;
    rom_video();
}
