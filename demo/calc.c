/* K4510: CALC [-s] [name] -- a spreadsheet, the way they are reckoned now,
 * in MS-DOS EDIT's visual language (demo/dosui.h), as EDIT and WORD are.
 *
 *   CALC              a new sheet (Untitled until it is saved)
 *   CALC NAME.CAL     that sheet;  CALC NAME.CSV imports a CSV
 *   CALC -s NAME      in the console's own colours (Options)
 *
 * Columns A-Z, AA-AZ (52), rows 1-999.  Type into a cell and it is taken
 * for what it looks like: a number is a number, an = starts a formula,
 * anything else is text.  A leading ' forces text ('2026 stays a year).
 *
 *   =A1*2  =(B3+B4)/2  =2^10  -C7  =SUM(A1:A9)  =AVERAGE(B1:B5;10)
 *   =$A$1  =A$1  =$A1     absolute and mixed references; F4 cycles them
 *   = <> < > <= >=        comparisons (TRUE / FALSE);  & joins text
 *   ="Total: "&A1         text in double quotes
 *   SUM AVERAGE MIN MAX COUNT COUNTA   over ranges and lists
 *   ABS INT SQRT ROUND(x[,n]) MOD PI()
 *   IF(c;a;b) AND OR NOT ISERROR IFERROR NA()
 *   LEN LEFT RIGHT MID UPPER LOWER TRIM CONCAT VALUE
 *   TODAY() NOW()          days since 1899-12-30, as Excel counts them
 *
 * Arguments part with , or ; (Excel's comma, LibreOffice's semicolon in
 * French); a formula is kept and shown as it was typed.
 *
 * The sheet works itself out again after every entry, in the order its
 * formulas need: a cell is left for the next pass while what it uses is
 * not yet known, and what is still left when a pass finds nothing to do is
 * circular -- #CIRC!.  The other errors are Excel's: #DIV/0!  #REF!  #NAME?
 * #VALUE!  #N/A  #NUM!, and #ERROR! for a formula that will not parse.  A
 * number wider than its column shows #####; text runs on into empty cells.
 *
 *   arrows PgUp PgDn        move          Shift+arrows   select a range
 *   Enter down  Tab right   keep and go   Esc            drop the entry
 *   Home / Ctrl+Home        the row's start / A1;  End / Ctrl+End the last used
 *   F2 edit the cell (arrows then move in the entry)   F4 $ on a reference
 *   Del clear   Ctrl+X Ctrl+C Ctrl+V cut copy paste (references move with
 *   the cell, $ holds them)   Ctrl+D Ctrl+R fill down / right
 *   Ctrl+G go to  Ctrl+W column width  Ctrl+S save  Ctrl+O open  Ctrl+N new
 *   Ctrl+Q / Esc leave  F9 recalculate  F1 the keys  F10 or Alt the menus
 *   Cells menu: General, Decimals..., Thousands, Percent, Date -- the cell or
 *   the range; Column Width (Ctrl+W), All Columns.  (Cells, not Format: a
 *   menu opens by its first letter, and File has the F.)
 *
 * The arithmetic is the MATH unit's ($D700, IEEE singles, as LOGO uses it):
 * seven digits, and a NOW() to about five minutes.
 *
 * Files.  A sheet is text, a line a cell -- A1:F:=B2*3 -- so EDIT can read
 * one: K4CALC 3 adds a format after the kind letter (A2:N2,:1234.5 is two
 * decimals with thousands; % percent, D date) and W:B:12 lines for column
 * widths.  K4CALC 2 sheets open as they are, K4CALC 1 (the first CALC's
 * VisiCalc spelling, @SUM and A1...B3) are brought over as they load.
 * File > Import CSV / Export CSV move a sheet to and from LibreOffice and
 * Excel: the separator is told from the file on the way in (, ; or Tab),
 * and is , on the way out unless Options says ;.  Values are exported, not
 * formulas, as those two do.
 *
 * Far memory ($0E..., nothing else runs with it): the cells' kinds at
 * $0E080000, the cells at $0E100000 (52 x 999 x 112 bytes), the clipboard
 * at $0E700000, the Open dialog's list at $0EE00000, a file being read at
 * $0F000000 (1 MB). */
#include "k4510.h"

void __fastcall__ rom_chrout(unsigned char c);
unsigned char rom_getin(void);
static unsigned char rom_args(void) { return (unsigned char)(((unsigned (*)(void))0xFF95)() & 0xFF); }
static void rom_video(void) { ((void (*)(void))0xFF92)(); }
#pragma static-locals (on)                 /* the image is tight: locals in memory, but for the parser, which recurses */
#include "dosui.h"

#define FS      0xD300u
#define MATHR   0xD700u
#define STATE   0x0E080000UL                 /* a byte a cell: kind (2 bits), the recalc generation above */
#define CELLS   0x0E100000UL
#define CLIP    0x0E700000UL
#define LOADBUF 0x0F000000UL
#define LOADMAX 0x00100000UL
#define NCOL    52
#define NROW    999
#define CELLSZ  112                          /* kind, err, rtype, fmt, value (4), source (64), result text (40) */
#define SRC     8
#define RES     72
#define TXTMAX  63
#define RESMAX  39
#define KIND_EMPTY 0                        /* a cell's kind -- not K_TEXT: that and its kin in dosui.h are the colours */
#define KIND_TEXT  1
#define KIND_VAL   2                            /* a number or a formula: either way it is worked out */
#define F_GEN   0x0F                         /* the format byte: decimals 0-9 or 15 general; 16 thousands; 32 percent; 64 date */
#define SHELL_RC (*(volatile uint8_t *)0x03FF)

enum { C_NEW = 1, C_OPEN, C_SAVE, C_SAVEAS, C_IMPORT, C_EXPORT, C_EXIT,
       C_CUT, C_COPY, C_PASTE, C_CLEAR, C_FILLD, C_FILLR, C_GOTO, C_RECALC,
       C_FGEN, C_FDEC, C_FTHOU, C_FPCT, C_FDATE, C_WIDTH, C_WIDTHALL,
       C_DOS, C_SYS, C_SEMI, C_HELP, C_FUNCS, C_ABOUT };

static void fs_w32(uint8_t r, uint32_t v) { REG(FS + r) = (uint8_t) v; REG(FS + r + 1) = (uint8_t)(v >> 8); REG(FS + r + 2) = (uint8_t)(v >> 16); REG(FS + r + 3) = (uint8_t)(v >> 24); }
static uint32_t fs_r32(uint8_t r) { return (uint32_t) REG(FS + r) | ((uint32_t) REG(FS + r + 1) << 8) | ((uint32_t) REG(FS + r + 2) << 16) | ((uint32_t) REG(FS + r + 3) << 24); }
static uint8_t fs_cmd(uint8_t c) { REG(FS) = c; return REG(FS + 1); }

/* ---- the MATH unit (as LOGO drives it) ----------------------------------- */
typedef unsigned long fbits;
static void w32r(uint16_t a, unsigned long v) { REG(a) = (uint8_t) v; REG(a + 1) = (uint8_t)(v >> 8); REG(a + 2) = (uint8_t)(v >> 16); REG(a + 3) = (uint8_t)(v >> 24); }
static unsigned long r32r(uint16_t a) { return (unsigned long) REG(a) | ((unsigned long) REG(a + 1) << 8) | ((unsigned long) REG(a + 2) << 16) | ((unsigned long) REG(a + 3) << 24); }
#define FREG(n) (MATHR + 4 * (n))
#define FOP(op, d, s) do { REG(0xD721u) = (uint8_t)(((d) << 4) | (s)); REG(0xD720u) = (op); } while (0)
/* the errors, Excel's names; a cell keeps the number, the screen shows the name */
enum { E_OK, E_DIV0, E_REF, E_NAME, E_VALUE, E_NA, E_CIRC, E_SYNTAX, E_NUM, E_N };
static const char *const errname[E_N] = { "", "#DIV/0!", "#REF!", "#NAME?", "#VALUE!", "#N/A", "#CIRC!", "#ERROR!", "#NUM!" };
static uint8_t err;                          /* the formula being worked out went wrong: which way */
static uint8_t defer;                        /* ...or used a formula not yet worked out this pass */
static fbits F0, F1, F10, F100, F2E9, FPI;
static fbits f2(uint8_t op, fbits a, fbits b)
{
    if (op == MATH_DIV && b == F0) { if (!err) err = E_DIV0; return F0; }
    w32r(FREG(0), a); w32r(FREG(1), b); FOP(op, 0, 1);
    if ((REG(0xD722u) & 4) && !err) err = E_NUM;   /* NaN or infinity */
    return r32r(FREG(0));
}
static fbits f1(uint8_t op, fbits a)
{
    w32r(FREG(1), a); FOP(op, 0, 1);
    if ((REG(0xD722u) & 4) && !err) err = E_NUM;
    return r32r(FREG(0));
}
static fbits fint(long v) { w32r(0xD724u, (unsigned long) v); FOP(MATH_ITOF, 0, 0); return r32r(FREG(0)); }
static long ftoi(fbits a) { w32r(FREG(1), a); FOP(MATH_FTOI, 0, 1); return (long) r32r(0xD724u); }
static int fcmp(fbits a, fbits b)
{
    uint8_t fl; w32r(FREG(0), a); w32r(FREG(1), b); FOP(MATH_CMP, 0, 1); fl = REG(0xD722u);
    return (fl & 1) ? 0 : (fl & 2) ? -1 : 1;
}
static char numbuf[26];
static const char *ftoa(fbits a)             /* general: the unit's own spelling, with a 0 before a bare point */
{
    w32r(FREG(1), a); w32r(0xD730u, (unsigned long)(uint16_t)(numbuf + 1)); FOP(MATH_FTOAR, 0, 1);
    if (numbuf[1] == '.') { numbuf[0] = '0'; return numbuf; }
    if (numbuf[1] == '-' && numbuf[2] == '.') { numbuf[0] = '-'; numbuf[1] = '0'; return numbuf; }
    return numbuf + 1;
}
static uint8_t is_digit(char c) { return (uint8_t)(c >= '0' && c <= '9'); }
static uint8_t upper(char c) { return (uint8_t)(c >= 'a' && c <= 'z' ? c - 32 : c); }
static uint8_t is_alpha(char c) { c = (char) upper(c); return (uint8_t)(c >= 'A' && c <= 'Z'); }
static uint8_t is_alnum(char c) { return (uint8_t)(is_alpha(c) || is_digit(c)); }

/* ---- the sheet ---------------------------------------------------------- */
static uint32_t caddr(uint8_t c, uint16_t r) { return CELLS + ((uint32_t) r * NCOL + c) * CELLSZ; }
static uint32_t saddr(uint8_t c, uint16_t r) { return STATE + (uint32_t) r * NCOL + c; }
static uint8_t rowused[NROW];                /* cells with anything in them, a row at a time: the empty rows are passed without a look */
static uint8_t colw[NCOL], cw = 9;           /* each column's width; the default, saved in the header */
static uint8_t gen = 1;                      /* this recalculation's number (1-63): a cell whose state carries it is worked out */
static uint8_t rec[CELLSZ], rrec[CELLSZ];    /* the cell in hand; a cell looked at while working one out */
static uint8_t srow[NCOL];                   /* one row's states */
static uint8_t kind_of(uint8_t c, uint16_t r) { return (uint8_t)(far_peek(saddr(c, r)) & 3); }
static void get_cell(uint8_t c, uint16_t r) { dma_copy(caddr(c, r), (uint32_t)(uint16_t) rec, CELLSZ); }   /* into rec */
static void put_rec(uint8_t c, uint16_t r) { dma_copy((uint32_t)(uint16_t) rec, caddr(c, r), CELLSZ); }   /* rec back */
static void set_state(uint8_t c, uint16_t r, uint8_t kind, uint8_t g) { far_poke(saddr(c, r), (uint8_t)(kind | (g << 2))); }
static char cname[6];
static const char *col_name(uint8_t c, char *p)
{
    if (c >= 26) { *p++ = 'A'; c = (uint8_t)(c - 26); }
    *p++ = (char)('A' + c); *p = 0;
    return p;
}
static const char *cell_name(uint8_t c, uint16_t r)
{
    char *p = (char *) col_name(c, cname);
    r++;
    if (r >= 100) *p++ = (char)('0' + r / 100);
    if (r >= 10) *p++ = (char)('0' + r / 10 % 10);
    *p++ = (char)('0' + r % 10); *p = 0;
    return cname;
}
/* A1, $B$12, AZ999 at s: 1 and past it (c, r, and which halves are held by
 * $), 2 and past it when the shape is right but the cell is off the sheet
 * (BA1, A1000: #REF!), 0 and not moved when it is no reference at all. */
