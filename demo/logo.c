/* K4510: LOGO -- turtle graphics on the 45GS10, new code (Doc, 2026-09-11:
 * "Can you make a Logo port for the K4510 (that runs on the 45gs10)?").
 *
 * The classic Logo people remember, natively:
 *
 *   FD BK LT RT PU PD HOME CS SETXY SETX SETY SETH SETPC HT ST  the turtle
 *   XCOR YCOR HEADING                                             its reporters
 *   REPEAT n [ ... ]   IF cond [ ... ]   IFELSE cond [ ... ] [ ... ]
 *   TO name :a :b ... END   STOP   OUTPUT expr   -- with real recursion
 *   MAKE "x expr   :x   PRINT   SHOW   RANDOM n   SQRT SIN COS INT   REPCOUNT
 *   LOAD "name   (a .LGO file, EX/ or the current directory)   BYE
 *
 * Numbers are IEEE floats, every one of them done by the MATH unit at $D700
 * (cc65 has no floating point; the unit has SIN, COS, SQRT and a float-to-text
 * conversion in hardware), so the turtle's heading is a real angle.  Lines
 * are the VICKY blitter's (op 6, as the Pascal GRAPH unit draws them), on a
 * 640x480 8-bit bitmap under the console's text -- the classic Logo screen:
 * the turtle draws below, the prompt lives above.
 *
 * The interpreter walks the SOURCE TEXT with a cursor (a list is a span of
 * it, a procedure a named span in the pool), which keeps it small and keeps
 * the whole program in RAM as text.  Procedure calls recurse on the C stack,
 * so logo.cfg gives it 4 KB of stack and a depth counter turns "too deep"
 * into a message instead of a crash.  Scoping is Logo's own: a procedure's
 * inputs are locals, MAKE writes the nearest local or else a global. */
#include <stdint.h>
#include <string.h>
#include "k4510.h"

#define TERM   0xDA00u
#define KBD    0xD100u
#define FSR    0xD300u
#define BLT    0xD070u
#define MATHR  0xD700u
#define GFX_BASE 0x200000UL
#define GW 640
#define GH 480
#define SPR_DATA (GFX_BASE + (unsigned long) GW * GH)   /* the turtle's 16x16 8-bpp glyph, right after the bitmap */
#define SPR_TAB  (SPR_DATA + 0x100UL)                  /* the sprite attribute table (128 x 16 B); entry 0 is the turtle */
#define CMDLINE ((char *) 0x0300)                      /* SWAP's command line: below our image (see demo/ranger.c) */

typedef unsigned long fbits;                       /* an IEEE single, as bits */

/* ---- the MATH unit ------------------------------------------------------ */
static void w32r(uint16_t a, unsigned long v) { REG(a) = (uint8_t) v; REG(a + 1) = (uint8_t)(v >> 8); REG(a + 2) = (uint8_t)(v >> 16); REG(a + 3) = (uint8_t)(v >> 24); }
static unsigned long r32r(uint16_t a) { return (unsigned long) REG(a) | ((unsigned long) REG(a + 1) << 8) | ((unsigned long) REG(a + 2) << 16) | ((unsigned long) REG(a + 3) << 24); }
#define FREG(n) (MATHR + 4 * (n))
#define FOP(op, d, s) do { REG(0xD721u) = (uint8_t)(((d) << 4) | (s)); REG(0xD720u) = (op); } while (0)
static fbits f2(uint8_t op, fbits a, fbits b) { w32r(FREG(0), a); w32r(FREG(1), b); FOP(op, 0, 1); return r32r(FREG(0)); }
static fbits f1(uint8_t op, fbits a) { w32r(FREG(1), a); FOP(op, 0, 1); return r32r(FREG(0)); }
#define fadd(a, b) f2(MATH_ADD, a, b)
#define fsub(a, b) f2(MATH_SUB, a, b)
#define fmul(a, b) f2(MATH_MUL, a, b)
#define fdiv(a, b) f2(MATH_DIV, a, b)
static fbits fint(long v) { w32r(0xD724u, (unsigned long) v); FOP(MATH_ITOF, 0, 0); return r32r(FREG(0)); }
static long ftoi(fbits a) { w32r(FREG(0), a); FOP(MATH_FTOI, 0, 0); return (long) r32r(0xD724u); }
static int fcmp(fbits a, fbits b)                  /* -1, 0, 1 */
{
    uint8_t fl; w32r(FREG(0), a); w32r(FREG(1), b); FOP(MATH_CMP, 0, 1); fl = REG(0xD722u);
    return (fl & 1) ? 0 : (fl & 2) ? -1 : 1;
}
static char numbuf[24];
static const char *ftoa(fbits a) { w32r(FREG(0), a); w32r(0xD730u, (unsigned long)(uint16_t) numbuf); FOP(MATH_FTOAR, 0, 0); return numbuf; }
static fbits F0, F1, F10, F180, F360, FDEG;        /* constants, made once */

