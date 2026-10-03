/* demo/vikeys.h -- VI's normal mode: counts, motions, operators and the
 * commands that are one key, shared by VI and EDIT (2026-10-02: "change
 * EDIT so that it supports VI movements and actions", off unless asked).
 * The text engine under it is ed.h's, so a motion here is the same motion
 * in both, and a fix to one is a fix to the other.
 *
 * What it does NOT do is read keys, draw, or run the : line: each editor
 * has its own of those.  vi_normal(k) takes one key in normal mode; it may
 * leave mode at 1 (an insert began -- the caller types until Esc) or 2 (the
 * : / or ? line was opened, cprompt says which -- the caller collects cmd
 * and runs it).  Before including, the editor defines:
 *
 *   vik_page      the lines a PgUp / PgDn moves (VI: the screen less its status)
 *   vik_put(a)    p and P: the register back in, after (1) or before (0)
 *   vik_key       1 if the key just read was a key code, not a character
 *                 (an accented letter shares $80-$FF with the arrows)
 *   VIK_CHARREG   defined: a charwise y, d or c fills the register (EDIT,
 *                 whose vik_put can put characters back); VI's register is
 *                 lines only, and a charwise change leaves it alone.
 *
 * #include "k4510.h" and "ed.h" first. */

static char cmd[NAMEMAX];
static uint8_t mode, pend, cmdlen;          /* mode: 0 normal, 1 insert, 2 the : line */
static unsigned cnt;                        /* the count being typed: 3dd, 5j */
static uint8_t cprompt = ':';               /* which line the : line is: : / or ? */
static uint8_t op;                          /* the operator waiting for a motion: d c y */

/* ---- motions and operators ----------------------------------------------
 * A motion is a cursor move.  An operator runs the same move and then acts on
 * what the cursor crossed, which is why dw, d$ and dG all come out of one
 * piece of code rather than three.  Charwise operators are clamped to the
 * line: crossing lines charwise is rare, and clamping is predictable. */
static unsigned sy; static uint8_t sxc;         /* where the operator started */

