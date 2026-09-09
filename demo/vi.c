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
 *   insert  Esc leaves; Backspace, Enter, printable
 */
#include "k4510.h"

#define TERM   0xDA00u
#define SLOTS  0x0E000000UL                 /* one 256-byte slot per line */
#define FLAT   0x0E800000UL                 /* the file, flat, for LOAD and SAVE */
#define SLOT(n) (SLOTS + ((uint32_t)(n) << 8))
#define REGS   0x0E900000UL                 /* the unnamed register: whole lines, same slot shape */
#define RSLOT(n) (REGS + ((uint32_t)(n) << 8))
#define UNDO   0x0F000000UL                 /* the journal: 512 bytes an entry, header + one slot */
#define USLOT(n) (UNDO + ((uint32_t)(n) << 9))
#define UNDOMAX 3000u
#define MAXLINES 32000u
#define REGMAX  2000u
#define NAMEMAX 64

void __fastcall__ rom_chrout(unsigned char c);
unsigned char rom_getin(void);
static unsigned char rom_args(void) { return ((unsigned char (*)(void))0xFF95)(); }
static unsigned char rom_load(void) { return ((unsigned char (*)(void))0xFF89)(); }
static unsigned char rom_save(void) { return ((unsigned char (*)(void))0xFF8C)(); }
static void rom_video(void) { ((void (*)(void))0xFF92)(); }
static void zp16(uint8_t a, uint16_t v) { REG(a) = v; REG(a + 1) = v >> 8; }
static void zp32(uint8_t a, uint32_t v) { REG(a)=v; REG(a+1)=v>>8; REG(a+2)=v>>16; REG(a+3)=v>>24; }
static uint32_t zpr32(uint8_t a) { return (uint32_t)REG(a) | ((uint32_t)REG(a+1)<<8) | ((uint32_t)REG(a+2)<<16) | ((uint32_t)REG(a+3)<<24); }
static void far_get(uint32_t p, void *d, unsigned n) { dma_copy(p, (uint32_t)(uint16_t)d, n); }
static void far_put(const void *s, uint32_t p, unsigned n) { dma_copy((uint32_t)(uint16_t)s, p, n); }

static uint8_t ln[256], tmp[256];           /* the cursor's line; a line being drawn */
static char name[NAMEMAX], cmd[NAMEMAX];
static unsigned nlines = 1, cy, top;
static uint8_t cx, cols, rows, mode, dirty, running = 1, pend, cmdlen;
static const char *note = "";
static unsigned cnt;                        /* the count being typed: 3dd, 5j */
static char pat[NAMEMAX]; static uint8_t patlen, lastdir = 1;
static uint8_t cprompt = ':';               /* which line the : line is: : / or ? */
static uint8_t op;                          /* the operator waiting for a motion: d c y */
static unsigned reglines;                   /* what the register holds */
static uint8_t reglinewise;

/* ---- screen ------------------------------------------------------------- */
static uint8_t clip, sx;         /* while clip is set, drop anything past the right edge:
                                    on the last row a wrap scrolls the whole screen */
static void put(char c) { if (clip) { if (sx >= cols) return; sx++; } REG(TERM) = (uint8_t)c; }
static void say(const char *s) { while (*s) put(*s++); }
static void num(unsigned long v) { char b[8]; uint8_t i = 0; if (!v) { put('0'); return; } while (v) { b[i++] = (char)('0' + v % 10); v /= 10; } while (i) put(b[--i]); }
static void at(uint8_t r, uint8_t c) { put(27); put('['); num((unsigned long)r + 1); put(';'); num((unsigned long)c + 1); put('H'); }
static void eeol(void) { put(27); put('['); put('K'); }
static void sgr(const char *s) { put(27); put('['); say(s); put('m'); }