/* ---- the console -------------------------------------------------------- */
static void put(char c) { REG(TERM) = (uint8_t) c; }
static void puts_(const char *s) { while (*s) put(*s++); }
static void nl(void) { put('\n'); }
static uint8_t getin(void) { return REG(KBD); }   /* 0 when nothing is waiting */

/* ---- the screen: a 640x480 8-bit bitmap under the text ------------------- */
static uint8_t gfx_ctrl, pencol = 1, pendown = 1;
static void gfx_open(void)
{
    uint8_t i;
    for (i = 0x21; i <= 0x25; i++) REG(VICKY + i) = 0;
    w32r(VICKY + 0x0A, SPR_TAB); REG(VICKY + 0x0E) = 1;                       /* the turtle is sprite 0 (turtle_show) */
    REG(VICKY + 0x26) = (uint8_t)(GW & 255); REG(VICKY + 0x27) = (uint8_t)(GW >> 8);
    REG(VICKY + 0x28) = 0; REG(VICKY + 0x29) = 0; REG(VICKY + 0x2A) = 0x20; REG(VICKY + 0x2B) = 0;
    gfx_ctrl = REG(VICKY);
    REG(VICKY) = (uint8_t)(gfx_ctrl & 0xF9);                                   /* 640x480, no line doubling */
    REG(VICKY + 0x20) = 0x19;                                                  /* enable | bitmap | 8 bpp */
}
static void gfx_close(void) { REG(VICKY + 0x20) = 0; REG(VICKY + 0x0E) = 0; REG(VICKY) = gfx_ctrl; }
static void gfx_clear(void) { w32r(DMA, 0); w32r(DMA + 4, GFX_BASE); w32r(DMA + 8, (unsigned long) GW * GH); REG(DMA + 0x0C) = 2; }
static void blt_target(uint8_t colour, unsigned long dst, unsigned w, unsigned h)   /* colour source, a surface of stride w */
{
    REG(BLT) = colour; REG(BLT + 1) = 0; REG(BLT + 2) = 0; REG(BLT + 3) = 0;
    w32r(BLT + 4, dst);
    REG(BLT + 8) = (uint8_t)(w & 255); REG(BLT + 9) = (uint8_t)(w >> 8);
    REG(BLT + 10) = (uint8_t)(h & 255); REG(BLT + 11) = (uint8_t)(h >> 8);
    REG(BLT + 14) = (uint8_t)(w & 255); REG(BLT + 15) = (uint8_t)(w >> 8);
}
static void blt_pt(uint8_t i, int x, int y) { REG(0xD084u + i * 4) = (uint8_t) x; REG(0xD085u + i * 4) = (uint8_t)(x >> 8); REG(0xD086u + i * 4) = (uint8_t) y; REG(0xD087u + i * 4) = (uint8_t)(y >> 8); }
static void gfx_line(int x0, int y0, int x1, int y1)
{
    blt_target(pencol, GFX_BASE, GW, GH);
    blt_pt(0, x0, y0); blt_pt(1, x1, y1);
    REG(0xD080u) = 6; REG(0xD082u) = 1;
}
static uint8_t px_get(int x, int y) { return far_peek(GFX_BASE + (unsigned long) y * GW + x); }
static void    px_set(int x, int y) { far_poke(GFX_BASE + (unsigned long) y * GW + x, pencol); }

/* ---- the turtle --------------------------------------------------------- */
static fbits tx, ty, th;                           /* position (origin the centre, y up) and heading (degrees, 0 = up, clockwise) */
static int clampi(long v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : (int) v; }
static uint8_t turtle_vis = 1;
static int turtle_px(void) { return clampi(ftoi(fadd(tx, fint(GW / 2))), 0, GW - 1); }
static int turtle_py(void) { return clampi(ftoi(fsub(fint(GH / 2), ty)), 0, GH - 1); }
/* The turtle is VICKY sprite 0: a 16x16 8-bpp arrow the blitter draws into
 * the sprite's own surface (TRIANGLE, op 7) each time it moves or turns --
 * tip forward, colour 15 -- so the glyph never touches the picture and HT/ST
 * are the enable bit.  Z = 3: over the bitmap and the text alike. */