static uint8_t ref_at(const char *s, uint8_t *c, uint16_t *r, uint8_t *abs, const char **end)
{
    uint8_t a = 0, l1, l2 = 0, i = 0; uint16_t col, n = 0, nd = 0;
    if (s[i] == '$') { a |= 1; i++; }
    if (!is_alpha(s[i])) return 0;
    l1 = upper(s[i++]);
    if (is_alpha(s[i])) l2 = upper(s[i++]);
    if (s[i] == '$') { a |= 2; i++; }
    if (!is_digit(s[i])) return 0;
    while (is_digit(s[i]) && nd < 5) { n = (uint16_t)(n * 10 + (s[i] - '0')); i++; nd++; }
    if (is_alnum(s[i]) || s[i] == '_') return 0;
    *end = s + i; *abs = a;
    col = l2 ? (uint16_t)((l1 - 'A' + 1) * 26 + (l2 - 'A')) : (uint16_t)(l1 - 'A');
    if (col >= NCOL || n < 1 || n > NROW || nd > 4) return 2;
    *c = (uint8_t) col; *r = (uint16_t)(n - 1);
    return 1;
}
/* Is the whole entry a plain number?  This decides that 42 and -3.5 are
 * numbers while 3 apples is text, so it reads the text and touches neither
 * the MATH unit nor the parser. */
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

/* ---- values: a number, a text, TRUE/FALSE, or an empty cell -------------- */
typedef struct { uint8_t t; fbits n; uint16_t s; } val;
#define T_NUM 0
#define T_STR 1
#define T_BOOL 2
#define T_EMPTY 3
static char sa[640]; static uint16_t san;    /* the texts a formula makes on its way: offsets into here, let go when it is done */
static void sa_reset(void) { sa[0] = 0; san = 1; }
static uint16_t sa_put(const char *s, uint16_t n)
{
    uint16_t o = san;
    if (san + n + 1 > sizeof sa) { if (!err) err = E_VALUE; return 0; }
    memcpy(sa + san, s, n); sa[san + n] = 0; san = (uint16_t)(san + n + 1);
    return o;
}
static void set_num(val *v, fbits n) { v->t = T_NUM; v->n = n; }
static void set_bool(val *v, uint8_t b) { v->t = T_BOOL; v->n = b ? F1 : F0; }
static const char *sp;                       /* the parser's place in the formula */
static fbits number(void);
static fbits num_of(const val *v)            /* as a number: text that reads as one counts, other text is #VALUE! */
{
    const char *save; fbits r; uint8_t neg = 0;
    if (v->t == T_EMPTY) return F0;
    if (v->t != T_STR) return v->n;
    if (!all_number(sa + v->s)) { if (!err) err = E_VALUE; return F0; }
    save = sp; sp = sa + v->s;
    if (*sp == '-') { neg = 1; sp++; } else if (*sp == '+') sp++;
    r = number(); sp = save;
    return neg ? f1(MATH_NEG, r) : r;
}
static void to_str(val *v)                   /* as text, in the arena */
{
    const char *s;
    if (v->t == T_STR) return;
    if (v->t == T_EMPTY) s = ""; else if (v->t == T_BOOL) s = fcmp(v->n, F0) ? "TRUE" : "FALSE"; else s = ftoa(v->n);
    v->s = sa_put(s, (uint16_t) strlen(s)); v->t = T_STR;
}
static uint8_t truth(const val *v) { return (uint8_t)(fcmp(num_of(v), F0) != 0); }
static int cmp_vals(val *a, val *b)          /* Excel's order: numbers, then text (any case), then TRUE/FALSE */
{
    uint8_t ka = a->t == T_STR ? 1 : a->t == T_BOOL ? 2 : 0, kb = b->t == T_STR ? 1 : b->t == T_BOOL ? 2 : 0;
    const char *p, *q; uint8_t x, y;
    if (a->t == T_EMPTY && b->t == T_STR) ka = 1;
    if (b->t == T_EMPTY && a->t == T_STR) kb = 1;
    if (ka != kb) return ka < kb ? -1 : 1;
    if (ka != 1) return fcmp(num_of(a), num_of(b));
    p = a->t == T_EMPTY ? "" : sa + a->s; q = b->t == T_EMPTY ? "" : sa + b->s;
    for (;;) { x = upper(*p); y = upper(*q); if (x != y) return x < y ? -1 : 1; if (!x) return 0; p++; q++; }
}

/* A cell's value as a formula sees it.  A formula not yet worked out this
 * pass defers the one asking; a plain number is read on the spot. */
static void cell_val(uint8_t c, uint16_t r, val *v)
{
    uint8_t st = far_peek(saddr(c, r)), k = (uint8_t)(st & 3);
    const char *save;
    if (k == KIND_EMPTY) { v->t = T_EMPTY; v->n = F0; return; }
    dma_copy(caddr(c, r), (uint32_t)(uint16_t) rrec, CELLSZ);
    if (k == KIND_TEXT) { v->t = T_STR; v->s = sa_put((const char *) rrec + SRC, (uint16_t) strlen((const char *) rrec + SRC)); return; }
    if ((st >> 2) != gen) {
        if (all_number((const char *) rrec + SRC)) {
            uint8_t neg = 0;
            save = sp; sp = (const char *) rrec + SRC;
            if (*sp == '-') { neg = 1; sp++; } else if (*sp == '+') sp++;
            v->n = number(); if (neg) v->n = f1(MATH_NEG, v->n); v->t = T_NUM;
            sp = save; return;
        }
        defer = 1; v->t = T_NUM; v->n = F0; return;
    }
    if (rrec[1]) { if (!err) err = rrec[1]; v->t = T_NUM; v->n = F0; return; }
    v->n = (fbits) rrec[4] | ((fbits) rrec[5] << 8) | ((fbits) rrec[6] << 16) | ((fbits) rrec[7] << 24);
    v->t = rrec[2];
    if (v->t == T_STR) v->s = sa_put((const char *) rrec + RES, (uint16_t) strlen((const char *) rrec + RES));
}

/* ---- working a formula out: recursive descent over its text -------------- */
static void expr(val *v);
static void skip(void) { while (*sp == ' ') sp++; }
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
static uint8_t more(void)                    /* another argument?  , or ; both part them */
{
    skip();
    if (*sp == ',' || *sp == ';') { sp++; return 1; }
    return 0;
}
static void close_paren(void) { skip(); if (*sp == ')') sp++; else if (!err) err = E_SYNTAX; }
#pragma static-locals (push, off)          /* the parser: expr() calls itself through every level */
static void skip_expr(void)                  /* the branch not taken: read past it, its errors nobody's */
{
    uint8_t e = err, d = defer; val w;
    expr(&w); err = e; defer = d;
}
/* a range at sp -- A1:B9 -- or 0 and sp where it was */
static uint8_t range_at(uint8_t *c0, uint16_t *r0, uint8_t *c1, uint16_t *r1)
{
    const char *save = sp, *e; uint8_t a, t; uint16_t x;
    skip();
    if (ref_at(sp, c0, r0, &a, &e) != 1) return 0;
    sp = e; skip();
    if (*sp != ':') { sp = save; return 0; }
    sp++; skip();
    if (ref_at(sp, c1, r1, &a, &e) != 1) { sp = save; return 0; }
    sp = e;
    if (*c1 < *c0) { t = *c0; *c0 = *c1; *c1 = t; }
    if (*r1 < *r0) { x = *r0; *r0 = *r1; *r1 = x; }
    return 1;
}
/* SUM and its kind: every value in the ranges and lists between ( and ) */
enum { A_SUM, A_AVG, A_MIN, A_MAX, A_COUNT, A_COUNTA };
static void aggregate(uint8_t which, val *v)
{
    fbits sum = F0, lo = F0, hi = F0; uint16_t count = 0, filled = 0, r, r0, r1;
    uint8_t c, c0, c1; val w;
    skip();
    if (*sp != '(') { if (!err) err = E_SYNTAX; set_num(v, F0); return; }
    sp++;
    for (;;) {
        if (range_at(&c0, &r0, &c1, &r1)) {
            for (r = r0; r <= r1; r++) for (c = c0; c <= c1; c++) {
                uint8_t k = kind_of(c, r);
                if (k == KIND_EMPTY) continue;
                filled++;
                if (k != KIND_VAL) continue;
                cell_val(c, r, &w);
                if (w.t != T_NUM) continue;  /* text and TRUE/FALSE in a range are not summed, as Excel has it */
                if (!count || fcmp(w.n, lo) < 0) lo = w.n;
                if (!count || fcmp(w.n, hi) > 0) hi = w.n;
                sum = f2(MATH_ADD, sum, w.n); count++;
            }
        } else {
            expr(&w);
            if (w.t != T_EMPTY) filled++;
            if (w.t == T_NUM || w.t == T_BOOL || (w.t == T_STR && all_number(sa + w.s))) {
                w.n = num_of(&w);
                if (!count || fcmp(w.n, lo) < 0) lo = w.n;
                if (!count || fcmp(w.n, hi) > 0) hi = w.n;
                sum = f2(MATH_ADD, sum, w.n); count++;
            }
        }
        if (more()) continue;
        close_paren(); break;
    }
    switch (which) {
    case A_SUM: set_num(v, sum); break;
    case A_AVG: if (!count) { if (!err) err = E_DIV0; set_num(v, F0); } else set_num(v, f2(MATH_DIV, sum, fint(count))); break;
    case A_MIN: set_num(v, lo); break;
    case A_MAX: set_num(v, hi); break;
    case A_COUNT: set_num(v, fint(count)); break;
    default: set_num(v, fint(filled)); break;
    }
}
#pragma code-name (push, "LOCODE")          /* at $1A00 (demo/calc.cfg), under the image */
/* days since 1899-12-30 for a date, and back (Howard Hinnant's civil arithmetic) */
static long days_of(int y, uint8_t m, uint8_t d)
{
    long era, yoe, doy, doe;
    if (m <= 2) y--;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153L * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468 + 25569;
}
static void date_of(long serial, int *y, uint8_t *m, uint8_t *d)
{
    long z = serial - 25569 + 719468, era, doe, yoe, doy, mp;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = z - era * 146097;
    yoe = (doe - doe / 1460L + doe / 36524L - doe / 146096L) / 365;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *d = (uint8_t)(doy - (153 * mp + 2) / 5 + 1);
    *m = (uint8_t)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yoe + era * 400 + (*m <= 2));
}
static fbits today(uint8_t with_time)        /* SYS $D505..$D50C, latched by a read of $D500 */
{
    uint8_t s, mi, h, d, mo; int y; fbits v;
    s = REG(SYS);                            /* the read latches them (a void read, cc65 drops) */
    s = REG(SYS + 5); mi = REG(SYS + 6); h = REG(SYS + 7); d = REG(SYS + 8); mo = REG(SYS + 9);
    y = (int)(REG(SYS + 10) | (REG(SYS + 11) << 8));
    v = fint(days_of(y, mo, d));
    if (with_time) v = f2(MATH_ADD, v, f2(MATH_DIV, fint((long) h * 3600 + (long) mi * 60 + s), fint(86400L)));
    return v;
}
#pragma code-name (pop)
static const char *const fnames[] = {
    "SUM", "AVERAGE", "AVG", "MIN", "MAX", "COUNT", "COUNTA",
    "ABS", "INT", "SQRT", "ROUND", "MOD", "PI",
    "IF", "AND", "OR", "NOT", "ISERROR", "IFERROR", "NA", "TRUE", "FALSE",
    "LEN", "LEFT", "RIGHT", "MID", "UPPER", "LOWER", "TRIM", "CONCAT", "CONCATENATE", "VALUE",
    "TODAY", "NOW", 0 };