static uint8_t isword(uint8_t c)
{
    return (uint8_t)((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_');
}
static void mv_w(void)                          /* forward a word */
{
    if (cx >= ln[0]) { if (cy + 1 < nlines) { goline(cy + 1); cx = 0; } return; }
    if (isword(ln[cx + 1])) { while (cx < ln[0] && isword(ln[cx + 1])) cx++; }
    else while (cx < ln[0] && !isword(ln[cx + 1]) && ln[cx + 1] != ' ') cx++;
    while (cx < ln[0] && ln[cx + 1] == ' ') cx++;
    if (cx >= ln[0] && cy + 1 < nlines) { goline(cy + 1); cx = 0; }
}
static void mv_b(void)                          /* back a word */
{
    if (!cx) { if (cy) { goline(cy - 1); cx = ln[0] ? (uint8_t)(ln[0] - 1) : 0; } return; }
    cx--;
    while (cx && ln[cx + 1] == ' ') cx--;
    if (isword(ln[cx + 1])) { while (cx && isword(ln[cx])) cx--; }
    else while (cx && !isword(ln[cx]) && ln[cx] != ' ') cx--;
}
static void mv_e(void)                          /* to the end of a word */
{
    if (cx + 1 >= ln[0]) { if (cy + 1 < nlines) { goline(cy + 1); cx = 0; } else return; }
    else cx++;
    while (cx < ln[0] && ln[cx + 1] == ' ') cx++;
    if (isword(ln[cx + 1])) { while (cx + 1 < ln[0] && isword(ln[cx + 2])) cx++; }
    else while (cx + 1 < ln[0] && !isword(ln[cx + 2]) && ln[cx + 2] != ' ') cx++;
}
static void first_nb(void) { cx = 0; while (cx < ln[0] && ln[cx + 1] == ' ') cx++; }

/* 0 = not a motion, 1 = charwise exclusive, 2 = charwise inclusive, 3 = linewise */
static uint8_t do_motion(uint8_t k, unsigned n)
{
    unsigned i;
    switch (k) {
    case 'h': case 0x82: case 0x08: for (i = 0; i < n; i++) if (cx) cx--; return 1;
    case 'l': case 0x83: case ' ': for (i = 0; i < n; i++) if (cx < ln[0]) cx++; return 1;
    case 'k': case 0x80: goline(cy > n ? cy - n : 0); return 3;
    case 'j': case 0x81: goline(cy + n); return 3;
    case '-': goline(cy > n ? cy - n : 0); first_nb(); return 3;          /* the line above, at its text */
    case '+': case 0x0D: goline(cy + n); first_nb(); return 3;           /* Enter: the line below, at its text */
    case 'w': for (i = 0; i < n; i++) mv_w(); return 1;
    case 'b': for (i = 0; i < n; i++) mv_b(); return 1;
    case 'e': for (i = 0; i < n; i++) mv_e(); return 2;
    case '0': case 0x84: cx = 0; return 1;
    case '^': first_nb(); return 1;
    case '$': case 0x85: cx = ln[0] ? (uint8_t)(ln[0] - 1) : 0; return 2;
    case 'G': goline(cnt ? n - 1 : nlines - 1); cx = 0; return 3;
    case 0x86: goline(cy > (unsigned)vik_page ? cy - vik_page : 0); return 3;
    case 0x87: goline(cy + vik_page); return 3;
    case 0x15: goline(cy > (unsigned)(vik_page / 2) ? cy - vik_page / 2 : 0); return 3;   /* Ctrl-U, Ctrl-D: half a page */
    case 0x04: goline(cy + vik_page / 2); return 3;
    default: return 0;
    }
}

static void del_lines(unsigned from, unsigned n)   /* yank then remove n lines at from */
{
    unsigned i;
    if (from >= nlines) return;
    if (from + n > nlines) n = nlines - from;
    line_out(cy);
    reg_take(from, n, 1);
    /* Record highest line first: undo replays a group backwards, so pushing in
     * reverse makes it re-insert from the lowest line upwards -- which is the
     * only order in which open_at() has somewhere to put each line. */
    for (i = n; i > 0; i--) u_del(from + i - 1);
    for (i = 0; i < n; i++) close_at(from);
    if (cy >= nlines) cy = nlines - 1;
    line_in(cy);
    if (cx > ln[0]) cx = ln[0] ? (uint8_t)(ln[0] - 1) : 0;
    dirty = 1;
}
static void del_span(uint8_t a, uint8_t b)         /* remove columns [a,b) of the cursor's line */
{
    uint8_t i;
    if (b > ln[0]) b = ln[0];
    if (a >= b) return;
    u_line(cy); line_in(cy);
    for (i = a; i + (b - a) < ln[0]; i++) ln[i + 1] = ln[i + 1 + (b - a)];
    ln[0] = (uint8_t)(ln[0] - (b - a));
    cx = a; dirty = 1;
}
#ifdef VIK_CHARREG
static void take_span(uint8_t a, uint8_t b)        /* columns [a,b) of the cursor's line into the register, as characters */
{
    uint8_t i;
    if (b > ln[0]) b = ln[0];
    if (a >= b) return;
    tmp[0] = (uint8_t)(b - a);
    for (i = a; i < b; i++) tmp[1 + i - a] = ln[1 + i];
    far_put(tmp, RSLOT(0), 256);
    reglines = 1; reglinewise = 0;
}
#endif
static void apply_op(uint8_t kind)
{
    unsigned lo, hi; uint8_t a, b;
    if (kind == 3) {                                /* linewise */
        lo = sy < cy ? sy : cy; hi = sy < cy ? cy : sy;
        if (op == 'y') { line_out(cy); reg_take(lo, hi - lo + 1, 1); cy = lo; line_in(cy); note = "yanked"; return; }
        u_begin();
        if (op == 'c') { del_lines(lo, hi - lo); u_line(lo); line_in(lo); cy = lo; ln[0] = 0; cx = 0; mode = 1; return; }
        del_lines(lo, hi - lo + 1);
        u_end(); return;
    }
    if (cy != sy) { cy = sy; line_in(cy); }         /* charwise stays on one line */
    a = sxc < cx ? sxc : cx; b = sxc < cx ? cx : sxc;
    if (kind == 2) b++;
#ifdef VIK_CHARREG
    take_span(a, b);
#endif
    if (op == 'y') { cx = a; note = "yanked"; return; }
    u_begin(); del_span(a, b);
    if (op == 'c') mode = 1; else u_end();
}

/* ---- :s and J --------------------------------------------------------------
 * Here rather than in ed.h: only the editors with VI's keys use them, and in
 * PROG this file is an overlay, out of the main image. */
static void do_sub(const char *c)
{
    uint8_t d, all = 0, whole = 0; unsigned l;
    if (*c == '%') { whole = 1; c++; }
    if (*c != 's') { note = "?"; return; }
    c++;
    d = (uint8_t)*c; if (!d) { note = "usage: :s/old/new/"; return; }
    c++;
    soldl = 0; while (*c && (uint8_t)*c != d && soldl < NAMEMAX - 1) sold[soldl++] = *c++;
    if ((uint8_t)*c == d) c++;
    snewl = 0; while (*c && (uint8_t)*c != d && snewl < NAMEMAX - 1) snew[snewl++] = *c++;
    if ((uint8_t)*c == d) c++;
    while (*c) { if (*c == 'g') all = 1; c++; }
    if (!soldl) { note = "nothing to replace"; return; }
    subs = 0; line_out(cy); u_begin();
    if (whole) { for (l = 0; l < nlines; l++) sub_line(l, all); }
    else sub_line(cy, all);
    u_end(); line_in(cy);
    if (cx > ln[0]) cx = ln[0] ? (uint8_t)(ln[0] - 1) : 0;
    full = 1;
    note = subs ? "substituted" : "not found";
    patlen = 0;
}

static void do_join(unsigned n)                 /* J: pull the next line onto this one */
{
    unsigned i; uint8_t plen, j;
    u_begin();
    for (i = 0; i < n; i++) {
        if (cy + 1 >= nlines) break;
        line_out(cy);
        far_get(SLOT(cy + 1), tmp, 256);
        if ((unsigned)ln[0] + tmp[0] + 1 > 255) { note = "line would be too long"; break; }
        u_line(cy);                                 /* before anything changes: it reads the slot through tmp */
        far_get(SLOT(cy + 1), tmp, 256);
        far_get(SLOT(cy), ln, 256);
        plen = ln[0];
        if (plen && tmp[0]) { ln[plen + 1] = ' '; plen++; }
        for (j = 0; j < tmp[0]; j++) ln[plen + 1 + j] = tmp[j + 1];
        ln[0] = (uint8_t)(plen + tmp[0]);
        line_out(cy);
        u_del(cy + 1); close_at(cy + 1);
        cx = plen ? (uint8_t)(plen - 1) : 0;
    }
    u_end(); line_in(cy); dirty = 1;
}

/* ---- mappings ------------------------------------------------------------
 * :map lhs rhs  in normal mode,  :imap lhs rhs  in insert.  The classic use
 * is  :imap jk <Esc>.  The editor's key reader holds a partial match back
 * until it either completes, cannot complete, or the typist stops (a
 * second: vim's timeoutlen -- half was too short for a deliberate j, k,
 * Doc 2026-09-09), and hands keys out of qbuf first.
 * <Esc> and <CR> are spelled out; everything else is literal. */
#ifndef VIK_MAPMAX
#define VIK_MAPMAX 16
#define VIK_MAPRHS 24
#endif
#define MAPLHS 8
static uint8_t mmode[VIK_MAPMAX], mll[VIK_MAPMAX], mrl[VIK_MAPMAX], nmaps;
static uint8_t mlhs[VIK_MAPMAX][MAPLHS], mrhs[VIK_MAPMAX][VIK_MAPRHS];
static uint8_t qbuf[VIK_MAPRHS + MAPLHS], qn, qi;   /* keys waiting to be handed out */
static uint8_t pb[MAPLHS], pbn;             /* a partial match, still growing */

static void q_push(const uint8_t *b, uint8_t n)
{
    uint8_t i;
    if (qi == qn) { qi = qn = 0; }
    for (i = 0; i < n && qn < sizeof qbuf; i++) qbuf[qn++] = b[i];
}
/* 2 = one of the maps IS pb, 1 = one of them starts with pb, 0 = none */
static uint8_t map_look(uint8_t md, uint8_t *which)
{
    uint8_t i, j, pre = 0;
    for (i = 0; i < nmaps; i++) {
        if (mmode[i] != md || mll[i] < pbn) continue;
        for (j = 0; j < pbn; j++) if (mlhs[i][j] != pb[j]) break;
        if (j < pbn) continue;
        if (mll[i] == pbn) { *which = i; return 2; }
        pre = 1;
    }
    return pre;
}
/* one key k just typed: 0 = it is held (part of a mapping, or the mapping
 * has gone into qbuf), 1 = it is not part of any, pass it on (qbuf may hold
 * keys typed before it, which go first: the caller takes from qbuf). */
static uint8_t map_feed(uint8_t k)
{
    uint8_t r, w = 0;
    if (pbn < MAPLHS) pb[pbn++] = k; else { q_push(pb, pbn); pbn = 0; return 1; }
    r = map_look(mode == 1 ? 1 : 0, &w);
    if (r == 2) { q_push(mrhs[w], mrl[w]); pbn = 0; return 0; }
    if (r == 1) return 0;                    /* could still become one */
    q_push(pb, pbn); pbn = 0;                /* it cannot: hand the keys over as typed */
    return 0;
}
static void map_timeout(void) { q_push(pb, pbn); pbn = 0; }   /* the typist stopped: the partial match was keys after all */
static void do_map(const char *c, uint8_t md)
{
    uint8_t n = 0;
    if (nmaps >= VIK_MAPMAX) { note = "map table full"; return; }
    while (*c == ' ') c++;
    while (*c && *c != ' ' && n < MAPLHS) mlhs[nmaps][n++] = (uint8_t)*c++;
    mll[nmaps] = n;
    while (*c == ' ') c++;
    n = 0;
    while (*c && n < VIK_MAPRHS) {
        if (c[0] == '<' && (c[1] == 'E' || c[1] == 'e') && c[4] == '>') { mrhs[nmaps][n++] = 0x1B; c += 5; }
        else if (c[0] == '<' && (c[1] == 'C' || c[1] == 'c') && c[3] == '>') { mrhs[nmaps][n++] = 0x0D; c += 4; }
        else mrhs[nmaps][n++] = (uint8_t)*c++;
    }
    mrl[nmaps] = n;
    if (!mll[nmaps] || !n) { note = "usage: :map lhs rhs"; return; }
    mmode[nmaps] = md; nmaps++;
    note = "mapped";
}

/* ---- one key in normal mode ------------------------------------------------ */
static void vi_normal(uint8_t k)
{
    uint8_t i; unsigned n;
    if (pend == 'r') {                                 /* r: replace one character */
        pend = 0;
        if (((k >= 0x20 && k < 0x7F) || (k >= 0x80 && !vik_key)) && cx < ln[0]) { u_begin(); u_line(cy); line_in(cy); ln[cx + 1] = k; u_end(); dirty = 1; }
        cnt = 0; return;
    }
    if (pend == 'g') { pend = 0; if (k == 'g') { if (op) { sy = cy; sxc = cx; goline(cnt ? cnt - 1 : 0); apply_op(3); op = 0; } else { goline(cnt ? cnt - 1 : 0); cx = 0; } } cnt = 0; return; }

    if (k >= '1' && k <= '9') { cnt = cnt * 10 + (unsigned)(k - '0'); return; }
    if (k == '0' && cnt) { cnt = cnt * 10; return; }
    n = cnt ? cnt : 1;

    if (op) {                                          /* an operator is waiting for its motion */
        if (k == op) {                                 /* dd cc yy: n whole lines */
            sy = cy; sxc = cx;
            if (n > 1) goline(cy + n - 1);
            apply_op(3);
        } else if (k == 'g') { pend = 'g'; return; }
        else if (k == 0x1B) { }                        /* Esc: no operator after all */
        else {
            sy = cy; sxc = cx;
            if (op == 'c' && k == 'w' && cx < ln[0] && ln[cx + 1] != ' ') k = 'e';   /* cw is ce, as in vi: the word, not the space after it */
            { uint8_t kind = do_motion(k, n); if (kind) apply_op(kind); }
        }
        op = 0; cnt = 0; return;
    }

    switch (k) {
    case 'd': case 'c': case 'y': op = k; return;
    case 'g': pend = 'g'; return;
    case 'r': pend = 'r'; return;
    case 'u': u_apply(0); break;
    case 0x12: u_apply(1); break;                      /* Ctrl-R */
    case 'i': u_begin(); u_line(cy); line_in(cy); mode = 1; break;
    case 'a': if (cx < ln[0]) cx++; u_begin(); u_line(cy); line_in(cy); mode = 1; break;
    case 'I': cx = 0; u_begin(); u_line(cy); line_in(cy); mode = 1; break;
    case 'A': cx = ln[0]; u_begin(); u_line(cy); line_in(cy); mode = 1; break;
    case 'x': u_begin(); u_line(cy); line_in(cy);
#ifdef VIK_CHARREG
              take_span(cx, (uint8_t)(cx + n > 255 ? 255 : cx + n));
#endif
              for (i = 0; i < n; i++) del_ch(); if (cx && cx >= ln[0]) cx--; u_end(); break;
    case 'X': u_begin(); u_line(cy); line_in(cy); for (i = 0; i < n; i++) if (cx) { cx--; del_ch(); } u_end(); break;
    case 's': u_begin(); u_line(cy); line_in(cy); for (i = 0; i < n; i++) del_ch(); mode = 1; break;
    case 'D': u_begin(); del_span(cx, ln[0]); u_end(); break;
    case 'C': u_begin(); del_span(cx, ln[0]); mode = 1; break;
    case 'S': u_begin(); u_line(cy); line_in(cy); ln[0] = 0; cx = 0; mode = 1; dirty = 1; break;
    case 'J': do_join(n); break;
    case '~': u_begin(); u_line(cy); line_in(cy);
              for (i = 0; i < n && cx < ln[0]; i++) { uint8_t c = ln[cx + 1];
                  if (c >= 'a' && c <= 'z') ln[cx + 1] = (uint8_t)(c - 32);
                  else if (c >= 'A' && c <= 'Z') ln[cx + 1] = (uint8_t)(c + 32);
                  cx++; }
              u_end(); dirty = 1; break;
    case 'p': vik_put(1); break;
    case 'P': vik_put(0); break;
    case 'o': u_begin(); line_out(cy); u_ins(cy + 1); open_at(cy + 1); cy++; cx = 0; line_in(cy); mode = 1; dirty = 1; break;
    case 'O': u_begin(); line_out(cy); u_ins(cy); open_at(cy); cx = 0; line_in(cy); mode = 1; dirty = 1; break;
    case ':': case '/': case '?': cprompt = k; mode = 2; cmdlen = 0; cmd[0] = 0; break;
    case 'n': search(lastdir ? 1 : -1); break;
    case 'N': search(lastdir ? -1 : 1); break;
    default: do_motion(k, n); break;
    }
    cnt = 0;
}