static void turtle_show(void)
{
    fbits r, sn, cs; int tipx, tipy, lx, ly, rx, ry;
    if (!turtle_vis) { far_poke(SPR_TAB + 8, 0x32); return; }
    r = fmul(th, FDEG); sn = f1(MATH_SIN, r); cs = f1(MATH_COS, r);
    tipx = 8 + (int) ftoi(fmul(fint(7), sn));  tipy = 8 - (int) ftoi(fmul(fint(7), cs));
    lx = 8 - (int) ftoi(fmul(fint(5), sn)) + (int) ftoi(fmul(fint(4), cs));  ly = 8 + (int) ftoi(fmul(fint(5), cs)) + (int) ftoi(fmul(fint(4), sn));
    rx = 8 - (int) ftoi(fmul(fint(5), sn)) - (int) ftoi(fmul(fint(4), cs));  ry = 8 + (int) ftoi(fmul(fint(5), cs)) - (int) ftoi(fmul(fint(4), sn));
    w32r(DMA, 0); w32r(DMA + 4, SPR_DATA); w32r(DMA + 8, 256); REG(DMA + 0x0C) = 2;   /* clear the glyph */
    blt_target(15, SPR_DATA, 16, 16);
    blt_pt(0, tipx, tipy); blt_pt(1, lx, ly); blt_pt(2, rx, ry);
    REG(0xD080u) = 7; REG(0xD082u) = 1;
    far_poke16(SPR_TAB, (unsigned)(turtle_px() - 8)); far_poke16(SPR_TAB + 2, (unsigned)(turtle_py() - 8));
    far_poke(SPR_TAB + 4, (uint8_t) SPR_DATA); far_poke(SPR_TAB + 5, (uint8_t)(SPR_DATA >> 8)); far_poke(SPR_TAB + 6, (uint8_t)(SPR_DATA >> 16)); far_poke(SPR_TAB + 7, (uint8_t)(SPR_DATA >> 24));
    far_poke(SPR_TAB + 9, 5); far_poke(SPR_TAB + 10, 0);                        /* 16 x 16, palette offset 0 */
    far_poke(SPR_TAB + 8, 0x33);                                                /* enable | 8 bpp | Z 3 */
}
static void turtle_home(void) { tx = F0; ty = F0; th = F0; }
/* FILL: paint the area under the turtle -- every pixel of the colour it
 * stands on that touches it -- in the pen colour.  Span filling (the
 * classic scanline algorithm) over far memory one pixel at a time, so a
 * whole screen takes a few seconds; ESC stops it. */
#define FSTK 300
static struct { int x1, x2, y; int8_t dy; } fstk[FSTK]; static uint16_t fsp;
static uint8_t fill_old;
static uint8_t inside(int x, int y) { return x >= 0 && x < GW && y >= 0 && y < GH && px_get(x, y) == fill_old; }
static void fpush(int x1, int x2, int y, int8_t dy) { if (y < 0 || y >= GH || fsp >= FSTK) return; fstk[fsp].x1 = x1; fstk[fsp].x2 = x2; fstk[fsp].y = y; fstk[fsp].dy = dy; fsp++; }
static void do_fill(void)
{
    int x = turtle_px(), y = turtle_py(), x1, x2, yy; int8_t dy;
    fill_old = px_get(x, y); if (fill_old == pencol) return;
    fsp = 0; fpush(x, x, y, 1); fpush(x, x, y - 1, -1);
    while (fsp) {
        if (REG(KBD + 3)) { fsp = 0; break; }
        fsp--; x1 = fstk[fsp].x1; x2 = fstk[fsp].x2; yy = fstk[fsp].y; dy = fstk[fsp].dy;
        x = x1;
        if (inside(x, yy)) {
            while (inside(x - 1, yy)) { px_set(x - 1, yy); x--; }
            if (x < x1) fpush(x, x1 - 1, yy - dy, (int8_t) -dy);
        }
        while (x1 <= x2) {
            while (inside(x1, yy)) { px_set(x1, yy); x1++; }
            if (x1 > x) fpush(x, x1 - 1, yy + dy, dy);
            if (x1 - 1 > x2) fpush(x2 + 1, x1 - 1, yy - dy, (int8_t) -dy);
            x1++;
            while (x1 < x2 && !inside(x1, yy)) x1++;
            x = x1;
        }
    }
}
static void turtle_to(fbits nx, fbits ny)
{
    if (pendown) {
        int x0 = clampi(ftoi(fadd(tx, fint(GW / 2))), 0, GW - 1), y0 = clampi(ftoi(fsub(fint(GH / 2), ty)), 0, GH - 1);
        int x1 = clampi(ftoi(fadd(nx, fint(GW / 2))), 0, GW - 1), y1 = clampi(ftoi(fsub(fint(GH / 2), ny)), 0, GH - 1);
        gfx_line(x0, y0, x1, y1);
    }
    tx = nx; ty = ny; turtle_show();
}
static void turtle_fd(fbits d)
{
    fbits r = fmul(th, FDEG);
    turtle_to(fadd(tx, fmul(d, f1(MATH_SIN, r))), fadd(ty, fmul(d, f1(MATH_COS, r))));
}
static void turtle_turn(fbits d)                   /* keep the heading in 0..360 */
{
    th = fadd(th, d);
    { uint8_t i;                                    /* bounded: RT 1e18 (or 1/0) never converged and ESC could not break it */
      for (i = 0; i < 64 && fcmp(th, F360) >= 0; i++) th = fsub(th, F360);
      for (i = 0; i < 64 && fcmp(th, F0) < 0; i++) th = fadd(th, F360);
      if (fcmp(th, F360) >= 0 || fcmp(th, F0) < 0) th = F0; }
    turtle_show();
}

