/* K4510: RX -- a REXX interpreter, the machine's glue language (2026-09-07).
 *
 * REXX as Mike Cowlishaw wrote it and the Amiga made famous: strings are the
 * only type, an unknown clause is a command for the current environment,
 * PARSE takes strings apart, and the shell's REXX rule (an unknown word is a
 * program on disk) already made every .prg a command.  What ARexx added was
 * the ports; this machine is single-tasking, so a port here is a file: the
 * script writes /RX/MAIL.CMD, SWAPs to the program with @/RX/MAIL.CMD as
 * its argument, and reads /RX/MAIL.RPL when it comes back (RC on the first
 * line, RESULT after it).
 *
 *   RX name [args]      runs name, name.RX or /RX/name.RX
 *   name args           the same, when the shell finds name.RX (rom/kernal.c try_rx)
 *
 * The subset, honestly: whole numbers only (32-bit; / truncates, % and // as
 * REXX), values up to 255 characters, no INTERPRET, no NUMERIC, no
 * SIGNAL ON.  Everything else people write in scripts is here: SAY PULL
 * PARSE (ARG PULL VAR VALUE SOURCE VERSION, UPPER, templates with words,
 * string patterns, column numbers), IF THEN ELSE, DO (n / var=a TO b BY c /
 * FOREVER / WHILE / UNTIL) END, LEAVE ITERATE, SELECT WHEN OTHERWISE, CALL
 * and internal functions with ARG and RETURN, PROCEDURE EXPOSE, compound
 * variables (stems), DROP, PUSH QUEUE, SIGNAL, EXIT, ADDRESS, RC RESULT
 * SIGL, and the built-ins listed at builtin().
 *
 * Environments: COMMAND (the K/OS shell, through $FF8F; RC is the shell's
 * result byte), any program name (the mailbox above), and TUBE (reserved:
 * the co-processor -- Linux on K4510x, BBC BASIC, CP/M).
 *
 * Memory: the script lives in far memory (SRC_PHYS) and is read a clause at
 * a time; variables live in a pool that is compacted through far memory
 * when it fills.  The program itself is at $2000 (demo/rexx.cfg). */
#include "k4510.h"
#include <string.h>
#include <stdlib.h>
#include <setjmp.h>


void __fastcall__ rom_chrout(unsigned char c);
unsigned char rom_getin(void);
static unsigned char rom_args(void) { return ((unsigned char (*)(void))0xFF95)(); }

/* ---- the storage device ------------------------------------------------ */
#define FS        0xD300u
#define FS_CMD    (FS + 0x00)
#define FS_ST     (FS + 0x01)
#define FS_NAME   (FS + 0x04)
#define FS_ADDR   (FS + 0x08)
#define FS_LEN    (FS + 0x0C)
#define FS_SIZE   (FS + 0x10)
#define TUBE      0xD800u
#define C_STAT    8
#define C_LOAD    9
#define C_SAVE    10
#define C_RM      13
static uint8_t fs_do(uint8_t cmd) { REG(FS_CMD) = cmd; return REG(FS_ST); }
static void fs_name(const char *s) { w32(FS_NAME, (uint32_t)(uint16_t)s); }
static uint32_t r32(uint16_t r) { return (uint32_t)REG(r) | ((uint32_t)REG(r + 1) << 8) | ((uint32_t)REG(r + 2) << 16) | ((uint32_t)REG(r + 3) << 24); }

/* ---- far memory the interpreter owns ---------------------------------- */
#define SRC_PHYS   0x00F00000UL     /* the script, up to 64 KB */
#define SCRATCH    0x00F10000UL     /* the pool, while it is compacted */
#define ARGS_PHYS  0x00F20000UL     /* CALL arguments: 8 levels x 8 args x 256 */
#define SAVE_PHYS  0x00F24000UL    /* the lexer token saved across a CALL, one 256 per level */
#define STREAM_PHYS 0x00F40000UL    /* LINEIN files: 4 x 64 KB */
#define OUT_PHYS   0x00F80000UL     /* LINEOUT: one file, buffered, 64 KB */
#define SCR_PHYS   0x00F90000UL     /* the console, kept across a SWAP */
#define SCREEN     0x00030000UL     /* where K/OS keeps the text screen (rom/kernal.c) */
#define SCRBYTES   (80UL * 60 * 4)
#define ARGN 8
#define ARGSZ 256

/* ---- limits ------------------------------------------------------------ */
#define VMAX 160                    /* a value: cc65 allows 255 bytes of locals, and one of these is a local in places */
#define CMAX 256                    /* a clause */
#define RUNMAX 6                    /* nested function calls in expressions */
#define TMAX 20                     /* expression temporaries */
#define NVAR 80
#define POOL 1536
#define NLAB 16
#define NFRAME 16
#define NQ 2

/* ---- state ------------------------------------------------------------- */
static char cbuf[RUNMAX][CMAX];
static uint8_t rlev;                /* which clause buffer: one per nested run loop */
#define CL (cbuf[rlev])
static const char *cp;              /* the parse pointer */
static uint16_t pos, cstart, srclen;   /* 16-bit: the script is at most 64 KB */
static char tmp[TMAX][VMAX]; static uint8_t tsp;
#define KMAX 3
static char kbuf[KMAX][CMAX]; static uint8_t ksp;   /* clause copies: cc65 allows 255 bytes of locals */
static jmp_buf top;
unsigned rx_sp(void);                    /* demo/rxasm.s: cc65's C stack pointer */
static unsigned sp0;                     /* what it was at the start */
/* The C stack grows DOWN from the top of the program area, straight at this
 * file's own buffers, and nothing but this check stands between them: a
 * recursion that overruns used to corrupt the variable pool and report
 * nonsense (2026-09-07).  Each nested call costs roughly 400 bytes. */
#define STACK_BUDGET 2200
static int exit_code;
static uint8_t call_depth;          /* ARG() levels */
static uint8_t var_lvl = 1;         /* PROCEDURE levels */
static char env[20] = "COMMAND", env_prev[20] = "COMMAND";
static char q[NQ][100]; static uint8_t qh, qn;
static uint32_t rnd_seed;
static uint8_t trace;
static uint8_t rtc_junk;                 /* the RTC latches on a READ of $D504, and a read whose
                                          * value goes nowhere is compiled away: it must be stored */
#define RTC_LATCH() (rtc_junk = REG(SYS + 4))

typedef struct { uint16_t name; uint16_t val; uint8_t cap; uint8_t lvl; uint8_t link; } var_t;
static var_t vars[NVAR]; static uint8_t nvars;
static char pool[POOL]; static uint16_t ptop;

typedef struct { char name[16]; uint16_t at; } label_t;
static label_t labels[NLAB]; static uint8_t nlabels;

#define CT_DO 1
#define CT_SEL 2
#define CT_IF 3
#define CT_CALL 4
typedef struct { uint8_t type; uint8_t flag; uint16_t dopos; uint16_t body; long cnt; long to, by; char var[24]; uint8_t rl; uint8_t vl; } frame_t;
static frame_t frames[NFRAME]; static uint8_t fsp;

/* LINEIN streams and the LINEOUT file */
typedef struct { char name[40]; uint32_t len, off; } stream_t;
static stream_t streams[4];
static char outname[40]; static uint32_t outlen;

/* ---- console ----------------------------------------------------------- */
static void outc(char c) { rom_chrout((unsigned char)c); }
static void outs(const char *s) { while (*s) rom_chrout((unsigned char)*s++); }
static void outn(long v) { char b[14]; ltoa(v, b, 10); outs(b); }
static void nl(void) { outc('\n'); }

static uint16_t line_of(uint16_t at)
{
    uint16_t i, n = 1;
    for (i = 0; i < at && i < srclen; i++) if (far_peek(SRC_PHYS + i) == '\n') n++;
    return n;
}
static void die(const char *m)
{
    outs("RX: line "); outn(line_of(cstart)); outs(": "); outs(m); nl();
    if (CL[0]) { outs("    "); outs(CL); nl(); }
    exit_code = 1;
    longjmp(top, 1);
}

/* ---- temporaries -------------------------------------------------------- */
static char *tpush(void)
{
    if (tsp >= TMAX || (unsigned)(sp0 - rx_sp()) > STACK_BUDGET) die("expression too deep");
    tmp[tsp][0] = 0; return tmp[tsp++];
}
static void tpop(void) { tsp--; }
static char *kpush(void) { if (ksp >= KMAX) die("statements too deep"); kbuf[ksp][0] = 0; return kbuf[ksp++]; }
static void kpop(void) { ksp--; }

/* ---- small string helpers ---------------------------------------------- */
static uint8_t upc(uint8_t c) { return (c >= 'a' && c <= 'z') ? (uint8_t)(c - 32) : c; }
static void upstr(char *s) { for (; *s; s++) *s = (char)upc((uint8_t)*s); }
static uint8_t is_symch(uint8_t c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '.' || c == '_' || c == '!' || c == '?' || c == '@' || c == '#' || c == '$'; }
static uint8_t is_digit(uint8_t c) { return c >= '0' && c <= '9'; }
static void setlen(char *d, const char *s, uint16_t n) { if (n >= VMAX) n = VMAX - 1; memcpy(d, s, n); d[n] = 0; }
static void cat(char *d, const char *s) { uint16_t l = strlen(d); while (*s && l < VMAX - 1) d[l++] = *s++; d[l] = 0; }
static void catc(char *d, char c) { uint16_t l = strlen(d); if (l < VMAX - 1) { d[l] = c; d[l + 1] = 0; } }
static uint8_t eqi(const char *a, const char *b)   /* case-insensitive equal */
{
    while (*a && *b) { if (upc((uint8_t)*a) != upc((uint8_t)*b)) return 0; a++; b++; }
    return *a == *b;
}
/* the symbol at *p, if p starts with one; upper-cased into w (max n) */
static uint8_t word_at(const char *p, char *w, uint8_t n)
{
    uint8_t i = 0;
    while (*p == ' ') p++;
    while (is_symch((uint8_t)*p) && i < n - 1) w[i++] = (char)upc((uint8_t)*p++);
    w[i] = 0;
    return i;
}
static const char *skipsp(const char *p) { while (*p == ' ') p++; return p; }
/* does the clause at p start with the keyword k?  returns the text after it */
static const char *kw(const char *p, const char *k)
{
    p = skipsp(p);
    while (*k) { if (upc((uint8_t)*p) != *k) return 0; p++; k++; }
    if (is_symch((uint8_t)*p)) return 0;
    return skipsp(p);
}
/* find the keyword k at the top level of the clause (outside quotes and
 * parentheses); returns the text after it, and cuts the clause there */
