/* K4510: HEXED -- a hex editor, for a file or for the machine's memory.
 *
 *   HEXED NAME        a file, loaded whole into far memory (8 MB at most)
 *   HEXED $ADDRESS    memory, by 28-bit physical address, edited live
 *
 * The bytes in hex on the left, and on the right as the K4510 code page
 * draws them (the control codes as CP437's little pictures).  Edit on either
 * side: hex digits on the left, any character on the right; Tab changes side.
 *
 *   arrows PgUp PgDn    move                Home End  the row's ends; again: the file's
 *   F5 or Ctrl-G        go to an offset     F3 or Ctrl-F  find (hex or text, by the side
 *   F2 or Ctrl-S        save                              you are on); F3 again: the next
 *   Ctrl-Z              undo                Esc       leave (asks first if the file changed)
 *   the mouse           click a byte on either side; the wheel scrolls
 *
 * A file is changed in place, and typing at its end makes it longer, which
 * is also how a new file is started.  The screen is the console as it stands,
 * in any MODE (16 bytes a row where there is room, 8 or 4 where there is
 * not), written as text32 cells a row at a time by DMA.
 */
#include "k4510.h"

#define TERM   0xDA00u
#define SCREEN 0x00030000UL
#define FS     0xD300u
#define HBUF   0x0D000000UL                  /* the file (far memory nothing else uses) */
#define MAXSZ  0x00800000UL
#define MOUSEX 0xD108u
#define MOUSEY 0xD10Au
#define MOUSEB 0xD10Cu
#define MOUSEW 0xD10Du
#define SHELL_RC (*(volatile uint8_t *)0x03FF)

void __fastcall__ rom_chrout(unsigned char c);
static unsigned char rom_args(void) { return ((unsigned char (*)(void))0xFF95)(); }

enum { BLACK, WHITE, RED, CYAN, PURPLE, GREEN, BLUE, YELLOW, ORANGE, BROWN, LRED, DGREY, GREY, LGREEN, LBLUE, LGREY };

/* A key: a character, or 0x100 | a K4510 key code -- KBDST bit 5 tells the
 * two apart, so the code page's upper half can be typed on the text side. */
#define KY(k)   (0x100u | (k))
#define K_UP    KY(0x80)
#define K_DOWN  KY(0x81)
#define K_LEFT  KY(0x82)
#define K_RIGHT KY(0x83)
#define K_HOME  KY(0x84)
#define K_END   KY(0x85)
#define K_PGUP  KY(0x86)
#define K_PGDN  KY(0x87)
#define K_DEL   KY(0x89)
#define K_F1    KY(0x90)
#define K_F2    KY(0x91)
#define K_F3    KY(0x92)
#define K_F5    KY(0x94)
static uint16_t getkey(void)
{
    uint8_t st = REG(KBDST), k;
    if (!(st & 0x80)) return 0;
    k = REG(KBD);
    return (st & 0x20) ? KY(k) : k;
}

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
static void say(const char *s) { while (*s) rom_chrout((uint8_t) *s++); }

static char name[80];
static uint8_t memmode, modified, pane, nib;
static uint32_t base, size, pos, top_off;
static uint8_t cols, rows, ox, oy, stride, fg0, bg0, bpr, gap, nrows, tx, flags_was;
static uint8_t win[1024];
static char msg[80];
static const char hexd[] = "0123456789ABCDEF";

/* ---- the screen --------------------------------------------------------- */
static uint8_t rb[80 * 4], rn, mrow = 0xFF, mcol = 0xFF;
static void rb_put(uint8_t glyph, uint8_t fg, uint8_t bg)
{
    uint8_t *q = rb + rn * 4;
    if (rn >= 80 || rn >= cols) return;
    q[0] = glyph; q[1] = 0; q[2] = fg; q[3] = bg; rn++;
}
static void rb_str(const char *s, uint8_t fg, uint8_t bg) { while (*s) rb_put((uint8_t) *s++, fg, bg); }
static void rb_hex(uint32_t v, uint8_t digits, uint8_t fg, uint8_t bg)
{
    while (digits--) rb_put((uint8_t) hexd[(uint8_t)(v >> (digits * 4)) & 15], fg, bg);
}
static void rb_out(uint8_t r)                /* a whole row, from column 0; the cell under the mouse lit */
{
    if (r == mrow && mcol < rn) { rb[mcol * 4 + 2] = WHITE; rb[mcol * 4 + 3] = LBLUE; }
    if (r < rows && rn) dma_copy((uint32_t)(uint16_t) rb, SCREEN + ((uint32_t)(r + oy) * stride + ox) * 4, (uint32_t) rn * 4);
    rn = 0;
}
static void rb_fill(uint8_t fg, uint8_t bg) { while (rn < cols && rn < 80) rb_put(' ', fg, bg); }