/* ---- the source pool, procedures, variables ------------------------------ */
#define POOLSZ 12288
#define NAMEL  16
#define NPROC  48
#define NPARM  6
#define NVAR   64
#define NLOC   96
static char pool[POOLSZ]; static uint16_t ptop;
static struct { char name[NAMEL]; uint8_t np; char parm[NPARM][NAMEL]; uint16_t b0, b1; } procs[NPROC]; static uint8_t nproc;
static struct { char name[NAMEL]; fbits v; } gvar[NVAR]; static uint8_t ngvar;
static struct { char name[NAMEL]; fbits v; } lvar[NLOC]; static uint8_t nlvar;
static uint8_t depth;
#define MAXDEPTH 100                                    /* the C stack (8 KB) is not the limit: the 256-byte hardware
                                                         * stack is, ~6 bytes of JSRs a level -- hw_sp() below is the guard */
static uint8_t hwsp;
static uint8_t hw_sp(void) { __asm__("tsx"); __asm__("stx %v", hwsp); return hwsp; }
#define HW_SP_MIN 0x30                                  /* what one more level of expr/call_proc needs, with margin */

/* the cursor into the text being run, and how a command ended */
static const char *cp, *cend;
static uint8_t flow;                               /* 0 run on, 1 STOP, 2 OUTPUT, 3 error, 4 BYE */
static fbits outv;
static long repcount;
static char word[NAMEL];

static void error(const char *m, const char *w) { if (flow == 3) return; puts_(m); if (w) { put(' '); puts_(w); } nl(); flow = 3; }
static void skipsp(void) { while (cp < cend && (*cp == ' ' || *cp == '\t' || *cp == '\r' || *cp == '\n')) cp++; }
static uint8_t isword(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '?'; }
static char upc(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }
static uint8_t getword(void)                       /* a word into word[] (upper-cased); 0 if none */
{
    uint8_t n = 0; skipsp();
    while (cp < cend && isword(*cp)) { if (n < NAMEL - 1) word[n++] = upc(*cp); cp++; }
    word[n] = 0; return n;
}
static uint8_t peekc(void) { skipsp(); return cp < cend ? (uint8_t) *cp : 0; }
static const char *list_end(const char *p, const char *e)    /* p just after '[': the matching ']' */
{
    uint8_t lv = 1;
    for (; p < e; p++) { if (*p == '[') lv++; else if (*p == ']') { if (--lv == 0) return p; } }
    return 0;
}
static uint8_t getlist(const char **a, const char **b)   /* [ ... ] -> its span; 0 if none */
{
    const char *e;
    if (peekc() != '[') return 0;
    e = list_end(cp + 1, cend); if (!e) { error("unbalanced [", 0); return 0; }
    *a = cp + 1; *b = e; cp = e + 1; return 1;
}
static int8_t proc_find(const char *n) { uint8_t i; for (i = 0; i < nproc; i++) if (!strcmp(procs[i].name, n)) return (int8_t) i; return -1; }
static fbits *var_find(const char *n)
{
    uint8_t i;
    for (i = nlvar; i > 0; i--) if (!strcmp(lvar[i - 1].name, n)) return &lvar[i - 1].v;
    for (i = 0; i < ngvar; i++) if (!strcmp(gvar[i].name, n)) return &gvar[i].v;
    return 0;
}
static void var_make(const char *n, fbits v)
{
    fbits *p = var_find(n);
    if (p) { *p = v; return; }
    if (ngvar >= NVAR) { error("too many variables", 0); return; }
    strcpy(gvar[ngvar].name, n); gvar[ngvar++].v = v;
}