static char *find_kw(char *p, const char *k)
{
    uint8_t depth = 0; char qc = 0;
    for (; *p; p++) {
        if (qc) { if (*p == qc) qc = 0; continue; }
        if (*p == '\'' || *p == '"') { qc = *p; continue; }
        if (*p == '(') depth++;
        else if (*p == ')') { if (depth) depth--; }
        else if (!depth && (p == cbuf[rlev] || !is_symch((uint8_t)p[-1])) && is_symch((uint8_t)*p)) {
            const char *a = p, *b = k;
            while (*b && upc((uint8_t)*a) == *b) { a++; b++; }
            if (!*b && !is_symch((uint8_t)*a)) { *p = 0; return (char *)skipsp(a); }
        }
    }
    return 0;
}

/* ---- numbers ------------------------------------------------------------ */
static uint8_t is_num(const char *s)
{
    uint8_t d = 0;
    s = skipsp(s);
    if (*s == '+' || *s == '-') s++;
    while (is_digit((uint8_t)*s)) { s++; d = 1; }
    s = skipsp(s);
    return d && !*s;
}
static long num(const char *s) { if (!is_num(s)) { outs("RX: not a number: "); outs(s); nl(); die("bad arithmetic"); } return atol(s); }
static void putnum(char *d, long v) { ltoa(v, d, 10); }

/* ---- variables ----------------------------------------------------------- */
static uint16_t palloc(uint16_t n);
static void compact(void)
{
    uint8_t i; uint16_t o;
    dma_copy((uint32_t)(uint16_t)pool, SCRATCH, POOL);
    ptop = 0;
    for (i = 0; i < nvars; i++) {
        if (vars[i].lvl == 0xFF) continue;
        o = vars[i].name; vars[i].name = ptop;
        while ((pool[ptop++] = (char)far_peek(SCRATCH + o++)) != 0) ;
        if (vars[i].link) continue;
        o = vars[i].val; vars[i].val = ptop;
        while ((pool[ptop++] = (char)far_peek(SCRATCH + o++)) != 0) ;
        vars[i].cap = (uint8_t)(ptop - vars[i].val - 1);
    }
}
static uint16_t palloc(uint16_t n)
{
    if (ptop + n > POOL) { compact(); if (ptop + n > POOL) die("out of variable space"); }
    ptop += n; return ptop - n;
}
static uint8_t var_find(const char *name, uint8_t lvl)   /* index+1, 0 if none */
{
    uint8_t i;
    for (i = 0; i < nvars; i++) if (vars[i].lvl == lvl && !strcmp(pool + vars[i].name, name)) return i + 1;
    return 0;
}
static var_t *var_real(uint8_t ix) { while (vars[ix].link) ix = vars[ix].link - 1; return &vars[ix]; }
static const char *var_get(const char *name)   /* 0 when unset */
{
    uint8_t ix = var_find(name, var_lvl);
    if (!ix) {                                            /* a stem's default: A. for A.3 */
        const char *d = strchr(name, '.');
        if (d && d[1]) { char s[VMAX]; setlen(s, name, d - name + 1); ix = var_find(s, var_lvl); }
        if (!ix) return 0;
    }
    return pool + var_real(ix - 1)->val;
}
static void var_set(const char *name, const char *val)
{
    uint8_t ix = var_find(name, var_lvl); var_t *v; uint16_t n = strlen(val);
    if (n >= VMAX) n = VMAX - 1;
    if (!ix) {
        uint8_t i;
        for (i = 0; i < nvars && vars[i].lvl != 0xFF; i++) ;      /* a dropped slot, or a new one */
        if (i == nvars) { if (nvars >= NVAR) die("too many variables"); nvars++; }
        ix = i + 1; v = &vars[i]; v->lvl = var_lvl; v->link = 0; v->cap = 0; v->val = 0;
        { uint16_t o = palloc(strlen(name) + 1); strcpy(pool + o, name); v->name = o; }
        if (name[strlen(name) - 1] == '.') {                       /* stem assignment drops its members */
            for (i = 0; i < nvars; i++)
                if (vars[i].lvl == var_lvl && i != ix - 1 && !strncmp(pool + vars[i].name, name, strlen(name))) vars[i].lvl = 0xFF;
        }
    }
    v = var_real(ix - 1);
    if (n > v->cap || !v->val) { v->val = palloc(n + 1); v->cap = (uint8_t)n; }
    memcpy(pool + v->val, val, n); pool[v->val + n] = 0;
}
static void var_setn(const char *name, long n) { char b[14]; putnum(b, n); var_set(name, b); }
static void var_drop(const char *name) { uint8_t ix = var_find(name, var_lvl); if (ix) vars[ix - 1].lvl = 0xFF; }
/* a compound name: the tail's symbols replaced by their values */
static void resolve(const char *sym, char *out)
{
    const char *d = strchr(sym, '.');
    if (!d || !d[1] || is_digit((uint8_t)sym[0])) { strcpy(out, sym); upstr(out); return; }
    setlen(out, sym, d - sym + 1); upstr(out);
    while (*d == '.') {
        const char *e = ++d; char piece[VMAX]; const char *v;
        while (*e && *e != '.') e++;
        setlen(piece, d, e - d); upstr(piece);
        v = (piece[0] && !is_digit((uint8_t)piece[0])) ? var_get(piece) : 0;
        cat(out, v ? v : (const char *)piece);
        if (*e == '.') cat(out, ".");
        d = e;
    }
}
static const char *value_of(const char *sym, char *scratch)   /* a symbol's value: itself (upper) when unset */
{
    const char *v;
    if (is_digit((uint8_t)sym[0]) || sym[0] == '.') { strcpy(scratch, sym); return scratch; }
    resolve(sym, scratch);
    v = var_get(scratch);
    return v ? v : (const char *)scratch;
}

/* ---- the CALL arguments -------------------------------------------------- */
static uint32_t arg_at(uint8_t lvl, uint8_t i) { return ARGS_PHYS + ((uint32_t)lvl * ARGN + i) * ARGSZ; }
static void arg_put(uint8_t lvl, uint8_t i, const char *s)
{
    uint32_t a = arg_at(lvl, i); uint16_t n = 0;
    while (*s && n < ARGSZ - 1) { far_poke(a + n, (uint8_t)*s++); n++; }
    far_poke(a + n, 0);
}
static void arg_get(uint8_t lvl, uint8_t i, char *d)
{
    uint32_t a = arg_at(lvl, i); uint16_t n = 0; uint8_t c;
    while ((c = far_peek(a + n)) != 0 && n < VMAX - 1) d[n++] = (char)c;
    d[n] = 0;
}
static uint8_t arg_count(uint8_t lvl) { uint8_t n = ARGN; while (n && !far_peek(arg_at(lvl, n - 1))) n--; return n; }

/* ---- reading the script a clause at a time -------------------------------- */
static uint8_t sc(uint16_t p) { return p < srclen ? far_peek(SRC_PHYS + p) : 0; }
/* the next clause into CL; 0 at the end of the script.  Comments go,
 * a trailing comma continues onto the next line, ';' and newline end it. */
static uint8_t read_clause(void)
{
    uint16_t n = 0; char qc = 0; uint8_t c;
    for (;;) {
        c = sc(pos);
        if (!c) { CL[0] = 0; return 0; }
        if (c == ' ' || c == '\n' || c == '\r' || c == ';' || c == '\t') { pos++; continue; }
        if (c == '/' && sc(pos + 1) == '*') {                     /* a comment before the clause */
            uint8_t d = 1; pos += 2;
            while (d && sc(pos)) { if (sc(pos) == '/' && sc(pos + 1) == '*') { d++; pos++; } else if (sc(pos) == '*' && sc(pos + 1) == '/') { d--; pos++; } pos++; }
            continue;
        }
        break;
    }
    cstart = pos;
    for (;;) {
        c = sc(pos);
        if (!c) break;
        if (qc) {
            if (c == '\n') die("unterminated string");
            if (c == qc) qc = 0;
        } else {
            if (c == '\'' || c == '"') qc = (char)c;
            else if (c == ';' || c == '\n') { pos++; break; }
            else if (c == '\r' || c == '\t') { pos++; continue; }
            else if (c == '/' && sc(pos + 1) == '*') {
                uint8_t d = 1; pos += 2;
                while (d && sc(pos)) { if (sc(pos) == '/' && sc(pos + 1) == '*') { d++; pos++; } else if (sc(pos) == '*' && sc(pos + 1) == '/') { d--; pos++; } pos++; }
                c = ' ';
                if (n && CL[n - 1] == ' ') continue;
                CL[n++] = ' '; continue;
            }
            else if (c == ',') {                                   /* continuation: a comma ending the line */
                uint16_t k = pos + 1;
                while (sc(k) == ' ' || sc(k) == '\r' || sc(k) == '\t') k++;
                if (sc(k) == '\n') { pos = k + 1; if (n && CL[n - 1] != ' ') CL[n++] = ' '; continue; }
            }
        }
        if (n < CMAX - 1) CL[n++] = (char)c; else die("clause too long");
        pos++;
    }
    while (n && CL[n - 1] == ' ') n--;
    CL[n] = 0;
    return 1;
}

/* ---- the labels ---------------------------------------------------------- */
static void scan_labels(void)
{
    uint16_t save = pos; pos = 0;
    while (read_clause()) {
        char w[16]; uint8_t n = word_at(CL, w, 16); const char *p = skipsp(CL + n);
        while (is_symch((uint8_t)*p)) p++;                          /* a long label: past the 15 kept */
        if (n && *p == ':' && !is_digit((uint8_t)w[0])) {
            if (nlabels < NLAB) { strcpy(labels[nlabels].name, w); labels[nlabels].at = cstart; nlabels++; }
        }
    }
    pos = save;
}
static uint16_t label_at(const char *name)
{
    uint8_t i; char w[16]; setlen(w, name, 15); upstr(w);
    for (i = 0; i < nlabels; i++) if (!strcmp(labels[i].name, w)) return labels[i].at;
    return 0xFFFF;
}