static uint8_t have(uint32_t a) { return memmode || a < size; }

static void draw_row(uint8_t r)
{
    uint32_t off = top_off + (uint32_t) r * bpr, a;
    uint8_t i, b, cur, *w = win + r * bpr;
    rb_hex(off, 7, GREY, bg0);
    rb_put(' ', fg0, bg0); rb_put(' ', fg0, bg0);
    for (i = 0; i < bpr; i++) {
        a = off + i; cur = a == pos; b = w[i];
        if (gap && i == 8) rb_put(' ', fg0, bg0);
        if (!have(a)) {
            if (cur) { rb_put('_', BLACK, YELLOW); rb_put('_', BLACK, YELLOW); }
            else { rb_put(' ', fg0, bg0); rb_put(' ', fg0, bg0); }
        } else if (cur && pane == 0) {
            rb_put((uint8_t) hexd[b >> 4], BLACK, nib ? fg0 : YELLOW);
            rb_put((uint8_t) hexd[b & 15], BLACK, nib ? YELLOW : fg0);
        } else if (cur) {
            rb_put((uint8_t) hexd[b >> 4], YELLOW, DGREY); rb_put((uint8_t) hexd[b & 15], YELLOW, DGREY);
        } else {
            rb_put((uint8_t) hexd[b >> 4], fg0, bg0); rb_put((uint8_t) hexd[b & 15], fg0, bg0);
        }
        rb_put(' ', fg0, bg0);
    }
    rb_put(' ', fg0, bg0);
    for (i = 0; i < bpr; i++) {
        a = off + i; cur = a == pos;
        if (!have(a)) rb_put((uint8_t)(cur ? '_' : ' '), cur ? BLACK : fg0, cur ? YELLOW : bg0);
        else if (cur && pane == 1) rb_put(w[i], BLACK, YELLOW);
        else if (cur) rb_put(w[i], YELLOW, DGREY);
        else rb_put(w[i], fg0, bg0);
    }
    rb_fill(fg0, bg0);
    rb_out((uint8_t)(1 + r));
}
static void draw_header(void)
{
    uint8_t i;
    rb_str(" HEXED  ", bg0, fg0);
    if (memmode) rb_str("memory", bg0, fg0);
    else { for (i = 0; name[i] && i < 30; i++) rb_put((uint8_t) name[i], bg0, fg0); }
    rb_str("   at $", bg0, fg0); rb_hex(pos, 7, bg0, fg0);
    if (!memmode) { rb_str("  size $", bg0, fg0); rb_hex(size, 6, bg0, fg0); }
    if (modified) rb_str("  *changed*", RED, fg0);
    rb_fill(bg0, fg0);
    rb_out(0);
}
static void draw_footer(void)
{
    if (msg[0]) rb_str(msg, bg0, fg0);
    else if (cols >= 75) rb_str(" TAB side  F5 go to  F3 find  F2 save  ^Z undo  F1 more  ESC leave", bg0, fg0);
    else rb_str(" TAB F5 go F3 find F2 save ESC", bg0, fg0);
    rb_fill(bg0, fg0);
    rb_out((uint8_t)(rows - 1));
}
static void redraw(void)
{
    uint8_t r;
    dma_copy(base + top_off, (uint32_t)(uint16_t) win, (uint32_t) bpr * nrows);
    draw_header();
    for (r = 0; r < nrows; r++) draw_row(r);
    draw_footer();
}

/* ---- moving ------------------------------------------------------------- */
static uint32_t last_pos(void) { return memmode ? 0x0FFFFFFFUL : size; }   /* a file's: one past the end, to add a byte */
static void go(uint32_t p)
{
    uint32_t span = (uint32_t) bpr * nrows;
    if ((int32_t) p < 0) p = 0;
    if (p > last_pos()) p = last_pos();
    pos = p;
    if (pos < top_off) top_off = pos - pos % bpr;
    else if (pos >= top_off + span) top_off = pos - pos % bpr - (span - bpr);
}
static void scroll(int8_t lines_)
{
    uint32_t lim = last_pos() - last_pos() % bpr, d = (uint32_t)(lines_ < 0 ? -lines_ : lines_) * bpr;
    if (lines_ < 0) top_off = top_off > d ? top_off - d : 0;
    else { top_off += d; if (top_off > lim) top_off = lim; }
}