/* ---- expressions --------------------------------------------------------- */
static fbits expr(void);
static void run(const char *a, const char *b);
static void call_proc(int8_t i);
static fbits number(void)                          /* digits[.digits] at the cursor */
{
    long ip = 0; uint8_t fr = 0; fbits v;
    while (cp < cend && *cp >= '0' && *cp <= '9') { ip = ip * 10 + (*cp - '0'); cp++; }
    v = fint(ip);
    if (cp < cend && *cp == '.') {
        long fp = 0; cp++;
        while (cp < cend && *cp >= '0' && *cp <= '9') { if (fr < 6) { fp = fp * 10 + (*cp - '0'); fr++; } cp++; }
        if (fr) { long sc = 1; uint8_t i; for (i = 0; i < fr; i++) sc *= 10; v = fadd(v, fdiv(fint(fp), fint(sc))); }
    }
    return v;
}
static fbits need(void)                            /* an argument, or an error */
{
    fbits v = expr();
    return v;
}
static fbits unary(void)
{
    uint8_t c = peekc(); int8_t pi; fbits v;
    if (c == '-') { cp++; return f1(MATH_NEG, unary()); }
    if (c == '(') { cp++; v = expr(); if (peekc() == ')') cp++; else error("missing )", 0); return v; }
    if (c >= '0' && c <= '9') return number();
    if (c == ':') { cp++; getword(); { fbits *p = var_find(word); if (!p) { error("no value for", word); return F0; } return *p; } }
    if (c == '"') { cp++; getword(); return F0; }      /* a word: printable by PRINT, no value here */
    if (!getword()) { error("I expected a number", 0); return F0; }
    if (!strcmp(word, "XCOR")) return tx;
    if (!strcmp(word, "YCOR")) return ty;
    if (!strcmp(word, "HEADING")) return th;
    if (!strcmp(word, "REPCOUNT")) return fint(repcount);
    if (!strcmp(word, "RANDOM")) { long n = ftoi(need()); static uint16_t seed;   /* an LCG: the frame-and-cursor mix repeated inside a REPEAT */
        if (!seed) seed = (uint16_t)(REG(0xD50Du) | REG(0xD50Eu) << 8) | 1;
        seed = seed * 25173u + 13849u; return fint(n > 0 ? (long)(seed >> 1) % n : 0); }
    if (!strcmp(word, "SQRT")) return f1(MATH_SQRT, need());
    if (!strcmp(word, "SIN")) return f1(MATH_SIN, fmul(need(), FDEG));
    if (!strcmp(word, "COS")) return f1(MATH_COS, fmul(need(), FDEG));
    if (!strcmp(word, "INT")) return f1(MATH_FLOOR, need());
    if (!strcmp(word, "ABS")) return f1(MATH_ABS, need());
    pi = proc_find(word);
    if (pi >= 0) { call_proc(pi); return flow == 2 ? (flow = 0, outv) : F0; }
    error("I don't know how to", word); return F0;
}
static fbits prod(void)
{
    fbits v = unary();
    for (;;) { uint8_t c = peekc(); if (c == '*') { cp++; v = fmul(v, unary()); } else if (c == '/') { cp++; v = fdiv(v, unary()); } else return v; }
}
static fbits sum(void)
{
    fbits v = prod();
    for (;;) { uint8_t c = peekc(); if (c == '+') { cp++; v = fadd(v, prod()); } else if (c == '-') { cp++; v = fsub(v, prod()); } else return v; }
}
static fbits expr(void)
{
    fbits v; uint8_t c;
    if (hw_sp() < HW_SP_MIN) { error("expression too deep", 0); return F0; }   /* 17 nested calls in an expression crashed the hardware stack */
    v = sum(); c = peekc();
    if (c == '<' || c == '>' || c == '=') { int r; cp++; r = fcmp(v, sum()); return fint(c == '<' ? r < 0 : c == '>' ? r > 0 : r == 0); }
    return v;
}