/* ---- the lexer ------------------------------------------------------------ */
#define T_END 0
#define T_SYM 1
#define T_STR 2
#define T_OP 3
#define T_LP 4
#define T_RP 5
#define T_COMMA 6
static uint8_t tk, tblank, tfunc;      /* kind; blanks before it; a symbol followed at once by ( */
static char tv[VMAX];                  /* the text (a string's body; an operator's spelling) */
static void lex(void)
{
    const char *p = cp; uint16_t n = 0;
    tblank = 0; tfunc = 0;
    while (*p == ' ') { p++; tblank = 1; }
    if (!*p) { tk = T_END; cp = p; return; }
    if (*p == '\'' || *p == '"') {
        char qc = *p++;
        for (;;) {
            if (!*p) die("unterminated string");
            if (*p == qc) { if (p[1] == qc) { if (n < VMAX - 1) tv[n++] = qc; p += 2; continue; } p++; break; }
            if (n < VMAX - 1) tv[n++] = *p; p++;
        }
        tv[n] = 0; tk = T_STR;
        if ((*p == 'x' || *p == 'X') && !is_symch((uint8_t)p[1])) {   /* 'FF'x */
            char b[VMAX]; uint16_t i, k = 0; uint8_t v = 0, h = 0;
            for (i = 0; tv[i]; i++) {
                uint8_t c = upc((uint8_t)tv[i]);
                if (c == ' ') continue;
                v = (uint8_t)((v << 4) | (c <= '9' ? c - '0' : c - 'A' + 10));
                if (++h == 2) { b[k++] = (char)v; h = 0; v = 0; }
            }
            b[k] = 0; memcpy(tv, b, k + 1); p++;
        }
        cp = p; return;
    }
    if (is_symch((uint8_t)*p)) {
        while (is_symch((uint8_t)*p) && n < VMAX - 1) tv[n++] = *p++;
        tv[n] = 0; tk = T_SYM; tfunc = (*p == '(');
        cp = p; return;
    }
    if (*p == '(') { tk = T_LP; cp = p + 1; return; }
    if (*p == ')') { tk = T_RP; cp = p + 1; return; }
    if (*p == ',') { tk = T_COMMA; cp = p + 1; return; }
    tk = T_OP; tv[0] = *p; tv[1] = 0; tv[2] = 0;
    if ((p[0] == '*' && p[1] == '*') || (p[0] == '/' && p[1] == '/') || (p[0] == '|' && p[1] == '|') || (p[0] == '&' && p[1] == '&') ||
        (p[0] == '=' && p[1] == '=') || (p[0] == '<' && p[1] == '=') || (p[0] == '>' && p[1] == '=') || (p[0] == '<' && p[1] == '>') || (p[0] == '>' && p[1] == '<') ||
        ((p[0] == '\\' || p[0] == '~' || p[0] == '^' || p[0] == '!') && (p[1] == '=' || p[1] == '>' || p[1] == '<'))) {
        tv[1] = p[1]; p += 2;
        if (tv[0] == '\\' && tv[1] == '=' && *p == '=') { tv[2] = '='; p++; }
        if (tv[0] == '~' || tv[0] == '^' || tv[0] == '!') tv[0] = '\\';
    } else { if (tv[0] == '~' || tv[0] == '^') tv[0] = '\\'; p++; }
    cp = p;
}
static uint8_t is_op(const char *s) { return tk == T_OP && !strcmp(tv, s); }

/* ---- the evaluator ---------------------------------------------------------- */
static void expr(char *out);
static void call_function(const char *name, uint8_t argc, char **argv, char *out);
static void primary(char *out)
{
    if (tk == T_STR) { strcpy(out, tv); lex(); return; }
    if (tk == T_SYM) {
        if (tfunc) {
            char name[28]; char *argv[ARGN]; uint8_t argc = 0;
            setlen(name, tv, 27); upstr(name);
            lex();                                    /* the ( */
            lex();
            while (tk != T_RP) {
                if (argc >= ARGN) die("too many arguments");
                argv[argc] = tpush();
                if (tk == T_COMMA) { argv[argc][0] = 0; }             /* an omitted argument */
                else expr(argv[argc]);
                argc++;
                if (tk == T_COMMA) { lex(); if (tk == T_RP) { if (argc < ARGN) { argv[argc] = tpush(); argv[argc][0] = 0; argc++; } } continue; }
                if (tk != T_RP) die("missing )");
            }
            lex();
            call_function(name, argc, argv, out);
            while (argc--) tpop();
            return;
        }
        { const char *v = value_of(tv, out); if (v != out) strcpy(out, v); }
        lex(); return;
    }
    if (tk == T_LP) { lex(); expr(out); if (tk != T_RP) die("missing )"); lex(); return; }
    if (is_op("-")) { lex(); primary(out); putnum(out, -num(out)); return; }
    if (is_op("+")) { lex(); primary(out); putnum(out, num(out)); return; }
    if (is_op("\\")) { lex(); primary(out); if (strcmp(out, "0") && strcmp(out, "1")) die("logical value not 0 or 1"); out[0] = (char)('1' - (out[0] - '0')); return; }
    die("expression expected");
}
static long ipow(long b, long e) { long r = 1; if (e < 0) return 0; while (e--) r *= b; return r; }
static void power(char *out)
{
    primary(out);
    while (is_op("**")) { char *r = tpush(); lex(); primary(r); putnum(out, ipow(num(out), num(r))); tpop(); }
}
static void term(char *out)
{
    power(out);
    for (;;) {
        char *r; long a, b; char op[3];
        if (!(tk == T_OP && (tv[0] == '*' || tv[0] == '/' || tv[0] == '%') && tv[1] != '*')) break;
        strcpy(op, tv); r = tpush(); lex(); power(r); a = num(out); b = num(r);
        if (op[0] != '*' && b == 0) die("division by zero");
        putnum(out, op[0] == '*' ? a * b : op[0] == '%' ? a / b : op[1] == '/' ? a % b : a / b);
        tpop();
    }
}
static void arith(char *out)
{
    term(out);
    while (tk == T_OP && (tv[0] == '+' || tv[0] == '-') && !tv[1]) {
        char *r = tpush(); char op = tv[0]; lex(); term(r);
        putnum(out, op == '+' ? num(out) + num(r) : num(out) - num(r)); tpop();
    }
}
static void concat(char *out)
{
    arith(out);
    for (;;) {
        char *r;
        if (is_op("||")) { r = tpush(); lex(); arith(r); cat(out, r); tpop(); continue; }
        if (tk == T_SYM || tk == T_STR || tk == T_LP || (tk == T_OP && tv[0] == '\\' && !tv[1])) {   /* abuttal, or a blank */
            uint8_t b = tblank; r = tpush(); arith(r); if (b) catc(out, ' '); cat(out, r); tpop(); continue;
        }
        break;
    }
}
static const char *strip2(const char *s, char *b)   /* both ends, for the loose = */
{
    uint16_t n; s = skipsp(s); strcpy(b, s); n = strlen(b);
    while (n && b[n - 1] == ' ') b[--n] = 0;
    return b;
}
static int compare(const char *a, const char *b, uint8_t strict)
{
    if (!strict && is_num(a) && is_num(b)) { long x = num(a), y = num(b); return x < y ? -1 : x > y; }
    if (strict) return strcmp(a, b);
    { char *sa = tpush(), *sb = tpush(); int r = strcmp(strip2(a, sa), strip2(b, sb)); tpop(); tpop(); return r; }
}
static void comparison(char *out)
{
    concat(out);
    for (;;) {
        char op[4]; char *r; int c; uint8_t v;
        if (tk != T_OP) break;
        if (!(tv[0] == '=' || tv[0] == '<' || tv[0] == '>' || (tv[0] == '\\' && tv[1]))) break;
        strcpy(op, tv); r = tpush(); lex(); concat(r);
        if (!strcmp(op, "=")) v = compare(out, r, 0) == 0;
        else if (!strcmp(op, "==")) v = compare(out, r, 1) == 0;
        else if (!strcmp(op, "\\=") || !strcmp(op, "<>") || !strcmp(op, "><")) v = compare(out, r, 0) != 0;
        else if (!strcmp(op, "\\==")) v = compare(out, r, 1) != 0;
        else { c = compare(out, r, 0);
            if (!strcmp(op, ">")) v = c > 0; else if (!strcmp(op, "<")) v = c < 0;
            else if (!strcmp(op, ">=") || !strcmp(op, "\\<")) v = c >= 0;
            else if (!strcmp(op, "<=") || !strcmp(op, "\\>")) v = c <= 0;
            else die("bad comparison"); }
        out[0] = (char)('0' + v); out[1] = 0; tpop();
    }
}
static uint8_t logical(const char *s) { if (!strcmp(s, "0")) return 0; if (!strcmp(s, "1")) return 1; die("logical value not 0 or 1"); return 0; }
static void andexpr(char *out)
{
    comparison(out);
    while (tk == T_OP && tv[0] == '&' && !tv[1]) { char *r = tpush(); uint8_t a; lex(); comparison(r); a = logical(out) & logical(r); out[0] = (char)('0' + a); out[1] = 0; tpop(); }
}
static void expr(char *out)
{
    andexpr(out);
    while (tk == T_OP && ((tv[0] == '|' && !tv[1]) || !strcmp(tv, "&&"))) {
        char *r = tpush(); uint8_t x = tv[1] == '&', a; lex(); andexpr(r);
        a = x ? logical(out) ^ logical(r) : logical(out) | logical(r);
        out[0] = (char)('0' + a); out[1] = 0; tpop();
    }
}
/* evaluate the text at p (to its end) into out */
static void eval(const char *p, char *out)
{
    const char *save = cp; uint8_t stk = tk;
    cp = p; lex(); expr(out);
    if (tk != T_END) die("unexpected text in expression");
    cp = save; tk = stk;
}
static long evaln(const char *p) { char *t = tpush(); long v; eval(p, t); v = num(t); tpop(); return v; }

