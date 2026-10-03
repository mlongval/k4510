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
 *   move     h j k l arrows  w b e  0 ^ $  G gg  PgUp PgDn  Ctrl-D Ctrl-U  Enter + -
 *   operate  d c y + any motion, or doubled: dd cc yy     (all of normal mode is demo/vikeys.h, EDIT -v's too)
 *   insert   i a I A  o O  s S  C
 *   change   x X r ~ J D  p P (the unnamed register)
 *   undo     u   redo Ctrl-R
 *   search   /pat  ?pat  n N          plain substrings, not patterns
 *   ex       :w :q :q! :wq :x
 *            :s/old/new/[g]  :%s/old/new/[g]
 *            :map lhs rhs    :imap lhs rhs      (:imap jk <Esc>)
 *            :set wrap  :set nowrap  :set wrap!   long lines folded, or scrolled sideways
 *                            (folded: gj gk move by a screen row, j k by a line, as vim)
 *            :renum [start [step]]   a BASIC file: its lines and GOTOs (u undoes)
 *            :make  :run     compile the .C / .PAS (CC, PAS) and go to the first error; then run it
 *            :cn :cp :cc N :cl       next, previous, Nth error; the list
 *   insert  Esc leaves; Backspace, Enter, printable
 */
#include "k4510.h"

#include "ed.h"                             /* the engine: lines, undo, files, search, :make (shared with PROG) */

static uint8_t vk;                          /* KBDST bit 6 for the last key read: 1 = a KEY_* code, 0 = a character sharing its byte (an é is $82 too) */
#define vik_key  vk
#define vik_page (rows - 2)
#define vik_put  do_put
#include "vikeys.h"                         /* normal mode: counts, motions, operators (shared with EDIT) */

static uint8_t running = 1;

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
    unsigned l = top + r; uint8_t c = 0, w;
    at((uint8_t)r, 0);
    if (l < nlines) {
        if (l == cy) { w = ln[0]; for (c = 0; (unsigned)(c + hoff) < w && c < cols; c++) put(ln[1 + c + hoff]); }
        else { far_get(SLOT(l), tmp, 256); w = tmp[0]; for (c = 0; (unsigned)(c + hoff) < w && c < cols; c++) put(tmp[1 + c + hoff]); }
    } else { put('~'); c = 1; }
    if (c < cols) eeol();                        /* a row full to its last cell: JIM's cursor is still ON that cell (the VT100's
                                                  * pending wrap), and an erase from there took the 80th character with it */
}
/* ---- :set wrap -------------------------------------------------------------
 * Doc, 2026-09-18: "definable line wrap like nvim".  Folded, a line is as many
 * screen rows as it needs and `top' is still a file line, so the screen always
 * starts at the head of one; a row is found by adding up the heights above it,
 * a length byte each from far memory and never more than a screenful of them.
 * The cursor's own line counts the cell the cursor is in: appending at the end
 * of a row that is exactly full puts the cursor on a row of its own, as vim.
 * What is drawn a line at a time stays so -- only a line whose HEIGHT changed
 * moves everything under it, and that asks for the full redraw. */
static uint8_t wrap = 1, lasth;

static uint8_t rows_for(unsigned n) { return n ? (uint8_t)((n - 1) / cols + 1) : 1; }
static uint8_t height(unsigned l)
{
    uint8_t w; unsigned n;
    if (l == cy) { n = ln[0]; if ((unsigned)cx + 1 > n) n = (unsigned)cx + 1; return rows_for(n); }
    far_get(SLOT(l), &w, 1);
    return rows_for(w);
}
static unsigned row_of(unsigned l)               /* the screen row line l starts on; l is at or under top */
{
    unsigned i, r = 0;
    for (i = top; i < l && r < rows; i++) r += height(i);
    return r;
}
static void draw_line(unsigned l, unsigned r)    /* every row of line l, from screen row r down */
{
    const uint8_t *b; unsigned w, c = 0; uint8_t h = height(l), x;
    if (l == cy) b = ln; else { far_get(SLOT(l), tmp, 256); b = tmp; }
    w = b[0];
    for (; h && r < (unsigned)(rows - 1); h--, r++) {
        at((uint8_t)r, 0);
        for (x = 0; x < cols && c < w; x++, c++) put((char)b[1 + c]);
        if (x < cols) eeol();
    }
}
static void draw_wrapped(void)
{
    unsigned r, l; uint8_t hc = height(cy);
    if (top != lasttop || lasthoff) full = 1;
    if (!full) {
        if (lastcy == cy) { if (hc != lasth) full = 1; }
        else if (hc != rows_for(ln[0]) || (lastcy < nlines && lastcy >= top && height(lastcy) != lasth)) full = 1;
    }
    if (full) {
        for (r = 0, l = top; r < (unsigned)(rows - 1); l++) {
            if (l < nlines) { draw_line(l, r); r += height(l); }
            else { at((uint8_t)r, 0); put('~'); eeol(); r++; }
        }
        full = 0;
    } else {
        if (lastcy != cy && lastcy >= top && lastcy < nlines && (r = row_of(lastcy)) < (unsigned)(rows - 1)) draw_line(lastcy, r);
        draw_line(cy, row_of(cy));
    }
    lasth = hc;
}