/* ---- commands ----------------------------------------------------------- */
static void call_proc(int8_t i)
{
    fbits args[NPARM]; uint8_t k, mark = nlvar;      /* run() restores the cursor itself: it must sit AFTER the arguments */
    for (k = 0; k < procs[i].np; k++) { args[k] = expr(); if (flow) return; }
    if (depth >= MAXDEPTH || hw_sp() < HW_SP_MIN) { error("too deep:", procs[i].name); return; }   /* COUNT 45 printed garbage, F 20 crashed (review 2026-09-12) */
    if (nlvar + procs[i].np > NLOC) { error("too many locals in", procs[i].name); return; }
    for (k = 0; k < procs[i].np; k++) { strcpy(lvar[nlvar].name, procs[i].parm[k]); lvar[nlvar++].v = args[k]; }
    depth++;
    run(pool + procs[i].b0, pool + procs[i].b1);
    depth--;
    nlvar = mark;
    if (flow == 1) flow = 0;                       /* STOP ends the procedure, not the caller */
}
static void define_proc(void)                      /* TO name :a :b ... <body> END, cursor after TO */
{
    uint8_t i, np = 0; const char *b0; int8_t old;
    if (!getword()) { error("TO needs a name", 0); return; }
    old = proc_find(word); i = old >= 0 ? (uint8_t) old : nproc;
    if (old < 0 && nproc >= NPROC) { error("too many procedures", 0); return; }
    strcpy(procs[i].name, word);
    while (peekc() == ':') { cp++; getword(); if (np < NPARM) strcpy(procs[i].parm[np++], word); }
    procs[i].np = np;
    b0 = cp;
    for (;;) {                                     /* the body runs to a lone END; punctuation is skipped, not the end */
        const char *w; skipsp();
        if (cp >= cend) { error("TO without END:", procs[i].name); return; }
        if (!isword(*cp)) { cp++; continue; }
        w = cp; getword();
        if (!strcmp(word, "END")) { procs[i].b0 = (uint16_t)(b0 - pool); procs[i].b1 = (uint16_t)(w - pool); break; }
    }
    if (old < 0) nproc++;
    puts_(procs[i].name); puts_(" defined"); nl();
}
static void do_print(void)
{
    uint8_t c = peekc();
    if (c == '"') { cp++; getword(); puts_(word); nl(); return; }
    if (c == '[') { const char *a, *b; getlist(&a, &b); while (a < b) put(*a++); nl(); return; }
    puts_(ftoa(expr())); nl();
}
static void command(void)
{
    uint8_t c = peekc(); const char *a, *b; long n; int8_t pi;
    if (REG(KBD + 3)) { error("stopped", 0); return; }   /* $D103: an ESC or Ctrl-C in the queue -- the break every interpreter honours */
    if (!c) return;
    if (c == '[' || c == ']') { cp++; error(c == '[' ? "a list where a command belongs" : "unexpected ]", 0); return; }
    if (!getword()) { cp++; error("I do not understand that", 0); return; }
    if (!strcmp(word, "FD") || !strcmp(word, "FORWARD")) { turtle_fd(need()); return; }
    if (!strcmp(word, "BK") || !strcmp(word, "BACK")) { turtle_fd(f1(MATH_NEG, need())); return; }
    if (!strcmp(word, "RT") || !strcmp(word, "RIGHT")) { turtle_turn(need()); return; }
    if (!strcmp(word, "LT") || !strcmp(word, "LEFT")) { turtle_turn(f1(MATH_NEG, need())); return; }
    if (!strcmp(word, "PU") || !strcmp(word, "PENUP")) { pendown = 0; return; }
    if (!strcmp(word, "PD") || !strcmp(word, "PENDOWN")) { pendown = 1; return; }
    if (!strcmp(word, "HT") || !strcmp(word, "HIDETURTLE")) { turtle_vis = 0; turtle_show(); return; }
    if (!strcmp(word, "ST") || !strcmp(word, "SHOWTURTLE")) { turtle_vis = 1; turtle_show(); return; }
    if (!strcmp(word, "FILL")) { do_fill(); return; }
    if (!strcmp(word, "EDIT") || !strcmp(word, "ED")) { extern void do_edit(void); do_edit(); return; }
    if (!strcmp(word, "HOME")) { fbits ox = tx, oy = ty; turtle_home(); tx = ox; ty = oy; turtle_to(F0, F0); return; }
    if (!strcmp(word, "CS") || !strcmp(word, "CLEARSCREEN") || !strcmp(word, "CLEAN")) { gfx_clear(); if (word[0] != 'C' || word[1] != 'L' || word[2] != 'E' || word[3] != 'A' || word[4] != 'N') turtle_home(); turtle_show(); return; }
    if (!strcmp(word, "SETXY")) { fbits x = need(), y = need(); turtle_to(x, y); return; }
    if (!strcmp(word, "SETX")) { turtle_to(need(), ty); return; }
    if (!strcmp(word, "SETY")) { turtle_to(tx, need()); return; }
    if (!strcmp(word, "SETH") || !strcmp(word, "SETHEADING")) { fbits d = need(); th = F0; turtle_turn(d); return; }
    if (!strcmp(word, "SETPC") || !strcmp(word, "SETPENCOLOR") || !strcmp(word, "SETPENCOLOUR")) { pencol = (uint8_t) ftoi(need()); return; }
    if (!strcmp(word, "PRINT") || !strcmp(word, "PR") || !strcmp(word, "SHOW")) { do_print(); return; }
    if (!strcmp(word, "MAKE")) { char nm[NAMEL]; if (peekc() != '"') { error("MAKE needs a \"name", 0); return; } cp++; getword(); strcpy(nm, word); var_make(nm, expr()); return; }
    if (!strcmp(word, "REPEAT")) {
        long i, save = repcount; n = ftoi(need()); if (!getlist(&a, &b)) { error("REPEAT needs a [list]", 0); return; }
        for (i = 1; i <= n && !flow; i++) { const char *sc = cp, *se = cend; repcount = i; run(a, b); cp = sc; cend = se; }
        repcount = save; return; }
    if (!strcmp(word, "IF")) { fbits t = expr(); if (!getlist(&a, &b)) { error("IF needs a [list]", 0); return; }
        if (fcmp(t, F0) != 0) { const char *sc = cp, *se = cend; run(a, b); cp = sc; cend = se; } return; }
    if (!strcmp(word, "IFELSE")) { const char *a2, *b2; fbits t = expr();
        if (!getlist(&a, &b) || !getlist(&a2, &b2)) { error("IFELSE needs two [lists]", 0); return; }
        { const char *sc = cp, *se = cend; if (fcmp(t, F0) != 0) run(a, b); else run(a2, b2); cp = sc; cend = se; } return; }
    if (!strcmp(word, "STOP")) { flow = 1; return; }
    if (!strcmp(word, "OUTPUT") || !strcmp(word, "OP")) { outv = expr(); if (!flow) flow = 2; return; }
    if (!strcmp(word, "TO")) { define_proc(); return; }
    if (!strcmp(word, "BYE")) { flow = 4; return; }
    if (!strcmp(word, "LOAD")) { extern void do_load(void); do_load(); return; }
    if (!strcmp(word, "HELP")) { puts_("FD BK RT LT PU PD HT ST HOME CS FILL SETXY SETH SETPC PRINT MAKE REPEAT IF IFELSE TO..END STOP OUTPUT LOAD EDIT BYE"); nl(); return; }
    pi = proc_find(word);
    if (pi >= 0) { call_proc(pi); if (flow == 2) { error("you don't say what to do with the output of", procs[pi].name); } return; }
    error("I don't know how to", word);
}
static void run(const char *a, const char *b)
{
    const char *sc = cp, *se = cend;
    cp = a; cend = b;
    while (!flow && peekc()) command();
    cp = sc; cend = se;
}

