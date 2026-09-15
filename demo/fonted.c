/* K4510: FONTED -- the machine's font, edited where it lives.
 *
 *   FONTED             the fonts in memory now
 *   FONTED NAME.FNT    that font file, loaded, then edited (Ctrl-S saves it back)
 *   FONTED -L NAME     load a font file and leave -- a line for STARTUP.BAT
 *
 * The screen reads its glyphs from memory -- 8x8 at $010000, 8x16 at
 * $010800 -- so an edit shows at once, everywhere, the grid of all 256
 * included.  A .FNT is both fonts as they lie there, 6144 bytes; a file of
 * 2048 or 4096 bytes is taken as the 8x8 or the 8x16 alone.  The fonts are
 * the host's again after a power-cycle, so a font of your own wants
 * FONTED -L in STARTUP.BAT.
 *
 *   arrows the pixel   Space flip it   [ ] PgUp PgDn another character
 *   Tab 8x8 / 8x16   G go to a code   the mouse: pick a character, paint pixels
 *   I invert   H mirror   V flip   W A S D shift   X clear   F fit from the other size
 *   C copy   P paste   U undo   Ctrl-S save   Ctrl-O open   Esc leave
 *
 * Each character's Unicode value comes from core/codepage.h, the table the
 * rest of the machine uses.
 */
#include "k4510.h"
#include "../core/codepage.h"

#define TERM    0xDA00u
#define SCREEN  0x00030000UL
#define FS      0xD300u
#define FONT_8  0x00010000UL
#define FONT_16 0x00010800UL
#define ORIG    0x0E000000UL                 /* both fonts as FONTED found them (far memory nothing else uses) */
#define MOUSEX  0xD108u
#define MOUSEY  0xD10Au
#define MOUSEB  0xD10Cu
#define SHELL_RC (*(volatile uint8_t *)0x03FF)

void __fastcall__ rom_chrout(unsigned char c);
static unsigned char rom_args(void) { return ((unsigned char (*)(void))0xFF95)(); }

enum { BLACK, WHITE, RED, CYAN, PURPLE, GREEN, BLUE, YELLOW, ORANGE, BROWN, LRED, DGREY, GREY, LGREEN, LBLUE, LGREY };
#define KY(k)   (0x100u | (k))
#define K_UP    KY(0x80)
#define K_DOWN  KY(0x81)
#define K_LEFT  KY(0x82)
#define K_RIGHT KY(0x83)
#define K_PGUP  KY(0x86)
#define K_PGDN  KY(0x87)
#define K_DEL   KY(0x89)
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

static char fname[64];
static uint8_t code = 'A', tall, px, py, modified, g[16], ubuf[16], ucode, utall, uhave, cbuf[16], ctall, chave;
static uint8_t cols, rows, ox, oy, stride, fg0, bg0, flags_was, gw, ex, mrow = 0xFF, mcol = 0xFF;
static char msg[80];
static const char hexd[] = "0123456789ABCDEF";

static uint8_t hgt(void) { return tall ? 16 : 8; }
static uint32_t gaddr(uint8_t c, uint8_t t) { return t ? FONT_16 + (uint16_t) c * 16 : FONT_8 + (uint16_t) c * 8; }
static void fetch(void) { dma_copy(gaddr(code, tall), (uint32_t)(uint16_t) g, hgt()); }
static void store(void) { dma_copy((uint32_t)(uint16_t) g, gaddr(code, tall), hgt()); modified = 1; }
static void keep(void) { memcpy(ubuf, g, 16); ucode = code; utall = tall; uhave = 1; }   /* before a change: for U */

/* ---- file --------------------------------------------------------------- */
static uint8_t load_font(void)               /* 0 done; 1 absent; 2 the wrong size */
{
    uint32_t size, at;
    fs_w32(4, (uint16_t) fname);
    if (fs_cmd(8)) return 1;
    size = fs_r32(0x10);
    if (size == 6144) at = FONT_8; else if (size == 2048) at = FONT_8; else if (size == 4096) at = FONT_16; else return 2;
    fs_w32(4, (uint16_t) fname); fs_w32(8, at); fs_w32(12, size);
    return fs_cmd(9) ? 1 : 0;
}
static void save_font(void)
{
    fs_w32(4, (uint16_t) fname); fs_w32(8, FONT_8); fs_w32(12, 6144);
    if (fs_cmd(10)) strcpy(msg, " could not save");
    else { modified = 0; strcpy(msg, " saved: both sizes, 6144 bytes"); }
}

