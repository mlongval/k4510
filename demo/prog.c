/* K4510: PROG [name] -- the programmer's front end: edit, compile, run.
 *
 * Doc, 2026-09-14: "a small ide/front end that can cover both CC and PAS
 * edit/compile cycle - something like turbo pascal but more modern ...
 * (not limited to just one file at a time, if possible)".  The keys are
 * the modern ones (Doc: "modern"), Turbo's function keys kept; the text
 * engine is VI's (demo/ed.h), so the two editors cannot drift.
 *
 *   row 0             the menu bar            F10, then arrows / Enter / Esc
 *   row 1             the open files, the current one lit, * when changed
 *   rows 2 .. eh+1    the text
 *   the separator     the file's full name, the message count
 *   MSGH rows         the messages: the compiler's, or what find in files found
 *   the last row      line:column, INS/OVR, the keys -- or what just happened
 *
 * Every key and every menu entry is a command number, run by one switch
 * (run_cmd), so a menu can never do something its key does not.
 *
 * Up to eight files (ed.h's buffers), each with 8 MB of far memory of its
 * own at $04000000 + n * 8 MB: its lines in the first 4 MB (16384 of them),
 * its undo from +6 MB.  $04000000-$07FFFFFF is used by nothing else on the
 * machine (checked 2026-09-14: BOOK is at $0C, SPLIT $0D, VI $0E-$0F).
 * Stage 2 of four (BUILD-LOG 2026-09-14); projects (PROJECT.K4P), the mouse
 * and selection come after. */
#include "k4510.h"
#include "ed.h"

#define KSTAT   0xD101u                 /* bit0 shift, bit1 ctrl held; bit6 the byte read was a key code */
#define KUP     0x80
#define KDOWN   0x81
#define KLEFT   0x82
#define KRIGHT  0x83
#define KHOME   0x84
#define KEND    0x85
#define KPGUP   0x86
#define KPGDN   0x87
#define KINS    0x88
#define KDEL    0x89
#define KF(n)   (0x8F + (n))            /* F1 = $90 ... F12 = $9B; F7 and F8 are the host's (menu, pause) */
#define MSGH    4                       /* message rows */
#define TEXT0   2                       /* the first row of text: under the menu bar and the files */

enum { C_NONE, C_OPEN, C_SAVE, C_SAVEAS, C_QUIT, C_UNDO, C_REDO, C_CUT, C_COPY, C_PASTE,
       C_FIND, C_NEXT, C_REPL, C_GOTO, C_MAKE, C_RUN, C_MNEXT, C_MPREV, C_RENUM, C_HELP, C_ABOUT,
       C_NEW, C_CLOSE, C_NEXTF, C_PREVF, C_FINDF };

static uint8_t running = 1, eh, over, msgs_due = 1, kmod, kcode, wantx;
static unsigned tgline = 0xFFFFu;        /* the line a run of typing is on: one undo for all of it */
static uint8_t hoff, lasthoff = 0xFF;
static unsigned lasttop = 0xFFFF, lastcy = 0xFFFF, mtop;
static char ibuf[NAMEMAX];               /* what a prompt is editing */
static char gline[140];                  /* find in files' command line: BSS, where the ROM can read it */
static uint8_t nameeq(const char *a, const char *b);

/* ---- keys --------------------------------------------------------------- */
static uint8_t key(void)
{
    uint8_t k;
    do { k = rom_getin(); } while (!k);
    kmod = REG(KSTAT);
    kcode = (uint8_t)((kmod & 0x40) ? 1 : 0);
    return k;
}

/* ---- drawing ------------------------------------------------------------ */
static void layout(void) { eh = (uint8_t)(rows - 4 - MSGH); }
static void pad(void) { while (sx < (uint8_t)(cols - 1)) put(' '); }
static void row_text(const uint8_t *l)
{
    uint8_t c, w = l[0];
    for (c = 0; (unsigned)(c + hoff) < w && c < cols; c++) put((char)l[1 + c + hoff]);
}
static void text_row(uint8_t r)
{
    unsigned l = top + r;
    at((uint8_t)(TEXT0 + r), 0);
    if (l < nlines) {
        if (l == cy) row_text(ln);
        else { far_get(SLOT(l), tmp, 256); row_text(tmp); }
    }
    eeol();
}

static const char *const mtitle[] = { "File", "Edit", "Search", "Build", "Help" };
struct item { const char *label, *keys; uint8_t cmd; };
static const struct item m_file[]   = { { "New", "^N", C_NEW }, { "Open...", "^O", C_OPEN }, { "Save", "^S F2", C_SAVE }, { "Save as...", "", C_SAVEAS },
                                        { "Close", "^W", C_CLOSE }, { "Next file", "F6", C_NEXTF }, { "Previous file", "sh-F6", C_PREVF }, { "Quit", "^Q", C_QUIT }, { 0, 0, 0 } };
