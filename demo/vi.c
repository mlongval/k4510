/* K4510: VI name -- a modal editor that keeps the file in far memory.
 *
 * Nothing about the text lives in the 64 KB. Every line is a 256-byte slot out
 * at $0E000000 -- a length byte and up to 255 characters -- and only the line
 * under the cursor is held down here, loaded when the cursor arrives and
 * written back when it leaves. So the file size is bounded by far memory
 * (32000 lines) rather than by the CPU's address space, and there is one code
 * path whatever the size: no small-file case to disagree with the big one.
 *
 * The DMA engine has memmove semantics, so opening or closing a line is a
 * single transfer of everything below it however long the file is, and
 * redrawing pulls each visible line straight out of far memory.
 *
 * Undo is a journal of whole line slots, also in far memory, so unlimited
 * undo costs no more here than one level would.  One thing the user did is
 * one group, and a new change truncates the journal above where we are --
 * which is what makes redo fall out for nothing.
 *
 *   counts   3dd  5j  2dw  10G          before almost anything
 *   move     h j k l arrows  w b e  0 ^ $  G gg  PgUp PgDn
 *   operate  d c y + any motion, or doubled: dd cc yy
 *   insert   i a I A  o O  s S  C
 *   change   x X r ~ J D  p P (the unnamed register)
 *   undo     u   redo Ctrl-R
 *   search   /pat  ?pat  n N          plain substrings, not patterns
 *   ex       :w :q :q! :wq :x
 *            :s/old/new/[g]  :%s/old/new/[g]
 *            :map lhs rhs    :imap lhs rhs      (:imap jk <Esc>)
 *            :renum [start [step]]   a BASIC file: its lines and GOTOs (u undoes)
 *            :make  :run     compile the .C / .PAS (CC, PAS) and go to the first error; then run it
 *            :cn :cp :cc N :cl       next, previous, Nth error; the list
 *   insert  Esc leaves; Backspace, Enter, printable
 */
#include "k4510.h"

#include "ed.h"                             /* the engine: lines, undo, files, search, :make (shared with PROG) */

static char cmd[NAMEMAX];
static uint8_t mode, running = 1, pend, cmdlen;
static unsigned cnt;                        /* the count being typed: 3dd, 5j */
static uint8_t cprompt = ':';               /* which line the : line is: : / or ? */
static uint8_t op;                          /* the operator waiting for a motion: d c y */

/* ---- drawing ------------------------------------------------------------
 * Only what changed.  Redrawing all 29 rows on every keypress meant the
 * raster was always somewhere in the middle of a half-written screen, which
 * showed as a fast flicker of wrong cells -- the cell is four bytes and the
 * beam does not wait for all four.  Typing now rewrites one line and the
 * status; a full redraw is asked for by the things that actually move text
 * about (open_at, close_at, a scroll, undo, load). */
static uint8_t hoff;
static unsigned lasttop = 0xFFFF, lastcy = 0xFFFF;
static uint8_t lasthoff = 0xFF;

