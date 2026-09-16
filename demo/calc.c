/* K4510: CALC -- a spreadsheet, the way they are reckoned now.
 *
 *   CALC              a new sheet (saved as SHEET.CAL unless named)
 *   CALC NAME.CAL     that sheet
 *
 * Columns A-Z, rows 1-99.  Type into a cell and it is taken for what it looks
 * like: a number is a number, an = starts a formula, anything else is text.
 * A leading ' forces text (so '2026 stays a year and not a number).
 *
 *     =A1*2    =(B3+B4)/2    =2^10    -C7    =SUM(A1:A9)    =AVERAGE(B1:B5,10)
 *     SUM AVERAGE MIN MAX COUNT over ranges and lists;  ABS INT SQRT
 *     ROUND(x) or ROUND(x,2);  PI().  A range is A1:B3.
 *
 * The sheet works itself out again after every entry.  A formula that cannot
 * be worked out -- a mistake in it, a division by zero, a cell it uses that
 * cannot -- shows #ERROR!.  A number wider than its column shows #####.
 *
 *   arrows PgUp PgDn        move        Enter keep and go down   Tab keep and go right
 *   Home start of the row   Ctrl-Home A1     End last cell used in the row
 *   F2  change the cell's entry          Del  clear the cell     Esc  drop an entry
 *   Ctrl-G go to a cell     Ctrl-S save    Ctrl-O open    Ctrl-N new sheet
 *   Ctrl-W column width     Ctrl-Q quit    F10 the same as a menu
 *
 * The arithmetic is the MATH unit's ($D700, IEEE singles, as LOGO uses it).
 * A sheet is saved as text, a line a cell -- A1:F:=B2*3 -- so EDIT can read
 * one too.  Sheets written by the first CALC (K4CALC 1, VisiCalc's @SUM and
 * A1...B3) are brought over to this spelling as they load.
 */
#include "k4510.h"

#define TERM    0xDA00u
#define SCREEN  0x00030000UL
#define FS      0xD300u
#define MATHR   0xD700u
#define CELLS   0x0E100000UL                 /* 26 x 99 cells (far memory nothing else uses) */
#define LOADBUF 0x0E200000UL
#define LOADMAX 0x00040000UL
#define NCOL    26
#define NROW    99
#define CELLSZ  48                           /* kind, flags, value (4), text (41 + NUL) */
#define TXTMAX  41
#define K_EMPTY 0
#define K_TEXT  1
#define K_VAL   2                            /* a number or a formula: either way it is worked out */
#define SHELL_RC (*(volatile uint8_t *)0x03FF)

void __fastcall__ rom_chrout(unsigned char c);
static unsigned char rom_args(void) { return ((unsigned char (*)(void))0xFF95)(); }