/* ---- text in: the prompt, and files ------------------------------------- */
static char line[160];
static uint8_t readline(const char *prompt)
{
    uint8_t n = 0, k;
    puts_(prompt);
    for (;;) {
        do { k = getin(); } while (!k);
        if (REG(KBD + 1) & 0x40) continue;             /* a function or cursor key */
        if (k == 0x0D || k == 0x0A) { nl(); line[n] = 0; return 1; }
        if (k == 0x08 || k == 0x7F) { if (n) { n--; put(0x08); put(' '); put(0x08); } continue; }
        if (k == 0x03) { nl(); line[0] = 0; return 0; }   /* Ctrl-C: drop the line */
        if (k >= 32 && n < sizeof line - 1) { line[n++] = (char) k; put((char) k); }
    }
}
/* Text arrives in the pool and stays there: a procedure's body is a span of
 * it.  Top-level commands run from the pool too (a TO on the prompt line
 * needs its body to persist). */
static void feed(const char *text, uint16_t len)
{
    uint16_t at = ptop; uint8_t np = nproc;
    if (ptop + len + 1 > POOLSZ) { error("out of room for text", 0); return; }
    memcpy(pool + ptop, text, len); ptop += len; pool[ptop++] = '\n';
    flow = 0; run(pool + at, pool + ptop);
    if (flow == 3) flow = 0;
    if (nproc == np) ptop = at;                    /* only a TO needs its text kept; 77 prompt lines used to fill the pool for good */
}
static void feed_to_end(void)                      /* a TO on the prompt: keep reading lines until END */
{
    uint16_t at = ptop;
    for (;;) {
        uint16_t l = (uint16_t) strlen(line);
        if (ptop + l + 2 > POOLSZ) { error("out of room for text", 0); return; }
        memcpy(pool + ptop, line, l); ptop += l; pool[ptop++] = '\n';
        { const char *p = line; while (*p == ' ') p++; if (upc(p[0]) == 'E' && upc(p[1]) == 'N' && upc(p[2]) == 'D' && (p[3] == 0 || p[3] == ' ')) break; }
        if (!readline("> ")) return;
    }
    flow = 0; run(pool + at, pool + ptop); if (flow == 3) flow = 0;
}
static char fn[40];
static uint8_t lgo_name(const char *what)             /* "name at the cursor -> fn = NAME.LGO */
{
    uint8_t i, n = 0;
    if (peekc() != '"') { error(what, 0); return 0; }
    cp++; getword();
    for (i = 0; word[i] && n < 30; i++) fn[n++] = word[i];
    if (!strchr(fn, '.')) { fn[n++] = '.'; fn[n++] = 'L'; fn[n++] = 'G'; fn[n++] = 'O'; } fn[n] = 0;
    return 1;
}
static uint8_t lgo_load(uint16_t at)                  /* the file into the pool at `at': here, else EX/; 1 if it loaded */
{
    w32r(FSR + 4, (unsigned long)(uint16_t) fn);
    w32r(FSR + 8, (unsigned long)(uint16_t)(pool + at));
    REG(FSR) = 9;                                        /* FS_LOAD: the whole file to ADDR, LEN = its size */
    if (REG(FSR + 1)) {                                  /* not here: try EX/ */
        static char fn2[44]; strcpy(fn2, "EX/"); strcat(fn2, fn);
        w32r(FSR + 4, (unsigned long)(uint16_t) fn2); REG(FSR) = 9;
        if (REG(FSR + 1)) return 0;
        strcpy(fn, fn2);
    }
    return 1;
}
static void run_file(void)
{
    uint16_t at = ptop; unsigned long sz;
    sz = r32r(FSR + 0x0C);                               /* LEN: bytes loaded */
    if (at + sz + 1 >= POOLSZ) { error("too big to load:", fn); return; }
    ptop = (uint16_t)(at + sz); pool[ptop++] = '\n';
    puts_("loaded "); puts_(fn); nl();
    flow = 0; run(pool + at, pool + ptop); if (flow == 3) flow = 0;
}
void do_load(void)                                 /* LOAD "name: NAME.LGO from EX/ or here, run as if typed */
{
    if (!lgo_name("LOAD needs a \"name")) return;
    if (!lgo_load(ptop)) { error("I can't find", fn); return; }
    run_file();
}
/* EDIT "name: VI on NAME.LGO (here, or the EX/ copy if only that exists; a
 * new file otherwise) through the shell's SWAP -- our image and the screen
 * are parked in far memory, the bitmap at $200000 is not touched -- then
 * the file is loaded and run, so the picture shows what you just wrote. */