/* ---- screen ------------------------------------------------------------- */
static uint8_t rb[80 * 4], rn;
static void rb_put(uint8_t glyph, uint8_t fg, uint8_t bg)
{
    uint8_t *q = rb + rn * 4;
    if (rn >= 80 || rn >= cols) return;
    q[0] = glyph; q[1] = 0; q[2] = fg; q[3] = bg; rn++;
}
static void rb_str(const char *s, uint8_t fg, uint8_t bg) { while (*s) rb_put((uint8_t) *s++, fg, bg); }
static void rb_to(uint8_t c) { while (rn < c && rn < cols) rb_put(' ', fg0, bg0); }
static void rb_out(uint8_t r, uint8_t bar)
{
    while (rn < cols && rn < 80) rb_put(' ', bar ? bg0 : fg0, bar ? fg0 : bg0);
    if (r == mrow && mcol < rn) { rb[mcol * 4 + 2] = WHITE; rb[mcol * 4 + 3] = LBLUE; }
    if (r < rows) dma_copy((uint32_t)(uint16_t) rb, SCREEN + ((uint32_t)(r + oy) * stride + ox) * 4, (uint32_t) rn * 4);
    rn = 0;
}
static void rb_hex(uint16_t v, uint8_t digits, uint8_t fg, uint8_t bg) { while (digits--) rb_put((uint8_t) hexd[(v >> (digits * 4)) & 15], fg, bg); }
static void rb_dec(uint8_t v, uint8_t fg, uint8_t bg)
{
    if (v >= 100) rb_put((uint8_t)('0' + v / 100), fg, bg);
    if (v >= 10) rb_put((uint8_t)('0' + v / 10 % 10), fg, bg);
    rb_put((uint8_t)('0' + v % 10), fg, bg);
}

static void redraw(void)
{
    uint8_t r, c, ch, on, i;
    rb_str(" FONTED  ", bg0, fg0);
    rb_str(fname[0] ? fname : "the fonts in memory", bg0, fg0);
    rb_str("   editing ", bg0, fg0); rb_str(tall ? "8x16" : "8x8", bg0, fg0);
    if (modified) rb_str("   *changed*", RED, fg0);
    rb_out(0, 1);
    rb_str("   ", GREY, bg0);
    for (c = 0; c < 16; c++) { rb_put((uint8_t) hexd[c], GREY, bg0); if (gw == 2) rb_put(' ', GREY, bg0); }
    rb_to(ex); rb_str(tall ? "8x16" : "8x8", GREY, bg0);
    rb_out(1, 0);
    for (r = 0; r < 16; r++) {
        rb_put(' ', GREY, bg0); rb_put((uint8_t) hexd[r], GREY, bg0); rb_put(' ', GREY, bg0);
        for (c = 0; c < 16; c++) {
            ch = (uint8_t)(r * 16 + c);
            on = ch == code;
            rb_put(ch, on ? BLACK : fg0, on ? YELLOW : bg0);
            if (gw == 2) rb_put(' ', fg0, bg0);
        }
        rb_to(ex);
        if (r < hgt())
            for (i = 0; i < 8; i++) {
                on = (g[r] >> (7 - i)) & 1;
                if (r == py && i == px) { rb_put(on ? 0xDB : 0xFA, on ? BLACK : BLACK, YELLOW); rb_put(on ? 0xDB : 0xFA, BLACK, YELLOW); }
                else if (on) { rb_put(0xDB, WHITE, bg0); rb_put(0xDB, WHITE, bg0); }
                else { rb_put(0xFA, DGREY, bg0); rb_put(' ', DGREY, bg0); }
            }
        rb_out((uint8_t)(2 + r), 0);
    }
    rb_str(" $", GREY, bg0); rb_hex(code, 2, WHITE, bg0); rb_put(' ', fg0, bg0); rb_dec(code, WHITE, bg0);
    rb_str("  U+", GREY, bg0); rb_hex(k4510_cp[code], 4, WHITE, bg0);
    rb_str("   in a line: ", GREY, bg0); rb_put('a', fg0, bg0); rb_put(code, fg0, bg0); rb_put(code, fg0, bg0); rb_put('b', fg0, bg0);
    rb_str(REG(0xD010) & 0x60 ? "   (this MODE shows 8x16)" : "   (this MODE shows 8x8)", DGREY, bg0);
    rb_out(18, 0);
    if (cols >= 60) {
        rb_str(" I invert  H mirror  V flip  WASD shift  X clear  F fit from the other size", GREY, bg0); rb_out(19, 0);
        rb_str(" C copy  P paste  U undo  G go to  Tab 8x8/8x16  Ctrl-S save  Ctrl-O open", GREY, bg0); rb_out(20, 0);
    } else {                                   /* MODE 7's 45 columns */
        rb_str(" I inv H mir V flip WASD shift X clr F fit", GREY, bg0); rb_out(19, 0);
        rb_str(" C/P copy U undo G go Tab size ^S ^O", GREY, bg0); rb_out(20, 0);
    }
    if (msg[0]) rb_str(msg, bg0, fg0);
    else if (cols >= 60) rb_str(" arrows + Space: pixels   [ ] PgUp PgDn: another character   Esc leave", bg0, fg0);
    else rb_str(" arrows Space pixels [ ] PgUp/Dn Esc", bg0, fg0);
    rb_out((uint8_t)(rows - 1), 1);
}