enum { F_SUM, F_AVERAGE, F_AVG, F_MIN, F_MAX, F_COUNT, F_COUNTA,
       F_ABS, F_INT, F_SQRT, F_ROUND, F_MOD, F_PI,
       F_IF, F_AND, F_OR, F_NOT, F_ISERROR, F_IFERROR, F_NA, F_TRUE, F_FALSE,
       F_LEN, F_LEFT, F_RIGHT, F_MID, F_UPPER, F_LOWER, F_TRIM, F_CONCAT, F_CONCATENATE, F_VALUE,
       F_TODAY, F_NOW };
static char ident[14];
static void function(val *v)                 /* a name and its brackets: SUM(A1:A9), ROUND(B2;2), PI() */
{
    uint8_t i, n = 0, k, b; val w, x; uint16_t len, a, cnt; const char *s;
    while (is_alnum(*sp) || *sp == '.' || *sp == '_') { if (n < sizeof ident - 1) ident[n++] = (char) upper(*sp); sp++; }
    ident[n] = 0;
    for (i = 0; fnames[i]; i++) if (!strcmp(ident, fnames[i])) break;
    if (!fnames[i]) { if (!err) err = E_NAME; set_num(v, F0); return; }
    if (i <= F_COUNTA) { aggregate((uint8_t)(i == F_SUM ? A_SUM : i <= F_AVG ? A_AVG : i - 1), v); return; }   /* MIN MAX COUNT COUNTA follow AVERAGE and AVG */
    if (i == F_PI || i == F_TRUE || i == F_FALSE) {   /* with or without their empty brackets */
        skip();
        if (*sp == '(') { sp++; close_paren(); }
        if (i == F_PI) set_num(v, FPI); else set_bool(v, (uint8_t)(i == F_TRUE));
        return;
    }
    skip();
    if (*sp != '(') { if (!err) err = E_SYNTAX; set_num(v, F0); return; }
    sp++;
    switch (i) {
    case F_NA: close_paren(); if (!err) err = E_NA; set_num(v, F0); return;
    case F_TODAY: close_paren(); set_num(v, today(0)); return;
    case F_NOW: close_paren(); set_num(v, today(1)); return;
    case F_IF:
        expr(&w); b = truth(&w);
        if (!more()) { if (!err) err = E_SYNTAX; set_num(v, F0); return; }
        if (b) expr(v); else skip_expr();
        if (more()) { if (b) skip_expr(); else expr(v); }
        else if (!b) set_bool(v, 0);
        close_paren(); return;
    case F_AND: case F_OR:
        k = (uint8_t)(i == F_AND);
        do { expr(&w); b = truth(&w); if (i == F_AND) k = (uint8_t)(k && b); else k = (uint8_t)(k || b); } while (more());
        close_paren(); set_bool(v, k); return;
    case F_NOT: expr(&w); close_paren(); set_bool(v, (uint8_t)!truth(&w)); return;
    case F_ISERROR: { uint8_t e = err; err = 0; expr(&w); b = (uint8_t)(err != 0); err = e; close_paren(); set_bool(v, b); return; }
    case F_IFERROR: { uint8_t e = err; err = 0; expr(v); b = (uint8_t)(err != 0); err = e;
        if (!more()) { if (!err) err = E_SYNTAX; return; }
        if (b) expr(v); else skip_expr();
        close_paren(); return; }
    case F_CONCAT: case F_CONCATENATE: {
        uint16_t offs[8]; uint8_t na = 0; char *d;
        do { expr(&w); to_str(&w); if (na < 8) offs[na++] = w.s; } while (more());
        close_paren();
        len = 0; for (k = 0; k < na; k++) len = (uint16_t)(len + strlen(sa + offs[k]));
        if (san + len + 1 > sizeof sa) { if (!err) err = E_VALUE; set_num(v, F0); return; }
        d = sa + san; v->t = T_STR; v->s = san;
        for (k = 0; k < na; k++) { strcpy(d, sa + offs[k]); d += strlen(d); }
        san = (uint16_t)(san + len + 1);
        return; }
    case F_LEN: expr(&w); close_paren(); to_str(&w); set_num(v, fint((long) strlen(sa + w.s))); return;
    case F_UPPER: case F_LOWER: case F_TRIM: {
        char *d;
        expr(&w); close_paren(); to_str(&w);
        s = sa + w.s; len = (uint16_t) strlen(s);
        a = sa_put(s, len); d = sa + a;
        if (i == F_TRIM) { char *q = d; uint8_t sp0 = 1; while (*s) { if (*s == ' ') { if (!sp0) *q++ = ' '; sp0 = 1; } else { *q++ = *s; sp0 = 0; } s++; } if (q > d && q[-1] == ' ') q--; *q = 0; }
        else for (; *d; d++) { if (i == F_UPPER) *d = (char) upper(*d); else if (*d >= 'A' && *d <= 'Z') *d = (char)(*d + 32); }
        v->t = T_STR; v->s = a; return; }
    case F_LEFT: case F_RIGHT: case F_MID: {
        long p = 1, c = 1;
        expr(&w); to_str(&w);
        if (i == F_MID) { if (!more()) { if (!err) err = E_SYNTAX; set_num(v, F0); return; } expr(&x); p = ftoi(f1(MATH_FLOOR, num_of(&x))); }
        if (more()) { expr(&x); c = ftoi(f1(MATH_FLOOR, num_of(&x))); }
        close_paren();
        s = sa + w.s; len = (uint16_t) strlen(s);
        if (c < 0 || p < 1) { if (!err) err = E_VALUE; set_num(v, F0); return; }
        if (i == F_RIGHT) { if (c > len) c = len; s += len - c; }
        else if (i == F_MID) { if (p > len) { c = 0; } else { s += p - 1; if (c > len - (p - 1)) c = len - (p - 1); } }
        else if (c > len) c = len;
        v->t = T_STR; v->s = sa_put(s, (uint16_t) c); return; }
    case F_VALUE: expr(&w); close_paren(); set_num(v, num_of(&w)); return;
    }
    expr(&w); w.n = num_of(&w);              /* the one-number functions */
    if (i == F_ROUND || i == F_MOD) {
        if (more()) { expr(&x); x.n = num_of(&x); cnt = 1; } else { x.n = F0; cnt = 0; }
        close_paren();
        if (i == F_MOD) {
            if (!cnt || x.n == F0) { if (!err) err = E_DIV0; set_num(v, F0); return; }
            set_num(v, f2(MATH_SUB, w.n, f2(MATH_MUL, x.n, f1(MATH_FLOOR, f2(MATH_DIV, w.n, x.n))))); return;
        }
        if (!cnt) set_num(v, f1(MATH_ROUND, w.n));
        else { fbits p = f2(MATH_POW, F10, x.n); set_num(v, f2(MATH_DIV, f1(MATH_ROUND, f2(MATH_MUL, w.n, p)), p)); }
        return;
    }
    close_paren();
    switch (i) {
    case F_ABS: set_num(v, f1(MATH_ABS, w.n)); return;
    case F_INT: set_num(v, f1(MATH_FLOOR, w.n)); return;   /* INT goes down, as a spreadsheet's does */
    default: set_num(v, f1(MATH_SQRT, w.n)); return;
    }
}
static void primary(val *v)
{
    uint8_t c, a, k; uint16_t r; const char *e;
    skip();
    if (is_digit(*sp) || *sp == '.') { set_num(v, number()); return; }
    if (*sp == '"') {                        /* "text", "" inside it for one quote */
        uint16_t o = san; char *d = sa + san;
        sp++;
        for (;;) {
            if (!*sp) { if (!err) err = E_SYNTAX; break; }
            if (*sp == '"') { if (sp[1] == '"') sp++; else { sp++; break; } }
            if (san + 2 > sizeof sa) { if (!err) err = E_VALUE; break; }
            *d++ = *sp++; san++;
        }
        *d = 0; san++;
        v->t = T_STR; v->s = o; return;
    }
    if (*sp == '(') { sp++; expr(v); close_paren(); return; }
    if (*sp == '#') {                        /* an error written out, #REF! after a paste that lost its cell */
        for (k = 1; k < E_N; k++) { for (a = 0; errname[k][a] && upper(sp[a]) == (uint8_t) errname[k][a]; a++) ; if (!errname[k][a]) break; }
        if (k < E_N) { sp += a; if (!err) err = k; } else if (!err) err = E_SYNTAX;
        set_num(v, F0); return;
    }
    k = ref_at(sp, &c, &r, &a, &e);
    if (k) { sp = e; if (k == 1) cell_val(c, r, v); else { if (!err) err = E_REF; set_num(v, F0); } return; }
    if (is_alpha(*sp)) { function(v); return; }
    if (!err) err = E_SYNTAX;
    set_num(v, F0);
}
static void unary(val *v)
{
    skip();
    if (*sp == '-') { sp++; unary(v); set_num(v, f1(MATH_NEG, num_of(v))); return; }
    if (*sp == '+') { sp++; unary(v); return; }
    primary(v);
}
static void power(val *v)
{
    val w;
    unary(v); skip();
    if (*sp == '^') { sp++; power(&w); set_num(v, f2(MATH_POW, num_of(v), num_of(&w))); }
}
static void term(val *v)
{
    val w; char op;
    power(v);
    for (;;) {
        skip(); op = *sp;
        if (op != '*' && op != '/') return;
        sp++; power(&w);
        set_num(v, f2(op == '*' ? MATH_MUL : MATH_DIV, num_of(v), num_of(&w)));
    }
}
static void sum(val *v)
{
    val w; char op;
    term(v);
    for (;;) {
        skip(); op = *sp;
        if (op != '+' && op != '-') return;
        sp++; term(&w);
        set_num(v, f2(op == '+' ? MATH_ADD : MATH_SUB, num_of(v), num_of(&w)));
    }
}
static void concat(val *v)                   /* & joins texts; a number joined is written out */
{
    val w; uint16_t la, lb;
    sum(v);
    for (;;) {
        skip();
        if (*sp != '&') return;
        sp++; sum(&w);
        to_str(v); to_str(&w);
        la = (uint16_t) strlen(sa + v->s); lb = (uint16_t) strlen(sa + w.s);
        if (san + la + lb + 1 > sizeof sa) { if (!err) err = E_VALUE; return; }
        memcpy(sa + san, sa + v->s, la); memcpy(sa + san + la, sa + w.s, lb + 1);
        v->s = san; san = (uint16_t)(san + la + lb + 1);
    }
}
static void expr(val *v)                     /* = <> < > <= >= bind last of all, as in Excel */
{
    val w; uint8_t op; int c;
    concat(v);
    for (;;) {
        skip();
        if (*sp == '=') { op = 0; sp++; }
        else if (*sp == '<') { sp++; if (*sp == '>') { op = 1; sp++; } else if (*sp == '=') { op = 4; sp++; } else op = 2; }
        else if (*sp == '>') { sp++; if (*sp == '=') { op = 5; sp++; } else op = 3; }
        else return;
        concat(&w);
        c = cmp_vals(v, &w);
        set_bool(v, (uint8_t)(op == 0 ? c == 0 : op == 1 ? c != 0 : op == 2 ? c < 0 : op == 3 ? c > 0 : op == 4 ? c <= 0 : c >= 0));
    }
}

#pragma static-locals (pop)

/* A pass down the sheet works out every formula whose cells are known; a
 * formula above what it uses waits for the next pass.  When a pass finds
 * nothing it can do, what is left is circular. */