static const struct item m_edit[]   = { { "Undo", "^Z", C_UNDO }, { "Redo", "^Y", C_REDO }, { "Cut line", "^X", C_CUT }, { "Copy line", "^C", C_COPY }, { "Paste", "^V", C_PASTE }, { 0, 0, 0 } };
static const struct item m_search[] = { { "Find...", "^F", C_FIND }, { "Find next", "F3", C_NEXT }, { "Find in files...", "sh-^F", C_FINDF },
                                        { "Replace...", "^R", C_REPL }, { "Go to line...", "^G", C_GOTO }, { 0, 0, 0 } };
static const struct item m_build[]  = { { "Compile", "F9", C_MAKE }, { "Compile and run", "^F9", C_RUN }, { "Next message", "F4", C_MNEXT }, { "Previous message", "sh-F4", C_MPREV }, { "Renumber BASIC", "", C_RENUM }, { 0, 0, 0 } };
static const struct item m_help[]   = { { "Keys", "F1", C_HELP }, { "About PROG", "", C_ABOUT }, { 0, 0, 0 } };
static const struct item *const menus[] = { m_file, m_edit, m_search, m_build, m_help };
#define NMENU 5

static uint8_t slen(const char *s) { uint8_t n = 0; while (s[n]) n++; return n; }
static uint8_t mx(uint8_t m) { uint8_t x = 1, i; for (i = 0; i < m; i++) x = (uint8_t)(x + slen(mtitle[i]) + 2); return x; }

static void menubar(int8_t sel)
{
    uint8_t i;
    at(0, 0); sgr("7"); clip = 1; sx = 0;
    put(' ');
    for (i = 0; i < NMENU; i++) {                     /* the escapes are not columns: clip off around them */
        if ((int8_t)i == sel) { clip = 0; sgr("0"); clip = 1; }
        put(' '); say(mtitle[i]); put(' ');
        if ((int8_t)i == sel) { clip = 0; sgr("7"); clip = 1; }
    }
    say("   PROG");
    pad(); clip = 0; eeol(); sgr("0");
}
static void tabrow(void)                              /* the open files: the current one lit, * when changed */
{
    uint8_t i, d; const char *nm;
    at(1, 0); clip = 1; sx = 0;
    for (i = 0; i < ed_nbuf; i++) {
        nm = i == ed_cur ? name : ed_bufs[i].name;
        d = i == ed_cur ? dirty : ed_bufs[i].dirty;
        if (i == ed_cur) { clip = 0; sgr("7"); clip = 1; }
        put(' '); say(nm[0] ? base_of(nm) : (const char *)"(untitled)"); if (d) put('*'); put(' ');
        if (i == ed_cur) { clip = 0; sgr("0"); clip = 1; }
        put(' ');
    }
    clip = 0; eeol();
}
static void sepline(void)
{
    at((uint8_t)(eh + TEXT0), 0); sgr("7"); clip = 1; sx = 0;
    say(" "); say(name[0] ? name : "(untitled)");
    if (dirty) say(" *");
    say("   messages");
    if (nerr) { say(" ("); num(nerr); say(")"); }
    pad(); clip = 0; eeol(); sgr("0");
}
static void msgpane(void)
{
    uint8_t r, j; unsigned i, l;
    if (nerr && ecur != 0xFFFFu) {
        if (ecur < mtop) mtop = ecur;
        if (ecur >= mtop + MSGH) mtop = ecur - MSGH + 1;
    }
    if (mtop >= nerr) mtop = 0;
    for (r = 0; r < MSGH; r++) {
        at((uint8_t)(eh + TEXT0 + 1 + r), 0); clip = 1; sx = 0;
        i = mtop + r;
        if (i < nerr) {
            far_get(ERRTAB + ((uint32_t)i << 7), ebuf, 128);
            if (i == ecur) { clip = 0; sgr("7"); clip = 1; }
            say(ebuf[3] == 'W' ? " warning  " : ebuf[3] == 'F' ? " found    " : " error    ");
            l = (unsigned)ebuf[0] | ((unsigned)ebuf[1] << 8);
            { char w[40]; ent_where(w); say(w); }     /* "line 12: " here, "UNIT.PAS:12: " elsewhere: asked now, not at F9 */
            for (j = 0; j < ebuf[5]; j++) put((char)ebuf[6 + j]);
            if (i == ecur) { pad(); clip = 0; sgr("0"); }   /* clip off first: at the edge it ate the escape, and JIM printed "[K" */
        } else if (!r && !nerr) {
            say(info[0] ? " " : " F9 compiles, Ctrl-F9 compiles and runs; what the compiler says comes here");
            if (info[0]) say(info);
        }
        clip = 0; eeol();
    }
}
static void status(void)
{
    at((uint8_t)(rows - 1), 0); sgr("7"); clip = 1; sx = 0;
    put(' '); num(cy + 1); put(':'); num((unsigned long)cx + 1);
    say(over ? "  OVR   " : "  INS   ");
    say(*note ? note : (const char *)"F1 help  F2 save  F9 make  ^F9 run  F6 next file  F10 menu");   /* cc65: a literal is char *, note const */
    clip = 0; eeol(); sgr("0");
}
/* The top band names the file in front (core/io.c's title stack, SYS+$44:
 * 0 clears it, a character adds one).  The emulator learns a file's name
 * when a program loads it, and switching tabs loads nothing -- the band
 * said PGA.C with PGH.H in front (Doc, 2026-09-14). */