void do_edit(void)
{
    if (!lgo_name("EDIT needs a \"name")) return;
    lgo_load(ptop);                                      /* only to learn where it lives; not run */
    gfx_close();
    strcpy(CMDLINE, "SWAP VI "); strcat(CMDLINE, fn);
    rom_shell(CMDLINE);
    gfx_open(); REG(TERM + 0x0E) |= 1; turtle_show();
    if (!lgo_load(ptop)) { error("nothing saved as", fn); return; }
    run_file();
}
/* ---- main ---------------------------------------------------------------- */
int main(void)
{
    F0 = fint(0); F1 = fint(1); F10 = fint(10); F180 = fint(180); F360 = fint(360);
    FDEG = fdiv(fint(314159L), fint(18000000L));           /* pi / 180 */
    gfx_open(); gfx_clear();
    REG(TERM + 0x0E) |= 1;                                 /* the console cursor: the ROM hides it for programs */
    turtle_home(); pendown = 1; pencol = 1;
    w32r(DMA, 0); w32r(DMA + 4, SPR_TAB); w32r(DMA + 8, 2048); REG(DMA + 0x0C) = 2;   /* an empty sprite table */
    turtle_show();
    puts_("K4510 LOGO -- the turtle is home.  HELP lists the words; BYE leaves."); nl();
    for (;;) {
        if (!readline("? ")) continue;
        if (!line[0]) continue;
        { const char *p = line; while (*p == ' ') p++;
          if (upc(p[0]) == 'T' && upc(p[1]) == 'O' && (p[2] == ' ' || p[2] == 0)) { feed_to_end(); }
          else feed(line, (uint16_t) strlen(line)); }
        if (flow == 4) break;
    }
    gfx_close();
    return 0;
}