/* ---- editing, and undoing it -------------------------------------------- */
static uint32_t upos[256];                   /* bit 31: this edit added the byte at the end */
static uint8_t uold[256], un, ucount;
static void set_byte(uint8_t v)
{
    uint32_t u = pos;
    if (!memmode && pos >= size) {
        if (size >= MAXSZ) { strcpy(msg, " the file is at 8 MB, as big as HEXED goes"); return; }
        far_poke(base + pos, 0); size++; u |= 0x80000000UL;
    }
    upos[un] = u; uold[un] = far_peek(base + pos); un++; if (ucount < 255) ucount++;
    far_poke(base + pos, v);
    modified = 1;
}
static void undo(void)
{
    uint32_t u, p;
    if (!ucount) { strcpy(msg, " nothing to undo"); return; }
    un--; ucount--;
    u = upos[un]; p = u & 0x0FFFFFFFUL;
    if (u & 0x80000000UL) size = p; else far_poke(base + p, uold[un]);
    nib = 0; go(p);
}

/* ---- the footer as a question ------------------------------------------- */
static uint8_t ask(const char *q, char *buf, uint8_t max)
{
    uint8_t n = 0;
    uint16_t k;
    buf[0] = 0;
    for (;;) {
        rb_str(q, bg0, fg0); rb_str(buf, bg0, fg0); rb_put('_', YELLOW, fg0);
        rb_fill(bg0, fg0); rb_out((uint8_t)(rows - 1));
        while ((k = getkey()) == 0) ;
        if (k == 13) return 1;
        if (k == 0x1B) return 0;
        if ((k == 8 || k == 0x7F || k == 0x14 || k == K_DEL) && n) buf[--n] = 0;
        else if (k >= 0x20 && k < 0x100 && n < max) { buf[n++] = (char) k; buf[n] = 0; }
    }
}
static int8_t hexval(char c)
{
    if (c >= '0' && c <= '9') return (int8_t)(c - '0');
    if (c >= 'a' && c <= 'f') return (int8_t)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (int8_t)(c - 'A' + 10);
    return -1;
}
static uint8_t parse_hex(const char *s, uint32_t *v)
{
    uint8_t n = 0; int8_t d;
    *v = 0;
    while (*s == ' ') s++;
    if (*s == '$') s++;
    while ((d = hexval(*s)) >= 0) { *v = (*v << 4) | (uint8_t) d; s++; n++; }
    return n;
}

/* ---- finding ------------------------------------------------------------ */
static uint8_t pat[32], plen, chunk[256];
static uint8_t find_in(uint32_t from, uint32_t end, uint32_t *at)
{
    uint32_t c;
    uint16_t n, i;
    uint8_t j;
    for (c = from; c < end; c += 256 - (plen - 1)) {
        n = end - c > 256 ? 256 : (uint16_t)(end - c);
        if (n < plen) break;
        dma_copy(base + c, (uint32_t)(uint16_t) chunk, n);
        for (i = 0; i + plen <= n; i++) {
            for (j = 0; j < plen && chunk[i + j] == pat[j]; j++) ;
            if (j == plen) { *at = c + i; return 1; }
        }
        if (n < 256) break;
    }
    return 0;
}
static void find_next(void)
{
    uint32_t at, end;
    uint8_t ok;
    strcpy(msg, " looking..."); draw_footer();
    if (memmode) {
        end = pos + 0x01000000UL; if (end > 0x10000000UL) end = 0x10000000UL;
        ok = find_in(pos + 1, end, &at);
    } else {
        ok = find_in(pos + 1, size, &at);
        if (!ok) ok = find_in(0, pos + plen < size ? pos + plen : size, &at);
    }
    if (ok) { msg[0] = 0; nib = 0; go(at); }
    else strcpy(msg, memmode ? " not in the next 16 MB" : " not found");
}
static void find_ask(void)
{
    static char q[65];
    const char *s;
    int8_t hi, lo;
    if (!ask(pane ? " find text: " : " find hex: ", q, pane ? 32 : 64) || !q[0]) return;
    plen = 0;
    if (pane) { for (s = q; *s && plen < 32; s++) pat[plen++] = (uint8_t) *s; }
    else for (s = q; *s && plen < 32; ) {
        while (*s == ' ') s++;
        if (!*s) break;
        hi = hexval(s[0]); lo = s[1] ? hexval(s[1]) : -1;
        if (hi < 0) { strcpy(msg, " hex: pairs of 0-9 A-F, like DE AD BE EF"); plen = 0; return; }
        if (lo < 0) { pat[plen++] = (uint8_t) hi; s++; }
        else { pat[plen++] = (uint8_t)(hi << 4 | lo); s += 2; }
    }
    if (plen) find_next();
}