static void draw(void)
{
    unsigned r;
    hoff = (!wrap && cx >= cols) ? (uint8_t)(cx - cols + 1) : 0;
    if (wrap) draw_wrapped();
    else {
        if (top != lasttop || hoff != lasthoff) full = 1;
        if (full) { for (r = 0; r < (unsigned)(rows - 1); r++) draw_row(r); full = 0; }
        else {
            if (lastcy != cy && lastcy >= top && lastcy < top + (unsigned)(rows - 1)) draw_row(lastcy - top);
            if (cy >= top && cy < top + (unsigned)(rows - 1)) draw_row(cy - top);
        }
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
    if (wrap) at((uint8_t)(row_of(cy) + cx / cols), (uint8_t)(cx % cols));
    else at((uint8_t)(cy - top), (uint8_t)(cx - hoff));
}
static void scroll_fit(void)
{
    if (cy < top) top = cy;
    while (cy >= top + (unsigned)(rows - 1)) top++;
    if (wrap) while (top < cy && row_of(cy) + height(cy) > (unsigned)(rows - 1)) top++;   /* the whole of the cursor's line */
}


static uint8_t getkey(void)                      /* a key, through the maps (vikeys.h) */
{
    uint8_t k, t0;
    for (;;) {
        if (qi < qn) return qbuf[qi++];
        if (!pbn) { do { k = rom_getin(); } while (!k); vk = (REG(0xD101) & 0x40) ? 1 : 0; }
        else {                                   /* waiting on the rest of a mapping */
            t0 = REG(0xD50D);
            for (;;) {
                k = rom_getin();
                if (k) { vk = (REG(0xD101) & 0x40) ? 1 : 0; break; }
                if ((uint8_t)(REG(0xD50D) - t0) > 60) { map_timeout(); break; }   /* a second, as vim's timeoutlen */
            }
            if (!pbn) continue;
        }
        if (map_feed(k)) return k;
    }
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
        else if (!memcmp(v, "wrap!", 5) || !memcmp(v, "invwrap", 7)) { wrap = (uint8_t)!wrap; full = 1; }
        else if (!memcmp(v, "nowrap", 6)) { wrap = 0; full = 1; }
        else if (!memcmp(v, "wrap", 4)) { wrap = 1; full = 1; }
        nb_reset(); nb_s("ts="); nb_n(ed_tabw); nb_s(wrap ? "  wrap" : "  nowrap"); note = nbuf;
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
    uint8_t k; unsigned n; uint8_t na = rom_args(); const char *a = *(const char **)0xF0; uint8_t j = 0;
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
        if (pend == 'g' && !op && wrap && (k == 'j' || k == 'k' || k == 0x80 || k == 0x81)) {   /* gj gk: a screen row, not a line */
            uint8_t x = (uint8_t)(cx % cols); unsigned t;
            pend = 0;
            for (n = cnt ? cnt : 1; n; n--) {
                if (k == 'j' || k == 0x81) {
                    if ((unsigned)(cx - x) + cols < ln[0]) t = (unsigned)(cx - x) + cols + x;
                    else if (cy + 1 < nlines) { goline(cy + 1); t = x; }
                    else break;
                } else {
                    if (cx >= cols) t = (unsigned)cx - cols;
                    else if (cy) { goline(cy - 1); t = (ln[0] ? (unsigned)(ln[0] - 1) / cols * cols : 0) + x; }
                    else break;
                }
                cx = (uint8_t)(ln[0] ? (t < ln[0] ? t : (unsigned)(ln[0] - 1)) : 0);
            }
            cnt = 0; continue;
        }
        vi_normal(k);
    }
    put(27); put('['); put('2'); put(' '); put('q');           /* the block back for the shell */
    REG(TERM + 0x0E) = 0; REG(TERM + 4) = 2;
    rom_video();
}
