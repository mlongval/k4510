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