/* ---- lines -------------------------------------------------------------- */
static void line_in(unsigned n)  { far_get(SLOT(n), ln, 256); }
static void line_out(unsigned n) { far_put(ln, SLOT(n), 256); }
static uint8_t zero;             /* its own byte: tmp belongs to whoever is mid-edit */
static void blank(unsigned n)    { zero = 0; far_put(&zero, SLOT(n), 1); }

static uint8_t full = 1;                    /* a full redraw is due */
static void open_at(unsigned n)             /* make room for a new line at n */
{
    if (nlines >= MAXLINES) return;
    full = 1;
    if (n < nlines) dma_copy(SLOT(n), SLOT(n + 1), (uint32_t)(nlines - n) << 8);
    nlines++;
    blank(n);
}
static void close_at(unsigned n)
{
    full = 1;
    if (nlines <= 1) { ln[0] = 0; line_out(0); return; }
    if (n + 1 < nlines) dma_copy(SLOT(n + 1), SLOT(n), (uint32_t)(nlines - n - 1) << 8);
    nlines--;
}
/* ---- the register ------------------------------------------------------- */
static void reg_take(unsigned from, unsigned n, uint8_t linewise)
{
    if (!n) return;
    if (n > REGMAX) n = REGMAX;
    dma_copy(SLOT(from), RSLOT(0), (uint32_t)n << 8);
    reglines = n; reglinewise = linewise;
}

/* ---- undo ---------------------------------------------------------------
 * A journal of whole line slots, out in far memory where space is not the
 * problem it would be on a 64 KB machine: unlimited undo is no harder here
 * than one level.  Entries carry a group number, so one thing the user did
 * -- a whole insertion, a 5dd -- undoes in one go.  ujp is where we are in
 * the journal; a new change truncates everything above it, which is what
 * makes redo fall out for free. */
static unsigned ujp, ujn;
static uint8_t useq, u_open;
static uint8_t uhdr[8];

static void u_begin(void) { if (!u_open) { u_open = 1; useq++; ujn = ujp; } }
static void u_end(void)   { u_open = 0; }

static void u_push(uint8_t o, unsigned line, const uint8_t *data)
{
    if (ujp >= UNDOMAX) return;                  /* the journal is full: older history stands */
    uhdr[0] = o; uhdr[1] = useq;
    uhdr[2] = (uint8_t)line; uhdr[3] = (uint8_t)(line >> 8);
    uhdr[4] = (uint8_t)cy;   uhdr[5] = (uint8_t)(cy >> 8);
    uhdr[6] = cx;            uhdr[7] = 0;
    far_put(uhdr, USLOT(ujp), 8);
    if (data) far_put(data, USLOT(ujp) + 8, 256);
    ujp++; ujn = ujp;
}
static void u_line(unsigned n)                   /* about to change line n */
{
    if (n >= nlines) return;
    if (n == cy) { line_out(cy); }
    far_get(SLOT(n), tmp, 256);
    u_push(1, n, tmp);
}
static void u_ins(unsigned n) { u_push(2, n, 0); }              /* line n is being added */
static void u_del(unsigned n) { far_get(SLOT(n), tmp, 256); u_push(3, n, tmp); }

static void goline(unsigned n)
{
    if (n >= nlines) n = nlines - 1;
    if (n == cy) return;
    line_out(cy);
    cy = n; line_in(cy);
    if (cx > ln[0]) cx = ln[0];
}

/* Walk the journal back one group, applying the inverse of each entry in
 * reverse order.  dir 0 undoes, dir 1 redoes. */