static uint8_t ask(const char *q, char *buf, uint8_t max)
{
    uint8_t n = (uint8_t) strlen(buf);
    uint16_t k;
    for (;;) {
        rb_str(q, bg0, fg0); rb_str(buf, bg0, fg0); rb_put('_', YELLOW, fg0);
        rb_out((uint8_t)(rows - 1), 1);
        while ((k = getkey()) == 0) ;
        if (k == 13) return n != 0;
        if (k == 0x1B) return 0;
        if ((k == 8 || k == 0x7F || k == 0x14 || k == K_DEL) && n) buf[--n] = 0;
        else if (k >= 0x20 && k < 0x7F && n < max) { buf[n++] = (char) k; buf[n] = 0; }
    }
}

/* ---- the operations ----------------------------------------------------- */
static void choose(uint8_t c) { code = c; fetch(); }
static void op(uint8_t k)
{
    uint8_t i, h = hgt(), t, b, o[16];
    keep();
    switch (k) {
    case 'I': for (i = 0; i < h; i++) g[i] = (uint8_t) ~g[i]; break;
    case 'H': for (i = 0; i < h; i++) { b = g[i]; t = 0; for (k = 0; k < 8; k++) if (b & (1 << k)) t |= (uint8_t)(0x80 >> k); g[i] = t; } break;
    case 'V': for (i = 0; i < h / 2; i++) { t = g[i]; g[i] = g[h - 1 - i]; g[h - 1 - i] = t; } break;
    case 'W': for (i = 0; i + 1 < h; i++) g[i] = g[i + 1]; g[h - 1] = 0; break;
    case 'S': for (i = h - 1; i > 0; i--) g[i] = g[i - 1]; g[0] = 0; break;
    case 'A': for (i = 0; i < h; i++) g[i] = (uint8_t)(g[i] << 1); break;
    case 'D': for (i = 0; i < h; i++) g[i] = (uint8_t)(g[i] >> 1); break;
    case 'X': memset(g, 0, h); break;
    case 'F':                                  /* this size from the other: rows doubled, or every other row */
        dma_copy(gaddr(code, (uint8_t) !tall), (uint32_t)(uint16_t) o, tall ? 8 : 16);
        if (tall) for (i = 0; i < 16; i++) g[i] = o[i / 2];
        else for (i = 0; i < 8; i++) g[i] = o[i * 2] | o[i * 2 + 1];
        break;
    case 'P':
        if (!chave) { strcpy(msg, " C copies a character first"); return; }
        if (ctall == tall) memcpy(g, cbuf, 16);
        else if (tall) for (i = 0; i < 16; i++) g[i] = cbuf[i / 2];
        else for (i = 0; i < 8; i++) g[i] = cbuf[i * 2] | cbuf[i * 2 + 1];
        break;
    }
    store();
}
static void undo(void)
{
    uint8_t t[16], c = code, tl = tall;
    if (!uhave) { strcpy(msg, " nothing to undo"); return; }
    code = ucode; tall = utall; fetch();
    memcpy(t, g, 16); memcpy(g, ubuf, 16); memcpy(ubuf, t, 16);   /* U again: redo */
    store();
    if (c != code || tl != tall) strcpy(msg, " undone -- on the character it was done to");
}