static void eval_cell(uint8_t c, uint16_t r)     /* rec holds the cell; answers in rec, written back */
{
    val v;
    err = 0; defer = 0; sa_reset();
    sp = (const char *) rec + SRC;
    if (*sp == '=') sp++;                    /* the = is how it was typed, not part of the sum */
    expr(&v); skip();
    if (*sp && !err) err = E_SYNTAX;
    if (defer) return;
    rec[1] = err; rec[2] = v.t == T_EMPTY ? T_NUM : v.t;
    if (v.t == T_EMPTY) v.n = F0;
    rec[4] = (uint8_t) v.n; rec[5] = (uint8_t)(v.n >> 8); rec[6] = (uint8_t)(v.n >> 16); rec[7] = (uint8_t)(v.n >> 24);
    if (v.t == T_STR) strncpy((char *) rec + RES, sa + v.s, RESMAX); else rec[RES] = 0;
    rec[RES + RESMAX] = 0;
    put_rec(c, r);
    set_state(c, r, KIND_VAL, gen);
}
static void recalc(void)
{
    uint16_t r, left; uint8_t c, did;
    gen = (uint8_t)(gen % 63 + 1);
    do {
        did = 0; left = 0;
        for (r = 0; r < NROW; r++) {
            if (!rowused[r]) continue;
            dma_copy(saddr(0, r), (uint32_t)(uint16_t) srow, NCOL);
            for (c = 0; c < NCOL; c++) {
                if ((srow[c] & 3) != KIND_VAL || (srow[c] >> 2) == gen) continue;
                get_cell(c, r);
                eval_cell(c, r);
                if (defer) left++; else did = 1;
            }
        }
    } while (left && did);
    if (!left) return;
    for (r = 0; r < NROW; r++) {             /* the circle: every cell still waiting */
        if (!rowused[r]) continue;
        dma_copy(saddr(0, r), (uint32_t)(uint16_t) srow, NCOL);
        for (c = 0; c < NCOL; c++) {
            if ((srow[c] & 3) != KIND_VAL || (srow[c] >> 2) == gen) continue;
            get_cell(c, r);
            rec[1] = E_CIRC; rec[2] = T_NUM; rec[RES] = 0;
            put_rec(c, r);
            set_state(c, r, KIND_VAL, gen);
        }
    }
}
/* An entry is taken for what it looks like: a number is a number, = is a
 * formula, a leading + or - is one too, and everything else is text -- so
 * 3 apples is a note and not a mistake.  A leading ' forces text.  The
 * cell's format stays, as a cell keeps its format in Excel. */
static uint8_t last_kind;
static void put_cell(uint8_t c, uint16_t r, const char *text)
{
    uint8_t n = 0, old = kind_of(c, r), fmt = (uint8_t)(old ? far_peek(caddr(c, r) + 3) : F_GEN);
    memset(rec, 0, CELLSZ);
    rec[3] = fmt;
    if (text[0]) {
        if (text[0] == '\'') { rec[0] = KIND_TEXT; text++; }
        else if (text[0] == '=') rec[0] = KIND_VAL;
        else if (all_number(text)) rec[0] = KIND_VAL;
        else if (text[0] == '+' || text[0] == '-') rec[0] = KIND_VAL;
        else rec[0] = KIND_TEXT;
        while (text[n] && n < TXTMAX) { rec[SRC + n] = (uint8_t) text[n]; n++; }
    }
    put_rec(c, r);
    set_state(c, r, rec[0], 0);
    if (old && !rec[0]) rowused[r]--; else if (!old && rec[0]) rowused[r]++;
    last_kind = rec[0];
}
static void set_fmt(uint8_t c, uint16_t r, uint8_t fmt) { far_poke(caddr(c, r) + 3, fmt); }
static void clear_sheet(void)
{
    uint8_t c;
    dma_fill(0, STATE, (uint32_t) NCOL * NROW); dma_fill(0, CELLS, (uint32_t) NCOL * NROW * CELLSZ);
    memset(rowused, 0, sizeof rowused);
    for (c = 0; c < NCOL; c++) colw[c] = cw;
}
static uint16_t last_row(void) { uint16_t r = NROW; while (r && !rowused[r - 1]) r--; return r; }   /* one past */
static uint8_t last_col(void)
{
    uint16_t r; uint8_t c, last = 0;
    for (r = 0; r < NROW; r++) {
        if (!rowused[r]) continue;
        dma_copy(saddr(0, r), (uint32_t)(uint16_t) srow, NCOL);
        for (c = (uint8_t)(NCOL - 1); c > last; c--) if (srow[c] & 3) { last = c; break; }
    }
    return (uint8_t)(last + 1);              /* one past */
}

/* ---- a formula moved: its references move with it, $ holds them -------- */
static char tbuf[TXTMAX + 1];
static void put_ref(char **d, uint8_t c, uint16_t r, uint8_t a)
{
    const char *s;
    if (a & 1) *(*d)++ = '$';
    s = col_name(c, *d); *d += strlen(*d);
    if (a & 2) *(*d)++ = '$';
    r++;
    if (r >= 100) *(*d)++ = (char)('0' + r / 100);
    if (r >= 10) *(*d)++ = (char)('0' + r / 10 % 10);
    *(*d)++ = (char)('0' + r % 10);
}
static void shift_refs(char *out, const char *in, int dc, int dr)   /* out: in, each relative reference moved by (dc, dr) */
{
    char *d = out; const char *e; uint8_t c, a, k, prev = 0; uint16_t r; int nc, nr;
    while (*in && d - out < TXTMAX - 7) {
        if (*in == '"') { *d++ = *in++; while (*in && *in != '"' && d - out < TXTMAX - 1) *d++ = *in++; if (*in) *d++ = *in++; prev = 0; continue; }
        if (!prev && (k = ref_at(in, &c, &r, &a, &e)) != 0) {
            if (k == 1) {
                nc = (a & 1) ? c : c + dc; nr = (a & 2) ? r : r + dr;
                if (nc < 0 || nc >= NCOL || nr < 0 || nr >= NROW) { strcpy(d, "#REF!"); d += 5; }
                else put_ref(&d, (uint8_t) nc, (uint16_t) nr, a);
            } else { while (in < e) *d++ = *in++; }
            in = e; prev = 1; continue;
        }
        prev = (uint8_t)(is_alnum(*in) || *in == '_' || *in == '$');
        *d++ = *in++;
    }
    *d = 0;
}