static void u_apply(uint8_t dir)
{
    uint8_t o, want; unsigned l;
    if (!dir && !ujp)       { note = "nothing to undo"; return; }
    if (dir && ujp >= ujn)  { note = "nothing to redo"; return; }
    line_out(cy);
    far_get(USLOT(dir ? ujp : ujp - 1), uhdr, 8);
    want = uhdr[1];
    for (;;) {
        if (!dir) { if (!ujp) break; far_get(USLOT(ujp - 1), uhdr, 8); }
        else      { if (ujp >= ujn) break; far_get(USLOT(ujp), uhdr, 8); }
        if (uhdr[1] != want) break;
        o = uhdr[0]; l = (unsigned)uhdr[2] | ((unsigned)uhdr[3] << 8);
        cy = (unsigned)uhdr[4] | ((unsigned)uhdr[5] << 8); cx = uhdr[6];
        if (!dir) {
            if (o == 1) { far_get(USLOT(ujp - 1) + 8, tmp, 256); far_get(SLOT(l), ln, 256);
                          far_put(ln, USLOT(ujp - 1) + 8, 256);          /* swap: redo needs the other side */
                          far_put(tmp, SLOT(l), 256); }
            else if (o == 2) { if (l < nlines) { close_at(l); } }        /* it was added: take it away */
            else             { open_at(l); far_get(USLOT(ujp - 1) + 8, tmp, 256); far_put(tmp, SLOT(l), 256); }
            ujp--;
        } else {
            if (o == 1) { far_get(USLOT(ujp) + 8, tmp, 256); far_get(SLOT(l), ln, 256);
                          far_put(ln, USLOT(ujp) + 8, 256);
                          far_put(tmp, SLOT(l), 256); }
            else if (o == 2) { open_at(l); }
            else             { if (l < nlines) close_at(l); }
            ujp++;
        }
    }
    if (cy >= nlines) cy = nlines - 1;
    line_in(cy);
    if (cx > ln[0]) cx = ln[0] ? (uint8_t)(ln[0] - 1) : 0;
    dirty = 1;
    full = 1;
    note = dir ? "redone" : "undone";
}

/* ---- files -------------------------------------------------------------- */
static void load_file(void)
{
    uint32_t l, off = 0; unsigned n = 0, i, chunk;
    zp16(0xF0, (uint16_t)name); zp32(0xF2, FLAT);
    if (!name[0] || rom_load()) { nlines = 1; ln[0] = 0; line_out(0); note = "new file"; return; }
    l = zpr32(0xF6);
    ln[0] = 0;
    while (off < l && n < MAXLINES) {
        chunk = (l - off) > 128 ? 128 : (unsigned)(l - off);
        far_get(FLAT + off, tmp, chunk);
        for (i = 0; i < chunk; i++) {
            if (tmp[i] == '\n') { line_out(n); n++; ln[0] = 0; if (n >= MAXLINES) break; }
            else if (tmp[i] != '\r' && ln[0] < 255) { ln[0]++; ln[ln[0]] = tmp[i]; }
        }
        off += chunk;
    }
    if (ln[0] && n < MAXLINES) { line_out(n); n++; }
    nlines = n ? n : 1;
    if (!n) { ln[0] = 0; line_out(0); }
    full = 1;
    cy = 0; line_in(0);
}
static void save_file(void)
{
    uint32_t off = 0; unsigned n; uint8_t l8;
    line_out(cy);
    for (n = 0; n < nlines; n++) {
        far_get(SLOT(n), &l8, 1);
        if (l8) dma_copy(SLOT(n) + 1, FLAT + off, l8);
        off += l8;
        dma_fill('\n', FLAT + off, 1); off++;
    }
    zp16(0xF0, (uint16_t)name); zp32(0xF2, FLAT); zp32(0xF6, off);
    note = rom_save() ? "not saved" : "written";
    if (note[0] == 'w') dirty = 0;
    line_in(cy);
}

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

/* ---- editing ------------------------------------------------------------ */
static void ins_ch(uint8_t c)
{
    uint8_t i;
    if (ln[0] >= 255) return;
    for (i = ln[0]; i > cx; i--) ln[i + 1] = ln[i];
    ln[cx + 1] = c; ln[0]++; cx++; dirty = 1;
}
static void del_ch(void)
{
    uint8_t i;
    if (cx >= ln[0]) return;
    for (i = (uint8_t)(cx + 1); i < ln[0]; i++) ln[i] = ln[i + 1];
    ln[0]--; dirty = 1;
}
static void split(void)                       /* Enter in insert mode */
{
    uint8_t i, rest = (uint8_t)(ln[0] - cx);
    for (i = 0; i < rest; i++) tmp[i + 1] = ln[cx + 1 + i];
    tmp[0] = rest;
    ln[0] = cx; line_out(cy);
    open_at(cy + 1);
    far_put(tmp, SLOT(cy + 1), 256);
    cy++; cx = 0; line_in(cy); dirty = 1;
}
/* Backspace at column 0: pull this line onto the end of the one above and
 * close the gap. Refused rather than truncated if the result would not fit
 * in a 255-character slot. */