static void draw_row(unsigned r)
{
    unsigned l = top + r; uint8_t c, w;
    at((uint8_t)r, 0);
    if (l < nlines) {
        if (l == cy) { w = ln[0]; for (c = 0; (unsigned)(c + hoff) < w && c < cols; c++) put(ln[1 + c + hoff]); }
        else { far_get(SLOT(l), tmp, 256); w = tmp[0]; for (c = 0; (unsigned)(c + hoff) < w && c < cols; c++) put(tmp[1 + c + hoff]); }
    } else put('~');
    eeol();
}
static void draw(void)
{
    unsigned r;
    hoff = (cx >= cols) ? (uint8_t)(cx - cols + 1) : 0;
    if (top != lasttop || hoff != lasthoff) full = 1;
    if (full) { for (r = 0; r < (unsigned)(rows - 1); r++) draw_row(r); full = 0; }
    else {
        if (lastcy != cy && lastcy >= top && lastcy < top + (unsigned)(rows - 1)) draw_row(lastcy - top);
        if (cy >= top && cy < top + (unsigned)(rows - 1)) draw_row(cy - top);
    }
    lasttop = top; lastcy = cy; lasthoff = hoff;

    /* the cursor's shape says the mode, as vim's does: a block in normal
     * mode, a bar inserting, an underline on the : line (DECSCUSR to JIM) */
    { uint8_t want = mode == 1 ? '6' : mode == 2 ? '4' : '2';
      if (want != curshape) { curshape = want; put(27); put('['); put((char)want); put(' '); put('q'); } }
    at((uint8_t)(rows - 1), 0);
    if (mode == 2) { put((char)cprompt); say(cmd); eeol(); at((uint8_t)(rows - 1), (uint8_t)(cmdlen + 1)); return; }
    sgr("7");
    clip = 1; sx = 0;                                /* text only: the escapes are not columns */
    say(" "); say(name[0] ? name : "[no name]");
    if (dirty) say(" [+]");
    say("  "); num(cy + 1); put('/'); num(nlines); say("  col "); num((unsigned long)cx + 1);
    if (mode == 1) say("   -- INSERT --");
    if (*note) { say("   "); say(note); }
    clip = 0;
    eeol();
    sgr("0");
    at((uint8_t)(cy - top), (uint8_t)(cx - hoff));
}
static void scroll_fit(void)
{
    if (cy < top) top = cy;
    while (cy >= top + (unsigned)(rows - 1)) top++;
}


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