/* ---- saving ------------------------------------------------------------- */
static void save(void)
{
    if (memmode) { strcpy(msg, " memory is changed as you type: nothing to save"); return; }
    fs_w32(4, (uint16_t) name); fs_w32(8, HBUF); fs_w32(12, size);
    if (fs_cmd(10)) { strcpy(msg, " could not save"); return; }
    modified = 0; strcpy(msg, " saved");
}

/* ---- the mouse ---------------------------------------------------------- */
static uint8_t bwas;
static uint8_t mouse(void)                   /* nonzero: something to redraw */
{
    uint16_t x = REG(MOUSEX) | ((uint16_t) REG(MOUSEX + 1) << 8);
    int16_t y = (int16_t)(REG(MOUSEY) | ((uint16_t) REG(MOUSEY + 1) << 8));
    int16_t sy = (int16_t)(REG(0xD014) | ((uint16_t) REG(0xD015) << 8));   /* the console layer's SCROLLY: the HD modes centre it */
    uint8_t ch = (REG(0xD010) & 0x60) ? 16 : 8, b = REG(MOUSEB), r, c, i, redraw_ = 0;
    int8_t w = (int8_t) REG(MOUSEW);
    int16_t ty = y + sy;
    uint16_t rel;
    r = ty < 0 ? 0xFF : (uint8_t)(ty / ch - oy);
    c = (uint8_t)(x / 8 - ox);
    if (r >= rows || c >= cols) r = c = 0xFF;
    if (r != mrow || c != mcol) { mrow = r; mcol = c; redraw_ = 1; }
    if (w) { scroll((int8_t)(w > 0 ? -3 : 3)); redraw_ = 1; }
    if ((b & 1) && !(bwas & 1) && r >= 1 && r <= nrows) {
        uint32_t row = top_off + (uint32_t)(r - 1) * bpr;
        if (c >= 9 && c < tx - 1) {
            rel = c - 9;
            if (gap && rel >= 24) rel--;
            i = (uint8_t)(rel / 3);
            if (i < bpr && rel % 3 < 2) { pane = 0; nib = (uint8_t)(rel % 3); go(row + i); redraw_ = 1; }
        } else if (c >= tx && c < tx + bpr) {
            pane = 1; nib = 0; go(row + (c - tx)); redraw_ = 1;
        }
    }
    bwas = b;
    return redraw_;
}

/* ---- the keys ----------------------------------------------------------- */
static uint8_t key(uint16_t k)               /* nonzero: leave */
{
    static char q[20];
    uint32_t v, rowstart;
    int8_t d;
    msg[0] = 0;
    switch (k) {
    case K_LEFT:  if (pane == 0 && nib) nib = 0; else if (pos) { go(pos - 1); nib = (uint8_t)(pane == 0 && have(pos)); } return 0;
    case K_RIGHT: if (pane == 0 && !nib && have(pos)) nib = 1; else { go(pos + 1); nib = 0; } return 0;
    case K_UP:    if (pos >= bpr) go(pos - bpr); return 0;
    case K_DOWN:  go(pos + bpr); return 0;
    case K_PGUP:  v = (uint32_t) bpr * nrows; go(pos > v ? pos - v : 0); scroll((int8_t) -(int8_t) nrows); go(pos); return 0;
    case K_PGDN:  v = (uint32_t) bpr * nrows; go(pos + v); return 0;
    case K_HOME:  rowstart = pos - pos % bpr; nib = 0; go(pos == rowstart ? 0 : rowstart); return 0;
    case K_END:   rowstart = pos - pos % bpr; nib = 0;
                  go(pos == rowstart + bpr - 1 || pos == last_pos() ? last_pos() : rowstart + bpr - 1); return 0;
    case 9:       pane ^= 1; nib = 0; return 0;
    case K_F1:    strcpy(msg, cols >= 75 ? " Home/End twice: the file's ends  PgUp PgDn  wheel scrolls  click picks a byte"
                                         : " Home/End x2: ends  ^Z undo"); return 0;
    case K_F2: case 0x13: save(); return 0;
    case K_F5: case 0x07:
        if (ask(" go to $", q, 8) && parse_hex(q, &v)) { nib = 0; go(v); }
        return 0;
    case K_F3: if (plen) { find_next(); return 0; } /* else as Ctrl-F */
    case 0x06: find_ask(); return 0;
    case 0x1A: undo(); return 0;
    case 0x1B:
        if (!modified) return 1;
        rb_str(" save the changes?  Y yes  N no  ESC stay", bg0, fg0); rb_fill(bg0, fg0); rb_out((uint8_t)(rows - 1));
        for (;;) {
            while ((k = getkey()) == 0) ;
            if (k == 'y' || k == 'Y') { save(); return !modified; }
            if (k == 'n' || k == 'N') return 1;
            if (k == 0x1B) return 0;
        }
    }
    if (k >= 0x100) return 0;
    if (pane == 0) {
        d = hexval((char) k);
        if (d < 0) return 0;
        v = have(pos) ? far_peek(base + pos) : 0;
        v = nib ? (v & 0xF0) | (uint8_t) d : (v & 0x0F) | ((uint8_t) d << 4);
        set_byte((uint8_t) v);
        if (nib) { nib = 0; go(pos + 1); } else nib = 1;
    } else if (k >= 0x20 || k == 0) {
        set_byte((uint8_t) k); go(pos + 1);
    }
    return 0;
}