/* ---- the screen --------------------------------------------------------- */
static uint8_t cc, lc, vc, th, ac, selon, gridall, entry_only;   /* the cursor, the view's left column, how many columns show, the window's rows, the selection's anchor */
static uint16_t cr, tr, ar, lastcr = 0xFFFF;
static char name[NAMEMAX], fbuf[NAMEMAX], ebuf[TXTMAX + 2];
static uint8_t entering, en, ep;             /* 0 none, 1 typing (arrows keep and go), 2 editing (arrows move in the entry) */
static const char *note = "";
static char nbuf[80]; static uint8_t nbn;
static uint8_t csvsemi;                      /* Options > CSV Semicolons */
static void nb_reset(void) { nbn = 0; nbuf[0] = 0; }
static void nb_s(const char *s) { while (*s && nbn < sizeof nbuf - 1) nbuf[nbn++] = *s++; nbuf[nbn] = 0; }
static void nb_n(unsigned long v) { char b[10]; uint8_t k = 0; do { b[k++] = (char)('0' + v % 10); v /= 10; } while (v); while (k && nbn < sizeof nbuf - 1) nbuf[nbn++] = b[--k]; nbuf[nbn] = 0; }
static const char *base_of(const char *p) { const char *b = p; for (; *p; p++) if (*p == '/') b = p + 1; return b; }
static void band_file(void)                  /* the top band names the file (core/io.c's title stack, SYS+$44), as EDIT */
{
    const char *b = base_of(name); uint8_t i;
    REG(0xD544) = 0;
    for (i = 0; b[i]; i++) REG(0xD544) = (uint8_t) b[i];
}
static char shown[34];
static const char *fmt_num(fbits v, uint8_t fmt)     /* a number as its format shows it */
{
    uint8_t d = (uint8_t)(fmt & 15), th_ = (uint8_t)(fmt & 16), pc = (uint8_t)(fmt & 32), i = 0, n, neg, g = 0;
    char t[16]; long m; fbits w, p;
    if (fmt & 64) {
        int y; uint8_t mo, dd;
        date_of(ftoi(f1(MATH_FLOOR, v)), &y, &mo, &dd);
        if (y < 0 || y > 9999) return ftoa(v);
        shown[0] = (char)('0' + y / 1000); shown[1] = (char)('0' + y / 100 % 10); shown[2] = (char)('0' + y / 10 % 10); shown[3] = (char)('0' + y % 10);
        shown[4] = '-'; shown[5] = (char)('0' + mo / 10); shown[6] = (char)('0' + mo % 10);
        shown[7] = '-'; shown[8] = (char)('0' + dd / 10); shown[9] = (char)('0' + dd % 10); shown[10] = 0;
        return shown;
    }
    if (pc) { v = f2(MATH_MUL, v, F100); if (d == 15) d = 0; }
    if (th_ && d == 15) d = 2;
    if (d == 15) return ftoa(v);
    p = f2(MATH_POW, F10, fint(d)); w = f2(MATH_MUL, v, p);
    if (fcmp(f1(MATH_ABS, w), F2E9) >= 0) return ftoa(v);
    m = ftoi(f1(MATH_ROUND, w));
    neg = m < 0; if (neg) m = -m;
    do {
        if (i == d && d) t[i++] = '.';
        if (i > d && th_ && g == 3) { t[i++] = ','; g = 0; }
        t[i++] = (char)('0' + m % 10); m /= 10;
        if (i > d) g++;
    } while ((m || i <= d) && i < 15);
    n = 0;
    if (neg) shown[n++] = '-';
    while (i) shown[n++] = t[--i];
    if (pc) shown[n++] = '%';
    shown[n] = 0;
    return shown;
}
static const char *cell_text(const uint8_t *cell)   /* a worked-out cell as the column shows it */
{
    fbits v;
    if (cell[1]) return errname[cell[1] < E_N ? cell[1] : E_SYNTAX];
    if (cell[2] == T_STR) return (const char *) cell + RES;
    v = (fbits) cell[4] | ((fbits) cell[5] << 8) | ((fbits) cell[6] << 16) | ((fbits) cell[7] << 24);
    if (cell[2] == T_BOOL) return fcmp(v, F0) ? "TRUE" : "FALSE";
    return fmt_num(v, cell[3]);
}
static uint8_t in_sel(uint8_t c, uint16_t r)
{
    uint8_t c0 = ac < cc ? ac : cc, c1 = ac < cc ? cc : ac; uint16_t r0 = ar < cr ? ar : cr, r1 = ar < cr ? cr : ar;
    return (uint8_t)(selon && c >= c0 && c <= c1 && r >= r0 && r <= r1);
}
static void sel_box(uint8_t *c0, uint16_t *r0, uint8_t *c1, uint16_t *r1)   /* the selection, or the cell alone */
{
    if (selon) { *c0 = ac < cc ? ac : cc; *c1 = ac < cc ? cc : ac; *r0 = ar < cr ? ar : cr; *r1 = ar < cr ? cr : ar; }
    else { *c0 = *c1 = cc; *r0 = *r1 = cr; }
}
#define GUT 5                                 /* the frame's bar and the row numbers: cells start at column 5 */
static uint8_t vis_cols(void)
{
    uint8_t x = GUT, n = 0, c;
    for (c = lc; c < NCOL; c++) { if (x + colw[c] > cols - 1) break; x = (uint8_t)(x + colw[c]); n++; }
    return n ? n : 1;
}
static void layout(void) { th = (uint8_t)(rows - 6); vc = vis_cols(); }
static void keep_visible(void)
{
    if (cr < tr) tr = cr;
    if (cr >= tr + th) tr = (uint16_t)(cr - th + 1);
    if (cc < lc) lc = cc;
    vc = vis_cols();
    while (cc >= lc + vc) { lc++; vc = vis_cols(); }
}
static void grid_row(uint8_t i)              /* window row i: its number, its cells, the scroll bar's cell */
{
    uint16_t r = (uint16_t)(tr + i); uint8_t c, x = GUT, w, n, j, k, f, b, t; const char *s; const char *run = 0; uint8_t runk = 0;
    cel(0, 0xB3, K_FRAME);
    if (r < NROW) { dma_copy(saddr(0, r), (uint32_t)(uint16_t) srow, NCOL); k = (uint8_t)(r == cr ? K_MSEL : K_MENU);
        cel(1, (uint8_t)(r >= 99 ? '0' + (r + 1) / 100 : ' '), k); cel(2, (uint8_t)(r >= 9 ? '0' + (r + 1) / 10 % 10 : ' '), k); cel(3, (uint8_t)('0' + (r + 1) % 10), k); cel(4, ' ', k); }
    else { for (j = 1; j < GUT; j++) cel(j, ' ', K_MENU); memset(srow, 0, NCOL); }
    for (c = lc; c < lc + vc; c++) {
        w = (uint8_t)(colw[c] - 1);
        k = (uint8_t)(r < NROW && c == cc && r == cr ? K_SEL : in_sel(c, r) ? K_MSEL : K_TEXT);
        f = kf[k]; b = kb[k];
        t = (uint8_t)(srow[c] & 3);
        if (t && !(entering && c == cc && r == cr)) get_cell(c, r);
        if (entering && c == cc && r == cr) { s = ebuf; for (j = 0; j < w; j++) cel((uint8_t)(x + j), (uint8_t)(*s ? *s++ : ' '), k); run = 0; }
        else if (t == KIND_TEXT || (t == KIND_VAL && rec[2] == T_STR && !rec[1])) {
            s = t == KIND_TEXT ? (const char *) rec + SRC : (const char *) rec + RES;
            for (j = 0; j < w; j++) cel((uint8_t)(x + j), (uint8_t)(*s ? *s++ : ' '), k);
            run = *s ? s : 0; runk = k;      /* what did not fit runs on into empty cells, as in Excel */
        } else if (t == KIND_VAL) {
            s = cell_text(rec); n = (uint8_t) strlen(s);
            if (k == K_TEXT && rec[1]) f = sysc ? kf[K_MHOT] : 10;   /* an error in light red */
            if (n > w) for (j = 0; j < w; j++) celc((uint8_t)(x + j), '#', f, b);
            else { for (j = 0; j < w - n; j++) celc((uint8_t)(x + j), ' ', f, b); for (; j < w; j++) celc((uint8_t)(x + j), (uint8_t) s[j - (w - n)], f, b); }
            run = 0;
        } else if (run && k == K_TEXT) { for (j = 0; j < w; j++) cel((uint8_t)(x + j), (uint8_t)(*run ? *run++ : ' '), runk); if (!*run) run = 0; }
        else { for (j = 0; j < w; j++) cel((uint8_t)(x + j), ' ', k); run = 0; }
        if (run && k != K_SEL) { cel((uint8_t)(x + w), (uint8_t) *run++, runk); if (!*run) run = 0; }
        else cel((uint8_t)(x + w), ' ', (uint8_t)(k == K_SEL ? K_TEXT : k));
        x = (uint8_t)(x + colw[c]);
    }
    for (; x < cols - 1; x++) cel(x, ' ', K_TEXT);
    t = thumb(tr, NROW - th + 1, vtrack(4, (uint8_t)(4 + th - 1)));
    cel((uint8_t)(cols - 1), (uint8_t)(!i ? 0x1E : i == th - 1 ? 0x1F : (i - 1 == t ? 0xDB : 0xB0)), K_SCROLL);
    flush((uint8_t)(4 + i), cols);
}
static void formula_bar(void)                /* row 2: the cell's name, what is in it, and what it comes to */
{
    uint8_t x, n, j, k; const char *s;
    cel(0, 0xB3, K_FRAME);
    s = cell_name(cc, cr); n = (uint8_t) strlen(s);
    cel(1, ' ', K_MENU); for (j = 0; j < 5; j++) cel((uint8_t)(2 + j), (uint8_t)(j < n ? s[j] : ' '), K_MENU); cel(7, ' ', K_MENU);
    for (x = 8; x < cols - 1; x++) cel(x, ' ', K_TEXT);
    if (entering) { s = ebuf; for (x = 9; *s && x < cols - 2; x++) cel(x, (uint8_t) *s++, K_TEXT); }
    else {
        k = kind_of(cc, cr);
        if (k) {
            get_cell(cc, cr);
            s = (const char *) rec + SRC;
            if (k == KIND_TEXT && (rec[SRC] == '=' || rec[SRC] == '+' || rec[SRC] == '-' || rec[SRC] == '\'' || all_number(s))) cel(9, '\'', K_TEXT), x = 10; else x = 9;
            for (; *s && x < cols - 2; x++) cel(x, (uint8_t) *s++, K_TEXT);
            if (k == KIND_VAL && rec[SRC] == '=') {   /* a formula: what it comes to, at the right */
                s = cell_text(rec); n = (uint8_t) strlen(s);
                if (n + 4 < cols - 2 - x) { x = (uint8_t)(cols - 3 - n); celc((uint8_t)(x - 2), '=', rec[1] ? (sysc ? kf[K_MHOT] : 10) : kf[K_TEXT], kb[K_TEXT]); for (j = 0; j < n; j++) celc((uint8_t)(x + j), (uint8_t) s[j], rec[1] ? (sysc ? kf[K_MHOT] : 10) : kf[K_TEXT], kb[K_TEXT]); }
            }
        }
    }
    cel((uint8_t)(cols - 1), 0xB3, K_FRAME);
    flush(2, cols);
}
static void col_heads(void)                  /* row 3 */
{
    uint8_t x = GUT, c, w, n, j, k; char b[3];
    cel(0, 0xB3, K_FRAME);
    for (j = 1; j < GUT; j++) cel(j, ' ', K_MENU);
    for (c = lc; c < lc + vc; c++) {
        w = colw[c]; k = (uint8_t)(c == cc ? K_MSEL : K_MENU);
        col_name(c, b); n = (uint8_t) strlen(b);
        for (j = 0; j < w; j++) cel((uint8_t)(x + j), ' ', k);
        for (j = 0; j < n; j++) cel((uint8_t)(x + (w - 1 - n) / 2 + j), (uint8_t) b[j], k);
        x = (uint8_t)(x + w);
    }
    for (; x < cols - 1; x++) cel(x, ' ', K_MENU);
    cel((uint8_t)(cols - 1), 0xB3, K_FRAME);
    flush(3, cols);
}
static void foot(void)                       /* the window's foot: └◄░░█░░►─┘, the thumb by the column */
{
    uint8_t x, t = (uint8_t)((unsigned) lc * (cols - 6) / NCOL);
    cel(0, 0xC0, K_FRAME);
    cel(1, 0x11, K_SCROLL);
    for (x = 0; x < cols - 5; x++) cel((uint8_t)(2 + x), (uint8_t)(x == t ? 0xDB : 0xB0), K_SCROLL);
    cel((uint8_t)(cols - 3), 0x10, K_SCROLL);
    cel((uint8_t)(cols - 2), 0xC4, K_FRAME);
    cel((uint8_t)(cols - 1), 0xD9, K_FRAME);
    flush((uint8_t)(rows - 2), cols);
}
static uint8_t modified;
static void draw(void)
{
    uint8_t i; uint8_t c0, c1; uint16_t r0, r1, r; val w; fbits s; uint8_t c, k, cnt = 0;
    if (full) { band_file(); menubar(-1); frame_top(1, name[0] ? base_of(name) : (const char *)"Untitled", 1); }
    formula_bar();
    if (!entry_only) col_heads();
    if (full || gridall || lastcr == 0xFFFF) for (i = 0; i < th; i++) grid_row(i);
    else {
        if (lastcr != cr && lastcr >= tr && lastcr < tr + th) grid_row((uint8_t)(lastcr - tr));
        grid_row((uint8_t)(cr - tr));
    }
    if (!entry_only) foot();
    nb_reset();
    if (selon) {                             /* the range, and what its numbers add up to */
        sel_box(&c0, &r0, &c1, &r1);
        nb_s(cell_name(c0, r0)); nb_s(":"); nb_s(cell_name(c1, r1));
        s = F0; err = 0; defer = 0; sa_reset();
        for (r = r0; r <= r1; r++) for (c = c0; c <= c1; c++) { if ((k = kind_of(c, r)) != KIND_VAL) continue; cell_val(c, r, &w); if (w.t == T_NUM && !err) { s = f2(MATH_ADD, s, w.n); cnt++; } err = 0; }
        if (cnt) { nb_s("  Sum "); nb_s(ftoa(s)); }
    } else nb_s(cell_name(cc, cr));
    if (modified) nb_s(" *");
    status_line(*note ? note : (const char *)"CALC  <F1=Help>  <F10 or Alt=Menus>", nbuf);
    full = 0; gridall = 0; entry_only = 0; lastcr = cr;
    if (entering) { cursor_shape('4'); cursor_at((uint8_t)(9 + ep < cols - 2 ? 9 + ep : cols - 2), 2); cursor_show(1); }
    else cursor_show(0);
}