/* 0 = not a motion, 1 = charwise exclusive, 2 = charwise inclusive, 3 = linewise */
static uint8_t do_motion(uint8_t k, unsigned n)
{
    unsigned i;
    switch (k) {
    case 'h': case 0x82: for (i = 0; i < n; i++) if (cx) cx--; return 1;
    case 'l': case 0x83: for (i = 0; i < n; i++) if (cx < ln[0]) cx++; return 1;
    case 'k': case 0x80: goline(cy > n ? cy - n : 0); return 3;
    case 'j': case 0x81: goline(cy + n); return 3;
    case 'w': for (i = 0; i < n; i++) mv_w(); return 1;
    case 'b': for (i = 0; i < n; i++) mv_b(); return 1;
    case 'e': for (i = 0; i < n; i++) mv_e(); return 2;
    case '0': case 0x84: cx = 0; return 1;
    case '^': while (cx < ln[0] && ln[cx + 1] == ' ') cx++; return 1;
    case '$': case 0x85: cx = ln[0] ? (uint8_t)(ln[0] - 1) : 0; return 2;
    case 'G': goline(cnt ? n - 1 : nlines - 1); cx = 0; return 3;
    case 0x86: goline(cy > (unsigned)(rows - 2) ? cy - (rows - 2) : 0); return 3;
    case 0x87: goline(cy + rows - 2); return 3;
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
    if (op == 'y') { cx = a; note = "yanked"; return; }
    u_begin(); del_span(a, b);
    if (op == 'c') mode = 1; else u_end();
}

/* ---- mappings ------------------------------------------------------------
 * :map lhs rhs  in normal mode,  :imap lhs rhs  in insert.  The classic use
 * is  :imap jk <Esc>.  Keys arrive through getkey(), which holds a partial
 * match back until it either completes, cannot complete, or the typist stops
 * -- the frame counter at $D50D is the second that decides the last one,
 * so a lone j still reaches the editor.
 * <Esc> and <CR> are spelled out; everything else is literal. */
#define MAPMAX  16
#define MAPLHS   8
#define MAPRHS  24
static uint8_t mmode[MAPMAX], mll[MAPMAX], mrl[MAPMAX], nmaps;
static uint8_t mlhs[MAPMAX][MAPLHS], mrhs[MAPMAX][MAPRHS];
static uint8_t qbuf[64], qn, qi;            /* keys waiting to be handed out */
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
static uint8_t vk;                              /* KBDST bit 6 for the last key read: 1 = a KEY_* code, 0 = a character sharing its byte (an é is $82 too) */
static uint8_t getkey(void)
{
    uint8_t k, r, w = 0; uint8_t t0;
    for (;;) {
        if (qi < qn) return qbuf[qi++];
        if (!pbn) { do { k = rom_getin(); } while (!k); vk = (REG(0xD101) & 0x40) ? 1 : 0; }
        else {                                   /* waiting on the rest of a mapping */
            t0 = REG(0xD50D);
            for (;;) {
                k = rom_getin();
                if (k) { vk = (REG(0xD101) & 0x40) ? 1 : 0; break; }
                if ((uint8_t)(REG(0xD50D) - t0) > 60) { q_push(pb, pbn); pbn = 0; break; }   /* a second, as vim's timeoutlen: half was too short for a deliberate j, k (Doc, 2026-09-09) */
            }
            if (!pbn) continue;
        }
        if (pbn < MAPLHS) pb[pbn++] = k; else { q_push(pb, pbn); pbn = 0; return k; }
        r = map_look(mode == 1 ? 1 : 0, &w);
        if (r == 2) { q_push(mrhs[w], mrl[w]); pbn = 0; continue; }
        if (r == 1) continue;                    /* could still become one */
        q_push(pb, pbn); pbn = 0;                /* it cannot: hand the keys over as typed */
    }
}
static void do_map(const char *c, uint8_t md)
{
    uint8_t n = 0;
    if (nmaps >= MAPMAX) { note = "map table full"; return; }
    while (*c == ' ') c++;
    while (*c && *c != ' ' && n < MAPLHS) mlhs[nmaps][n++] = (uint8_t)*c++;
    mll[nmaps] = n;
    while (*c == ' ') c++;
    n = 0;
    while (*c && n < MAPRHS) {
        if (c[0] == '<' && (c[1] == 'E' || c[1] == 'e') && c[4] == '>') { mrhs[nmaps][n++] = 0x1B; c += 5; }
        else if (c[0] == '<' && (c[1] == 'C' || c[1] == 'c') && c[3] == '>') { mrhs[nmaps][n++] = 0x0D; c += 4; }
        else mrhs[nmaps][n++] = (uint8_t)*c++;
    }
    mrl[nmaps] = n;
    if (!mll[nmaps] || !n) { note = "usage: :map lhs rhs"; return; }
    mmode[nmaps] = md; nmaps++;
    note = "mapped";
}

static void err_list(void)                           /* :cl -- the whole list, a screenful */
{
    unsigned i, l; uint8_t r = 0;
    if (!nerr) { note = info[0] ? info : "no messages"; return; }
    REG(TERM + 4) = 2;
    for (i = 0; i < nerr && r < (uint8_t)(rows - 1); i++, r++) {
        far_get(ERRTAB + ((uint32_t)i << 7), ebuf, 128);
        at(r, 0); clip = 1; sx = 0;
        if (i == ecur) sgr("7");
        num(i + 1); say(ebuf[3] == 'W' ? "  warning  " : "  error    ");
        l = (unsigned)ebuf[0] | ((unsigned)ebuf[1] << 8);
        { char w[40]; ent_where(w); say(w); }         /* "line 12: " here, "UNIT.PAS:12: " elsewhere */
        { uint8_t j; for (j = 0; j < ebuf[5]; j++) put((char)ebuf[6 + j]); }
        if (i == ecur) sgr("0");
        clip = 0; eeol();
    }
    at((uint8_t)(rows - 1), 0); sgr("7"); say(" :cc N goes to one -- a key returns "); sgr("0");
    while (!rom_getin()) ;
    screen_back();
    note = "";
}
static uint8_t excmd(const char *w)                  /* cmd is the word w (upper case here), alone or before a space */
{
    uint8_t i = 0;
    while (w[i]) { if (rn_up((uint8_t)cmd[i]) != (uint8_t)w[i]) return 0; i++; }
    return (uint8_t)(cmd[i] == 0 || cmd[i] == ' ');
}

static void do_cmd(void)
{
    uint8_t i = 0, w = 0, q = 0;
    if (cprompt != ':') {                        /* a search, not a command */
        patlen = 0; while (cmd[patlen] && patlen < NAMEMAX - 1) { pat[patlen] = cmd[patlen]; patlen++; }
        lastdir = (uint8_t)(cprompt == '/' ? 1 : 0);
        mode = 0; cmdlen = 0; cmd[0] = 0; cprompt = ':';
        if (patlen) search(lastdir ? 1 : -1);
        return;
    }
    /* :set before :s -- "set ts=2" starts with s, and was taken as a
     * substitute with e for its delimiter (caught by the Tab test) */
    if (excmd("SET")) {                              /* :set ts=N / tabstop=N -- VI.RC's way to say it too */
        const char *v = cmd + 3;
        while (*v == ' ') v++;
        if (rn_up((uint8_t)v[0]) == 'T' && rn_up((uint8_t)v[1]) == 'S' && v[2] == '=') ed_set_tabw(v + 3);
        else if (!memcmp(v, "tabstop=", 8)) ed_set_tabw(v + 8);
        nb_reset(); nb_s("ts="); nb_n(ed_tabw); note = nbuf;
        mode = 0; cmdlen = 0; cmd[0] = 0; return;
    }
    if (cmd[0] == 's' || (cmd[0] == '%' && cmd[1] == 's')) { do_sub(cmd); mode = 0; cmdlen = 0; cmd[0] = 0; return; }
    if (cmd[0] == 'm' && cmd[1] == 'a' && cmd[2] == 'p') { do_map(cmd + 3, 0); mode = 0; cmdlen = 0; cmd[0] = 0; return; }
    if (cmd[0] == 'i' && cmd[1] == 'm' && cmd[2] == 'a' && cmd[3] == 'p') { do_map(cmd + 4, 1); mode = 0; cmdlen = 0; cmd[0] = 0; return; }
    if (rn_up((uint8_t)cmd[0]) == 'R' && rn_up((uint8_t)cmd[1]) == 'E' && rn_up((uint8_t)cmd[2]) == 'N' && rn_up((uint8_t)cmd[3]) == 'U' && rn_up((uint8_t)cmd[4]) == 'M') {
        do_renum(cmd + 5); mode = 0; cmdlen = 0; cmd[0] = 0; return; }
    if (excmd("MAKE") || excmd("RUN") || excmd("CN") || excmd("CP") || excmd("CC") || excmd("CL")) {
        mode = 0;
        if (excmd("MAKE")) do_make();
        else if (excmd("RUN")) do_run();
        else if (excmd("CL")) err_list();
        else if (!nerr) note = "no messages -- :make first";
        else if (excmd("CN")) { if (ecur + 1 < nerr || ecur == 0xFFFFu) err_go(ecur + 1); else note = "no more messages"; }
        else if (excmd("CP")) { if (ecur != 0xFFFFu && ecur > 0) err_go(ecur - 1); else note = "no earlier message"; }
        else { unsigned n = 0; const char *p = cmd + 2; while (*p == ' ') p++;
               while (*p >= '0' && *p <= '9') n = n * 10 + (unsigned)(*p++ - '0');
               if (n >= 1 && n <= nerr) err_go(n - 1); else note = "no such message"; }
        cmdlen = 0; cmd[0] = 0; return;
    }
    /* The command word only, and either case: ":w quiz" used to quit (the q
     * in the NAME), and with caps lock on ":Q" did nothing at all -- a way
     * into the editor with no way out (Doc, 2026-09-12, from MS BASIC). */
    while (cmd[i] && cmd[i] != ' ') {
        char c = cmd[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c == 'w') w = 1; if (c == 'q') q = 1; if (c == 'x') { w = 1; q = 1; }
        i++;
    }
    if (w) { if (cmd[1] == ' ' && cmd[2]) { for (i = 0; cmd[i + 2] && i < NAMEMAX - 1; i++) name[i] = cmd[i + 2]; name[i] = 0; } save_file(); }
    if (q) { if (dirty && !w && cmd[i - 1] != '!') note = "unsaved -- :q! or :wq"; else running = 0; }
    mode = 0; cmdlen = 0; cmd[0] = 0;
}

/* ---- the startup file ----------------------------------------------------- */
/* /SYSTEM/ETC/VI.RC, one ex command to a line, run once the file is in: the place
 * for `imap jk <Esc>` and the other mappings, which otherwise have to be typed
 * again every session. A line beginning with " is a comment, as in vi. Having
 * no VI.RC is the ordinary case and costs one failed open. */
static const char rcname[] = "/SYSTEM/ETC/VI.RC";
static void run_rc(void)
{
    uint32_t l, off = 0; unsigned chunk, i; const char *keep = note;
    zp16(0xF0, (uint16_t)rcname); zp32(0xF2, FLAT);
    if (rom_load()) return;
    l = zpr32(0xF6);
    cmdlen = 0; cprompt = ':';
    while (off < l) {
        chunk = (l - off) > 128 ? 128 : (unsigned)(l - off);
        far_get(FLAT + off, tmp, chunk);
        for (i = 0; i < chunk; i++) {
            if (tmp[i] == '\n') {
                cmd[cmdlen] = 0;
                if (cmdlen && cmd[0] != '"') do_cmd();
                cmdlen = 0; cmd[0] = 0; cprompt = ':';
            } else if (tmp[i] != '\r' && cmdlen < NAMEMAX - 2) cmd[cmdlen++] = (char)tmp[i];
        }
        off += chunk;
    }
    cmd[cmdlen] = 0;                                       /* a last line with no newline */
    if (cmdlen && cmd[0] != '"') do_cmd();
    mode = 0; cmdlen = 0; cmd[0] = 0; cprompt = ':'; note = keep;
}

void main(void)
{
    uint8_t k, i; unsigned n; uint8_t na = rom_args(); const char *a = *(const char **)0xF0; uint8_t j = 0;
    while (na && *a == ' ') { a++; na--; }
    while (j < na && j < NAMEMAX - 1 && a[j] != ' ') { name[j] = a[j]; j++; }
    name[j] = 0;
    cols = REG(TERM + 5); rows = REG(TERM + 6);
    if (!cols) cols = 80;
    if (!rows) rows = 30;
    load_file();
    run_rc();                                              /* after the file: a mapping applies to a real buffer */
    REG(TERM + 4) = 2; REG(TERM + 0x0E) = 1;
    while (running) {
        scroll_fit();
        draw();
        k = getkey();
        if (mode == 2) {                                   /* the : line */
            if (k == 0x0D) do_cmd();
            else if (k == 0x1B) { mode = 0; cmdlen = 0; cmd[0] = 0; cprompt = ':'; }
            else if (k == 0x08) { if (cmdlen) cmd[--cmdlen] = 0; else mode = 0; }
            else if (k >= 0x20 && k < 0x7F && cmdlen < NAMEMAX - 2) { cmd[cmdlen++] = (char)k; cmd[cmdlen] = 0; }
            continue;
        }
        note = "";
        if (mode == 1) {                                   /* insert */
            if (k == 0x1B) { mode = 0; if (cx) cx--; u_end(); }   /* the whole insertion is one undo */
            else if (k == 0x0D) { u_ins(cy + 1); split(); }
            else if (k == 0x08) { if (cx) { cx--; del_ch(); } else { if (cy) u_del(cy); join_prev(); } }
            else if (k >= 0x80 && !vk) ins_ch(k);          /* an accented letter, not a key: KBDST bit 6 tells them apart */
            else if (k == 0x89) del_ch();
            else if (k == 0x82) { if (cx) cx--; }
            else if (k == 0x83) { if (cx < ln[0]) cx++; }
            else if (k == 0x80) goline(cy ? cy - 1 : 0);
            else if (k == 0x81) goline(cy + 1);
            else if (k == 0x09) ed_tab();                  /* spaces to the next stop: :set ts=N */
            else if (k >= 0x20 && k < 0x7F) ins_ch(k);
            continue;
        }
        if (mode == 0 && u_open && k != 0x1B) { }        /* groups close on Esc, or when the next one opens */
        if (pend == 'r') {                                 /* r: replace one character */
            pend = 0;
            if (((k >= 0x20 && k < 0x7F) || (k >= 0x80 && !vk)) && cx < ln[0]) { u_begin(); u_line(cy); line_in(cy); ln[cx + 1] = k; u_end(); dirty = 1; }
            cnt = 0; continue;
        }
        if (pend == 'g') { pend = 0; if (k == 'g') { if (op) { sy = cy; sxc = cx; goline(cnt ? cnt - 1 : 0); apply_op(3); op = 0; } else { goline(cnt ? cnt - 1 : 0); cx = 0; } } cnt = 0; continue; }

        if (k >= '1' && k <= '9') { cnt = cnt * 10 + (unsigned)(k - '0'); continue; }
        if (k == '0' && cnt) { cnt = cnt * 10; continue; }
        n = cnt ? cnt : 1;

        if (op) {                                          /* an operator is waiting for its motion */
            if (k == op) {                                 /* dd cc yy: n whole lines */
                sy = cy; sxc = cx;
                if (n > 1) goline(cy + n - 1);
                apply_op(3);
            } else if (k == 'g') { pend = 'g'; continue; }
            else {
                sy = cy; sxc = cx;
                { uint8_t kind = do_motion(k, n); if (kind) apply_op(kind); }
            }
            op = 0; cnt = 0; continue;
        }

        switch (k) {
        case 'd': case 'c': case 'y': op = k; continue;
        case 'g': pend = 'g'; continue;
        case 'r': pend = 'r'; continue;
        case 'u': u_apply(0); break;
        case 0x12: u_apply(1); break;                      /* Ctrl-R */
        case 'i': u_begin(); u_line(cy); line_in(cy); mode = 1; break;
        case 'a': if (cx < ln[0]) cx++; u_begin(); u_line(cy); line_in(cy); mode = 1; break;
        case 'I': cx = 0; u_begin(); u_line(cy); line_in(cy); mode = 1; break;
        case 'A': cx = ln[0]; u_begin(); u_line(cy); line_in(cy); mode = 1; break;
        case 'x': u_begin(); u_line(cy); line_in(cy); for (i = 0; i < n; i++) del_ch(); if (cx && cx >= ln[0]) cx--; u_end(); break;
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
        case 'p': do_put(1); break;
        case 'P': do_put(0); break;
        case 'o': u_begin(); line_out(cy); u_ins(cy + 1); open_at(cy + 1); cy++; cx = 0; line_in(cy); mode = 1; dirty = 1; break;
        case 'O': u_begin(); line_out(cy); u_ins(cy); open_at(cy); cx = 0; line_in(cy); mode = 1; dirty = 1; break;
        case ':': case '/': case '?': cprompt = k; mode = 2; cmdlen = 0; cmd[0] = 0; break;
        case 'n': search(lastdir ? 1 : -1); break;
        case 'N': search(lastdir ? -1 : 1); break;
        default: do_motion(k, n); break;
        }
        cnt = 0;
    }
    put(27); put('['); put('2'); put(' '); put('q');           /* the block back for the shell */
    REG(TERM + 0x0E) = 0; REG(TERM + 4) = 2;
    rom_video();
}