/* ---- input --------------------------------------------------------------- */
static void readln(char *b, uint8_t max)
{
    uint8_t n = 0, k;
    for (;;) {
        k = rom_getin();
        if (!k) { wait_vblank(); continue; }
        if (k == 0x0D) break;
        if (k == 0x08 || k == 0x89) { if (n) { n--; outc(8); outc(' '); outc(8); } continue; }
        if (k >= 0x80 || k < 0x20) continue;
        if (n < max - 1) { b[n++] = (char)k; outc((char)k); }
    }
    b[n] = 0; nl();
}
static void pull_line(char *b)
{
    if (qn) { strcpy(b, q[qh]); qh = (uint8_t)((qh + 1) % NQ); qn--; return; }
    readln(b, VMAX);
}
static void queue_put(const char *s, uint8_t front)
{
    if (qn >= NQ) die("queue full");
    if (front) { qh = (uint8_t)((qh + NQ - 1) % NQ); setlen(q[qh], s, 99); }
    else setlen(q[(qh + qn) % NQ], s, 99);
    qn++;
}

/* ---- streams: LINEIN / LINEOUT / LINES ------------------------------------- */
static uint8_t stream_open(const char *name)
{
    uint8_t i;
    for (i = 0; i < 4; i++) if (!strcmp(streams[i].name, name)) return i;
    for (i = 0; i < 4 && streams[i].name[0]; i++) ;
    if (i == 4) { i = 0; }                                    /* the oldest goes */
    setlen(streams[i].name, name, 39);
    fs_name(streams[i].name); w32(FS_ADDR, STREAM_PHYS + (uint32_t)i * 0x10000UL); w32(FS_LEN, 0xFFFFUL);
    if (fs_do(C_LOAD)) { streams[i].len = 0; } else streams[i].len = r32(FS_LEN);
    streams[i].off = 0;
    return i;
}
static void out_flush(void)
{
    if (!outname[0]) return;
    fs_name(outname); w32(FS_ADDR, OUT_PHYS); w32(FS_LEN, outlen); fs_do(C_SAVE);
}
static void lineout(const char *name, const char *s)
{
    if (strcmp(outname, name)) {
        out_flush();
        setlen(outname, name, 39);
        fs_name(outname); w32(FS_ADDR, OUT_PHYS); w32(FS_LEN, 0xFFFFUL);
        outlen = fs_do(C_LOAD) ? 0 : r32(FS_LEN);           /* append to what is there */
    }
    while (*s) { if (outlen < 0xFFFEUL) far_poke(OUT_PHYS + outlen++, (uint8_t)*s); s++; }
    far_poke(OUT_PHYS + outlen++, '\n');
}

/* ---- the environments ------------------------------------------------------ */
#define CMDLINE ((char *)0x0300)     /* the page SWAP carries across: the callee is loaded over our image */
unsigned char __fastcall__ rom_shell(const char *line);
static void mailbox(const char *cmd, char *result)
{
    static const char mail[] = "/RX/MAIL.CMD", reply[] = "/RX/MAIL.RPL";
    char nm[40]; uint16_t n; uint8_t st;
    strcpy(nm, mail); fs_name(nm); w32(FS_ADDR, (uint32_t)(uint16_t)cmd); w32(FS_LEN, strlen(cmd)); fs_do(C_SAVE);
    strcpy(nm, reply); fs_name(nm); fs_do(C_RM);
    strcpy(CMDLINE, "SWAP "); strcat(CMDLINE, env); strcat(CMDLINE, " @"); strcat(CMDLINE, mail);
    st = rom_shell(CMDLINE);                       /* plain SWAP: the port answers in a file, not on the screen */
    strcpy(nm, reply); fs_name(nm); w32(FS_ADDR, SCRATCH); w32(FS_LEN, 0xFFFFUL);
    if (fs_do(C_LOAD)) { var_setn("RC", st ? -3 : 0); result[0] = 0; return; }   /* no reply: -3 = command not found, like REXX */
    n = (uint16_t)r32(FS_LEN);
    { char *b = tpush(); uint16_t i = 0, k = 0; uint8_t c;
      while (i < n && (c = far_peek(SCRATCH + i)) != '\n' && k < VMAX - 1) { b[k++] = (char)c; i++; }
      b[k] = 0; var_setn("RC", is_num(b) ? atol(b) : 0);
      if (i < n) i++;
      k = 0; while (i < n && k < VMAX - 1) { c = far_peek(SCRATCH + i++); if (c == '\r') continue; b[k++] = (char)c; }
      while (k && (b[k - 1] == '\n' || b[k - 1] == ' ')) k--;
      b[k] = 0; strcpy(result, b); tpop(); }
}
/* Does this command line start a program?  The shell's own rule: a word that
 * names a file (or that file with .PRG) is a program, and a program is loaded
 * at $6000 -- through the middle of this interpreter.  Those go through SWAP,
 * which saves and restores us; a built-in command does not need to, and would
 * lose its output to SWAP's screen restore if it did. */
static uint8_t is_program(const char *s)
{
    char nm[44]; uint8_t i = 0;
    while (*s == ' ') s++;
    while (*s && *s != ' ' && i < 38) nm[i++] = *s++;
    nm[i] = 0;
    if (!i) return 0;
    if (eqi(nm, "SWAP")) return 2;                     /* already swapping: leave the line alone */
    if (eqi(nm, "RUN") || eqi(nm, "EXEC") || eqi(nm, "CPM") ||
        eqi(nm, "BBC") || eqi(nm, "BBCBASIC") || eqi(nm, "MON") || eqi(nm, "WOZ")) return 1;
    fs_name(nm); if (!fs_do(C_STAT)) return 1;
    strcat(nm, ".PRG"); fs_name(nm); if (!fs_do(C_STAT)) return 1;
    return 0;
}
static void command(const char *s)
{
    char *r = tpush();
    if (trace) { outs(">>> "); outs(s); nl(); }
    if (!strcmp(env, "COMMAND") || !strcmp(env, "K/OS") || !strcmp(env, "SHELL")) {
        uint8_t prog = is_program(s);
        uint16_t n = strlen(s); if (n > 240) n = 240;
        if (prog == 1) { strcpy(CMDLINE, "SWAP -k "); memcpy(CMDLINE + 8, s, n); CMDLINE[8 + n] = 0; }   /* -k: what it drew stays, as at the prompt */
        else      { memcpy(CMDLINE, s, n); CMDLINE[n] = 0; }     /* the low page either way: SWAP loads over our image */
        var_setn("RC", (long)rom_shell(CMDLINE));
    } else if (!strcmp(env, "TUBE")) {
        /* The co-processor's shell (the same door `!` opens, and gated the same
         * way).  RESULT takes the output, newlines folded to blanks, up to a
         * value's length; for more than that, redirect to a file and LINEIN it. */
        uint16_t n = 0, idle = 0;
        if (!(REG(TUBE) & 4)) { var_setn("RC", -3); var_set("RESULT", ""); tpop(); return; }
        { uint16_t k = strlen(s); if (k > 250) k = 250; memcpy(CMDLINE, s, k); CMDLINE[k] = 0; }
        w32(TUBE + 4, (uint32_t)(uint16_t)CMDLINE);       /* the low page, as the ROM's `!` does it */
        REG(TUBE + 8) = 25; REG(TUBE + 9) = 80;
        REG(TUBE + 3) = 4;
        for (;;) {
            uint8_t st = REG(TUBE), c;
            if (st & 0x80) { c = REG(TUBE + 1); idle = 0;
                if (c == '\r') continue;
                if (c == '\n' || c == '\t') c = ' ';
                if (n < VMAX - 1) r[n++] = (char)c;
                continue; }
            if (!(st & 1)) break;
            wait_vblank();
            if (++idle > 900) { REG(TUBE + 3) = 2; break; }        /* 15 s of silence: stop it */
        }
        r[n] = 0; while (n && r[n - 1] == ' ') r[--n] = 0;
        var_set("RESULT", r); var_setn("RC", 0);
    } else if (!strcmp(env, "RX") || !strcmp(env, "NONE")) {
        var_setn("RC", 0);
    } else {
        mailbox(s, r);
        var_set("RESULT", r);
    }
    tpop();
}