void main(void)
{
    const char *p;
    uint8_t i = 0, q = 0, lf, f;
    uint16_t k;
    uint32_t v;

    rom_args();
    p = *(const char **) 0xF0;
    while (*p == ' ') p++;
    if (!*p) { say("usage: HEXED NAME   or   HEXED $ADDRESS\n"); SHELL_RC = 1; return; }
    if (*p == '$') {
        if (!parse_hex(p, &v)) { say("hexed: $ADDRESS in hex, like $30000\n"); SHELL_RC = 1; return; }
        memmode = 1; base = 0; pos = v & 0x0FFFFFFFUL;
    } else {
        if (*p == '"') { q = 1; p++; }
        while (*p && i < sizeof name - 1 && (q ? *p != '"' : *p != ' ')) name[i++] = *p++;
        name[i] = 0;
        base = HBUF;
        fs_w32(4, (uint16_t) name);
        if (fs_cmd(8)) { size = 0; strcpy(msg, " a new file: type to fill it, F2 saves"); }
        else {
            size = fs_r32(0x10);
            if (size > MAXSZ) { say("hexed: the file is over 8 MB\n"); SHELL_RC = 1; return; }
            fs_w32(4, (uint16_t) name); fs_w32(8, HBUF); fs_w32(12, MAXSZ);
            if (fs_cmd(9)) { say("hexed: could not read it\n"); SHELL_RC = 1; return; }
        }
    }

    cols = REG(TERM + 5); rows = REG(TERM + 6);
    ox = REG(TERM + 7);   oy = REG(TERM + 8);
    stride = REG(TERM + 0x0D);
    fg0 = REG(TERM + 0x14); bg0 = REG(TERM + 0x15);
    if (fg0 == bg0) { fg0 = WHITE; bg0 = BLUE; }
    if (!cols) cols = 80;
    if (!rows) rows = 25;
    if (!stride) stride = cols;
    bpr = cols >= 75 ? 16 : cols >= 42 ? 8 : 4;
    gap = bpr == 16;
    tx = (uint8_t)(9 + bpr * 3 + gap + 1);
    nrows = (uint8_t)(rows - 2);
    if ((uint16_t) nrows * bpr > sizeof win) nrows = (uint8_t)(sizeof win / bpr);
    flags_was = REG(TERM + 0x0E);
    REG(TERM + 0x0E) = (uint8_t)(flags_was & ~1);   /* no blinking cursor over ours */
    top_off = pos - pos % bpr; go(pos);                     /* the address asked for on the top line */

    redraw();
    lf = REG(SYS + 0x0D);
    for (;;) {
        k = getkey();
        f = 0;
        if (k) { if (key(k)) break; f = 1; }
        if (mouse()) f = 1;
        if (memmode && (uint8_t)(REG(SYS + 0x0D) - lf) >= 15) { lf = REG(SYS + 0x0D); f = 1; }   /* memory moves by itself: look again */
        if (f) redraw();
    }
    REG(TERM + 0x0E) = flags_was;
    rom_chrout(12);
}