/* ---- files: a line a cell ----------------------------------------------- */
#pragma code-name (push, "HICODE")           /* at $E000 (demo/calc.cfg): the main image is full */
static uint8_t obuf[256];
static uint16_t on;                          /* 16 bits: an 8-bit count wraps before it reaches 256, and never flushes */
static uint8_t oerr;
static void oflush(void) { if (!on) return; fs_w32(8, (uint16_t) obuf); fs_w32(12, on); if (fs_cmd(4)) oerr = 1; on = 0; }
static void emit(char ch) { obuf[on++] = (uint8_t) ch; if (on == sizeof obuf) oflush(); }
static void emits(const char *s) { while (*s) emit(*s++); }
static uint8_t out_open(const char *nm) { fs_w32(4, (uint16_t) nm); if (fs_cmd(2)) return 0; on = 0; oerr = 0; return 1; }
static uint8_t out_close(void) { oflush(); fs_cmd(5); return (uint8_t)!oerr; }
static void save_sheet(void)
{
    uint8_t c; uint16_t r;
    if (!out_open(name)) { note = "Could not write that name"; return; }
    emits("K4CALC 3 W"); emit((char)('0' + cw / 10)); emit((char)('0' + cw % 10)); emit('\n');
    for (c = 0; c < NCOL; c++) if (colw[c] != cw) { emits("W:"); col_name(c, cname); emits(cname); emit(':'); if (colw[c] >= 10) emit((char)('0' + colw[c] / 10)); emit((char)('0' + colw[c] % 10)); emit('\n'); }
    for (r = 0; r < NROW; r++) {
        if (!rowused[r]) continue;
        for (c = 0; c < NCOL; c++) {
            if (!kind_of(c, r)) continue;
            get_cell(c, r);
            emits(cell_name(c, r)); emit(':');
            emit(rec[0] == KIND_TEXT ? 'T' : (rec[SRC] == '=' ? 'F' : 'N'));   /* text, formula, number */
            if (rec[3] != F_GEN) {           /* its format: decimals, then , % D */
                if ((rec[3] & 15) != 15) emit((char)('0' + (rec[3] & 15)));
                if (rec[3] & 16) emit(','); if (rec[3] & 32) emit('%'); if (rec[3] & 64) emit('D');
            }
            emit(':');
            emits((const char *) rec + SRC); emit('\n');
        }
    }
    if (!out_close()) note = "The disk would not take it all";
    else { modified = 0; note = "Saved"; }
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
        t[n + 1] = 0;                        /* the end moves too (it did not until 2026-10-10: *100 came over as *10000) */
        while (n) { t[n] = t[n - 1]; n--; }
        t[0] = '=';
    }
}
static uint32_t lp, lend;                    /* the file being read, in far memory */
static char line[TXTMAX + 24];
static uint8_t read_line(void)               /* the next line into line[]; 0 at the end */
{
    uint8_t n = 0, k;
    if (lp >= lend) return 0;
    while (lp < lend && (k = far_peek(lp)) != '\n') { if (n < sizeof line - 1 && k != '\r') line[n++] = (char) k; lp++; }
    lp++; line[n] = 0;
    return 1;
}
static uint8_t load_file(const char *nm)     /* 0 loaded, 1 absent, 2 too big or unreadable */
{
    uint8_t st;
    fs_w32(4, (uint16_t) nm); fs_w32(8, LOADBUF); fs_w32(12, LOADMAX);
    st = fs_cmd(9);
    if (st == 1) return 1;
    if (st) return 2;
    lp = LOADBUF; lend = LOADBUF + fs_r32(12);
    return 0;
}
static uint8_t load_sheet(const char *nm)    /* 0 loaded, 1 absent, 2 not a sheet */
{
    static char text[TXTMAX + 2];
    uint8_t n, c, k, legacy = 0, fmt, sheetfmt = F_GEN, a; uint16_t r; const char *e, *p;
    n = load_file(nm);
    if (n) return n;
    if (far_peek(LOADBUF) != 'K' || far_peek(LOADBUF + 1) != '4' || far_peek(LOADBUF + 2) != 'C') return 2;
    clear_sheet();
    while (read_line()) {
        if (line[0] == 'K' && line[1] == '4') {                  /* K4CALC v Wnn [Fx] */
            legacy = (uint8_t)(line[7] == '1');
            for (n = 0; line[n]; n++) {
                if (line[n] == 'W' && is_digit(line[n + 1])) { cw = (uint8_t)((line[n + 1] - '0') * 10 + (is_digit(line[n + 2]) ? line[n + 2] - '0' : 0)); if (!is_digit(line[n + 2])) cw = (uint8_t)(line[n + 1] - '0'); }
                if (line[n] == 'F' && line[n + 1] == '$') sheetfmt = 2;   /* the older sheets' one format for all: two places */
            }
            if (cw < 3 || cw > 40) cw = 9;
            for (c = 0; c < NCOL; c++) colw[c] = cw;
            continue;
        }
        if (line[0] == 'W' && line[1] == ':') {                  /* W:AB:12 -- one column's width */
            p = line + 2; k = (uint8_t)(upper(*p++) - 'A');
            if (is_alpha(*p)) k = (uint8_t)((k + 1) * 26 + (upper(*p++) - 'A'));
            if (*p++ != ':') continue;
            n = 0; while (is_digit(*p)) n = (uint8_t)(n * 10 + (*p++ - '0'));
            if (k < NCOL && n >= 3 && n <= 40) colw[k] = n;
            continue;
        }
        if (ref_at(line, &c, &r, &a, &e) != 1 || *e != ':') continue;
        p = e + 1; k = upper(*p++);
        memset(rec, 0, CELLSZ);
        if (k == 'T' || k == 'L') rec[0] = KIND_TEXT;               /* L: the first CALC's label */
        else if (k == 'N' || k == 'F' || k == 'V') rec[0] = KIND_VAL;
        else continue;
        fmt = sheetfmt;
        while (*p && *p != ':') {                                 /* the format letters, K4CALC 3 */
            if (is_digit(*p)) fmt = (uint8_t)((fmt & 0xF0) | (*p - '0'));
            else if (*p == ',') fmt |= 16; else if (*p == '%') fmt |= 32; else if (*p == 'D') fmt |= 64;
            p++;
        }
        if (*p != ':') continue;
        rec[3] = rec[0] == KIND_VAL ? fmt : F_GEN;
        if ((rec[3] & 64) && colw[c] < 11) colw[c] = 11;
        strncpy(text, p + 1, TXTMAX); text[TXTMAX] = 0;
        if (legacy && rec[0] == KIND_VAL) modernise(text);
        strncpy((char *) rec + SRC, text, TXTMAX);
        put_rec(c, r);
        if (!kind_of(c, r)) rowused[r]++;
        set_state(c, r, rec[0], 0);
    }
    recalc(); modified = 0;
    return 0;
}
/* CSV, for LibreOffice and Excel.  Out: values as shown to a number's
 * general spelling (never the thousands' commas), text quoted when it must
 * be, a formula's value and not the formula, as those two write it.  In:
 * the separator told from the first line, quotes undone, each field taken
 * for what it looks like. */
static void export_csv(const char *nm)
{
    uint8_t c, lastc = last_col(), q; uint16_t r, lastr = last_row(); const char *s; char sep = csvsemi ? ';' : ',';
    if (!out_open(nm)) { note = "Could not write that name"; return; }
    for (r = 0; r < lastr; r++) {
        for (c = 0; c < lastc; c++) {
            if (c) emit(sep);
            if (!kind_of(c, r)) continue;
            get_cell(c, r);
            if (rec[0] == KIND_TEXT) s = (const char *) rec + SRC;
            else if (rec[1] || rec[2] != T_NUM) s = cell_text(rec);
            else { fbits v = (fbits) rec[4] | ((fbits) rec[5] << 8) | ((fbits) rec[6] << 16) | ((fbits) rec[7] << 24); s = ftoa(v); }
            for (q = 0; s[q]; q++) if (s[q] == sep || s[q] == '"' || s[q] == ',' || s[q] == ';') break;
            q = (uint8_t)(s[q] != 0 || (rec[0] == KIND_TEXT && (all_number(s) || s[0] == '=' || s[0] == '+' || s[0] == '-')));
            if (q) { emit('"'); for (; *s; s++) { if (*s == '"') emit('"'); emit(*s); } emit('"'); }
            else emits(s);
        }
        emit('\n');
    }
    if (!out_close()) note = "The disk would not take it all"; else note = "Exported";
}
static uint8_t import_csv(const char *nm)    /* 0 done, 1 absent, 2 unreadable */
{
    static char field[TXTMAX + 2]; uint8_t n, c, quoted, k, cut = 0; uint16_t r = 0, nc = 0, ns = 0, nt = 0; char sep, *p;
    n = load_file(nm);
    if (n) return n;
    clear_sheet();
    read_line();
    for (p = line, quoted = 0; *p; p++) { if (*p == '"') quoted ^= 1; else if (!quoted) { if (*p == ',') nc++; else if (*p == ';') ns++; else if (*p == '\t') nt++; } }
    sep = nt > nc && nt > ns ? '\t' : ns > nc ? ';' : ',';
    lp = LOADBUF;
    while (read_line() && r < NROW) {
        p = line; c = 0;
        if (!line[0]) { r++; continue; }
        for (;;) {
            n = 0; quoted = 0;
            while (*p == ' ' && *(p + 1) != sep) p++;
            if (*p == '"') { quoted = 1; p++; for (;;) { if (!*p) break; if (*p == '"') { if (p[1] == '"') p++; else { p++; break; } } if (n < TXTMAX) field[n++] = *p; p++; } while (*p && *p != sep) p++; }
            else while (*p && *p != sep) { if (n < TXTMAX) field[n++] = *p; p++; }
            field[n] = 0;
            if (n) {
                if (c < NCOL) {
                    k = (uint8_t)(quoted && !all_number(field) && field[0] != '=');   /* a quoted text that would read as a formula or number: ' keeps it text */
                    if (k && (field[0] == '+' || field[0] == '-' || field[0] == '\'')) { memmove(field + 1, field, n + 1); field[0] = '\''; }
                    put_cell(c, r, field);
                } else cut = 1;
            }
            c++;
            if (*p != sep) break;
            p++;
        }
        r++;
    }
    if (lp < lend) cut = 1;
    recalc(); modified = 1;
    note = cut ? "Imported; what lay past AZ999 was left out" : "Imported";
    return 0;
}

#pragma code-name (pop)

/* ---- the clipboard and the fills -------------------------------------------- */
static uint8_t clip_w, clip_c0, clip_on; static uint16_t clip_h, clip_r0; static char cnote[24];
static void copy_sel(void)
{
    uint8_t c0, c1, c; uint16_t r0, r1, r;
    sel_box(&c0, &r0, &c1, &r1);
    clip_w = (uint8_t)(c1 - c0 + 1); clip_h = (uint16_t)(r1 - r0 + 1); clip_c0 = c0; clip_r0 = r0; clip_on = 1;
    for (r = r0; r <= r1; r++) for (c = c0; c <= c1; c++) {
        if (kind_of(c, r)) dma_copy(caddr(c, r), CLIP + ((uint32_t)(r - r0) * clip_w + (c - c0)) * CELLSZ, CELLSZ);
        else dma_fill(0, CLIP + ((uint32_t)(r - r0) * clip_w + (c - c0)) * CELLSZ, CELLSZ);
    }
    nb_reset(); nb_n((unsigned long) clip_w * clip_h); nb_s(clip_w * clip_h == 1 ? " cell copied" : " cells copied");
    strcpy(cnote, nbuf); note = cnote;         /* its own buffer: draw() builds the status line's right end in nbuf */
}
/* a copy of the cell at CLIP's slot i put at (c, r), its references moved by (dc, dr) */
static void place(uint32_t src, uint8_t c, uint16_t r, int dc, int dr)
{
    uint8_t fmt;
    dma_copy(src, (uint32_t)(uint16_t) rrec, CELLSZ);
    if (rrec[0] == KIND_VAL && rrec[SRC] != 0 && !all_number((const char *) rrec + SRC)) { shift_refs(tbuf, (const char *) rrec + SRC, dc, dr); put_cell(c, r, tbuf); }
    else if (rrec[0] == KIND_TEXT) { tbuf[0] = '\''; strcpy(tbuf + 1, (const char *) rrec + SRC); put_cell(c, r, tbuf); }
    else put_cell(c, r, (const char *) rrec + SRC);
    fmt = rrec[0] ? rrec[3] : F_GEN;
    set_fmt(c, r, fmt);
}
#pragma code-name (push, "HICODE")
static void clear_sel(void)
{
    uint8_t c0, c1, c; uint16_t r0, r1, r;
    sel_box(&c0, &r0, &c1, &r1);
    for (r = r0; r <= r1; r++) for (c = c0; c <= c1; c++) if (kind_of(c, r)) put_cell(c, r, "");
    modified = 1; gridall = 1; recalc();
}
static void paste(void)
{
    uint8_t c; uint16_t r;
    if (!clip_on) { note = "Nothing copied yet"; return; }
    for (r = 0; r < clip_h && cr + r < NROW; r++) for (c = 0; c < clip_w && cc + c < NCOL; c++)
        place(CLIP + ((uint32_t) r * clip_w + c) * CELLSZ, (uint8_t)(cc + c), (uint16_t)(cr + r), (int) cc - clip_c0, (int) cr - clip_r0);
    if (clip_w > 1 || clip_h > 1) { selon = 1; ac = (uint8_t)(cc + clip_w - 1 < NCOL ? cc + clip_w - 1 : NCOL - 1); ar = (uint16_t)(cr + clip_h - 1 < NROW ? cr + clip_h - 1 : NROW - 1); }
    modified = 1; gridall = 1; recalc();
}
static void fill(uint8_t right)              /* Ctrl+D / Ctrl+R: the first row / column of the selection into the rest; a lone cell takes from above / the left */
{
    uint8_t c0, c1, c; uint16_t r0, r1, r;
    sel_box(&c0, &r0, &c1, &r1);
    if (!selon) { if (right) { if (!cc) return; c0--; } else { if (!cr) return; r0--; } }
    if (right) { if (c1 == c0) return; for (r = r0; r <= r1; r++) for (c = (uint8_t)(c0 + 1); c <= c1; c++) place(caddr(c0, r), c, r, c - c0, 0); }
    else { if (r1 == r0) return; for (c = c0; c <= c1; c++) for (r = (uint16_t)(r0 + 1); r <= r1; r++) place(caddr(c, r0), c, r, 0, r - r0); }
    modified = 1; gridall = 1; recalc();
}
static void fmt_sel(uint8_t how, uint8_t arg)   /* 0 general, 1 decimals arg, 2 thousands, 3 percent, 4 date -- on the cell or the range */
{
    uint8_t c0, c1, c, f; uint16_t r0, r1, r;
    sel_box(&c0, &r0, &c1, &r1);
    for (r = r0; r <= r1; r++) for (c = c0; c <= c1; c++) {
        f = kind_of(c, r) ? far_peek(caddr(c, r) + 3) : F_GEN;
        switch (how) {
        case 0: f = F_GEN; break;
        case 1: f = (uint8_t)((f & 0x30) | arg); break;
        case 2: f ^= 16; f &= 0xBF; if ((f & 15) == 15 && (f & 16)) f = (uint8_t)((f & 0xF0) | 2); break;
        case 3: f ^= 32; f &= 0xBF; if ((f & 15) == 15 && (f & 32)) f = (uint8_t)(f & 0xF0); break;
        default: f = (uint8_t)((f & 64) ? F_GEN : 64 | 15); if ((f & 64) && colw[c] < 11) colw[c] = 11; break;   /* a date is ten cells wide */
        }
        if (!kind_of(c, r)) { if (f != F_GEN) { put_cell(c, r, ""); far_poke(saddr(c, r), 0); } else continue; }
        set_fmt(c, r, f);
    }
    modified = 1; gridall = 1;
}