enum { BLACK, WHITE, RED, CYAN, PURPLE, GREEN, BLUE, YELLOW, ORANGE, BROWN, LRED, DGREY, GREY, LGREEN, LBLUE, LGREY };
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
#define K_F2    KY(0x91)
#define K_F10   KY(0x99)
static uint8_t kmods;                        /* shift/ctrl/alt of the key just read: KBDST bits 0-2 */
static uint16_t getkey(void)
{
    uint8_t st = REG(KBDST), k;
    if (!(st & 0x80)) return 0;
    k = REG(KBD);
    kmods = (uint8_t)(REG(KBDST) & 7);       /* the read above latches them */
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

/* ---- the MATH unit (as LOGO drives it) ----------------------------------- */
typedef unsigned long fbits;
static void w32r(uint16_t a, unsigned long v) { REG(a) = (uint8_t) v; REG(a + 1) = (uint8_t)(v >> 8); REG(a + 2) = (uint8_t)(v >> 16); REG(a + 3) = (uint8_t)(v >> 24); }
static unsigned long r32r(uint16_t a) { return (unsigned long) REG(a) | ((unsigned long) REG(a + 1) << 8) | ((unsigned long) REG(a + 2) << 16) | ((unsigned long) REG(a + 3) << 24); }
#define FREG(n) (MATHR + 4 * (n))
#define FOP(op, d, s) do { REG(0xD721u) = (uint8_t)(((d) << 4) | (s)); REG(0xD720u) = (op); } while (0)
static uint8_t err;                          /* the formula being worked out went wrong */
static fbits f2(uint8_t op, fbits a, fbits b)
{
    w32r(FREG(0), a); w32r(FREG(1), b); FOP(op, 0, 1);
    if (REG(0xD722u) & 4) err = 1;           /* NaN or infinity: a division by zero, a root of -1 */
    return r32r(FREG(0));
}
static fbits f1(uint8_t op, fbits a)
{
    w32r(FREG(1), a); FOP(op, 0, 1);
    if (REG(0xD722u) & 4) err = 1;
    return r32r(FREG(0));
}
static fbits fint(long v) { w32r(0xD724u, (unsigned long) v); FOP(MATH_ITOF, 0, 0); return r32r(FREG(0)); }
static long ftoi(fbits a) { w32r(FREG(1), a); FOP(MATH_FTOI, 0, 1); return (long) r32r(0xD724u); }
static int fcmp(fbits a, fbits b)
{
    uint8_t fl; w32r(FREG(0), a); w32r(FREG(1), b); FOP(MATH_CMP, 0, 1); fl = REG(0xD722u);
    return (fl & 1) ? 0 : (fl & 2) ? -1 : 1;
}
static char numbuf[24];
static const char *ftoa(fbits a) { w32r(FREG(1), a); w32r(0xD730u, (unsigned long)(uint16_t) numbuf); FOP(MATH_FTOAR, 0, 1); return numbuf; }
static fbits F0, F10, F100, FBIG, FPI;

/* ---- the sheet ---------------------------------------------------------- */
static uint32_t caddr(uint8_t c, uint8_t r) { return CELLS + ((uint32_t) r * NCOL + c) * CELLSZ; }
static uint8_t kinds[NCOL * NROW];            /* each cell's kind, near: the work passes the empty ones without a DMA */
static uint8_t vb[6];
static fbits cell_value(uint8_t c, uint8_t r, uint8_t *kind)   /* text or an empty cell counts 0 */
{
    fbits v;
    *kind = kinds[(uint16_t) r * NCOL + c];
    if (*kind != K_VAL) return F0;
    dma_copy(caddr(c, r), (uint32_t)(uint16_t) vb, 6);
    if (vb[1]) err = 1;
    v = (fbits) vb[2] | ((fbits) vb[3] << 8) | ((fbits) vb[4] << 16) | ((fbits) vb[5] << 24);
    return v;
}

/* ---- working a formula out: recursive descent over its text -------------- */
static const char *sp;
static fbits expr(void);
static void skip(void) { while (*sp == ' ') sp++; }
static uint8_t is_digit(char c) { return c >= '0' && c <= '9'; }
static uint8_t upper(char c) { return (uint8_t)(c >= 'a' && c <= 'z' ? c - 32 : c); }
static uint8_t cellref(uint8_t *c, uint8_t *r)    /* A1..Z99 at sp: 1 and past it, or 0 and not moved */
{
    uint8_t col = upper(sp[0]), n = 0, i = 1;
    if (col < 'A' || col > 'Z' || !is_digit(sp[1])) return 0;
    while (is_digit(sp[i]) && i < 4) { n = (uint8_t)(n * 10 + (sp[i] - '0')); i++; }
    if (n < 1 || n > NROW) return 0;
    *c = (uint8_t)(col - 'A'); *r = (uint8_t)(n - 1);
    sp += i;
    return 1;
}
/* Is the whole entry a plain number?  This is what decides that 42 and -3.5
 * are numbers while 3 apples is text -- so it reads the text and touches
 * neither the MATH unit nor sp. */
static uint8_t all_number(const char *s)
{
    uint8_t d = 0;
    if (*s == '+' || *s == '-') s++;
    while (is_digit(*s)) { s++; d = 1; }
    if (*s == '.') { s++; while (is_digit(*s)) { s++; d = 1; } }
    if (!d) return 0;
    if (*s == 'E' || *s == 'e') {
        s++;
        if (*s == '+' || *s == '-') s++;
        if (!is_digit(*s)) return 0;
        while (is_digit(*s)) s++;
    }
    return (uint8_t)(*s == 0);
}
static fbits number(void)
{
    long m = 0;
    int8_t scale = 0, e = 0, es = 1;
    uint8_t nd = 0;
    fbits v;
    while (is_digit(*sp)) { if (nd < 9) { m = m * 10 + (*sp - '0'); if (m) nd++; } else scale++; sp++; }
    if (*sp == '.') { sp++; while (is_digit(*sp)) { if (nd < 9) { m = m * 10 + (*sp - '0'); if (m) nd++; scale--; } sp++; } }
    if ((*sp == 'E' || *sp == 'e') && (is_digit(sp[1]) || ((sp[1] == '+' || sp[1] == '-') && is_digit(sp[2])))) {
        sp++;
        if (*sp == '-') { es = -1; sp++; } else if (*sp == '+') sp++;
        while (is_digit(*sp)) { if (e < 40) e = (int8_t)(e * 10 + (*sp - '0')); sp++; }
        scale = (int8_t)(scale + es * e);
    }
    v = fint(m);
    if (scale) v = f2(MATH_MUL, v, f2(MATH_POW, F10, fint(scale)));
    return v;
}
/* SUM and its kind: every value in the ranges and lists between ( and ) */
static fbits aggregate(uint8_t which)         /* 0 SUM 1 AVERAGE 2 MIN 3 MAX 4 COUNT */
{
    fbits sum = F0, lo = F0, hi = F0, v;
    uint16_t count = 0;
    uint8_t c0, r0, c1, r1, c, r, kind;
    const char *save;
    skip();
    if (*sp != '(') { err = 1; return F0; }
    sp++;
    for (;;) {
        skip();
        save = sp;
        if (cellref(&c0, &r0)) {
            skip();
            if (*sp == ':') sp++;
            else { sp = save; goto single; }
            skip();
            if (!cellref(&c1, &r1)) { err = 1; return F0; }
            if (c1 < c0) { c = c0; c0 = c1; c1 = c; }
            if (r1 < r0) { r = r0; r0 = r1; r1 = r; }
            for (r = r0; r <= r1; r++) for (c = c0; c <= c1; c++) {
                v = cell_value(c, r, &kind);
                if (kind != K_VAL) continue;
                if (!count || fcmp(v, lo) < 0) lo = v;
                if (!count || fcmp(v, hi) > 0) hi = v;
                sum = f2(MATH_ADD, sum, v); count++;
            }
        } else {
single:
            v = expr();
            if (!count || fcmp(v, lo) < 0) lo = v;
            if (!count || fcmp(v, hi) > 0) hi = v;
            sum = f2(MATH_ADD, sum, v); count++;
        }
        skip();
        if (*sp == ',') { sp++; continue; }
        if (*sp == ')') { sp++; break; }
        err = 1; return F0;
    }
    switch (which) {
    case 0: return sum;
    case 1: if (!count) { err = 1; return F0; } return f2(MATH_DIV, sum, fint(count));
    case 2: return lo;
    case 3: return hi;
    default: return fint(count);
    }
}
static fbits function(void)                  /* a name and its brackets: SUM(A1:A9), ROUND(B2,2), PI() */
{
    static const char *const names[11] = { "SUM", "AVERAGE", "AVG", "MIN", "MAX", "COUNT", "ABS", "INT", "SQRT", "ROUND", "PI" };
    static const uint8_t agg[6] = { 0, 1, 1, 2, 3, 4 };   /* AVG is AVERAGE's older, shorter name */
    uint8_t i, n = 0, has_d = 0;
    fbits v, d = F0, p;
    for (i = 0; i < 11; i++) {
        for (n = 0; names[i][n] && upper(sp[n]) == (uint8_t) names[i][n]; n++) ;
        if (!names[i][n]) break;
    }
    if (i == 11) { err = 1; return F0; }
    sp += n;
    if (i < 6) return aggregate(agg[i]);
    if (i == 10) {                           /* PI, with or without its empty brackets */
        skip();
        if (*sp == '(') { sp++; skip(); if (*sp == ')') sp++; else err = 1; }
        return FPI;
    }
    skip();
    if (*sp != '(') { err = 1; return F0; }
    sp++; v = expr(); skip();
    if (i == 9 && *sp == ',') { sp++; d = expr(); skip(); has_d = 1; }   /* ROUND(x, places) */
    if (*sp != ')') { err = 1; return F0; }
    sp++;
    switch (i) {
    case 6: return f1(MATH_ABS, v);
    case 7: return f1(MATH_FLOOR, v);        /* INT goes down, as a spreadsheet's does */
    case 8: return f1(MATH_SQRT, v);
    default:
        if (!has_d) return f1(MATH_ROUND, v);
        p = f2(MATH_POW, F10, d);
        return f2(MATH_DIV, f1(MATH_ROUND, f2(MATH_MUL, v, p)), p);
    }
}
static fbits primary(void)
{
    uint8_t c, r, kind;
    fbits v;
    skip();
    if (is_digit(*sp) || *sp == '.') return number();
    if (*sp == '(') { sp++; v = expr(); skip(); if (*sp == ')') sp++; else err = 1; return v; }
    if (cellref(&c, &r)) return cell_value(c, r, &kind);   /* a reference is a letter and a digit: A1 before ABS */
    if (upper(*sp) >= 'A' && upper(*sp) <= 'Z') return function();
    err = 1;
    return F0;
}
static fbits unary(void)
{
    skip();
    if (*sp == '-') { sp++; return f1(MATH_NEG, unary()); }
    if (*sp == '+') { sp++; return unary(); }
    return primary();
}
static fbits power(void)
{
    fbits v = unary();
    skip();
    if (*sp == '^') { sp++; v = f2(MATH_POW, v, power()); }
    return v;
}
static fbits term(void)
{
    fbits v = power();
    for (;;) {
        skip();
        if (*sp == '*') { sp++; v = f2(MATH_MUL, v, power()); }
        else if (*sp == '/') { sp++; v = f2(MATH_DIV, v, power()); }
        else return v;
    }
}
static fbits expr(void)
{
    fbits v = term();
    for (;;) {
        skip();
        if (*sp == '+') { sp++; v = f2(MATH_ADD, v, term()); }
        else if (*sp == '-') { sp++; v = f2(MATH_SUB, v, term()); }
        else return v;
    }
}

static uint8_t rec[CELLSZ];
static void recalc(void)                     /* twice down the sheet, row by row: a formula above what it uses settles on the second */
{
    uint8_t pass, c, r;
    uint16_t i;
    fbits v;
    for (pass = 0; pass < 2; pass++)
        for (r = 0, i = 0; r < NROW; r++)
            for (c = 0; c < NCOL; c++, i++) {
                if (kinds[i] != K_VAL) continue;
                dma_copy(caddr(c, r), (uint32_t)(uint16_t) rec, CELLSZ);
                err = 0; sp = (const char *) rec + 6;
                if (*sp == '=') sp++;        /* the = is how it was typed, not part of the sum */
                v = expr(); skip();
                if (*sp) err = 1;
                rec[1] = err;
                rec[2] = (uint8_t) v; rec[3] = (uint8_t)(v >> 8); rec[4] = (uint8_t)(v >> 16); rec[5] = (uint8_t)(v >> 24);
                dma_copy((uint32_t)(uint16_t) rec, caddr(c, r), 6);
            }
}
/* An entry is taken for what it looks like: a number is a number, = is a
 * formula, a leading + or - is one too, and everything else is text -- so
 * 3 apples is a note and not a mistake.  A leading ' forces text. */
static void put_cell(uint8_t c, uint8_t r, const char *text)
{
    uint8_t n = 0;
    memset(rec, 0, CELLSZ);
    if (text[0]) {
        if (text[0] == '\'') { rec[0] = K_TEXT; text++; }
        else if (text[0] == '=') rec[0] = K_VAL;
        else if (all_number(text)) rec[0] = K_VAL;
        else if (text[0] == '+' || text[0] == '-') rec[0] = K_VAL;
        else rec[0] = K_TEXT;
        while (text[n] && n < TXTMAX) { rec[6 + n] = (uint8_t) text[n]; n++; }
    }
    dma_copy((uint32_t)(uint16_t) rec, caddr(c, r), CELLSZ);
    kinds[(uint16_t) r * NCOL + c] = rec[0];
}

/* ---- the screen --------------------------------------------------------- */
static uint8_t cols, rows, ox, oy, stride, fg0, bg0, flags_was, cw = 9, fmt2, modified;
static uint8_t cc, cr, lc, tr, vc, vr;       /* the cursor, the view's left column and top row, how many show */
static char fname[64], msg[80], ebuf[TXTMAX + 2];
static uint8_t entering, en, only_entry;       /* only_entry: the key changed the entry line and nothing else */
static uint8_t rb[80 * 4], rn;
static void rb_put(uint8_t glyph, uint8_t fg, uint8_t bg)
{
    uint8_t *q = rb + rn * 4;
    if (rn >= 80 || rn >= cols) return;
    q[0] = glyph; q[1] = 0; q[2] = fg; q[3] = bg; rn++;
}
static void rb_str(const char *s, uint8_t fg, uint8_t bg) { while (*s) rb_put((uint8_t) *s++, fg, bg); }
static void rb_out(uint8_t r, uint8_t bar)
{
    while (rn < cols && rn < 80) rb_put(' ', bar ? bg0 : fg0, bar ? fg0 : bg0);
    if (r < rows) dma_copy((uint32_t)(uint16_t) rb, SCREEN + ((uint32_t)(r + oy) * stride + ox) * 4, (uint32_t) rn * 4);
    rn = 0;
}
static char cname[4];
static const char *cell_name(uint8_t c, uint8_t r)
{
    cname[0] = (char)('A' + c);
    if (r >= 9) { cname[1] = (char)('0' + (r + 1) / 10); cname[2] = (char)('0' + (r + 1) % 10); cname[3] = 0; }
    else { cname[1] = (char)('1' + r); cname[2] = 0; }
    return cname;
}
static char shown[24];
static const char *show_value(const uint8_t *cell)   /* the value as the column shows it */
{
    fbits v = (fbits) cell[2] | ((fbits) cell[3] << 8) | ((fbits) cell[4] << 16) | ((fbits) cell[5] << 24);
    long n;
    uint8_t i = 0, neg;
    char t[12];
    if (cell[1]) return "#ERROR!";
    if (fmt2 && fcmp(f1(MATH_ABS, v), FBIG) < 0) {
        n = ftoi(f1(MATH_ROUND, f2(MATH_MUL, v, F100)));
        neg = n < 0; if (neg) n = -n;
        do { t[i++] = (char)('0' + n % 10); n /= 10; if (i == 2) t[i++] = '.'; } while (n || i < 4);
        n = 0;
        if (neg) shown[n++] = '-';
        while (i) shown[n++] = t[--i];
        shown[n] = 0;
        return shown;
    }
    return ftoa(v);
}
static void draw_cell(uint8_t c, uint8_t r)
{
    uint8_t w = (uint8_t)(cw - 1), n, i, cur = c == cc && r == cr, f, b;
    const char *s;
    if (!kinds[(uint16_t) r * NCOL + c]) rec[0] = K_EMPTY;          /* an empty cell: no DMA */
    else dma_copy(caddr(c, r), (uint32_t)(uint16_t) rec, CELLSZ);
    f = cur ? bg0 : (rec[0] == K_TEXT ? fg0 : (rec[1] ? LRED : WHITE));
    b = cur ? fg0 : bg0;
    if (rec[0] == K_TEXT) {
        s = (const char *) rec + 6;
        for (i = 0; i < w; i++) rb_put((uint8_t)(*s ? *s++ : ' '), f, b);
    } else if (rec[0] == K_VAL) {
        s = show_value(rec);
        n = (uint8_t) strlen(s);
        if (n > w) for (i = 0; i < w; i++) rb_put('#', f, b);       /* too wide for the column */
        else { for (i = n; i < w; i++) rb_put(' ', f, b); rb_str(s, f, b); }
    } else for (i = 0; i < w; i++) rb_put(' ', f, b);
    rb_put(' ', fg0, bg0);
}
static void draw_status(void)                /* where the cell is, what is in it, what it comes to */
{
    rb_str(" CALC  ", bg0, fg0);
    rb_str(cell_name(cc, cr), bg0, fg0);
    rb_str("  ", bg0, fg0);
    dma_copy(caddr(cc, cr), (uint32_t)(uint16_t) rec, CELLSZ);
    if (rec[0] == K_TEXT) rb_str((const char *) rec + 6, bg0, fg0);
    else if (rec[0] == K_VAL) { rb_str((const char *) rec + 6, bg0, fg0); rb_str("   ", bg0, fg0); rb_str(show_value(rec), bg0, fg0); }
    rb_str("   ", bg0, fg0); rb_str(fname, bg0, fg0);
    if (modified) rb_str(" *", RED, fg0);
    rb_out(0, 1);
}
static void draw_entry(void)
{
    if (entering) { rb_put('>', YELLOW, bg0); rb_put(' ', fg0, bg0); rb_str(ebuf, WHITE, bg0); rb_put('_', YELLOW, bg0); }
    else if (msg[0]) rb_str(msg, YELLOW, bg0);
    rb_out(1, 0);
}
static void redraw(void)
{
    uint8_t c, r, i;
    draw_status(); draw_entry();
    rb_str("   ", GREY, bg0);
    for (c = lc; c < NCOL && c < lc + vc; c++) {
        for (i = 0; i < (cw - 1) / 2; i++) rb_put(' ', GREY, bg0);
        rb_put((uint8_t)('A' + c), c == cc ? WHITE : GREY, bg0);
        while (rn < 3 + (c - lc + 1) * cw) rb_put(' ', GREY, bg0);
    }
    rb_out(2, 0);
    for (i = 0; i < vr; i++) {
        r = (uint8_t)(tr + i);
        if (r >= NROW) { rb_out((uint8_t)(3 + i), 0); continue; }
        rb_put((uint8_t)(r >= 9 ? '0' + (r + 1) / 10 : ' '), r == cr ? WHITE : GREY, bg0);
        rb_put((uint8_t)('0' + (r + 1) % 10), r == cr ? WHITE : GREY, bg0);
        rb_put(' ', fg0, bg0);
        for (c = lc; c < NCOL && c < lc + vc; c++) draw_cell(c, r);
        rb_out((uint8_t)(3 + i), 0);
    }
    if (cols >= 78) rb_str(" type to enter  = a formula  Enter down  Tab right  F2 edit  Del clear  Ctrl-G go to  Ctrl-S save  F10 menu", bg0, fg0);
    else rb_str(" type  = formula  F2 edit  Del  ^G go to  ^S save  F10 menu", bg0, fg0);
    rb_out((uint8_t)(rows - 1), 1);
}
static void keep_visible(void)
{
    if (cr < tr) tr = cr;
    if (cr >= tr + vr) tr = (uint8_t)(cr - vr + 1);
    if (cc < lc) lc = cc;
    if (cc >= lc + vc) lc = (uint8_t)(cc - vc + 1);
}
static void layout(void) { vc = (uint8_t)((cols - 3) / cw); if (!vc) vc = 1; vr = (uint8_t)(rows - 4); keep_visible(); }

static uint8_t ask(const char *q, char *buf, uint8_t max)
{
    uint8_t n = (uint8_t) strlen(buf);
    uint16_t k;
    for (;;) {
        rb_str(q, YELLOW, bg0); rb_str(buf, WHITE, bg0); rb_put('_', YELLOW, bg0); rb_out(1, 0);
        while ((k = getkey()) == 0) ;
        if (k == 13) return n != 0;
        if (k == 0x1B) return 0;
        if ((k == 8 || k == 0x7F || k == 0x14 || k == K_DEL) && n) buf[--n] = 0;
        else if (k >= 0x20 && k < 0x100 && n < max) { buf[n++] = (char) k; buf[n] = 0; }
    }
}

/* ---- files: a line a cell ----------------------------------------------- */
static uint8_t obuf[256];
static uint16_t on;                          /* 16 bits: an 8-bit count wraps before it reaches 256, and never flushes */
static uint8_t oerr;
static void flush(void) { if (!on) return; fs_w32(8, (uint16_t) obuf); fs_w32(12, on); if (fs_cmd(4)) oerr = 1; on = 0; }
static void emit(char ch) { obuf[on++] = (uint8_t) ch; if (on == sizeof obuf) flush(); }
static void emits(const char *s) { while (*s) emit(*s++); }
static void save_sheet(void)
{
    uint8_t c, r;
    fs_w32(4, (uint16_t) fname);
    if (fs_cmd(2)) { strcpy(msg, " could not write that name"); return; }
    on = 0; oerr = 0;
    emits("K4CALC 2 W"); emit((char)('0' + cw / 10)); emit((char)('0' + cw % 10)); emits(fmt2 ? " F$\n" : " FG\n");
    for (r = 0; r < NROW; r++) for (c = 0; c < NCOL; c++) {
        dma_copy(caddr(c, r), (uint32_t)(uint16_t) rec, CELLSZ);
        if (!rec[0]) continue;
        emits(cell_name(c, r)); emit(':');
        emit(rec[0] == K_TEXT ? 'T' : (rec[6] == '=' ? 'F' : 'N'));   /* text, formula, number */
        emit(':');
        emits((const char *) rec + 6); emit('\n');
    }
    flush(); fs_cmd(5);
    if (oerr) strcpy(msg, " the disk would not take it all");
    else { modified = 0; strcpy(msg, " saved"); }
}
/* A cell out of a K4CALC 1 sheet, brought over to this spelling: @SUM(A1...A9)
 * was how the first CALC wrote it, SUM(A1:A9) is how this one does. */
static void modernise(char *t)
{
    char *s = t, *d = t;
    uint8_t n;
    while (*s) {
        if (*s == '@') { s++; continue; }
        if (s[0] == '.' && s[1] == '.') { s += 2; if (*s == '.') s++; *d++ = ':'; continue; }
        *d++ = *s++;
    }
    *d = 0;
    if (t[0] != '=' && !all_number(t)) {     /* it was a value, so it is a formula now */
        n = (uint8_t) strlen(t);
        if (n > TXTMAX - 1) { t[TXTMAX - 1] = 0; n = TXTMAX - 1; }
        while (n) { t[n] = t[n - 1]; n--; }
        t[0] = '=';
    }
}
static uint8_t load_sheet(void)              /* 0 loaded, 1 absent, 2 not a sheet */
{
    static char line[TXTMAX + 16], text[TXTMAX + 2];
    uint32_t p, end;
    uint8_t n, st, c, r, k, legacy = 0;
    fs_w32(4, (uint16_t) fname); fs_w32(8, LOADBUF); fs_w32(12, LOADMAX);
    st = fs_cmd(9);
    if (st == 1) return 1;
    if (st) return 2;
    end = LOADBUF + fs_r32(12);
    if (far_peek(LOADBUF) != 'K' || far_peek(LOADBUF + 1) != '4' || far_peek(LOADBUF + 2) != 'C') return 2;
    dma_fill(0, CELLS, (uint32_t) NCOL * NROW * CELLSZ); memset(kinds, 0, sizeof kinds);
    for (p = LOADBUF; p < end; ) {
        n = 0;
        while (p < end && (k = far_peek(p)) != '\n') { if (n < sizeof line - 1 && k != '\r') line[n++] = (char) k; p++; }
        p++; line[n] = 0;
        if (line[0] == 'K' && line[1] == '4') {                  /* K4CALC v Wnn Fx */
            legacy = (uint8_t)(line[7] == '1');
            for (n = 0; line[n]; n++) {
                if (line[n] == 'W' && is_digit(line[n + 1])) { cw = (uint8_t)((line[n + 1] - '0') * 10 + (is_digit(line[n + 2]) ? line[n + 2] - '0' : 0)); if (!is_digit(line[n + 2])) cw = (uint8_t)(line[n + 1] - '0'); }
                if (line[n] == 'F') fmt2 = line[n + 1] == '$';
            }
            if (cw < 3 || cw > 20) cw = 9;
            continue;
        }
        sp = line;
        if (!cellref(&c, &r) || sp[0] != ':' || sp[2] != ':') continue;
        k = upper(sp[1]);
        memset(rec, 0, CELLSZ);
        if (k == 'T' || k == 'L') rec[0] = K_TEXT;               /* L: the first CALC's label */
        else if (k == 'N' || k == 'F' || k == 'V') rec[0] = K_VAL;
        else continue;
        strncpy(text, sp + 3, TXTMAX); text[TXTMAX] = 0;
        if (legacy && rec[0] == K_VAL) modernise(text);
        strncpy((char *) rec + 6, text, TXTMAX);
        dma_copy((uint32_t)(uint16_t) rec, caddr(c, r), CELLSZ);
        kinds[(uint16_t) r * NCOL + c] = rec[0];
    }
    recalc(); modified = 0;
    return 0;
}

/* ---- the keys ----------------------------------------------------------- */
static void commit(void)
{
    ebuf[en] = 0;
    put_cell(cc, cr, ebuf);
    entering = 0; modified = 1;
    recalc();
}
static uint8_t leave(void)
{
    uint16_t k;
    if (!modified) return 1;
    rb_str(" not saved -- save first?  Y yes  N no  Esc stay", YELLOW, bg0); rb_out(1, 0);
    for (;;) {
        while ((k = getkey()) == 0) ;
        if (k == 'y' || k == 'Y') { if (ask(" save as: ", fname, 60)) save_sheet(); return !modified; }
        if (k == 'n' || k == 'N') return 1;
        if (k == 0x1B) return 0;
    }
}
static void do_save(void) { if (ask(" save as: ", fname, 60)) save_sheet(); }
static void do_open(void)
{
    uint8_t n;
    if (!ask(" open: ", fname, 60)) return;
    n = load_sheet();
    if (n == 1) strcpy(msg, " no such file");
    else if (n == 2) strcpy(msg, " that is not a CALC sheet");
    layout();
}
static void do_new(void)
{
    uint16_t k;
    rb_str(" clear the whole sheet?  Y", YELLOW, bg0); rb_out(1, 0);
    while ((k = getkey()) == 0) ;
    if (k == 'y' || k == 'Y') { dma_fill(0, CELLS, (uint32_t) NCOL * NROW * CELLSZ); memset(kinds, 0, sizeof kinds); modified = 1; cc = cr = lc = tr = 0; }
}
static void do_width(void)
{
    static char q[4];
    uint8_t n;
    q[0] = 0;
    if (!ask(" column width (3-20): ", q, 2)) return;
    n = (uint8_t)(q[1] ? (q[0] - '0') * 10 + (q[1] - '0') : q[0] - '0');
    if (n >= 3 && n <= 20) { cw = n; layout(); modified = 1; } else strcpy(msg, " a width is 3 to 20");
}
static void do_format(void) { fmt2 ^= 1; modified = 1; strcpy(msg, fmt2 ? " numbers to two places" : " numbers as they come"); }
static uint8_t do_menu(void)                 /* F10: the things without a key of their own; nonzero: leave */
{
    uint16_t k;
    rb_str(" S save  O open  N new sheet  W column width  F number format  Q quit", YELLOW, bg0); rb_out(1, 0);
    while ((k = getkey()) == 0) ;
    switch (upper((char) k)) {
    case 'S': do_save(); break;
    case 'O': do_open(); break;
    case 'N': do_new(); break;
    case 'W': do_width(); break;
    case 'F': do_format(); break;
    case 'Q': return leave();
    }
    return 0;
}
static void go_to(void)
{
    static char q[5];
    uint8_t c, r;
    q[0] = 0;
    if (!ask(" go to: ", q, 3)) return;
    sp = q;
    if (cellref(&c, &r)) { cc = c; cr = r; } else strcpy(msg, " a cell is a letter and a number, like B12");
}
static void row_end(void)                    /* End: the last cell in this row with anything in it */
{
    uint8_t c, last = 0;
    for (c = 0; c < NCOL; c++) if (kinds[(uint16_t) cr * NCOL + c]) last = c;
    cc = last;
}
static void sheet_end(void)                  /* Ctrl-End: the corner of what is used */
{
    uint8_t c, r, lastc = 0, lastr = 0;
    uint16_t i = 0;
    for (r = 0; r < NROW; r++) for (c = 0; c < NCOL; c++, i++) if (kinds[i]) { lastr = r; if (c > lastc) lastc = c; }
    cc = lastc; cr = lastr;
}
static uint8_t key(uint16_t k)               /* nonzero: leave */
{
    if (entering) {
        switch (k) {
        case 13:      commit(); if (cr < NROW - 1) cr++; return 0;
        case 9:       commit(); if (kmods & 1) { if (cc) cc--; } else if (cc < NCOL - 1) cc++; return 0;
        case K_DOWN:  commit(); if (cr < NROW - 1) cr++; return 0;
        case K_UP:    commit(); if (cr) cr--; return 0;
        case K_LEFT:  commit(); if (cc) cc--; return 0;
        case K_RIGHT: commit(); if (cc < NCOL - 1) cc++; return 0;
        case 0x1B:    entering = 0; return 0;
        case 8: case 0x7F: case 0x14: case K_DEL: if (en) ebuf[--en] = 0; only_entry = 1; return 0;
        }
        if (k >= 0x20 && k < 0x100 && en < TXTMAX) { ebuf[en++] = (char) k; ebuf[en] = 0; }
        only_entry = 1;
        return 0;
    }
    msg[0] = 0;
    switch (k) {
    case K_UP:    if (cr) cr--; return 0;
    case K_DOWN:  if (cr < NROW - 1) cr++; return 0;
    case K_LEFT:  if (cc) cc--; return 0;
    case K_RIGHT: if (cc < NCOL - 1) cc++; return 0;
    case 9:       if (kmods & 1) { if (cc) cc--; } else if (cc < NCOL - 1) cc++; return 0;   /* Shift-Tab goes back */
    case 13:      if (cr < NROW - 1) cr++; return 0;
    case K_PGUP:  cr = cr > vr ? (uint8_t)(cr - vr) : 0; return 0;
    case K_PGDN:  cr = cr + vr < NROW ? (uint8_t)(cr + vr) : NROW - 1; return 0;
    case K_HOME:  if (kmods & 2) { cc = cr = 0; } else cc = 0; return 0;      /* Ctrl-Home the sheet, Home the row */
    case K_END:   if (kmods & 2) sheet_end(); else row_end(); return 0;
    case K_DEL:   put_cell(cc, cr, ""); modified = 1; recalc(); return 0;
    case K_F2:
        dma_copy(caddr(cc, cr), (uint32_t)(uint16_t) rec, CELLSZ);
        en = 0;
        /* text that would read as a number or a formula needs its ' back */
        if (rec[0] == K_TEXT && (rec[6] == '=' || rec[6] == '+' || rec[6] == '-' || rec[6] == '\'' || all_number((const char *) rec + 6))) ebuf[en++] = '\'';
        strcpy(ebuf + en, (const char *) rec + 6); en = (uint8_t) strlen(ebuf);
        entering = 1;
        return 0;
    case K_F10:   return do_menu();
    case 0x07:    go_to(); return 0;         /* Ctrl-G */
    case 0x0F:    do_open(); return 0;       /* Ctrl-O */
    case 0x0E:    do_new(); return 0;        /* Ctrl-N */
    case 0x17:    do_width(); return 0;      /* Ctrl-W */
    case 0x13:    do_save(); return 0;       /* Ctrl-S */
    case 0x11:    return leave();            /* Ctrl-Q */
    case 0x1B:    return leave();
    }
    if (k >= 0x20 && k < 0x100) { entering = 1; en = 0; ebuf[en++] = (char) k; ebuf[en] = 0; only_entry = 1; }
    return 0;
}

void main(void)
{
    const char *p;
    uint8_t i = 0, q = 0, e;
    uint16_t k;

    rom_args();
    p = *(const char **) 0xF0;
    while (*p == ' ') p++;
    if (*p == '"') { q = 1; p++; }
    while (*p && i < 60 && (q ? *p != '"' : *p != ' ')) fname[i++] = *p++;
    fname[i] = 0;

    F0 = fint(0); F10 = fint(10); F100 = fint(100); FBIG = fint(20000000L);
    FPI = f2(MATH_MUL, f1(MATH_ATAN, fint(1)), fint(4));
    dma_fill(0, CELLS, (uint32_t) NCOL * NROW * CELLSZ);
    if (!i) { strcpy(fname, "SHEET.CAL"); strcpy(msg, " a new sheet: type a number or a word, = for a formula; F10 the menu"); }
    else {
        e = load_sheet();
        if (e == 1) strcpy(msg, " a new sheet: Ctrl-S saves it");
        else if (e == 2) { const char *m = "calc: that is not a CALC sheet\n"; while (*m) rom_chrout((uint8_t) *m++); SHELL_RC = 1; return; }
    }

    cols = REG(TERM + 5); rows = REG(TERM + 6);
    ox = REG(TERM + 7);   oy = REG(TERM + 8);
    stride = REG(TERM + 0x0D);
    fg0 = REG(TERM + 0x14); bg0 = REG(TERM + 0x15);
    if (fg0 == bg0) { fg0 = WHITE; bg0 = BLUE; }
    if (!cols) cols = 80;
    if (!rows) rows = 25;
    if (!stride) stride = cols;
    layout();
    flags_was = REG(TERM + 0x0E);
    REG(TERM + 0x0E) = (uint8_t)(flags_was & ~1);

    redraw();
    for (;;) {
        k = getkey();
        if (!k) continue;
        only_entry = 0;
        if (key(k)) break;
        if (only_entry) draw_entry();              /* typing: one line, not the sheet */
        else { keep_visible(); redraw(); }
    }
    REG(TERM + 0x0E) = flags_was;
    rom_chrout(12);
}