/* ---- the mouse ---------------------------------------------------------- */
static uint8_t bwas, paint;
static uint8_t mouse(void)
{
    uint16_t x = REG(MOUSEX) | ((uint16_t) REG(MOUSEX + 1) << 8);
    int16_t y = (int16_t)(REG(MOUSEY) | ((uint16_t) REG(MOUSEY + 1) << 8));
    int16_t sy = (int16_t)(REG(0xD014) | ((uint16_t) REG(0xD015) << 8)), ty;
    uint8_t ch = (REG(0xD010) & 0x60) ? 16 : 8, b = REG(MOUSEB), r, c, f = 0, i, bit;
    ty = y + sy;
    r = ty < 0 ? 0xFF : (uint8_t)(ty / ch - oy);
    c = (uint8_t)(x / 8 - ox);
    if (r >= rows || c >= cols) r = c = 0xFF;
    if (r != mrow || c != mcol) { mrow = r; mcol = c; f = 1; }
    if ((b & 1) && r >= 2 && r < 18) {
        if (!(bwas & 1) && c >= 3 && c < 3 + 16 * gw) { choose((uint8_t)((r - 2) * 16 + (c - 3) / gw)); f = 1; }
        else if (c >= ex && c < ex + 16 && r - 2 < hgt()) {
            i = (uint8_t)((c - ex) / 2); px = i; py = (uint8_t)(r - 2); bit = (uint8_t)(0x80 >> i);
            if (!(bwas & 1)) { keep(); paint = !(g[py] & bit); }
            if (paint) g[py] |= bit; else g[py] &= (uint8_t) ~bit;
            store(); f = 1;
        }
    }
    bwas = b;
    return f;
}

static uint8_t leave(void)
{
    uint16_t k;
    if (!modified) return 1;
    rb_str(" changed: Y save  N leave them in memory  R put back as it was  Esc stay", bg0, fg0); rb_out((uint8_t)(rows - 1), 1);
    for (;;) {
        while ((k = getkey()) == 0) ;
        if (k == 'y' || k == 'Y') { if (!fname[0]) strcpy(fname, "/HOME/MY.FNT"); if (ask(" save as: ", fname, 60)) save_font(); return !modified; }
        if (k == 'n' || k == 'N') return 1;
        if (k == 'r' || k == 'R') { dma_copy(ORIG, FONT_8, 6144); return 1; }
        if (k == 0x1B) return 0;
    }
}
static uint8_t key(uint16_t k)
{
    static char q[4];
    uint8_t c, n;
    msg[0] = 0;
    switch (k) {
    case K_UP:    if (py) py--; return 0;
    case K_DOWN:  if (py + 1 < hgt()) py++; return 0;
    case K_LEFT:  if (px) px--; return 0;
    case K_RIGHT: if (px < 7) px++; return 0;
    case ' ':     keep(); g[py] ^= (uint8_t)(0x80 >> px); store(); return 0;
    case '[':     choose((uint8_t)(code - 1)); return 0;
    case ']':     choose((uint8_t)(code + 1)); return 0;
    case K_PGUP:  choose((uint8_t)(code - 16)); return 0;
    case K_PGDN:  choose((uint8_t)(code + 16)); return 0;
    case 9:       tall ^= 1; if (py >= hgt()) py = (uint8_t)(hgt() - 1); fetch(); return 0;
    case 0x13:    if (!fname[0]) strcpy(fname, "/HOME/MY.FNT"); if (ask(" save as: ", fname, 60)) save_font(); return 0;
    case 0x0F:
        if (ask(" open: ", fname, 60)) {
            n = load_font();
            if (n == 1) strcpy(msg, " no such file"); else if (n == 2) strcpy(msg, " a font is 6144, 2048 or 4096 bytes");
            else { modified = 0; strcpy(msg, " loaded -- and on the screen already"); }
            fetch();
        }
        return 0;
    case 0x1B:    return leave();
    }
    if (k >= 0x100) return 0;
    c = (uint8_t) k; if (c >= 'a' && c <= 'z') c = (uint8_t)(c - 32);
    switch (c) {
    case 'I': case 'H': case 'V': case 'W': case 'A': case 'S': case 'D': case 'X': case 'F': case 'P': op(c); break;
    case 'C': memcpy(cbuf, g, 16); ctall = tall; chave = 1; strcpy(msg, " copied"); break;
    case 'U': undo(); break;
    case 'G':
        q[0] = 0;
        if (ask(" go to $", q, 2)) {
            n = 0;
            for (c = 0; q[c]; c++) { k = (uint8_t) q[c]; if (k >= 'a') k -= 32; n = (uint8_t)(n * 16 + (k <= '9' ? k - '0' : k - 'A' + 10)); }
            choose(n);
        }
        break;
    }
    return 0;
}