#pragma code-name (pop)

#pragma code-name (push, "LOCODE")
static void goto_dlg(void)
{
    uint8_t c, a; uint16_t r; const char *e;
    fbuf[0] = 0;
    if (!form1("Go To", "Cell:", fbuf, 8, "OK") || !fbuf[0]) return;
    if (ref_at(fbuf, &c, &r, &a, &e) == 1 && !*e) { cc = c; cr = r; selon = 0; } else note = "A cell is a letter and a number, like B12";
}
static void dec_dlg(void)
{
    fbuf[0] = '2'; fbuf[1] = 0;
    if (!form1("Decimals", "Places (0-9):", fbuf, 2, "OK")) return;
    if (fbuf[0] < '0' || fbuf[0] > '9' || fbuf[1]) { note = "0 to 9 places"; return; }
    fmt_sel(1, (uint8_t)(fbuf[0] - '0'));
}
#pragma code-name (pop)

/* ---- the entry ------------------------------------------------------------ */
static void begin_entry(uint8_t mode)
{
    uint8_t k = kind_of(cc, cr);
    en = 0;
    if (mode == 2 && k) {
        get_cell(cc, cr);
        if (k == KIND_TEXT && (rec[SRC] == '=' || rec[SRC] == '+' || rec[SRC] == '-' || rec[SRC] == '\'' || all_number((const char *) rec + SRC))) ebuf[en++] = '\'';   /* text that would read as a number or a formula needs its ' back */
        strcpy(ebuf + en, (const char *) rec + SRC); en = (uint8_t) strlen(ebuf);
    }
    ebuf[en] = 0; ep = en; entering = mode;
}
static void commit(void)
{
    ebuf[en] = 0;
    put_cell(cc, cr, ebuf);
    entering = 0; modified = 1; gridall = 1;
    recalc();
}
static void ins_char(char c) { if (en >= TXTMAX) return; memmove(ebuf + ep + 1, ebuf + ep, en - ep + 1); ebuf[ep++] = c; en++; }
static void del_char(uint8_t at) { if (at >= en) return; memmove(ebuf + at, ebuf + at + 1, en - at); en--; }
static void cycle_ref(void)                  /* F4: the reference under the point goes A1, $A$1, A$1, $A1, A1 */
{
    const char *s = ebuf, *e; uint8_t c, a, k, prev = 0; uint16_t r; char *d; uint8_t head;
    while (*s) {
        if (*s == '"') { s++; while (*s && *s != '"') s++; if (*s) s++; prev = 0; continue; }
        if (!prev && (k = ref_at(s, &c, &r, &a, &e)) == 1 && s - ebuf <= ep && e - ebuf >= ep) {
            a = (uint8_t)(a == 0 ? 3 : a == 3 ? 2 : a == 2 ? 1 : 0);
            head = (uint8_t)(s - ebuf);
            strcpy(tbuf, e); d = ebuf + head; put_ref(&d, c, r, a); strcpy(d, tbuf);
            en = (uint8_t) strlen(ebuf); ep = (uint8_t)(d - ebuf);
            return;
        }
        if (k) { s = e; prev = 1; continue; }
        prev = (uint8_t)(is_alnum(*s) || *s == '_' || *s == '$');
        s++;
    }
    note = "F4 wants a cell reference under the cursor";
}

/* ---- the commands ---------------------------------------------------------- */
static uint8_t save_as(void)
{
    strcpy(fbuf, name[0] ? name : "SHEET.CAL");
    if (!form1("Save As", "File Name:", fbuf, NAMEMAX, "OK") || !fbuf[0]) return 0;
    strcpy(name, fbuf); save_sheet(); full = 1;
    return (uint8_t)!modified;
}
static uint8_t may_leave(void)
{
    uint8_t a;
    if (!modified) return 1;
    a = ask(0, "The sheet is not saved.  Save it now?", "Yes", "No", "Cancel");
    if (a == 0) { if (name[0]) { save_sheet(); return (uint8_t)!modified; } return save_as(); }
    return (uint8_t)(a == 1);
}
static void fresh(void) { cc = lc = 0; cr = tr = 0; selon = 0; entering = 0; modified = 0; full = 1; lastcr = 0xFFFF; }
static uint8_t ext_is(const char *n, const char *e)
{
    const char *d = 0;
    for (; *n; n++) if (*n == '.') d = n;
    if (!d) return 0;
    for (d++; *d && *e && upper(*d) == (uint8_t) *e; d++, e++) ;
    return (uint8_t)(!*d && !*e);
}
static void open_name(const char *nm)
{
    uint8_t e;
    strcpy(fbuf, nm);
    e = ext_is(fbuf, "CSV") ? import_csv(fbuf) : load_sheet(fbuf);
    if (e == 0) { strcpy(name, fbuf); fresh(); if (ext_is(name, "CSV")) { name[0] = 0; modified = 1; note = "Imported: Save As gives it a name"; } }
    else if (e == 1) { clear_sheet(); strcpy(name, fbuf); fresh(); note = "A new sheet"; }
    else note = "That is not a CALC sheet";
    layout();
}
#pragma code-name (push, "HICODE")
#pragma rodata-name (push, "HICODE")
/* The keys and the formulas pages, and About, at $E000 as EDIT keeps its:
 * lines ended by NUL, the whole by $FF. */
static const char helpall[] =
    "Moving        arrows, PgUp PgDn, Home, End; Ctrl+Home A1, Ctrl+End last\0"
    "Entering      type; Enter keeps and goes down, Tab right, Esc drops it\0"
    "              F2 edits the cell (arrows then move in the entry); F4 puts\0"
    "              $ on the reference under the cursor: A1 $A$1 A$1 $A1\0"
    "Selecting     Shift with an arrow; Del clears, Format applies to it all\0"
    "Clipboard     Ctrl+X Ctrl+C Ctrl+V -- a pasted formula's references move\0"
    "              with it, $ holds them;  Ctrl+D fills down, Ctrl+R right\0"
    "Files         Ctrl+N new, Ctrl+O open, Ctrl+S save, Ctrl+Q exit;\0"
    "              File > Import CSV, Export CSV for LibreOffice and Excel\0"
    "Also          Ctrl+G go to, Ctrl+W column width, F9 recalculate\0"
    "Menus         F10, or Alt and the letter; Esc closes; the mouse works\0"
    "\0"
    "CALC -s NAME  starts in the console's own colours (Options)\0"
    "\xFF";
static const char funcall[] =
    "=A1*2  =(B3+B4)/2  =2^10  =SUM(A1:A9)  =AVERAGE(B1:B5;10)   , or ;\0"
    "= <> < > <= >=  compare (TRUE/FALSE);  & joins text;  \"text\" quoted\0"
    "SUM AVERAGE MIN MAX COUNT COUNTA   over ranges and lists\0"
    "ABS INT SQRT ROUND(x;n) MOD(a;b) PI()\0"
    "IF(c;a;b) AND OR NOT ISERROR IFERROR(x;alt) NA()\0"
    "LEN LEFT(s;n) RIGHT(s;n) MID(s;from;n) UPPER LOWER TRIM CONCAT VALUE\0"
    "TODAY() NOW()   days since 1899-12-30; Cells > Date shows them as dates\0"
    "\0"
    "#DIV/0! #REF! #NAME? #VALUE! #N/A #NUM!  as Excel;  #CIRC! a circle;\0"
    "#ERROR! a formula that will not parse;  ##### a number too wide\0"
    "\xFF";
static const char aboutall[] =
    "CALC -- a spreadsheet\0"
    "\0"
    "MS-DOS EDIT's manner, Excel's spelling,\0"
    "the MATH unit's arithmetic.\0"
    "\xFF";
static void help(uint8_t which)
{
    const char *l[16], *p = which == 2 ? aboutall : which ? funcall : helpall; uint8_t n = 0;
    while (*p != '\xFF' && n < 15) { l[n++] = p; while (*p++) ; }
    l[n] = 0;
    text_box(which == 2 ? "About" : which ? "CALC -- the formulas" : "CALC -- the keys", l);
}
static void width_dlg(uint8_t all)
{
    uint8_t n = 0, i, w = colw[cc];
    fbuf[0] = (char)('0' + w / 10); fbuf[1] = (char)('0' + w % 10); fbuf[2] = 0;
    if (w < 10) { fbuf[0] = fbuf[1]; fbuf[1] = 0; }
    if (!form1(all ? "All Columns" : "Column Width", "Width (3-40):", fbuf, 3, "OK")) return;
    for (i = 0; fbuf[i] >= '0' && fbuf[i] <= '9'; i++) n = (uint8_t)(n * 10 + (fbuf[i] - '0'));
    if (!i || fbuf[i] || n < 3 || n > 40) { note = "A width is 3 to 40"; return; }
    if (all) { cw = n; for (i = 0; i < NCOL; i++) colw[i] = n; } else colw[cc] = n;
    modified = 1; full = 1; layout(); keep_visible();
}
#pragma rodata-name (pop)
#pragma code-name (pop)
static void run_cmd(uint8_t c)
{
    uint8_t ok;
    switch (c) {
    case C_NEW:    if (may_leave()) { clear_sheet(); name[0] = 0; fresh(); note = ""; } break;
    case C_OPEN:   if (may_leave() && open_dialog(fbuf)) { strcpy(name, fbuf); open_name(name); } full = 1; break;
    case C_SAVE:   if (name[0]) { save_sheet(); break; }   /* else as Save As */
    case C_SAVEAS: save_as(); break;
    case C_IMPORT: if (may_leave() && open_dialog(fbuf)) { ok = import_csv(fbuf); if (ok == 1) note = "No such file"; else if (ok == 2) note = "Could not read it"; else { name[0] = 0; fresh(); modified = 1; note = "Imported: Save As gives it a name"; } } full = 1; break;
    case C_EXPORT: { uint8_t i; const char *b = base_of(name); for (i = 0; b[i] && b[i] != '.' && i < NAMEMAX - 5; i++) fbuf[i] = b[i]; if (!i) { strcpy(fbuf, "SHEET"); i = 5; } strcpy(fbuf + i, ".CSV"); }
                   if (form1("Export CSV", "File Name:", fbuf, NAMEMAX, "OK") && fbuf[0]) export_csv(fbuf); full = 1; break;
    case C_EXIT:   if (may_leave()) running = 0; break;
    case C_CUT:    copy_sel(); clear_sel(); break;
    case C_COPY:   copy_sel(); break;
    case C_PASTE:  paste(); break;
    case C_CLEAR:  clear_sel(); break;
    case C_FILLD:  fill(0); break;
    case C_FILLR:  fill(1); break;
    case C_GOTO:   goto_dlg(); break;
    case C_RECALC: recalc(); gridall = 1; break;
    case C_FGEN:   fmt_sel(0, 0); break;
    case C_FDEC:   dec_dlg(); break;
    case C_FTHOU:  fmt_sel(2, 0); break;
    case C_FPCT:   fmt_sel(3, 0); break;
    case C_FDATE:  fmt_sel(4, 0); break;
    case C_WIDTH:  width_dlg(0); break;
    case C_WIDTHALL: width_dlg(1); break;
    case C_DOS:    scheme(0); full = 1; break;
    case C_SYS:    scheme(1); full = 1; break;
    case C_SEMI:   csvsemi ^= 1; note = csvsemi ? "CSV goes out with semicolons" : "CSV goes out with commas"; break;
    case C_HELP:   help(0); break;
    case C_FUNCS:  help(1); break;
    case C_ABOUT:  help(2); break;
    }
}
static const char *const mtitle[] = { "File", "Edit", "Cells", "Options", "Help" };   /* Cells, not Format: dosui opens a menu by its first letter, and File has the F */
static const struct item m_file[]   = { { "New", 0, C_NEW, "Ctrl+N" }, { "Open...", 0, C_OPEN, "Ctrl+O" }, { "Save", 0, C_SAVE, "Ctrl+S" },
                                        { "Save As...", 5, C_SAVEAS, "" }, { "", 0, C_SEP, "" }, { "Import CSV...", 0, C_IMPORT, "" }, { "Export CSV...", 2, C_EXPORT, "" },
                                        { "", 0, C_SEP, "" }, { "Exit", 1, C_EXIT, "Ctrl+Q" }, { 0, 0, 0, 0 } };