/* ---- the built-in functions -------------------------------------------------- */
/* positions and lengths are int (16-bit): a value is at most VMAX-1 long */
static int argn(uint8_t argc, char **argv, uint8_t i, int def) { return (i < argc && argv[i][0]) ? (int)num(argv[i]) : def; }
static const char *args_(uint8_t argc, char **argv, uint8_t i, const char *def) { return (i < argc && argv[i][0]) ? (const char *)argv[i] : def; }
static char padc(uint8_t argc, char **argv, uint8_t i) { return (i < argc && argv[i][0]) ? argv[i][0] : ' '; }
/* word n (1-based) of s: start pointer and length, 0 if none */
static const char *wordn(const char *s, int n, uint8_t *len)
{
    for (;;) {
        const char *e;
        s = skipsp(s); if (!*s) return 0;
        e = s; while (*e && *e != ' ') e++;
        if (--n == 0) { *len = (uint8_t)(e - s); return s; }
        s = e;
    }
}
static int words_(const char *s) { int n = 0; uint8_t l; while (wordn(s, n + 1, &l)) n++; return n; }
static void two_digits(char *d, uint8_t v) { d[0] = (char)('0' + v / 10); d[1] = (char)('0' + v % 10); d[2] = 0; }
static void padfill(char *out, const char *s, int from, int n, char pd)   /* out = s[from..from+n) padded */
{
    int i, l = strlen(s);
    if (n > VMAX - 1) n = VMAX - 1;
    for (i = 0; i < n; i++) { int k = from + i; out[i] = (k >= 0 && k < l) ? s[k] : pd; }
    out[n] = 0;
}
static void builtin(const char *f, uint8_t argc, char **argv, char *out)
{
    const char *s = argc ? argv[0] : ""; int l = strlen(s); int n, i;
    out[0] = 0;
    switch (f[0]) {
    case 'A':
        if (!strcmp(f, "ABS")) { long v = num(s); putnum(out, v < 0 ? -v : v); return; }
        if (!strcmp(f, "ARG")) {
            if (!argc || !s[0]) { putnum(out, arg_count(call_depth)); return; }
            n = (int)num(s);
            if (argc > 1 && argv[1][0]) { uint8_t there = n >= 1 && n <= ARGN && far_peek(arg_at(call_depth, (uint8_t)(n - 1))) != 0; out[0] = (char)('0' + (upc((uint8_t)argv[1][0]) == 'E' ? there : !there)); out[1] = 0; return; }
            if (n >= 1 && n <= ARGN) arg_get(call_depth, (uint8_t)(n - 1), out);
            return;
        }
        if (!strcmp(f, "ADDRESS")) { strcpy(out, env); return; }
        break;
    case 'C':
        if (!strcmp(f, "CENTRE") || !strcmp(f, "CENTER")) { n = argn(argc, argv, 1, 0); padfill(out, s, -((n - l) / 2), n, padc(argc, argv, 2)); return; }
        if (!strcmp(f, "COPIES")) { n = argn(argc, argv, 1, 0); while (n-- > 0) cat(out, s); return; }
        if (!strcmp(f, "C2D")) { putnum(out, (uint8_t)s[0]); return; }
        break;
    case 'D':
        if (!strcmp(f, "D2C")) { out[0] = (char)num(s); out[1] = 0; return; }
        if (!strcmp(f, "D2X")) { n = argn(argc, argv, 1, 0); ltoa(num(s), out, 16); upstr(out); while ((int)strlen(out) < n) { memmove(out + 1, out, strlen(out) + 1); out[0] = '0'; } return; }
        if (!strcmp(f, "DATATYPE")) {
            char t = argc > 1 ? (char)upc((uint8_t)argv[1][0]) : 0; uint8_t ok = l > 0;
            if (!t) { strcpy(out, is_num(s) ? "NUM" : "CHAR"); return; }
            if (t == 'N' || t == 'W') ok = is_num(s);
            else for (i = 0; i < l; i++) { uint8_t c = (uint8_t)s[i], u = upc(c);
                if (t == 'U' ? !(c >= 'A' && c <= 'Z') : t == 'L' ? !(c >= 'a' && c <= 'z') : t == 'M' ? !(u >= 'A' && u <= 'Z') :
                    t == 'A' ? !((u >= 'A' && u <= 'Z') || is_digit(c)) : t == 'X' ? !(is_digit(c) || (u >= 'A' && u <= 'F') || c == ' ') : 1) ok = 0; }
            out[0] = (char)('0' + ok); out[1] = 0; return;
        }
        if (!strcmp(f, "DATE")) {
            static const char mon[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
            char o = argc && s[0] ? (char)upc((uint8_t)s[0]) : 'N'; uint8_t d, m; uint16_t y;
            RTC_LATCH(); d = REG(SYS + 8); m = REG(SYS + 9); y = (uint16_t)REG(SYS + 0x0A) | ((uint16_t)REG(SYS + 0x0B) << 8);
            if (m < 1 || m > 12) m = 1;
            if (o == 'S') { putnum(out, y); two_digits(out + 4, m); two_digits(out + 6, d); return; }
            if (o == 'E' || o == 'U' || o == 'O') {
                uint8_t a = o == 'E' ? d : o == 'U' ? m : (uint8_t)(y % 100), b = o == 'E' ? m : o == 'U' ? d : m, c = o == 'O' ? d : (uint8_t)(y % 100);
                two_digits(out, a); out[2] = '/'; two_digits(out + 3, b); out[5] = '/'; two_digits(out + 6, c); return;
            }
            if (o == 'M') { setlen(out, mon + (m - 1) * 3, 3); return; }
            putnum(out, d); catc(out, ' '); setlen(out + strlen(out), mon + (m - 1) * 3, 3); catc(out, ' '); putnum(out + strlen(out), y);
            return;
        }
        break;
    case 'E':
        if (!strcmp(f, "EXISTS")) { char nm[40]; setlen(nm, s, 39); fs_name(nm); out[0] = (char)('0' + !fs_do(C_STAT)); out[1] = 0; return; }
        break;
    case 'K':
        if (!strcmp(f, "KEY")) { uint8_t k = rom_getin(); if (k) { out[0] = (char)k; out[1] = 0; } return; }    /* a key, or '' */
        break;
    case 'L':
        if (!strcmp(f, "LENGTH")) { putnum(out, l); return; }
        if (!strcmp(f, "LEFT")) { padfill(out, s, 0, argn(argc, argv, 1, 0), padc(argc, argv, 2)); return; }
        if (!strcmp(f, "LASTPOS")) {
            const char *h = args_(argc, argv, 1, ""); int hl = strlen(h), st = argn(argc, argv, 2, hl), k;
            for (k = (st < hl ? st : hl) - l; k >= 0; k--) if (!memcmp(h + k, s, l)) { putnum(out, k + 1); return; }
            out[0] = '0'; out[1] = 0; return;
        }
        if (!strcmp(f, "LOWER")) { for (i = 0; i <= l; i++) out[i] = (s[i] >= 'A' && s[i] <= 'Z') ? (char)(s[i] + 32) : s[i]; return; }
        if (!strcmp(f, "LINEIN")) {
            uint8_t ix = stream_open(s); stream_t *st = &streams[ix]; uint8_t c; n = 0;
            while (st->off < st->len && (c = far_peek(STREAM_PHYS + (uint32_t)ix * 0x10000UL + st->off)) != '\n') { st->off++; if (c != '\r' && n < VMAX - 1) out[n++] = (char)c; }
            if (st->off < st->len) st->off++;
            out[n] = 0; return;
        }
        if (!strcmp(f, "LINES")) { uint8_t ix = stream_open(s); out[0] = (char)('0' + (streams[ix].off < streams[ix].len)); out[1] = 0; return; }
        if (!strcmp(f, "LINEOUT")) { if (argc > 1) lineout(s, argv[1]); else { out_flush(); if (!strcmp(outname, s)) outname[0] = 0; } out[0] = '0'; out[1] = 0; return; }
        break;
    case 'M':
        if (!strcmp(f, "MAX") || !strcmp(f, "MIN")) {
            long v = num(s);
            for (i = 1; i < argc; i++) { long x = num(argv[i]); if (f[1] == 'A' ? x > v : x < v) v = x; }
            putnum(out, v); return;
        }
        break;
    case 'P':
        if (!strcmp(f, "POS")) {
            const char *h = args_(argc, argv, 1, ""); int hl = strlen(h), k;
            for (k = argn(argc, argv, 2, 1) - 1; l && k >= 0 && k + l <= hl; k++) if (!memcmp(h + k, s, l)) { putnum(out, k + 1); return; }
            out[0] = '0'; out[1] = 0; return;
        }
        /* Under 64 KB is the CPU's own view, so PEEK(53504) is VICKY's control
         * register and not the far memory behind it; 64 KB and up is far memory. */
        if (!strcmp(f, "PEEK")) { uint32_t a = (uint32_t)num(s); putnum(out, a < 0x10000UL ? REG((uint16_t)a) : far_peek(a)); return; }
        if (!strcmp(f, "POKE")) { uint32_t a = (uint32_t)num(s); uint8_t v = (uint8_t)argn(argc, argv, 1, 0); if (a < 0x10000UL) REG((uint16_t)a) = v; else far_poke(a, v); out[0] = '0'; out[1] = 0; return; }
        break;
    case 'Q':
        if (!strcmp(f, "QUEUED")) { putnum(out, qn); return; }
        break;
    case 'R':
        if (!strcmp(f, "RIGHT")) { n = argn(argc, argv, 1, 0); padfill(out, s, l - n, n, padc(argc, argv, 2)); return; }
        if (!strcmp(f, "REVERSE")) { for (i = 0; i < l; i++) out[i] = s[l - 1 - i]; out[l] = 0; return; }
        if (!strcmp(f, "RANDOM")) {
            int lo = argn(argc, argv, 0, 0), hi = argn(argc, argv, 1, 999);
            if (argc == 1) { hi = lo; lo = 0; }
            rnd_seed = rnd_seed * 1103515245UL + 12345UL;
            putnum(out, lo + (int)((rnd_seed >> 8) % (uint32_t)(hi - lo + 1))); return;
        }
        break;
    case 'S':
        if (!strcmp(f, "SUBSTR")) { int st = argn(argc, argv, 1, 1); if (st < 1) die("SUBSTR: start < 1"); padfill(out, s, st - 1, argn(argc, argv, 2, l - st + 1 > 0 ? l - st + 1 : 0), padc(argc, argv, 3)); return; }
        if (!strcmp(f, "STRIP")) {
            char opt = argc > 1 ? (char)upc((uint8_t)argv[1][0]) : 'B'; char pd = padc(argc, argv, 2); const char *e;
            if (opt != 'T') while (*s == pd) s++;
            e = s + strlen(s);
            if (opt != 'L') while (e > s && e[-1] == pd) e--;
            setlen(out, s, e - s); return;
        }
        if (!strcmp(f, "SPACE")) {
            int ns = argn(argc, argv, 1, 1); char pd = padc(argc, argv, 2); uint8_t wl; const char *w;
            for (i = 1; (w = wordn(s, i, &wl)) != 0; i++) { if (i > 1) for (n = 0; n < ns; n++) catc(out, pd); n = strlen(out); if (n + wl < VMAX) { memcpy(out + n, w, wl); out[n + wl] = 0; } }
            return;
        }
        if (!strcmp(f, "SIGN")) { long v = num(s); putnum(out, v < 0 ? -1 : v > 0); return; }
        if (!strcmp(f, "SUBWORD")) {
            int st = argn(argc, argv, 1, 1), ln = argn(argc, argv, 2, 999); uint8_t wl; const char *w = wordn(s, st, &wl), *e;
            if (!w) return;
            e = w + wl; for (i = 1; i < ln; i++) { const char *x = wordn(s, st + i, &wl); if (!x) break; e = x + wl; }
            setlen(out, w, e - w); return;
        }
        if (!strcmp(f, "SLEEP")) { n = (int)num(s); while (n-- > 0) wait_vblank(); out[0] = '0'; out[1] = 0; return; }   /* frames */
        if (!strcmp(f, "SYMBOL")) { char *r = tpush(); resolve(s, r); strcpy(out, var_get(r) ? "VAR" : "LIT"); tpop(); return; }
        break;
    case 'T':
        if (!strcmp(f, "TRUNC")) { putnum(out, num(s)); return; }
        if (!strcmp(f, "TIME")) {
            char o = argc && s[0] ? (char)upc((uint8_t)s[0]) : 'N'; uint8_t hh, mm, ss;
            RTC_LATCH(); ss = REG(SYS + 5); mm = REG(SYS + 6); hh = REG(SYS + 7);
            if (o == 'S') { putnum(out, (long)hh * 3600 + (long)mm * 60 + ss); return; }
            if (o == 'M') { putnum(out, (long)hh * 60 + mm); return; }
            if (o == 'H') { putnum(out, hh); return; }
            if (o == 'E' || o == 'R') { putnum(out, (long)(r32(SYS + 0x36) / 1000)); return; }   /* whole seconds since power-on */
            two_digits(out, hh); out[2] = ':'; two_digits(out + 3, mm); out[5] = ':'; two_digits(out + 6, ss); return;
        }
        break;
    case 'U':
        if (!strcmp(f, "UPPER")) { strcpy(out, s); upstr(out); return; }
        break;
    case 'V':
        if (!strcmp(f, "VERIFY")) {
            const char *ref = args_(argc, argv, 1, ""); uint8_t match = argc > 2 && upc((uint8_t)argv[2][0]) == 'M';
            for (i = argn(argc, argv, 3, 1) - 1; i < l; i++) { uint8_t in = strchr(ref, s[i]) != 0; if (in == match) { putnum(out, i + 1); return; } }
            out[0] = '0'; out[1] = 0; return;
        }
        if (!strcmp(f, "VALUE")) { char *r = tpush(); const char *v; resolve(s, r); v = var_get(r); strcpy(out, v ? v : (const char *)r); if (argc > 1) var_set(r, argv[1]); tpop(); return; }
        break;
    case 'W':
        if (!strcmp(f, "WORD")) { uint8_t wl; const char *w = wordn(s, argn(argc, argv, 1, 1), &wl); if (w) setlen(out, w, wl); return; }
        if (!strcmp(f, "WORDS")) { putnum(out, words_(s)); return; }
        if (!strcmp(f, "WORDLENGTH")) { uint8_t wl = 0; wordn(s, argn(argc, argv, 1, 1), &wl); putnum(out, wl); return; }
        if (!strcmp(f, "WORDINDEX")) { uint8_t wl; const char *w = wordn(s, argn(argc, argv, 1, 1), &wl); putnum(out, w ? (int)(w - s) + 1 : 0); return; }
        if (!strcmp(f, "WORDPOS")) {
            const char *h = args_(argc, argv, 1, ""); int nw = words_(s), nh = words_(h), k;
            for (i = argn(argc, argv, 2, 1); nw && i + nw - 1 <= nh; i++) {
                for (k = 0; k < nw; k++) { uint8_t a, b; const char *x = wordn(s, k + 1, &a), *y = wordn(h, i + k, &b); if (a != b || memcmp(x, y, a)) break; }
                if (k == nw) { putnum(out, i); return; }
            }
            out[0] = '0'; out[1] = 0; return;
        }
        break;
    case 'X':
        if (!strcmp(f, "X2D")) { putnum(out, strtol(s, 0, 16)); return; }
        break;
    }
    outs("RX: no such function: "); outs(f); nl();
    die("unknown function");
}

/* ---- running clauses ------------------------------------------------------------ */
static void run_loop(void);
static void exec_stmt(const char *s);
static uint8_t ret_flag;                     /* a RETURN reached the run loop that owns the CALL */
static char retval[VMAX];

static frame_t *push_frame(uint8_t type)
{
    frame_t *f;
    if (fsp >= NFRAME) die("too deeply nested");
    f = &frames[fsp++]; memset(f, 0, sizeof *f); f->type = type; f->rl = rlev; f->vl = var_lvl;
    return f;
}
/* the statement word that decides nesting: DO/SELECT open, END closes.
 * IF x THEN DO, WHEN x THEN DO, ELSE DO, OTHERWISE DO all count. */
static const char *stmt_head(char *c)
{
    const char *r;
    for (;;) {
        char w[12]; uint8_t n = word_at(c, w, 12);
        if (!n) return c;
        if (!strcmp(w, "IF") || !strcmp(w, "WHEN")) { r = find_kw(c, "THEN"); if (!r) return c; c = (char *)r; continue; }
        if (!strcmp(w, "ELSE") || !strcmp(w, "OTHERWISE") || !strcmp(w, "THEN")) { c = (char *)skipsp(c) + n; continue; }
        { const char *p = skipsp(c) + n; if (*p == ':') { c = (char *)p + 1; continue; } }   /* a label in front */
        return c;
    }
}
/* from inside a block: to its END (clauses consumed, the END included) */
static void skip_to_end(void)
{
    uint8_t depth = 0;
    for (;;) {
        char w[12]; char *keep;
        if (!read_clause()) die("END missing");
        keep = kpush(); strcpy(keep, CL);
        word_at(stmt_head(keep), w, 12); kpop();
        if (!strcmp(w, "DO") || !strcmp(w, "SELECT")) depth++;
        else if (!strcmp(w, "END")) { if (!depth) return; depth--; }
    }
}
static const char *peek_else(void)          /* the next clause, if it is an ELSE: the text after it (clause consumed) */
{
    uint16_t save = pos; const char *r;
    if (!read_clause()) { pos = save; return 0; }
    r = kw(CL, "ELSE");
    if (r) return r;
    pos = save; return 0;
}
/* skip one statement (the text s, or the next clause when s is empty) */
static void skip_stmt(const char *s)
{
    char w[12]; const char *r;
    s = skipsp(s);
    if (!*s) { if (!read_clause()) return; s = CL; }
    word_at(s, w, 12);
    if (!strcmp(w, "DO") || !strcmp(w, "SELECT")) { skip_to_end(); return; }
    if (!strcmp(w, "IF")) {
        char *c = (char *)s; r = find_kw(c, "THEN");
        if (!r) { if (!read_clause()) die("THEN missing"); r = kw(CL, "THEN"); if (!r) die("THEN expected"); }
        { char *keep = kpush(); strcpy(keep, r); skip_stmt(keep); kpop(); }
        r = peek_else(); if (r) { char *keep = kpush(); strcpy(keep, r); skip_stmt(keep); kpop(); }
        return;
    }
}
static void after_block(void);
/* the "=" of a controlled DO: the clause starts with a symbol, then blanks,
 * then a single "=".  (Blanks are allowed: "do j = 10 to 1" is REXX too, and
 * requiring "j=10" was the first thing every test script tripped over.) */
static char *ctrl_eq(char *p)
{
    char *q = p;
    if (!is_symch((uint8_t)*q) || is_digit((uint8_t)*q)) return 0;
    while (is_symch((uint8_t)*q)) q++;
    while (*q == ' ') q++;
    return (*q == '=' && q[1] != '=') ? q : 0;
}
/* the DO clause at f->dopos, parsed again: first=1 sets the loop up, else steps it.
 * returns 1 to run the body, 0 when the loop is over (clauses then skipped to END). */
static uint8_t do_step(frame_t *f, uint8_t first)
{
    char *w, *u, *body; uint8_t go = 1;
    pos = f->dopos; read_clause();
    body = (char *)stmt_head(CL);
    body = (char *)kw(body, "DO");
    if (!body) die("DO expected");
    w = find_kw(body, "WHILE"); u = w ? 0 : find_kw(body, "UNTIL");
    if (first) {
        char *e;
        f->cnt = -1;                                          /* -1: no count */
        if (!*body || kw(body, "FOREVER")) { if (!*body && !w && !u) f->cnt = 1; }
        else if ((e = ctrl_eq(body)) != 0) {                   /* i = a TO b BY c FOR n */
            char *to, *by, *fr; long v;
            *e = 0; word_at(body, f->var, 24); body = e + 1;
            to = find_kw(body, "TO"); by = find_kw(body, "BY"); fr = find_kw(body, "FOR");
            if (!by && to) by = find_kw(to, "BY");
            if (!fr && to) fr = find_kw(to, "FOR");
            if (!fr && by) fr = find_kw(by, "FOR");
            if (!to && by) to = find_kw(by, "TO");
            v = evaln(body); var_setn(f->var, v);
            f->to = to ? evaln(to) : 0x7FFFFFFFL; f->by = by ? evaln(by) : 1; f->flag = (uint8_t)(to != 0);
            if (fr) f->cnt = evaln(fr);
            if (f->by >= 0 ? v > f->to : v < f->to) go = 0;
        } else f->cnt = evaln(body);
        if (f->cnt == 0) go = 0;
    } else {
        if (u) { char *t = tpush(); eval(u, t); go = !logical(t); tpop(); if (!go) return 0; }
        if (f->var[0]) { long v = num(var_get(f->var)) + f->by; var_setn(f->var, v); if (f->by >= 0 ? v > f->to : v < f->to) go = 0; }
        if (f->cnt > 0) { if (--f->cnt == 0) go = 0; }
        else if (f->cnt == 0) go = 0;
    }
    if (go && w) { char *t = tpush(); eval(w, t); go = logical(t); tpop(); }
    if (go) { f->body = pos; return 1; }
    skip_to_end();
    return 0;
}
static void end_of_loop(void)
{
    frame_t *f;
    if (!fsp) die("END without DO");
    f = &frames[fsp - 1];
    if (f->type == CT_SEL) { fsp--; after_block(); return; }
    if (f->type != CT_DO) die("END without DO");
    if (do_step(f, 0)) return;
    fsp--; after_block();
}
/* a block just closed: the IF that owned it may have an ELSE to skip; the
 * SELECT that owned it is done */
static void after_block(void)
{
    while (fsp) {
        frame_t *f = &frames[fsp - 1];
        if (f->type == CT_IF) { const char *r; fsp--; r = peek_else(); if (r) { char *keep = kpush(); strcpy(keep, r); skip_stmt(keep); kpop(); } continue; }
        if (f->type == CT_SEL && f->flag == 1) { fsp--; skip_to_end(); continue; }
        break;
    }
}
/* run the statement s; a block statement pushes a frame and returns */
static void run_stmt(const char *s, uint8_t owner)   /* owner: push CT_IF first when s opens a block */
{
    char w[12];
    s = skipsp(s);
    if (!*s) { if (!read_clause()) return; if (owner) { word_at(stmt_head(CL), w, 12); if (!strcmp(w, "DO") || !strcmp(w, "SELECT")) push_frame(CT_IF); } exec_stmt(CL); return; }
    if (owner) { word_at(s, w, 12); if (!strcmp(w, "DO") || !strcmp(w, "SELECT")) push_frame(CT_IF); }
    { char *keep = kpush(); strcpy(keep, s); memmove(CL, keep, strlen(keep) + 1); kpop(); exec_stmt(CL); }
}
static void call_internal(uint16_t at, uint8_t argc, char **argv, char *out)
{
    frame_t *f; uint8_t i; const char *save_cp = cp; uint8_t save_tk = tk; uint8_t save_tblank = tblank, save_tfunc = tfunc;
    uint16_t save_pos = pos, save_cstart = cstart;
    if (rlev + 1 >= RUNMAX || call_depth + 1 >= 8 || (unsigned)(sp0 - rx_sp()) > STACK_BUDGET) die("calls too deep");
    { uint16_t k = 0; do { far_poke(SAVE_PHYS + (uint32_t)call_depth * 256 + k, (uint8_t)tv[k]); } while (tv[k++]); }
    f = push_frame(CT_CALL); f->dopos = pos; f->cnt = (long)fsp - 1;
    call_depth++;
    for (i = 0; i < ARGN; i++) arg_put(call_depth, i, i < argc ? argv[i] : "");
    var_setn("SIGL", line_of(cstart));
    pos = at; rlev++; ret_flag = 0; retval[0] = 0;
    run_loop();
    rlev--; call_depth--;
    while (var_lvl > f->vl) {                                 /* PROCEDURE's variables go */
        for (i = 0; i < nvars; i++) if (vars[i].lvl == var_lvl) vars[i].lvl = 0xFF;
        var_lvl--;
    }
    fsp = (uint8_t)f->cnt;
    pos = f->dopos; cstart = save_cstart; if (pos < save_pos) pos = save_pos;
    strcpy(out, retval);
    cp = save_cp; tk = save_tk; tblank = save_tblank; tfunc = save_tfunc;
    { uint16_t k = 0; uint8_t c; do { c = far_peek(SAVE_PHYS + (uint32_t)call_depth * 256 + k); tv[k] = (char)c; } while (c && ++k < VMAX - 1); }
}
static void call_function(const char *name, uint8_t argc, char **argv, char *out)
{
    uint16_t at = label_at(name);
    if (at != 0xFFFF) { call_internal(at, argc, argv, out); return; }
    builtin(name, argc, argv, out);
}
/* PARSE: the template t applied to the source s (one segment; commas handled by the caller) */
static void parse_into(const char *s, const char *t, uint8_t upper)
{
    char *src = tpush(); uint16_t start = 0, mpos = 0, len; const char *p = t;
    static char names[12][32]; uint8_t nn = 0;
    strcpy(src, s); if (upper) upstr(src); len = strlen(src);
    for (;;) {
        uint16_t end = len, next = len; uint8_t pat = 0;
        /* collect variables up to the next pattern */
        nn = 0;
        for (;;) {
            p = skipsp(p);
            if (!*p) break;
            if (*p == '\'' || *p == '"') {                          /* a string pattern */
                char qc = *p++, *b = tpush(); uint16_t k = 0; const char *hit;
                while (*p && *p != qc) b[k++] = *p++; b[k] = 0; if (*p) p++;
                hit = k ? strstr(src + start, b) : 0;
                if (hit) { end = hit - src; next = end + k; } else { end = len; next = len; }
                tpop(); pat = 1; break;
            }
            if (*p == '(') { const char *v; char *nm = tpush(), *b = tpush(); uint16_t k = 0; const char *hit; p++;
                while (*p && *p != ')') nm[k++] = *p++; nm[k] = 0; if (*p) p++;
                v = value_of(nm, b); if (v != b) strcpy(b, v);
                hit = b[0] ? strstr(src + start, b) : 0;
                if (hit) { end = hit - src; next = end + strlen(b); } else { end = len; next = len; }
                tpop(); tpop(); pat = 1; break; }
            if (is_digit((uint8_t)*p) || ((*p == '+' || *p == '-') && is_digit((uint8_t)p[1]))) {   /* a column */
                long n = atol(p); char sg = *p; long col;
                while (*p == '+' || *p == '-' || is_digit((uint8_t)*p)) p++;
                col = (sg == '+' || sg == '-') ? (long)mpos + n : n - 1;
                if (col < 0) col = 0; if (col > len) col = len;
                end = (uint16_t)col; if (end < start) end = len; next = (uint16_t)col; pat = 2; break;
            }
            if (nn < 12) { uint8_t k = 0; while (is_symch((uint8_t)*p) && k < 31) names[nn][k++] = *p++; names[nn][k] = 0; if (k) nn++; else p++; }
            else while (is_symch((uint8_t)*p)) p++;
        }
        /* hand src[start..end) to the names, a word each, the last one the rest */
        { char *piece = tpush(); uint8_t i; const char *w = src + start, *e = src + end;
          for (i = 0; i < nn; i++) {
              while (w < e && *w == ' ') w++;
              if (i == nn - 1) setlen(piece, w, e - w);
              else { const char *x = w; while (x < e && *x != ' ') x++; setlen(piece, w, x - w); w = x; }
              if (strcmp(names[i], ".")) { char *r = tpush(); resolve(names[i], r); var_set(r, piece); tpop(); }
          } tpop(); }
        if (!pat) { tpop(); break; }
        if (pat == 2) { start = next; mpos = next; }
        else { mpos = end; start = next; }
    }
}
static void parse_cmd(const char *p, uint8_t upper)
{
    char *src = tpush(); const char *r; uint8_t from_args = 0; char *tpl = kpush();
    if ((r = kw(p, "UPPER")) != 0) { upper = 1; p = r; }
    if ((r = kw(p, "ARG")) != 0) { from_args = 1; p = r; }
    else if ((r = kw(p, "PULL")) != 0) { pull_line(src); p = r; }
    else if ((r = kw(p, "VAR")) != 0) { char *nm = tpush(); uint8_t n = 0; char *rn = tpush(); const char *v; r = skipsp(r); while (is_symch((uint8_t)*r)) nm[n++] = *r++; nm[n] = 0; resolve(nm, rn); v = var_get(rn); strcpy(src, v ? v : (const char *)rn); p = r; tpop(); tpop(); }
    else if ((r = kw(p, "VALUE")) != 0) { char *w = find_kw((char *)r, "WITH"); if (!w) die("WITH missing"); eval(r, src); p = w; }
    else if ((r = kw(p, "SOURCE")) != 0) { strcpy(src, "K4510 COMMAND RX"); p = r; }
    else if ((r = kw(p, "VERSION")) != 0) { strcpy(src, "RX 0.1 K4510 7 Sep 2026"); p = r; }
    else if ((r = kw(p, "LINEIN")) != 0) { readln(src, VMAX); p = r; }
    else die("PARSE what?");
    strcpy(tpl, p);
    if (from_args) {                                           /* one template segment per argument */
        char *seg = tpl; uint8_t i = 0;
        for (;;) {
            char *c = strchr(seg, ','); if (c) *c = 0;
            arg_get(call_depth, i, src); parse_into(src, seg, upper);
            if (!c || ++i >= ARGN) break;
            seg = c + 1;
        }
    } else { char *c = strchr(tpl, ','); if (c) *c = 0; parse_into(src, tpl, upper); }
    kpop(); tpop();
}
static void exec_stmt(const char *s)
{
    char w[16]; uint8_t n; const char *r;
    s = skipsp(s);
    if (!*s) return;
    if (trace) { outs("... "); outs(s); nl(); }
    n = word_at(s, w, 16);
    r = skipsp(s + n);
    if (n && *r == ':' ) { run_stmt(r + 1, 0); return; }                      /* a label */
    if (n && *r == '=' && r[1] != '=' && !is_digit((uint8_t)w[0])) {          /* assignment */
        char *v = tpush(); char *name = tpush(); char *sym = tpush();
        setlen(sym, skipsp(s), n); resolve(sym, name);
        eval(r + 1, v); var_set(name, v); tpop(); tpop(); tpop(); return;
    }
    if (!strcmp(w, "SAY")) { char *v = tpush(); if (*r) eval(r, v); outs(v); nl(); tpop(); return; }
    if (!strcmp(w, "NOP")) return;
    if (!strcmp(w, "IF")) {
        char *v = tpush(); char *t = find_kw((char *)r, "THEN"); uint8_t yes;
        eval(r, v); yes = logical(v); tpop();
        if (!t) { if (!read_clause()) die("THEN missing"); t = (char *)kw(CL, "THEN"); if (!t) die("THEN expected"); }
        if (yes) {
            char *keep = kpush(); char w2[12]; strcpy(keep, t);
            if (!*keep) { if (!read_clause()) { kpop(); return; } strcpy(keep, CL); }
            word_at(stmt_head(keep), w2, 12);
            if (!strcmp(w2, "DO") || !strcmp(w2, "SELECT")) { push_frame(CT_IF); run_stmt(keep, 0); kpop(); return; }
            run_stmt(keep, 0); kpop();
            { const char *e = peek_else(); if (e) { char *k2 = kpush(); strcpy(k2, e); skip_stmt(k2); kpop(); } }
        } else {
            char *keep = kpush(); strcpy(keep, t); skip_stmt(keep); kpop();
            { const char *e = peek_else(); if (e) { char *k2 = kpush(); strcpy(k2, e); run_stmt(k2, 1); kpop(); } }
        }
        return;
    }
    if (!strcmp(w, "ELSE")) die("ELSE without IF");
    if (!strcmp(w, "THEN")) die("THEN without IF");
    if (!strcmp(w, "DO")) { frame_t *f = push_frame(CT_DO); f->dopos = cstart; if (!do_step(f, 1)) { fsp--; after_block(); } return; }
    if (!strcmp(w, "END")) { end_of_loop(); return; }
    if (!strcmp(w, "LEAVE") || !strcmp(w, "ITERATE")) {
        uint8_t i = fsp; char nm[24]; word_at(r, nm, 24);
        while (i) { frame_t *f = &frames[i - 1]; if (f->type == CT_CALL) i = 0; else if (f->type == CT_DO && (!nm[0] || !strcmp(nm, f->var))) break; else i--; }
        if (!i) die(w[0] == 'L' ? "LEAVE outside a loop" : "ITERATE outside a loop");
        fsp = i;
        if (w[0] == 'L') { fsp--; skip_to_end(); after_block(); }
        else { if (!do_step(&frames[fsp - 1], 0)) { fsp--; after_block(); } }
        return;
    }
    if (!strcmp(w, "SELECT")) { push_frame(CT_SEL); return; }
    if (!strcmp(w, "WHEN")) {
        frame_t *f = fsp ? &frames[fsp - 1] : 0; char *v; char *t; uint8_t yes;
        if (!f || f->type != CT_SEL) die("WHEN outside SELECT");
        v = tpush(); t = find_kw((char *)r, "THEN"); eval(r, v); yes = logical(v); tpop();
        if (!t) { if (!read_clause()) die("THEN missing"); t = (char *)kw(CL, "THEN"); if (!t) die("THEN expected"); }
        if (yes) {
            char *keep = kpush(); char w2[12]; f->flag = 1; strcpy(keep, t);
            if (!*keep) { if (!read_clause()) { kpop(); return; } strcpy(keep, CL); }
            word_at(stmt_head(keep), w2, 12);
            run_stmt(keep, 0); kpop();
            if (strcmp(w2, "DO") && strcmp(w2, "SELECT")) after_block();
        } else { char *keep = kpush(); strcpy(keep, t); skip_stmt(keep); kpop(); }
        return;
    }
    if (!strcmp(w, "OTHERWISE")) {
        frame_t *f = fsp ? &frames[fsp - 1] : 0;
        if (!f || f->type != CT_SEL) die("OTHERWISE outside SELECT");
        f->flag = 2; run_stmt(r, 0); return;
    }
    if (!strcmp(w, "EXIT")) { if (*r) { char *v = tpush(); eval(r, v); exit_code = is_num(v) ? (int)atol(v) : 0; tpop(); } longjmp(top, 2); }
    if (!strcmp(w, "RETURN")) {
        uint8_t i = fsp;
        /* into a temp first: retval is one global, and this expression may
         * itself call a function whose RETURN would land in it half-way
         * through (fact(n) = n * fact(n-1) found that). */
        if (*r) { char *v = tpush(); eval(r, v); strcpy(retval, v); tpop(); } else retval[0] = 0;
        while (i && frames[i - 1].type != CT_CALL) i--;
        if (!i) { longjmp(top, 2); }                             /* RETURN from the main level = EXIT */
        ret_flag = 1; return;
    }
    if (!strcmp(w, "CALL")) {
        char name[28]; char *argv[ARGN]; uint8_t argc = 0, i; char *out; uint16_t at;
        n = word_at(r, name, 28); r = skipsp(r + n);
        if (!strcmp(name, "ON") || !strcmp(name, "OFF")) return;
        cp = r; lex();
        while (tk != T_END) {
            if (argc >= ARGN) die("too many arguments");
            argv[argc] = tpush();
            if (tk == T_COMMA) argv[argc][0] = 0; else expr(argv[argc]);
            argc++;
            if (tk == T_COMMA) lex(); else if (tk != T_END) die("bad CALL arguments");
        }
        out = tpush();
        at = label_at(name);
        if (at != 0xFFFF) { call_internal(at, argc, argv, out); if (out[0]) var_set("RESULT", out); else var_drop("RESULT"); }
        else { builtin(name, argc, argv, out); var_set("RESULT", out); }
        tpop(); for (i = 0; i < argc; i++) tpop();
        return;
    }
    if (!strcmp(w, "PROCEDURE")) {
        if (var_lvl >= 250) die("PROCEDURE too deep");
        var_lvl++;
        if ((r = kw(r, "EXPOSE")) != 0) {
            while (*r) { char nm[32]; uint8_t k = word_at(r, nm, 32); uint8_t ox; r = skipsp(r + k); if (!k) { r++; continue; }
                ox = var_find(nm, (uint8_t)(var_lvl - 1));
                if (!ox) { var_lvl--; var_set(nm, ""); ox = var_find(nm, var_lvl); var_lvl++; }
                { uint8_t i; for (i = 0; i < nvars && vars[i].lvl != 0xFF; i++) ; if (i == nvars) { if (nvars >= NVAR) die("too many variables"); nvars++; }
                  vars[i].lvl = var_lvl; vars[i].link = ox; vars[i].cap = 0; vars[i].val = 0; vars[i].name = vars[ox - 1].name; } }
        }
        return;
    }
    if (!strcmp(w, "PARSE")) { parse_cmd(r, 0); return; }
    if (!strcmp(w, "PULL")) { char *b = kpush(); strcpy(b, "PULL "); strcat(b, r); parse_cmd(b, 1); kpop(); return; }
    if (!strcmp(w, "ARG")) { char *b = kpush(); strcpy(b, "ARG "); strcat(b, r); parse_cmd(b, 1); kpop(); return; }
    if (!strcmp(w, "DROP")) { char *nm = tpush(), *rn = tpush(); while (*r) { uint8_t k = word_at(r, nm, VMAX); r = skipsp(r + k); if (!k) { r++; continue; } resolve(nm, rn); var_drop(rn); } tpop(); tpop(); return; }
    if (!strcmp(w, "UPPER")) { char *nm = tpush(), *rn = tpush(), *v = tpush(); while (*r) { const char *x; uint8_t k = word_at(r, nm, VMAX); r = skipsp(r + k); if (!k) { r++; continue; } resolve(nm, rn); x = var_get(rn); if (x) { strcpy(v, x); upstr(v); var_set(rn, v); } } tpop(); tpop(); tpop(); return; }
    if (!strcmp(w, "PUSH") || !strcmp(w, "QUEUE")) { char *v = tpush(); if (*r) eval(r, v); queue_put(v, w[1] == 'U'); tpop(); return; }
    if (!strcmp(w, "SIGNAL")) {
        char nm[32]; uint16_t at; uint8_t i = fsp;
        if (kw(r, "ON") || kw(r, "OFF")) return;                 /* condition traps: accepted, not built */
        { const char *u = kw(r, "VALUE");
          if (u) { char *v = tpush(); eval(u, v); setlen(nm, v, 15); tpop(); } else word_at(r, nm, 32); }
        at = label_at(nm); if (at == 0xFFFF) { outs("RX: no such label: "); outs(nm); nl(); die("SIGNAL"); }
        while (i && frames[i - 1].type != CT_CALL) i--;
        fsp = i; var_setn("SIGL", line_of(cstart)); pos = at; return;
    }
    if (!strcmp(w, "ADDRESS")) {
        char nm[20]; uint8_t k;
        if (!*r) { strcpy(nm, env); strcpy(env, env_prev); strcpy(env_prev, nm); return; }
        { const char *u = kw(r, "VALUE");                  /* NOT "r = kw(r,...)": kw returns 0 when it does not
                                                            * match, and that nulled the pointer the rest of the
                                                            * clause is read from (ADDRESS TUBE did nothing) */
          if (u) { char *v = tpush(); eval(u, v); strcpy(env_prev, env); setlen(env, v, 19); upstr(env); tpop(); return; } }
        k = word_at(r, nm, 20); r = skipsp(r + k);
        if (*r) { char *v = tpush(); char keep[20]; strcpy(keep, env); strcpy(env, nm); eval(r, v); command(v); strcpy(env, keep); tpop(); return; }
        strcpy(env_prev, env); strcpy(env, nm); return;
    }
    if (!strcmp(w, "TRACE")) { char nm[8]; word_at(r, nm, 8); trace = (nm[0] == 'A' || nm[0] == 'R' || nm[0] == 'I' || nm[0] == 'C'); return; }
    if (!strcmp(w, "NUMERIC") || !strcmp(w, "OPTIONS")) return;
    if (!strcmp(w, "INTERPRET")) die("INTERPRET is not built");
    /* a command for the environment */
    { char *v = tpush(); eval(s, v); command(v); tpop(); }
}
static void run_loop(void)
{
    uint8_t base = fsp;
    while (!ret_flag) {
        if (!read_clause()) { if (rlev) { retval[0] = 0; ret_flag = 1; } break; }
        exec_stmt(CL);
    }
    if (rlev) ret_flag = 0;                                     /* consumed by the caller (call_internal) */
    (void)base;
}

/* ---- main ---------------------------------------------------------------------- */
static uint8_t load_script(const char *name)
{
    char nm[VMAX];
    uint8_t i;
    for (i = 0; i < 3; i++) {
        if (i == 0) strcpy(nm, name);
        else if (i == 1) { strcpy(nm, name); strcat(nm, ".RX"); }
        else { if (name[0] == '/') return 0; strcpy(nm, "/RX/"); strcat(nm, name); strcat(nm, ".RX"); }
        fs_name(nm); w32(FS_ADDR, SRC_PHYS); w32(FS_LEN, 0xFFFEUL);
        if (!fs_do(C_LOAD)) { srclen = (uint16_t)r32(FS_LEN); return 1; }
    }
    return 0;
}
void main(void)
{
    unsigned char n = rom_args();
    const char *a = *(const char **)0xF0; char name[VMAX]; uint8_t k = 0, i;
    sp0 = rx_sp();                                        /* the floor the guard measures from */
    while (n && *a == ' ') { a++; n--; }
    while (*a && *a != ' ' && k < VMAX - 1) name[k++] = *a++;
    name[k] = 0;
    while (*a == ' ') a++;
    if (!k) { outs("RX name [arguments]   -- runs name, name.RX or /RX/name.RX"); nl(); return; }
    if (!load_script(name)) { outs("RX: not found: "); outs(name); nl(); return; }
    far_poke(SRC_PHYS + srclen, 0);
    rnd_seed = r32(SYS + 0x36) | 1;
    for (i = 0; i < ARGN; i++) { if (i) arg_put(0, i, ""); else arg_put(0, 0, a); }
    scan_labels();
    pos = 0; CL[0] = 0;
    switch (setjmp(top)) {
    case 0: run_loop(); break;
    case 1: break;                                         /* die() said it */
    case 2: break;                                         /* EXIT */
    }
    out_flush();
    if (exit_code) { outs("RX: exit "); outn(exit_code); nl(); }
}