void main(void)
{
    const char *p;
    uint8_t i = 0, q = 0, only_load = 0, f, e;
    uint16_t k;

    rom_args();
    p = *(const char **) 0xF0;
    while (*p == ' ') p++;
    if ((p[0] == '-') && (p[1] == 'L' || p[1] == 'l') && (p[2] == ' ' || !p[2])) { only_load = 1; p += 2; while (*p == ' ') p++; }
    if (*p == '"') { q = 1; p++; }
    while (*p && i < 60 && (q ? *p != '"' : *p != ' ')) fname[i++] = *p++;
    fname[i] = 0;
    if (only_load) {
        if (!i) { say("usage: FONTED -L NAME.FNT\n"); SHELL_RC = 1; return; }
        e = load_font();
        if (e) { say(e == 1 ? "fonted: no such file\n" : "fonted: a font is 6144, 2048 or 4096 bytes\n"); SHELL_RC = 1; }
        return;
    }
    dma_copy(FONT_8, ORIG, 6144);
    if (i) {
        e = load_font();
        if (e == 1) strcpy(msg, " a new file: Ctrl-S saves the fonts to it");
        else if (e == 2) { say("fonted: a font is 6144, 2048 or 4096 bytes\n"); SHELL_RC = 1; return; }
    }

    cols = REG(TERM + 5); rows = REG(TERM + 6);
    ox = REG(TERM + 7);   oy = REG(TERM + 8);
    stride = REG(TERM + 0x0D);
    fg0 = REG(TERM + 0x14); bg0 = REG(TERM + 0x15);
    if (fg0 == bg0) { fg0 = WHITE; bg0 = BLUE; }
    if (!cols) cols = 80;
    if (!rows) rows = 25;
    if (!stride) stride = cols;
    if (rows < 22) { say("fonted: wants 22 rows or more (MODE 0, 1, 5, 6 or 7)\n"); SHELL_RC = 1; return; }
    gw = cols >= 60 ? 2 : 1;
    ex = (uint8_t)(3 + 16 * gw + 3);
    tall = (REG(0xD010) & 0x60) ? 1 : 0;       /* start on the size this MODE shows */
    flags_was = REG(TERM + 0x0E);
    REG(TERM + 0x0E) = (uint8_t)(flags_was & ~1);
    fetch();

    redraw();
    for (;;) {
        f = 0;
        k = getkey();
        if (k) { if (key(k)) break; f = 1; }
        if (mouse()) f = 1;
        if (f) redraw();
    }
    REG(TERM + 0x0E) = flags_was;
    rom_chrout(12);
}