static const struct item m_edit[]   = { { "Cut", 2, C_CUT, "Ctrl+X" }, { "Copy", 0, C_COPY, "Ctrl+C" }, { "Paste", 0, C_PASTE, "Ctrl+V" },
                                        { "Clear", 2, C_CLEAR, "Del" }, { "", 0, C_SEP, "" }, { "Fill Down", 5, C_FILLD, "Ctrl+D" }, { "Fill Right", 5, C_FILLR, "Ctrl+R" },
                                        { "", 0, C_SEP, "" }, { "Go To...", 0, C_GOTO, "Ctrl+G" }, { "Recalculate", 3, C_RECALC, "F9" }, { 0, 0, 0, 0 } };
static const struct item m_fmt[]    = { { "General", 0, C_FGEN, "" }, { "Decimals...", 0, C_FDEC, "" }, { "Thousands", 0, C_FTHOU, "" },
                                        { "Percent", 0, C_FPCT, "" }, { "Date", 1, C_FDATE, "" }, { "", 0, C_SEP, "" },
                                        { "Column Width...", 7, C_WIDTH, "Ctrl+W" }, { "All Columns...", 2, C_WIDTHALL, "" }, { 0, 0, 0, 0 } };
static const struct item m_opt[]    = { { "DOS Colours", 0, C_DOS, "" }, { "System Colours", 0, C_SYS, "" }, { "", 0, C_SEP, "" },
                                        { "CSV Semicolons", 0, C_SEMI, "" }, { 0, 0, 0, 0 } };
static const struct item m_help[]   = { { "Keyboard", 0, C_HELP, "F1" }, { "Formulas", 0, C_FUNCS, "Shift+F1" }, { "About CALC...", 0, C_ABOUT, "" }, { 0, 0, 0, 0 } };
static const struct item *const menus[] = { m_file, m_edit, m_fmt, m_opt, m_help };
static uint8_t marked(uint8_t c) { return (uint8_t)((c == C_DOS && !sysc) || (c == C_SYS && sysc) || (c == C_SEMI && csvsemi)); }

/* ---- the keys and the mouse ------------------------------------------------- */
static uint8_t tabc = 0xFF, keep_tab;       /* the column a run of Tabs began in: Enter goes back to it, as Excel does */
static void go_cell(uint8_t shift, uint8_t c, uint16_t r)   /* the cursor to (c, r); with Shift the selection grows to it */
{
    if (!keep_tab) tabc = 0xFF; keep_tab = 0;
    if (shift) { if (!selon) { selon = 1; ac = cc; ar = cr; } gridall = 1; } else if (selon) { selon = 0; gridall = 1; }
    cc = c >= NCOL ? (uint8_t)(NCOL - 1) : c;
    cr = r >= NROW ? (uint16_t)(NROW - 1) : r;
}
static void move(uint8_t shift, int dc, int dr)   /* by so much; dc and dr are ints: cc65 would subtract two bytes as bytes */
{
    int nc = (int) cc + dc; long nr = (long) cr + dr;
    go_cell(shift, (uint8_t)(nc < 0 ? 0 : nc), (uint16_t)(nr < 0 ? 0 : nr));
}
static void row_end(void)                    /* End: the last cell in this row with anything in it */
{
    uint8_t c, last = 0;
    dma_copy(saddr(0, cr), (uint32_t)(uint16_t) srow, NCOL);
    for (c = 0; c < NCOL; c++) if (srow[c] & 3) last = c;
    cc = last;
}
static void relayout(void) { layout(); keep_visible(); }
static void tab_move(uint8_t back) { if (tabc == 0xFF) tabc = cc; keep_tab = 1; move(0, back ? -1 : 1, 0); }
static void enter_move(void) { uint8_t t = tabc; go_cell(0, t == 0xFF ? cc : t, (uint16_t)(cr + 1)); }
static void grid_click(uint8_t shift)
{
    uint8_t c, x = GUT; uint16_t r;
    if (mrow < 4 || mrow >= 4 + th || mcol < GUT || mcol >= cols - 1) return;
    r = (uint16_t)(tr + mrow - 4); if (r >= NROW) return;
    for (c = lc; c < lc + vc; c++) { if (mcol < x + colw[c]) break; x = (uint8_t)(x + colw[c]); }
    if (c >= lc + vc) return;
    if (entering) commit();
    go_cell(shift, c, r);
}
static void do_key(uint8_t k)
{
    uint8_t i, shift = (uint8_t)(kmod & 1), ctrl = (uint8_t)(kmod & 2);
    if (kcode == 2) {
        if (mev == 4) { if (mwheel > 0) { tr = tr > 3 ? (uint16_t)(tr - 3) : 0; } else { tr = (uint16_t)(tr + 3 < NROW - th ? tr + 3 : NROW - th); } if (cr < tr) cr = tr; if (cr >= tr + th) cr = (uint16_t)(tr + th - 1); gridall = 1; return; }
        if (mev == 3 || mev == 5) { if (mev == 5) relayout(); return; }
        if (mrow == 0 && mev == 1) { i = title_at(mcol); if (i < ui_nmenu) run_cmd(menu(i)); return; }
        if (mcol == cols - 1 && mrow >= 4 && mrow < 4 + th && mev == 1) {   /* the scroll bar: its arrows, a page either side */
            if (mrow == 4) move(0, 0, -1); else if (mrow == 4 + th - 1) move(0, 0, 1); else move(0, 0, mrow - 4 < th / 2 ? -(int) th : (int) th);
            return;
        }
        grid_click((uint8_t)(shift || mev == 2));
        return;
    }
    if (kcode && k >= KALT && k < KALT + 26) { if (entering) commit(); i = title_of(k); if (i < ui_nmenu) run_cmd(menu(i)); return; }
    if (entering) {
        if (!kcode) {
            switch (k) {
            case 0x0D: commit(); enter_move(); return;
            case 0x09: commit(); tab_move(shift); return;
            case 0x1B: entering = 0; gridall = 1; return;
            case 0x08: if (ep) { ep--; del_char(ep); } entry_only = 1; return;
            case 0x7F: del_char(ep); entry_only = 1; return;
            }
            if (k >= 0x20) { ins_char((char) k); entry_only = 1; }
            return;
        }
        switch (k) {
        case KDEL:   del_char(ep); entry_only = 1; return;
        case KF(2):  entering = 2; entry_only = 1; return;
        case KF(4):  cycle_ref(); entry_only = 1; return;
        case KHOME:  ep = 0; entry_only = 1; return;
        case KEND:   ep = en; entry_only = 1; return;
        case KLEFT:  if (entering == 2) { if (ep) ep--; entry_only = 1; return; } commit(); move(0, -1, 0); return;
        case KRIGHT: if (entering == 2) { if (ep < en) ep++; entry_only = 1; return; } commit(); move(0, 1, 0); return;
        case KUP:    commit(); move(0, 0, -1); return;
        case KDOWN:  commit(); move(0, 0, 1); return;
        case KF(10): commit(); run_cmd(menu(0)); return;
        }
        return;
    }
    if (!kcode) {
        switch (k) {
        case 0x0D: enter_move(); return;
        case 0x09: tab_move(shift); return;
        case 0x0E: run_cmd(C_NEW); return;           /* ^N */
        case 0x0F: run_cmd(C_OPEN); return;          /* ^O */
        case 0x13: run_cmd(C_SAVE); return;          /* ^S */
        case 0x11: case 0x1B: run_cmd(C_EXIT); return;   /* ^Q, Esc */
        case 0x18: run_cmd(C_CUT); return;           /* ^X */
        case 0x03: run_cmd(C_COPY); return;          /* ^C */
        case 0x16: run_cmd(C_PASTE); return;         /* ^V */
        case 0x04: run_cmd(C_FILLD); return;         /* ^D */
        case 0x12: run_cmd(C_FILLR); return;         /* ^R */
        case 0x07: run_cmd(C_GOTO); return;          /* ^G */
        case 0x17: run_cmd(C_WIDTH); return;         /* ^W */
        case 0x08: case 0x7F: run_cmd(C_CLEAR); return;
        }
        if (k >= 0x20) { begin_entry(1); ins_char((char) k); gridall = selon; selon = 0; entry_only = !gridall; }
        return;
    }
    switch (k) {
    case KUP:    move(shift, 0, -1); break;
    case KDOWN:  move(shift, 0, 1); break;
    case KLEFT:  move(shift, -1, 0); break;
    case KRIGHT: move(shift, 1, 0); break;
    case KPGUP:  move(shift, 0, -(int) th); break;
    case KPGDN:  move(shift, 0, th); break;
    case KHOME:  go_cell(shift, 0, ctrl ? 0 : cr); break;
    case KEND:   if (ctrl) { uint16_t lr = last_row(); uint8_t lcol = last_col(); go_cell(shift, (uint8_t)(lcol ? lcol - 1 : 0), (uint16_t)(lr ? lr - 1 : 0)); } else { uint8_t o = cc; row_end(); i = cc; cc = o; go_cell(shift, i, cr); } break;
    case KDEL:   run_cmd(C_CLEAR); break;
    case KF(1):  run_cmd(shift ? C_FUNCS : C_HELP); break;
    case KF(2):  begin_entry(2); entry_only = 1; break;
    case KF(9):  run_cmd(C_RECALC); break;
    case KF(10): run_cmd(menu(0)); break;
    }
}

void main(void)
{
    const char *p; uint8_t i = 0, q = 0, sys = 0, k;
    rom_args();
    p = *(const char **) 0xF0;
    while (*p == ' ') p++;
    if (p[0] == '-' && (p[1] == 's' || p[1] == 'S') && (p[2] == ' ' || !p[2])) { sys = 1; p += 2; while (*p == ' ') p++; }   /* CALC [-s] [name] */
    if (*p == '"') { q = 1; p++; }
    while (*p && i < NAMEMAX - 1 && (q ? *p != '"' : *p != ' ')) name[i++] = *p++;
    name[i] = 0;

    F0 = fint(0); F1 = fint(1); F10 = fint(10); F100 = fint(100); F2E9 = fint(2000000000L);
    FPI = f2(MATH_MUL, f1(MATH_ATAN, fint(1)), fint(4));
    clear_sheet();
    ui_init();
    ui_titles = mtitle; ui_menus = menus; ui_nmenu = 5; ui_marked = marked; ui_dirtab = 0x0EE00000UL; ui_name = "CALC "; ui_relayout = relayout;
    layout();
    scheme(sys);
    ui_start(); cursor_show(0);
    ptr_on();
    fresh();
    if (name[0]) { strcpy(fbuf, name); name[0] = 0; open_name(fbuf); }
    else note = "A new sheet: type a number or a word, = for a formula; F1 the keys";
    while (running) {
        keep_visible();
        draw();
        k = event();
        if (kcode != 2 || mev == 1) note = "";
        do_key(k);
    }
    ptr_off();
    ui_end();                                         /* the block cursor, JIM's modes and an empty screen for the shell */
    rom_video();
}