static void join_prev(void)
{
    uint8_t plen, i;
    if (!cy) return;
    far_get(SLOT(cy - 1), tmp, 256);
    plen = tmp[0];
    if ((unsigned)plen + ln[0] > 255) { note = "line would be too long"; return; }
    for (i = 0; i < ln[0]; i++) tmp[plen + 1 + i] = ln[i + 1];
    tmp[0] = (uint8_t)(plen + ln[0]);
    far_put(tmp, SLOT(cy - 1), 256);
    close_at(cy);
    cy--; cx = plen; line_in(cy); dirty = 1;
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
 * -- the frame counter at $D50D is the half-second that decides the last one,
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
static uint8_t getkey(void)
{
    uint8_t k, r, w = 0; uint8_t t0;
    for (;;) {
        if (qi < qn) return qbuf[qi++];
        if (!pbn) { do { k = rom_getin(); } while (!k); }
        else {                                   /* waiting on the rest of a mapping */
            t0 = REG(0xD50D);
            for (;;) {
                k = rom_getin();
                if (k) break;
                if ((uint8_t)(REG(0xD50D) - t0) > 30) { q_push(pb, pbn); pbn = 0; break; }
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

/* ---- search --------------------------------------------------------------
 * Plain substrings, not patterns.  A regex engine is a lot of code and this
 * covers what you actually do to a BASIC or Pascal file; the whole file
 * streams out of far memory, so length is not the issue it would be. */
static uint8_t in_line(const uint8_t *l, uint8_t from, uint8_t *col)
{
    uint8_t i, j;
    if (!patlen || patlen > l[0]) return 0;
    for (i = from; i + patlen <= l[0]; i++) {
        for (j = 0; j < patlen; j++) if (l[i + 1 + j] != (uint8_t)pat[j]) break;
        if (j == patlen) { *col = i; return 1; }
    }
    return 0;
}
static void search(int8_t dir)
{
    unsigned l, tried; uint8_t col;
    if (!patlen) { note = "no pattern"; return; }
    line_out(cy);
    l = cy;
    for (tried = 0; tried <= nlines; tried++) {
        if (dir > 0) { if (!tried) { far_get(SLOT(l), tmp, 256);
                                     if (in_line(tmp, (uint8_t)(cx + 1), &col)) { goline(l); cx = col; return; } }
                       l = (l + 1 >= nlines) ? 0 : l + 1; }
        else         { l = l ? l - 1 : nlines - 1; }
        far_get(SLOT(l), tmp, 256);
        if (in_line(tmp, 0, &col)) { goline(l); cx = col; if (l == cy) note = "wrapped"; return; }
    }
    note = "not found";
}

/* ---- substitute ----------------------------------------------------------
 * :s/old/new/ on this line, :%s/old/new/g on every line.  Any character may
 * be the delimiter, as in vi. */
static char sold[NAMEMAX], snew[NAMEMAX];
static uint8_t soldl, snewl;
static unsigned subs;
static void sub_line(unsigned l, uint8_t all)
{
    uint8_t col = 0, i, hit = 0;
    far_get(SLOT(l), tmp, 256);
    for (;;) {
        patlen = soldl; for (i = 0; i < soldl; i++) pat[i] = sold[i];
        if (!in_line(tmp, col, &col)) break;
        if ((unsigned)tmp[0] - soldl + snewl > 255) break;
        if (!hit) { u_push(1, l, tmp); hit = 1; }
        if (snewl > soldl) { for (i = tmp[0]; i > col; i--) tmp[i + snewl - soldl] = tmp[i]; }
        else if (snewl < soldl) { for (i = (uint8_t)(col + soldl); i < tmp[0]; i++) tmp[i + 1 - soldl + snewl] = tmp[i + 1]; }
        for (i = 0; i < snewl; i++) tmp[col + 1 + i] = (uint8_t)snew[i];
        tmp[0] = (uint8_t)(tmp[0] - soldl + snewl);
        col = (uint8_t)(col + snewl);
        subs++;
        if (!all) break;
    }
    if (hit) { far_put(tmp, SLOT(l), 256); dirty = 1; }
}
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

static void do_put(uint8_t after)               /* p / P: the register's lines back in */
{
    unsigned at, i;
    if (!reglines) { note = "nothing to put"; return; }
    u_begin();
    line_out(cy);
    at = after ? cy + 1 : cy;
    for (i = 0; i < reglines; i++) { u_ins(at + i); open_at(at + i); dma_copy(RSLOT(i), SLOT(at + i), 256); }
    u_end();
    full = 1;
    cy = at; line_in(cy); cx = 0; dirty = 1;
}
static void do_join(unsigned n)                 /* J: pull the next line onto this one */
{
    unsigned i; uint8_t plen, j;
    u_begin();
    for (i = 0; i < n; i++) {
        if (cy + 1 >= nlines) break;
        line_out(cy);
        far_get(SLOT(cy + 1), tmp, 256);
        far_get(SLOT(cy), ln, 256);
        plen = ln[0];
        if ((unsigned)plen + tmp[0] + 1 > 255) { note = "line would be too long"; break; }
        if (plen && tmp[0]) { ln[plen + 1] = ' '; plen++; }
        for (j = 0; j < tmp[0]; j++) ln[plen + 1 + j] = tmp[j + 1];
        u_line(cy);
        ln[0] = (uint8_t)(plen + tmp[0]);
        line_out(cy);
        u_del(cy + 1); close_at(cy + 1);
        cx = plen ? (uint8_t)(plen - 1) : 0;
    }
    u_end(); line_in(cy); dirty = 1;
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
    if (cmd[0] == 's' || (cmd[0] == '%' && cmd[1] == 's')) { do_sub(cmd); mode = 0; cmdlen = 0; cmd[0] = 0; return; }
    if (cmd[0] == 'm' && cmd[1] == 'a' && cmd[2] == 'p') { do_map(cmd + 3, 0); mode = 0; cmdlen = 0; cmd[0] = 0; return; }
    if (cmd[0] == 'i' && cmd[1] == 'm' && cmd[2] == 'a' && cmd[3] == 'p') { do_map(cmd + 4, 1); mode = 0; cmdlen = 0; cmd[0] = 0; return; }
    while (cmd[i]) { if (cmd[i] == 'w') w = 1; if (cmd[i] == 'q') q = 1; if (cmd[i] == 'x') { w = 1; q = 1; } i++; }
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
            else if (k == 0x89) del_ch();
            else if (k == 0x82) { if (cx) cx--; }
            else if (k == 0x83) { if (cx < ln[0]) cx++; }
            else if (k == 0x80) goline(cy ? cy - 1 : 0);
            else if (k == 0x81) goline(cy + 1);
            else if (k >= 0x20 && k < 0x7F) ins_ch(k);
            continue;
        }
        if (mode == 0 && u_open && k != 0x1B) { }        /* groups close on Esc, or when the next one opens */
        if (pend == 'r') {                                 /* r: replace one character */
            pend = 0;
            if (k >= 0x20 && k < 0x7F && cx < ln[0]) { u_begin(); u_line(cy); line_in(cy); ln[cx + 1] = k; u_end(); dirty = 1; }
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
    REG(TERM + 0x0E) = 0; REG(TERM + 4) = 2;
    rom_video();
}