static char bandnm[NAMEMAX];
static void band_file(void)
{
    const char *b = base_of(name); uint8_t i;
    REG(0xD544) = 0;
    for (i = 0; b[i]; i++) REG(0xD544) = (uint8_t)b[i];
    for (i = 0; name[i] && i < NAMEMAX - 1; i++) bandnm[i] = name[i];
    bandnm[i] = 0;
}
static void draw(void)
{
    uint8_t r, want;
    if (full || !nameeq(bandnm, name)) band_file();
    layout();
    if (cy < top) top = cy;
    while (cy >= top + eh) top++;
    hoff = (cx >= cols) ? (uint8_t)(cx - cols + 1) : 0;
    if (top != lasttop || hoff != lasthoff) full = 1;
    if (full) { menubar(-1); for (r = 0; r < eh; r++) text_row(r); msgs_due = 1; full = 0; }
    else {
        if (lastcy != cy && lastcy >= top && lastcy < top + eh) text_row((uint8_t)(lastcy - top));
        text_row((uint8_t)(cy - top));
    }
    tabrow();
    sepline();
    if (msgs_due) { msgpane(); msgs_due = 0; }
    status();
    lasttop = top; lastcy = cy; lasthoff = hoff;
    want = over ? '2' : '6';                              /* a block overwriting, a bar inserting (DECSCUSR) */
    if (want != curshape) { curshape = want; put(27); put('['); put((char)want); put(' '); put('q'); }
    at((uint8_t)(TEXT0 + cy - top), (uint8_t)(cx - hoff));
}

/* ---- the one-line prompt, on the status row ------------------------------ */
static uint8_t prompt(const char *label, char *buf, uint8_t max)   /* 1: Enter, 0: Esc */
{
    uint8_t n = slen(buf), k;
    for (;;) {
        at((uint8_t)(rows - 1), 0); sgr("7"); clip = 1; sx = 0;
        put(' '); say(label); say(buf);
        clip = 0; eeol(); sgr("0");
        at((uint8_t)(rows - 1), (uint8_t)(1 + slen(label) + n));
        k = key();
        if (k == 0x0D) return 1;
        if (k == 0x1B) return 0;
        if (k == 0x08) { if (n) buf[--n] = 0; }
        else if (k >= 0x20 && k < 0x7F && n < max - 1) { buf[n++] = (char)k; buf[n] = 0; }
    }
}
static uint8_t ask(const char *q)                     /* one key, folded to lower case */
{
    uint8_t k;
    at((uint8_t)(rows - 1), 0); sgr("7"); clip = 1; sx = 0;
    put(' '); say(q);
    clip = 0; eeol(); sgr("0");
    k = key();
    return (uint8_t)((k >= 'A' && k <= 'Z') ? k + 32 : k);
}

/* ---- editing ------------------------------------------------------------ */
static void t_end(void) { if (tgline != 0xFFFFu) { u_end(); tgline = 0xFFFFu; } }
static void t_begin(void)                             /* a run of typing on one line is one undo */
{
    if (tgline != cy) { t_end(); u_begin(); u_line(cy); line_in(cy); tgline = cy; }
}
static void go(unsigned n) { t_end(); goline(n); }
static void type_ch(uint8_t c)
{
    uint8_t i;
    t_begin();
    if (c == '}' && cx >= ed_tabw && cx == ln[0]) {     /* a } alone on its line goes back a level */
        for (i = 1; i <= cx && ln[i] == ' '; i++) ;
        if (i > cx) { cx = (uint8_t)(cx - ed_tabw); ln[0] = cx; }
    }
    if (over && cx < ln[0]) { ln[cx + 1] = c; cx++; dirty = 1; }
    else ins_ch(c);
    wantx = cx;
}
static void enter(void)                               /* split the line, keeping its indent */
{
    uint8_t ind = 0, i;
    t_end();
    while (ind < ln[0] && ln[ind + 1] == ' ') ind++;
    if (ind > cx) ind = cx;
    u_begin(); u_line(cy); line_in(cy); u_ins(cy + 1);
    split();
    for (i = 0; i < ind; i++) ins_ch(' ');
    u_end();
    wantx = cx;
}
static void backspace(void)
{
    if (cx) { t_begin(); cx--; del_ch(); wantx = cx; return; }
    if (!cy) return;
    t_end(); line_out(cy);
    far_get(SLOT(cy - 1), tmp, 256);
    if ((unsigned)tmp[0] + ln[0] > 255) { note = "the lines would not fit on one"; return; }
    u_begin(); u_line(cy - 1); u_del(cy);
    join_prev();
    u_end();
    wantx = cx;
}
static void delete_fwd(void)
{
    uint8_t i;
    if (cx < ln[0]) { t_begin(); del_ch(); return; }
    if (cy + 1 >= nlines) return;
    t_end(); line_out(cy);
    far_get(SLOT(cy + 1), tmp, 256);
    if ((unsigned)tmp[0] + ln[0] > 255) { note = "the lines would not fit on one"; return; }
    u_begin(); u_line(cy); u_del(cy + 1);
    far_get(SLOT(cy + 1), tmp, 256);
    for (i = 0; i < tmp[0]; i++) ln[ln[0] + 1 + i] = tmp[i + 1];
    ln[0] = (uint8_t)(ln[0] + tmp[0]);
    line_out(cy);
    close_at(cy + 1);
    u_end(); dirty = 1;
}
static uint8_t wordc(uint8_t c) { return (uint8_t)((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'); }
static void word_left(void)
{
    if (!cx) { if (cy) { go(cy - 1); cx = ln[0]; } return; }
    while (cx && !wordc(ln[cx])) cx--;
    while (cx && wordc(ln[cx])) cx--;
}
static void word_right(void)
{
    if (cx >= ln[0]) { if (cy + 1 < nlines) { go(cy + 1); cx = 0; } return; }
    while (cx < ln[0] && wordc(ln[cx + 1])) cx++;
    while (cx < ln[0] && !wordc(ln[cx + 1])) cx++;
}
static void cut_line(void)
{
    t_end(); line_out(cy);
    reg_take(cy, 1, 1);
    u_begin();
    if (nlines == 1) { u_line(0); ln[0] = 0; line_out(0); }
    else { u_del(cy); close_at(cy); if (cy >= nlines) cy = nlines - 1; }
    u_end();
    line_in(cy); cx = 0; dirty = 1; full = 1;
    note = "line cut -- ^V puts it back";
}

/* ---- the files -------------------------------------------------------------
 * One file is the engine's current one; the rest wait in ed_bufs.  Each has
 * its own 8 MB of far memory, handed out by bufbase: a file is never moved,
 * only its record is. */
static uint32_t bufbase(uint8_t k) { return 0x04000000UL + ((uint32_t)k << 23); }
static void buf_fresh(void)                           /* the current file has just been (re)loaded */
{
    ujp = ujn = 0; useq = 0; tgline = 0xFFFFu;
    cx = 0; top = 0; dirty = 0; full = 1; wantx = 0; lasttop = 0xFFFF;
}
static uint8_t free_base(void)                        /* an 8 MB block no open file is using */
{
    uint8_t k, i, used;
    for (k = 0; k < NBUF; k++) {
        used = 0;
        for (i = 0; i < ed_nbuf; i++) if ((i == ed_cur ? ed_slots : ed_bufs[i].slots) == bufbase(k)) used = 1;
        if (!used) return k;
    }
    return 0;
}
static uint8_t nameeq(const char *a, const char *b)   /* the same name, either case */
{
    while (*a && *b) { if (rn_up((uint8_t)*a) != rn_up((uint8_t)*b)) return 0; a++; b++; }
    return (uint8_t)(*a == *b);
}
static int8_t find_buf(const char *nm, uint8_t by_base)   /* the open file called nm, or -1 */
{
    uint8_t i; const char *x, *y;
    for (i = 0; i < ed_nbuf; i++) {
        x = i == ed_cur ? name : ed_bufs[i].name;
        if (!x[0]) continue;
        y = nm;
        if (by_base) { x = base_of(x); y = base_of(nm); }
        if (nameeq(x, y)) return (int8_t)i;
    }
    return -1;
}
static void switch_to(uint8_t i)
{
    if (i == ed_cur || i >= ed_nbuf) return;
    t_end(); ed_buf_store(); ed_buf_fetch(i);
    tgline = 0xFFFFu; wantx = cx; lasttop = 0xFFFF;
}
static uint8_t is_empty(void) { return (uint8_t)(!name[0] && !dirty && nlines == 1 && !ln[0]); }
static uint8_t open_in_tab(const char *nm)            /* nm ("" = a new file) becomes the current file */
{
    int8_t j; uint8_t k, i;
    if (nm[0] && (j = find_buf(nm, 0)) >= 0) { switch_to((uint8_t)j); return 1; }
    if (!is_empty()) {                                /* an untouched (untitled) file is reused */
        if (ed_nbuf >= NBUF) { note = "eight files are open -- ^W closes one"; return 0; }
        t_end(); ed_buf_store();
        k = free_base();
        ed_cur = ed_nbuf++;
        ed_slots = bufbase(k); ed_undo = ed_slots + 0x00600000UL;
    }
    for (i = 0; nm[i] && i < NAMEMAX - 1; i++) name[i] = nm[i];
    name[i] = 0;
    nlines = 1; cy = 0;
    load_file();
    buf_fresh();
    return 1;
}
static uint8_t save_as(void)
{
    uint8_t i;
    for (i = 0; name[i] && i < NAMEMAX - 1; i++) ibuf[i] = name[i];
    ibuf[i] = 0;
    if (!prompt("Save as: ", ibuf, NAMEMAX) || !ibuf[0]) return 0;
    for (i = 0; ibuf[i]; i++) name[i] = ibuf[i];
    name[i] = 0;
    t_end(); save_file();
    full = 1;
    return (uint8_t)(note[0] == 'w');
}
static uint8_t save(void)
{
    if (!name[0]) return save_as();
    t_end(); save_file();
    return (uint8_t)(note[0] == 'w');
}
static uint8_t may_leave(void)                        /* 1 if the current text may be dropped */
{
    uint8_t k;
    if (!dirty) return 1;
    k = ask("Changed -- S save, D discard, Esc stay ");
    if (k == 's') return save();
    return (uint8_t)(k == 'd');
}
static void open_file(void)
{
    ibuf[0] = 0;
    if (prompt("Open: ", ibuf, NAMEMAX) && ibuf[0]) open_in_tab(ibuf);
    full = 1;
}
static void close_tab(void)
{
    uint8_t k;
    if (!may_leave()) { full = 1; return; }
    t_end();
    if (ed_nbuf == 1) { name[0] = 0; load_file(); buf_fresh(); note = "closed"; return; }
    for (k = ed_cur; k + 1 < ed_nbuf; k++) ed_bufs[k] = ed_bufs[k + 1];
    ed_nbuf--;
    ed_buf_fetch(ed_cur < ed_nbuf ? ed_cur : (uint8_t)(ed_nbuf - 1));
    tgline = 0xFFFFu; wantx = cx; lasttop = 0xFFFF;
}
static void quit_all(void)                            /* every changed file asked about, in turn */
{
    uint8_t i;
    t_end(); ed_buf_store();
    for (i = 0; i < ed_nbuf; i++) {
        if (!ed_bufs[i].dirty) continue;
        switch_to(i); full = 1; draw();
        if (!may_leave()) { full = 1; return; }
        ed_buf_store();
    }
    running = 0;
}
/* The compilers read the disk, so everything changed is saved first -- a
 * unit edited in another tab must be what PAS sees. */
static uint8_t save_all(void)
{
    uint8_t i, cur = ed_cur, ok = 1;
    t_end(); ed_buf_store();
    for (i = 0; i < ed_nbuf; i++)
        if (ed_bufs[i].dirty && ed_bufs[i].name[0]) {
            ed_buf_fetch(i); save_file();
            if (note[0] != 'w') ok = 0;
            ed_buf_store();
        }
    ed_buf_fetch(cur);
    return ok;
}
/* After a run: the program may have used the far memory the other files
 * are in (SWAP keeps only the 64 KB), so each is read back -- they were
 * all saved before the run. */
static void reload_others(void)
{
    uint8_t i, cur = ed_cur, x; unsigned y;
    ed_buf_store();
    for (i = 0; i < ed_nbuf; i++)
        if (i != cur && ed_bufs[i].name[0]) {
            ed_buf_fetch(i);
            y = cy; x = cx;
            load_file();
            ujp = ujn = 0; useq = 0;
            goline(y); cx = x <= ln[0] ? x : ln[0];
            dirty = 0;
            ed_buf_store();
        }
    ed_buf_fetch(cur);
}

/* ---- the messages ----------------------------------------------------------
 * An entry names its file (ed.h: [102] the length, [103..] the name).  The
 * current file's are gone to at once; another file's -- a unit, a header,
 * what find in files found -- is switched to if open, or opened from the
 * directory the compiler (or the search) ran in. */
static void goto_msg(unsigned i)
{
    unsigned l; uint8_t fl, j, k; char fn[26]; int8_t b;
    far_get(ERRTAB + ((uint32_t)i << 7), ebuf, 128);
    ecur = i; msgs_due = 1;
    l = (unsigned)ebuf[0] | ((unsigned)ebuf[1] << 8);
    fl = ebuf[102];
    if (fl && fl < 26) {
        for (j = 0; j < fl; j++) fn[j] = (char)ebuf[103 + j];
        fn[fl] = 0;
        b = find_buf(fn, 1);
        if (b >= 0) switch_to((uint8_t)b);
        else {
            for (k = 0; ed_mkdir[k] && k < NAMEMAX - 1; k++) ibuf[k] = ed_mkdir[k];
            for (j = 0; fn[j] && k < NAMEMAX - 1; j++) ibuf[k++] = fn[j];
            ibuf[k] = 0;
            if (!open_in_tab(ibuf)) return;
        }
        far_get(ERRTAB + ((uint32_t)i << 7), ebuf, 128);
        if (l) { go(l - 1); cx = ebuf[2] ? (uint8_t)(ebuf[2] - 1) : 0; if (cx > ln[0]) cx = ln[0]; wantx = cx; }
    }
    nb_reset(); nb_s(ebuf[3] == 'W' ? "warning " : ebuf[3] == 'F' ? "found " : "error ");
    nb_n(i + 1); nb_s(" of "); nb_n(nerr); nb_s(": ");
    { char w[40]; ent_where(w); nb_s(w); }
    nb_t(ebuf + 6, ebuf[5]);
    note = nbuf;
}
static void find_files(void)                          /* Shift-Ctrl-F: tools/k4510-grep, into the message pane */
{
    const char *e = base_of(name), *s; uint8_t k = 0, i = 0;
    ibuf[0] = 0;
    if (!prompt("Find in files: ", ibuf, NAMEMAX) || !ibuf[0]) return;
    t_end();
    for (s = name; s < e && k < NAMEMAX - 1; ) ed_mkdir[k++] = *s++;   /* this file's directory, "" for here */
    ed_mkdir[k] = 0;
    for (s = "!k4510-grep '"; *s; ) gline[i++] = *s++;
    for (s = ed_mkdir; *s && i < 70; s++) if (!(s[1] == 0 && *s == '/' && s != ed_mkdir)) gline[i++] = *s;
    gline[i++] = '\''; gline[i++] = ' '; gline[i++] = '\'';
    for (s = ibuf; *s && i < sizeof gline - 6; s++) {
        if (*s == '\'') { gline[i++] = '\''; gline[i++] = '\\'; gline[i++] = '\''; gline[i++] = '\''; }
        else gline[i++] = *s;
    }
    gline[i++] = '\''; gline[i] = 0;
    rom_shell(gline);
    screen_back();
    err_load();
    msgs_due = 1;
    if (nerr) goto_msg(0);
    else note = info[0] ? info : "nothing found";
}

/* ---- search ------------------------------------------------------------- */
static void find_one(void)
{
    uint8_t i;
    for (i = 0; i < patlen; i++) ibuf[i] = pat[i];
    ibuf[i] = 0;
    if (!prompt("Find: ", ibuf, NAMEMAX) || !ibuf[0]) return;
    t_end();
    for (patlen = 0; ibuf[patlen]; patlen++) pat[patlen] = ibuf[patlen];
    search(1);
}
static void replace(void)
{
    unsigned l;
    ibuf[0] = 0;
    if (!prompt("Replace: ", ibuf, NAMEMAX) || !ibuf[0]) return;
    for (soldl = 0; ibuf[soldl]; soldl++) sold[soldl] = ibuf[soldl];
    ibuf[0] = 0;
    if (!prompt("With: ", ibuf, NAMEMAX)) return;
    for (snewl = 0; ibuf[snewl]; snewl++) snew[snewl] = ibuf[snewl];
    t_end(); subs = 0; line_out(cy); u_begin();
    for (l = 0; l < nlines; l++) sub_line(l, 1);
    u_end(); line_in(cy);
    if (cx > ln[0]) cx = ln[0];
    patlen = 0; full = 1;
    nb_reset(); nb_n(subs); nb_s(subs == 1 ? " replaced (^Z undoes)" : " replaced (^Z undoes all of them)"); note = nbuf;
}
static void goto_line(void)
{
    unsigned n = 0; uint8_t i;
    ibuf[0] = 0;
    if (!prompt("Go to line: ", ibuf, 8)) return;
    for (i = 0; ibuf[i] >= '0' && ibuf[i] <= '9'; i++) n = n * 10 + (unsigned)(ibuf[i] - '0');
    if (n) { go(n - 1); cx = 0; wantx = 0; }
}

/* ---- help --------------------------------------------------------------- */
static const char *const helptext[] = {
    "PROG -- edit, compile, run.  The keys:",
    "",
    "  arrows Home End PgUp PgDn   move        Ctrl+arrows    a word at a time",
    "  Ctrl+Home  Ctrl+End         the ends    Insert         insert / overwrite",
    "  Enter      a new line, keeping the indent   Tab   spaces to the next stop",
    "",
    "  Ctrl-S  F2   save            Ctrl-O   open (a tab)   Ctrl-N   a new file",
    "  F6  Shift-F6 the next / previous file    Ctrl-W close one    Ctrl-Q quit",
    "  Ctrl-Z       undo            Ctrl-Y   redo",
    "  Ctrl-X  Ctrl-C  Ctrl-V       cut, copy, paste the line",
    "  Ctrl-F  F3   find, again     Ctrl-R   replace        Ctrl-G   go to line",
    "  Shift-Ctrl-F find in files: every source file in this file's directory",
    "",
    "  F9           save what changed, compile this .C (CC) or .PAS (PAS)",
    "  Ctrl-F9      compile, then run it; a key comes back",
    "  F4  Shift-F4 the next / previous message -- another file's opens in a tab",
    "  F10          the menu (arrows, Enter, Esc)        F1  this page",
    "",
    "  F7 and F8 are the machine's own (its menu, pause): PROG leaves them alone.",
    0 };
static void help(void)
{
    uint8_t r;
    REG(TERM + 4) = 2;
    for (r = 0; helptext[r] && r < rows - 2; r++) { at(r, 0); clip = 1; sx = 0; say(helptext[r]); clip = 0; }
    at((uint8_t)(rows - 1), 0); sgr("7"); say(" a key returns "); sgr("0");
    key();
    full = 1;
}

/* ---- the commands ------------------------------------------------------- */
static void run_cmd(uint8_t c)
{
    switch (c) {
    case C_NEW:    open_in_tab(""); break;
    case C_OPEN:   open_file(); break;
    case C_SAVE:   save(); break;
    case C_SAVEAS: save_as(); break;
    case C_CLOSE:  close_tab(); break;
    case C_NEXTF:  switch_to((uint8_t)(ed_cur + 1 < ed_nbuf ? ed_cur + 1 : 0)); break;
    case C_PREVF:  switch_to((uint8_t)(ed_cur ? ed_cur - 1 : ed_nbuf - 1)); break;
    case C_QUIT:   quit_all(); break;
    case C_UNDO:   t_end(); u_apply(0); break;
    case C_REDO:   t_end(); u_apply(1); break;
    case C_CUT:    cut_line(); break;
    case C_COPY:   t_end(); line_out(cy); reg_take(cy, 1, 1); note = "line copied -- ^V puts it in"; break;
    case C_PASTE:  t_end(); do_put(0); break;
    case C_FIND:   find_one(); break;
    case C_NEXT:   t_end(); search(1); break;
    case C_FINDF:  find_files(); break;
    case C_REPL:   replace(); break;
    case C_GOTO:   goto_line(); break;
    case C_MAKE:   if (!save_all()) { note = "a file would not save -- nothing compiled"; break; }
                   do_make(); if (ecur != 0xFFFFu) goto_msg(ecur); msgs_due = 1; break;
    case C_RUN:    if (!save_all()) { note = "a file would not save -- nothing run"; break; }
                   do_run(); reload_others(); if (ecur != 0xFFFFu) goto_msg(ecur); msgs_due = 1; break;
    case C_MNEXT:  t_end(); if (!nerr) note = "no messages -- F9 compiles";
                   else if (ecur + 1 < nerr || ecur == 0xFFFFu) goto_msg(ecur + 1); else note = "no more messages";
                   break;
    case C_MPREV:  t_end(); if (nerr && ecur != 0xFFFFu && ecur > 0) goto_msg(ecur - 1); else note = "no earlier message";
                   break;
    case C_RENUM:  t_end(); do_renum(""); break;
    case C_HELP:   help(); break;
    case C_ABOUT:  note = "PROG, the K4510's programmer's front end -- stage 2: eight files, find in files"; break;
    }
    wantx = cx;
}

/* ---- the menu -----------------------------------------------------------
 * F10: the bar lights up, the first menu opens under its title as a box
 * over the text.  Left and Right change menu, Up and Down the entry, Enter
 * runs it, Esc or F10 again closes. */
static uint8_t mwidth(const struct item *it, uint8_t *n)   /* inner width; *n = entries */
{
    uint8_t w = 0, r;
    for (*n = 0; it[*n].label; (*n)++) {
        r = (uint8_t)(slen(it[*n].label) + slen(it[*n].keys) + 4);
        if (r > w) w = r;
    }
    return w;
}
static void menu_draw(uint8_t m, uint8_t sel)
{
    const struct item *it = menus[m];
    uint8_t n, w = mwidth(it, &n), x = mx(m), r, k;
    menubar((int8_t)m);
    for (r = 0; r <= n && r < eh + 1; r++) {
        at((uint8_t)(1 + r), x); clip = 1; sx = 0;
        if (r == n) { put((char)0xC0); for (k = 0; k < w; k++) put((char)0xC4); put((char)0xD9); }
        else {
            put((char)0xB3);
            if (r == sel) { clip = 0; sgr("7"); clip = 1; }
            put(' '); say(it[r].label);
            for (k = (uint8_t)(slen(it[r].label) + 1); k < (uint8_t)(w - slen(it[r].keys) - 1); k++) put(' ');
            say(it[r].keys); put(' ');
            if (r == sel) { clip = 0; sgr("0"); clip = 1; }
            put((char)0xB3);
        }
        clip = 0;
    }
    at((uint8_t)(1 + sel), (uint8_t)(x + 1));
}
static uint8_t menu(void)                             /* the command chosen, or C_NONE */
{
    uint8_t m = 0, sel = 0, n, k, r;
    for (;;) {
        mwidth(menus[m], &n);
        menu_draw(m, sel);
        k = key();
        if (k == 0x1B || (k == KF(10) && kcode)) break;
        if (k == 0x0D) { full = 1; return menus[m][sel].cmd; }
        if (!kcode) continue;
        if (k == KUP) sel = sel ? (uint8_t)(sel - 1) : (uint8_t)(n - 1);
        else if (k == KDOWN) sel = (uint8_t)(sel + 1 < n ? sel + 1 : 0);
        else if (k == KLEFT || k == KRIGHT) {
            m = (uint8_t)(k == KLEFT ? (m ? m - 1 : NMENU - 1) : (m + 1 < NMENU ? m + 1 : 0));
            sel = 0;
            tabrow(); for (r = 0; r < eh; r++) text_row(r);   /* the old menu away */
        }
    }
    full = 1;
    return C_NONE;
}

/* ---- the keys ------------------------------------------------------------ */
static void do_key(uint8_t k)
{
    uint8_t ctrl = (uint8_t)(kmod & 2), shift = (uint8_t)(kmod & 1);
    if (!kcode) {
        switch (k) {
        case 0x0D: enter(); return;
        case 0x08: backspace(); return;
        case 0x09: t_begin(); ed_tab(); wantx = cx; return;       /* spaces to the next stop: set ts= in VI.RC */
        case 0x1B: return;
        case 0x0E: run_cmd(C_NEW); return;           /* ^N */
        case 0x0F: run_cmd(C_OPEN); return;          /* ^O */
        case 0x13: run_cmd(C_SAVE); return;          /* ^S */
        case 0x17: run_cmd(C_CLOSE); return;         /* ^W */
        case 0x11: run_cmd(C_QUIT); return;          /* ^Q */
        case 0x1A: run_cmd(C_UNDO); return;          /* ^Z */
        case 0x19: run_cmd(C_REDO); return;          /* ^Y */
        case 0x18: run_cmd(C_CUT); return;           /* ^X */
        case 0x03: run_cmd(C_COPY); return;          /* ^C */
        case 0x16: run_cmd(C_PASTE); return;         /* ^V */
        case 0x06: run_cmd(shift ? C_FINDF : C_FIND); return;   /* ^F; with Shift, in files */
        case 0x12: run_cmd(C_REPL); return;          /* ^R */
        case 0x07: run_cmd(C_GOTO); return;          /* ^G */
        }
        if ((k >= 0x20 && k < 0x7F) || k >= 0x80) type_ch(k);   /* an accented letter comes as a character, not a key */
        return;
    }
    switch (k) {
    case KLEFT:  if (ctrl) word_left(); else if (cx) cx--; else if (cy) { go(cy - 1); cx = ln[0]; } wantx = cx; break;
    case KRIGHT: if (ctrl) word_right(); else if (cx < ln[0]) cx++; else if (cy + 1 < nlines) { go(cy + 1); cx = 0; } wantx = cx; break;
    case KUP:    if (cy) { go(cy - 1); cx = wantx < ln[0] ? wantx : ln[0]; } break;
    case KDOWN:  if (cy + 1 < nlines) { go(cy + 1); cx = wantx < ln[0] ? wantx : ln[0]; } break;
    case KHOME:  if (ctrl) go(0); cx = 0; wantx = 0; break;
    case KEND:   if (ctrl) go(nlines - 1); cx = ln[0]; wantx = cx; break;
    case KPGUP:  go(cy > (unsigned)(eh - 1) ? cy - (eh - 1) : 0); cx = wantx < ln[0] ? wantx : ln[0]; break;
    case KPGDN:  go(cy + eh - 1); cx = wantx < ln[0] ? wantx : ln[0]; break;
    case KINS:   over = (uint8_t)!over; break;
    case KDEL:   delete_fwd(); break;
    case KF(1):  run_cmd(C_HELP); break;
    case KF(2):  run_cmd(C_SAVE); break;
    case KF(3):  run_cmd(C_NEXT); break;
    case KF(4):  run_cmd(shift ? C_MPREV : C_MNEXT); break;
    case KF(6):  run_cmd(shift ? C_PREVF : C_NEXTF); break;
    case KF(9):  run_cmd(ctrl ? C_RUN : C_MAKE); break;
    case KF(10): run_cmd(menu()); break;
    }
}

static void fresh(void)                               /* the first file: no messages yet */
{
    buf_fresh();
    nerr = 0; nwarn = 0; ecur = 0xFFFFu; info[0] = 0; mtop = 0;
}

void main(void)
{
    uint8_t k, na = rom_args(); const char *a = *(const char **)0xF0; uint8_t j = 0;
    while (na && *a == ' ') { a++; na--; }
    while (j < na && j < NAMEMAX - 1 && a[j] != ' ') { name[j] = a[j]; j++; }
    name[j] = 0;
    cols = REG(TERM + 5); rows = REG(TERM + 6);
    if (!cols) cols = 80;
    if (!rows) rows = 30;
    ed_maxlines = 16384u;                                 /* a file's lines in its 4 MB */
    ed_cur = 0; ed_nbuf = 1;
    ed_slots = bufbase(0); ed_undo = ed_slots + 0x00600000UL;
    ed_rc_tabw();                                         /* VI's set ts=N, one tab width for both editors */
    load_file();
    fresh();
    if (!name[0]) note = "no file yet -- type, then ^S names it; ^O opens one, ^N a new one";
    REG(TERM + 4) = 1; REG(TERM + 4) = 2; REG(TERM + 0x0E) = 1;
    while (running) {
        draw();
        k = key();
        note = "";
        do_key(k);
    }
    put(27); put('['); put('2'); put(' '); put('q');     /* the block back for the shell */
    REG(TERM + 0x0E) = 0; REG(TERM + 4) = 1; REG(TERM + 4) = 2;
    rom_video();
}
