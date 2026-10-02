/* K4510: PROG [-s] [name] -- the programmer's front end: edit, compile, run.
 *
 * Doc, 2026-09-14: "a small ide/front end that can cover both CC and PAS
 * edit/compile cycle - something like turbo pascal but more modern ...
 * (not limited to just one file at a time, if possible)".  And 2026-10-02:
 * "modify PROG so that its interface is similar to this new EDIT" -- so it
 * wears MS-DOS EDIT's clothes now (demo/dosui.h) and edits in EDIT's window
 * (demo/dosed.h), on VI's engine (demo/ed.h): three editors, one engine.
 *
 *   row 0             the menu bar            F10, Alt+letter, or the mouse
 *   row 1             the window's top: the open files as tabs, the one in
 *                     front lit, * when changed; the project's name first
 *   rows 2 .. eh+1    the text, framed, its scroll bar right
 *   row eh+2          the window's foot: the horizontal scroll bar
 *   row eh+3          the messages window's top: how many
 *   MSGH rows         the messages: the compiler's, or what find in files found
 *   the last row      what just happened, the keys; line:column
 *
 * Every key and every menu entry is a command number, run by one switch
 * (run_cmd), so a menu can never do something its key does not.
 *
 * Up to eight files (ed.h's buffers), each with 8 MB of far memory of its
 * own at $04000000 + n * 8 MB: its lines in the first 4 MB (16384 of them),
 * its undo from +6 MB.  $04000000-$07FFFFFF is used by nothing else on the
 * machine (checked 2026-09-14: BOOK is at $0C, SPLIT $0D, VI $0E-$0F; EDIT
 * took $08-$0B on 2026-10-02).  The Open dialog's list is at $07F00000. */
#include "k4510.h"
#define ED_NO_RENUM                             /* renumbering BASIC is EDIT's and VI's: PROG has no room for it */
#include "ed.h"
#include "dosui.h"
#include "dosed.h"

#define MSGH    4                       /* message rows */

enum { C_OPEN = 1, C_SAVE, C_SAVEAS, C_QUIT, C_UNDO, C_REDO, C_CUT, C_COPY, C_PASTE,
       C_FIND, C_NEXT, C_REPL, C_GOTO, C_MAKE, C_RUN, C_MNEXT, C_MPREV, C_RENUM, C_HELP, C_ABOUT,
       C_NEW, C_CLOSE, C_NEXTF, C_PREVF, C_FINDF, C_NEWPROJ, C_SELALL, C_CLEAR, C_DOS, C_SYS };

static uint8_t eh, msgs_due = 1;
static unsigned mtop;
static char ibuf[NAMEMAX];               /* what a dialog is editing */
static char gline[140];                  /* find in files' command line: BSS, where the ROM can read it */
static uint8_t nameeq(const char *a, const char *b);

/* From here to the messages the code lives at $E000 (HICODE, demo/prog.cfg):
 * the project and the files are PROG's cold half, and $2000-$CFFF no longer
 * held everything. */
#pragma code-name (push, "HICODE")
/* ---- the project -----------------------------------------------------------
 * PROG's stage 3 (Doc, 2026-09-14): a PROJECT.K4P beside the sources,
 * KEY=value lines --
 *     NAME=GAME        LANG=C or PAS        SRC=GAME.C UTIL.C (C: all of them)
 *     MAIN=GAME.PAS    (Pascal: the program; its units come by `uses`)
 *     OUT=game.prg     (or the NAME's, in lower case)
 * When the file in front has one beside it, F9 builds the project, not the
 * file (CC -p / PAS -p: tools/k4510-cc and k4510-pas), and Ctrl-F9 runs
 * the project's program -- so F9 works from a header or a unit too. */
#define PJRAW 0x0ED00000UL                   /* PROJECT.K4P as loaded (far memory nothing else uses) */
static char pj_file[NAMEMAX], pj_dir[NAMEMAX], pj_for[NAMEMAX];
static char pj_name[20], pj_lang[4], pj_main[26], pj_out[26], pj_src[90];
static char pl[100];                         /* one line of it */
static void pj_set(char *dst, uint8_t max, const char *v)
{
    uint8_t n = 0;
    while (*v && n < max - 1) dst[n++] = *v++;
    while (n && (dst[n - 1] == ' ' || dst[n - 1] == '\t')) n--;
    dst[n] = 0;
}
static void pj_line(void)
{
    char *v = pl, key[8]; uint8_t k = 0;
    while (*v == ' ' || *v == '\t') v++;
    if (*v == '#' || !*v) return;
    while (*v && *v != '=' && *v != ' ' && k < sizeof key - 1) key[k++] = (char)rn_up((uint8_t)*v++);
    key[k] = 0;
    while (*v == ' ') v++;
    if (*v != '=') return;
    v++;
    while (*v == ' ' || *v == '\t') v++;
    if (!memcmp(key, "NAME", 5)) pj_set(pj_name, sizeof pj_name, v);
    else if (!memcmp(key, "LANG", 5)) pj_set(pj_lang, sizeof pj_lang, v);
    else if (!memcmp(key, "MAIN", 5)) pj_set(pj_main, sizeof pj_main, v);
    else if (!memcmp(key, "SRC", 4)) pj_set(pj_src, sizeof pj_src, v);
    else if (!memcmp(key, "OUT", 4)) pj_set(pj_out, sizeof pj_out, v);
}
static uint8_t pj_scan(void)                         /* the project beside the file in front: 1 if there is one */
{
    const char *e, *s; uint8_t k = 0, n = 0; uint32_t l, off = 0; unsigned chunk, i;
    static const char pjn[] = "PROJECT.K4P";
    pj_name[0] = pj_lang[0] = pj_main[0] = pj_out[0] = pj_src[0] = 0;
    e = base_of(name);
    for (s = name; s < e && k < NAMEMAX - 13; ) pj_dir[k++] = *s++;
    pj_dir[k] = 0;
    for (i = 0; i < k; i++) pj_file[i] = pj_dir[i];
    for (s = pjn; *s; ) pj_file[k++] = *s++;
    pj_file[k] = 0;
    zp16(0xF0, (uint16_t)pj_file); zp32(0xF2, PJRAW);
    if (!name[0] || rom_load()) { pj_file[0] = 0; return 0; }
    l = zpr32(0xF6);
    while (off < l) {
        chunk = (l - off) > 128 ? 128 : (unsigned)(l - off);
        far_get(PJRAW + off, tmp, chunk);
        for (i = 0; i < chunk; i++) {
            if (tmp[i] == '\n') { pl[n] = 0; pj_line(); n = 0; }
            else if (tmp[i] != '\r' && n < sizeof pl - 1) pl[n++] = (char)tmp[i];
        }
        off += chunk;
    }
    pl[n] = 0; pj_line();
    if (!pj_name[0]) { e = pj_main[0] ? pj_main : pj_src; for (k = 0; e[k] && e[k] != '.' && e[k] != ' ' && k < sizeof pj_name - 1; k++) pj_name[k] = e[k]; pj_name[k] = 0; }
    if (!pj_lang[0]) {                                /* from the main file's extension */
        e = pj_main[0] ? pj_main : pj_src; s = 0;
        for (; *e && *e != ' '; e++) if (*e == '.') s = e;
        pj_set(pj_lang, sizeof pj_lang, (s && rn_up((uint8_t)s[1]) == 'P') ? "PAS" : "C");
    }
    return 1;
}
/* the project's build and run, for the engine's do_make and do_run; nothing
 * set when there is no project, and the file's own way is used */
static void pj_arm(void)
{
    const char *s; uint8_t i = 0, k;
    ed_mkline[0] = ed_runname[0] = 0;
    if (!pj_scan()) return;
    for (s = rn_up((uint8_t)pj_lang[0]) == 'P' ? "PAS -p " : "CC -p "; *s; ) ed_mkline[i++] = *s++;
    for (s = pj_file; *s && i < sizeof ed_mkline - 1; ) ed_mkline[i++] = *s++;
    ed_mkline[i] = 0;
    for (k = 0; pj_dir[k]; k++) ed_mkdir[k] = pj_dir[k];
    ed_mkdir[k] = 0;
    i = 0;
    for (s = pj_dir; *s && i < sizeof ed_runname - 1; ) ed_runname[i++] = *s++;
    if (pj_out[0]) { for (s = pj_out; *s && *s != '.' && i < sizeof ed_runname - 1; ) ed_runname[i++] = *s++; }
    else for (s = pj_name; *s && i < sizeof ed_runname - 1; s++) ed_runname[i++] = (char)((*s >= 'A' && *s <= 'Z') ? *s + 32 : *s);
    ed_runname[i] = 0;
}

/* ---- drawing ------------------------------------------------------------ */
/* The window's top: the project, then a tab for each open file -- the one in
 * front lit, * when changed.  tab_at() is this arithmetic again, for the mouse. */
static uint8_t tabw(uint8_t i)
{
    const char *nm = i == ed_cur ? name : ed_bufs[i].name;
    return (uint8_t)((nm[0] ? slen(base_of(nm)) : 8) + ((i == ed_cur ? dirty : ed_bufs[i].dirty) ? 1 : 0) + 2);
}
static uint8_t tab_x0(void) { return (uint8_t)(pj_name[0] ? slen(pj_name) + 5 : 2); }
static void tabrow(void)
{
    uint8_t i, x, j, k; const char *nm;
    cel(0, 0xDA, K_FRAME);
    for (x = 1; x < cols - 1; x++) cel(x, 0xC4, K_FRAME);
    cel((uint8_t)(cols - 1), 0xBF, K_FRAME);
    x = 2;
    if (pj_name[0]) { cel(x++, '[', K_FRAME); for (j = 0; pj_name[j]; j++) cel(x++, (uint8_t)pj_name[j], K_FRAME); cel(x++, ']', K_FRAME); x++; }
    for (i = 0; i < ed_nbuf; i++) {
        nm = i == ed_cur ? name : ed_bufs[i].name;
        nm = nm[0] ? base_of(nm) : (const char *)"Untitled";
        k = i == ed_cur ? K_TITLE : K_FRAME;
        if (x + tabw(i) >= cols - 1) break;
        cel(x++, ' ', k);
        for (j = 0; nm[j]; j++) cel(x++, (uint8_t)nm[j], k);
        if (i == ed_cur ? dirty : ed_bufs[i].dirty) cel(x++, '*', k);
        cel(x++, ' ', k);
        x++;
    }
    flush(1, cols);
}
static void msgpane(void)
{
    uint8_t r, j, y0 = (uint8_t)(eh + 3), x, k; unsigned i;
    char t[24];
    if (nerr && ecur != 0xFFFFu) {
        if (ecur < mtop) mtop = ecur;
        if (ecur >= mtop + MSGH) mtop = ecur - MSGH + 1;
    }
    if (mtop >= nerr) mtop = 0;
    for (j = 0; "Messages"[j]; j++) t[j] = "Messages"[j];    /* not in nbuf: a note may be living there */
    if (nerr) {
        char d[6]; uint8_t n = 0; unsigned v = nerr;
        do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v);
        t[j++] = ' '; t[j++] = '(';
        while (n) t[j++] = d[--n];
        t[j++] = ')';
    }
    t[j] = 0;
    frame_top(y0, t, 0);
    for (r = 0; r < MSGH; r++) {
        i = mtop + r;
        k = (nerr && i == ecur) ? K_SEL : K_TEXT;
        cel(0, 0xB3, K_FRAME);
        for (x = 1; x < cols - 1; x++) cel(x, ' ', k);
        cel((uint8_t)(cols - 1), 0xB3, K_FRAME);
        x = 2;
        if (i < nerr) {
            const char *w0;
            far_get(ERRTAB + ((uint32_t)i << 7), ebuf, 128);
            for (w0 = ebuf[3] == 'W' ? "warning  " : ebuf[3] == 'F' ? "found    " : "error    "; *w0 && x < cols - 1; ) cel(x++, (uint8_t)*w0++, k);
            { char w[40]; ent_where(w); for (j = 0; w[j] && x < cols - 1; j++) cel(x++, (uint8_t)w[j], k); }
            for (j = 0; j < ebuf[5] && x < cols - 1; j++) cel(x++, ebuf[6 + j], k);
        } else if (!r && !nerr) {
            const char *s = info[0] ? info : "F9 compiles, Ctrl+F9 compiles and runs; what the compiler says comes here";
            for (; *s && x < cols - 1; ) cel(x++, (uint8_t)*s++, k);
        }
        flush((uint8_t)(y0 + 1 + r), cols);
    }
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
    char p[16]; uint8_t i = 0, all;
    if (full || !nameeq(bandnm, name)) band_file();
    if (!nameeq(pj_for, name)) {                      /* another file in front: its project, if it has one */
        if (!pj_scan()) pj_name[0] = 0;
        for (i = 0; name[i] && i < NAMEMAX - 1; i++) pj_for[i] = name[i];
        pj_for[i] = 0; i = 0;
    }
    if (full) menubar(-1);
    all = window();
    tabrow();
    if (msgs_due || all) { msgpane(); msgs_due = 0; }
    full = 0;
    if (over) { p[0] = 'O'; p[1] = 'V'; p[2] = 'R'; p[3] = ' '; p[4] = ' '; i = 5; }
    where(p + i);
    status_line(*note ? note : (const char *)"F1=Help  F2=Save  F9=Compile  Ctrl+F9=Run  F10=Menus", p);
    window_cursor();
}

/* PROG's own key: a } alone on its line goes back a level, before it is typed */
static void brace(void)
{
    uint8_t i;
    if (selon || over || cx < ed_tabw || cx != ln[0]) return;
    t_begin();
    for (i = 1; i <= cx && ln[i] == ' '; i++) ;
    if (i > cx) { cx = (uint8_t)(cx - ed_tabw); ln[0] = cx; }
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
    if (!form1("Save As", "File Name:", ibuf, NAMEMAX, "OK") || !ibuf[0]) return 0;
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
    k = ask(0, "Loaded file is not saved.  Save it now?", "Yes", "No", "Cancel");
    if (k == 0) return save();
    return (uint8_t)(k == 1);
}
/* a .K4P opens with its sources, each in a tab; the main one ends in front */
static uint8_t is_k4p(const char *n)
{
    const char *d = 0;
    for (; *n; n++) if (*n == '.') d = n;
    return (uint8_t)(d && rn_up((uint8_t)d[1]) == 'K' && d[2] == '4' && rn_up((uint8_t)d[3]) == 'P' && !d[4]);
}
static void open_path(const char *nm)
{
    char p[NAMEMAX]; const char *s; uint8_t k, j;
    if (!open_in_tab(nm) || !is_k4p(name) || !pj_scan()) return;
    for (s = pj_src[0] ? pj_src : pj_main; *s; ) {
        while (*s == ' ') s++;
        if (!*s) break;
        for (k = 0; pj_dir[k] && k < NAMEMAX - 1; k++) p[k] = pj_dir[k];
        for (j = 0; s[j] && s[j] != ' ' && k < NAMEMAX - 1; j++) p[k++] = s[j];
        p[k] = 0; s += j;
        if (!open_in_tab(p)) break;
    }
}
static void open_file(void)
{
    ibuf[0] = 0;
    if (open_dialog(ibuf)) open_path(ibuf);
    full = 1;
}
/* File > New project: a folder with a PROJECT.K4P and a first file that
 * compiles as it stands -- C in HELLO.C's manner, or a Pascal program */
static char tbuf[400];
static unsigned tn;                          /* not a byte: past 255 it wrapped and wrote over the start (cc65 saw it) */
static void tcat(const char *s) { while (*s && tn < sizeof tbuf - 1) tbuf[tn++] = *s++; tbuf[tn] = 0; }
static uint8_t wfile(const char *path)              /* tbuf, written to path */
{
    far_put(tbuf, FLAT, tn);
    zp16(0xF0, (uint16_t)path); zp32(0xF2, FLAT); zp32(0xF6, tn);
    return (uint8_t)!rom_save();
}
static void new_project(void)
{
    char nm[17], path[NAMEMAX]; uint8_t i, k, pas; const char *s;
    ibuf[0] = 0;
    if (!form1("New Project", "Name (letters, digits):", ibuf, 17, "OK") || !ibuf[0]) { full = 1; return; }
    for (i = 0, k = 0; ibuf[i] && k < 16; i++) {
        char c = (char)rn_up((uint8_t)ibuf[i]);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') nm[k++] = c;
    }
    nm[k] = 0;
    if (!k) { note = "a project needs a name of letters and digits"; full = 1; return; }
    k = ask("New Project", "Which language?", "C", "Pascal", "Cancel");
    if (k > 1) { full = 1; return; }
    pas = (uint8_t)(k == 1);
    tn = 0; tcat("MKDIR "); tcat(nm);
    for (i = 0; i <= tn; i++) gline[i] = tbuf[i];
    rom_shell(gline);                                 /* an error if it is there already: then it is used as it is */
    screen_back();
    tn = 0;
    tcat("# "); tcat(nm); tcat(" -- a PROG project.  F9 builds it from any of its files.\n");
    tcat("NAME="); tcat(nm); tcat("\nLANG="); tcat(pas ? "PAS" : "C"); tcat("\n");
    tcat(pas ? "MAIN=" : "SRC="); tcat(nm); tcat(pas ? ".PAS\n" : ".C\n");
    k = 0; for (s = nm; *s; ) path[k++] = *s++;
    for (s = "/PROJECT.K4P"; *s; ) path[k++] = *s++;
    path[k] = 0;
    if (!wfile(path)) { note = "the project file would not save"; full = 1; return; }
    tn = 0;
    if (pas) {
        tcat("program "); tcat(nm); tcat(";\nbegin\n  writeln('Hello from "); tcat(nm); tcat(".');\nend.\n");
    } else {
        tcat("/* "); tcat(nm); tcat(" -- a C program for the K4510 */\n#include \"k4510.h\"\n\n");
        tcat("void __fastcall__ rom_chrout(unsigned char c);\n\n");
        tcat("static void print(const char *s) { while (*s) rom_chrout(*s++); }\n\n");
        tcat("void main(void)\n{\n    print(\"Hello from "); tcat(nm); tcat(".\\n\");\n}\n");
    }
    k = 0; for (s = nm; *s; ) path[k++] = *s++;
    path[k++] = '/';
    for (s = nm; *s; ) path[k++] = *s++;
    for (s = pas ? ".PAS" : ".C"; *s; ) path[k++] = *s++;
    path[k] = 0;
    if (!wfile(path)) { note = "the first file would not save"; full = 1; return; }
    k = 0; for (s = nm; *s; ) path[k++] = *s++;
    for (s = "/PROJECT.K4P"; *s; ) path[k++] = *s++;
    path[k] = 0;
    open_path(path);
    nb_reset(); nb_s(nm); nb_s(pas ? ": a new Pascal project -- F9 builds it" : ": a new C project -- F9 builds it"); note = nbuf;
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
#pragma code-name (pop)                              /* the rest in the main image: HI is full */
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
    if (!form1("Find in Files", "Find What:", ibuf, NAMEMAX, "OK") || !ibuf[0]) return;
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

/* ---- help --------------------------------------------------------------- */
static const char *const helptext[] = {
    "arrows Home End PgUp PgDn   move        Ctrl+arrows    a word at a time",
    "Ctrl+Home  Ctrl+End         the ends    Insert         insert / overwrite",
    "Enter      a new line, keeping the indent   Tab   spaces to the next stop",
    "",
    "Ctrl+S  F2   save            Ctrl+O   open (a tab)   Ctrl+N   a new file",
    "F6  Shift+F6 the next / previous file    Ctrl+W close one    Ctrl+Q quit",
    "Ctrl+Z  Ctrl+Y  undo, redo    Ctrl+X Ctrl+C Ctrl+V  cut, copy, paste",
    "Shift+move, a drag or Ctrl+A select; typing replaces; Tab Shift+Tab indent",
    "Ctrl+F  F3   find, again     Ctrl+R   change         Ctrl+G   go to line",
    "Shift+Ctrl+F find in files: every source file in this file's directory",
    "A PROJECT.K4P beside the file: F9 builds the project (File > New Project)",
    "",
    "F9           save what changed, compile .C (CC) or .PAS (PAS); a .RX: saved",
    "Ctrl+F9      compile, then run it (a .RX: RX runs it); a key comes back",
    "F4  Shift+F4 the next / previous message -- another file's opens in a tab",
    "F10, Alt+letter  the menus                  F1  this page",
    "Mouse: the text, a menu, a tab, a message, the scroll bars; the wheel",
    "",
    "PROG -s starts in the console's own colours (Options).",
    0 };
static const char *const abouttext[] = { "PROG -- edit, compile, run", "", "MS-DOS EDIT's manner, VI's engine,", "CC and PAS behind F9.", 0 };

/* ---- the commands ------------------------------------------------------- */
static void run_cmd(uint8_t c)
{
    if (c != C_CUT && c != C_COPY && c != C_PASTE && c != C_CLEAR && c != C_NONE) sel_clear();   /* any other command drops the selection */
    switch (c) {
    case C_NEW:    open_in_tab(""); break;
    case C_NEWPROJ: new_project(); break;
    case C_OPEN:   open_file(); break;
    case C_SAVE:   save(); break;
    case C_SAVEAS: save_as(); break;
    case C_CLOSE:  close_tab(); break;
    case C_NEXTF:  switch_to((uint8_t)(ed_cur + 1 < ed_nbuf ? ed_cur + 1 : 0)); break;
    case C_PREVF:  switch_to((uint8_t)(ed_cur ? ed_cur - 1 : ed_nbuf - 1)); break;
    case C_QUIT:   quit_all(); break;
    case C_UNDO:   t_end(); u_apply(0); break;
    case C_REDO:   t_end(); u_apply(1); break;
    case C_CUT:    do_cut(); break;
    case C_COPY:   do_copy(); break;
    case C_PASTE:  do_paste(); break;
    case C_CLEAR:  do_clear(); break;
    case C_SELALL: select_all(); break;
    case C_FIND:   find_dlg(); break;
    case C_NEXT:   find_next(); break;
    case C_FINDF:  find_files(); break;
    case C_REPL:   change_dlg(); break;
    case C_GOTO:   goto_dlg(); break;
    case C_MAKE:   if (!save_all()) { note = "A file would not save -- nothing compiled"; break; }
                   pj_arm(); do_make(); if (ecur != 0xFFFFu) goto_msg(ecur); msgs_due = 1; full = 1; break;
    case C_RUN:    if (!save_all()) { note = "A file would not save -- nothing run"; break; }
                   pj_arm(); ptr_off(); do_run(); ptr_on(); reload_others(); if (ecur != 0xFFFFu) goto_msg(ecur); msgs_due = 1; full = 1; break;
    case C_MNEXT:  t_end(); if (!nerr) note = "No messages -- F9 compiles";
                   else if (ecur + 1 < nerr || ecur == 0xFFFFu) goto_msg(ecur + 1); else note = "No more messages";
                   break;
    case C_MPREV:  t_end(); if (nerr && ecur != 0xFFFFu && ecur > 0) goto_msg(ecur - 1); else note = "No earlier message";
                   break;
    case C_DOS:    scheme(0); full = 1; break;
    case C_SYS:    scheme(1); full = 1; break;
    case C_HELP:   text_box("PROG -- the keys", helptext); break;
    case C_ABOUT:  text_box("About", abouttext); break;
    }
    wantx = cx;
}

/* ---- the menus ------------------------------------------------------------ */
static const char *const mtitle[] = { "File", "Edit", "Search", "Build", "Options", "Help" };
static const struct item m_file[]   = { { "New", 0, C_NEW, "Ctrl+N" }, { "New Project...", 4, C_NEWPROJ, "" }, { "Open...", 0, C_OPEN, "Ctrl+O" },
                                        { "Save", 0, C_SAVE, "Ctrl+S" }, { "Save As...", 5, C_SAVEAS, "" }, { "Close", 0, C_CLOSE, "Ctrl+W" },
                                        { "", 0, C_SEP, "" }, { "Next File", 1, C_NEXTF, "F6" }, { "Previous File", 0, C_PREVF, "Shift+F6" },
                                        { "", 0, C_SEP, "" }, { "Exit", 1, C_QUIT, "Ctrl+Q" }, { 0, 0, 0, 0 } };
static const struct item m_edit[]   = { { "Undo", 0, C_UNDO, "Ctrl+Z" }, { "Redo", 0, C_REDO, "Ctrl+Y" }, { "", 0, C_SEP, "" },
                                        { "Cut", 2, C_CUT, "Ctrl+X" }, { "Copy", 0, C_COPY, "Ctrl+C" }, { "Paste", 0, C_PASTE, "Ctrl+V" },
                                        { "Clear", 2, C_CLEAR, "Del" }, { "Select All", 7, C_SELALL, "Ctrl+A" }, { 0, 0, 0, 0 } };
static const struct item m_search[] = { { "Find...", 0, C_FIND, "Ctrl+F" }, { "Repeat Last Find", 0, C_NEXT, "F3" },
                                        { "Find in Files...", 5, C_FINDF, "Shift+Ctrl+F" }, { "Change...", 0, C_REPL, "Ctrl+R" },
                                        { "Go To Line...", 0, C_GOTO, "Ctrl+G" }, { 0, 0, 0, 0 } };
static const struct item m_build[]  = { { "Compile", 0, C_MAKE, "F9" }, { "Compile and Run", 12, C_RUN, "Ctrl+F9" },
                                        { "", 0, C_SEP, "" }, { "Next Message", 0, C_MNEXT, "F4" }, { "Previous Message", 0, C_MPREV, "Shift+F4" },
                                        { 0, 0, 0, 0 } };
static const struct item m_opt[]    = { { "DOS Colours", 0, C_DOS, "" }, { "System Colours", 0, C_SYS, "" }, { 0, 0, 0, 0 } };
static const struct item m_help[]   = { { "Keyboard", 0, C_HELP, "F1" }, { "About PROG...", 0, C_ABOUT, "" }, { 0, 0, 0, 0 } };
static const struct item *const menus[] = { m_file, m_edit, m_search, m_build, m_opt, m_help };
static uint8_t marked(uint8_t c) { return (uint8_t)((c == C_DOS && !sysc) || (c == C_SYS && sysc)); }

/* ---- the mouse's clicks ----------------------------------------------------- */
static uint8_t tab_at(uint8_t c)                      /* the open file whose tab is at column c, or 0xFF: tabrow's arithmetic */
{
    uint8_t i, x = tab_x0(), w;
    for (i = 0; i < ed_nbuf; i++) {
        w = tabw(i);
        if (c >= x && c < (uint8_t)(x + w)) return i;
        x = (uint8_t)(x + w + 1);
    }
    return 0xFF;
}
static void do_mouse(void)
{
    uint8_t r = mrow, i; unsigned d;
    if (window_mouse()) return;
    if (mev != 1) return;
    if (r == 0) { i = title_at(mcol); if (i < ui_nmenu) run_cmd(menu(i)); return; }
    if (r == 1) { i = tab_at(mcol); if (i != 0xFF && i != ed_cur) { sel_clear(); t_end(); switch_to(i); } return; }
    if (r > eh + 3 && r <= eh + 3 + MSGH) {           /* a message: to it */
        d = mtop + (r - eh - 4);
        if (d < nerr) { sel_clear(); goto_msg(d); msgs_due = 1; }
    }
}

/* ---- the keys ------------------------------------------------------------ */
static void do_key(uint8_t k)
{
    uint8_t ctrl = (uint8_t)(kmod & 2), shift = (uint8_t)(kmod & 1), i;
    if (kcode == 2) { do_mouse(); return; }
    if (kcode && k >= KALT && k < KALT + 26) { i = title_of(k); if (i < ui_nmenu) run_cmd(menu(i)); return; }
    if (!kcode) switch (k) {
        case 0x01: run_cmd(C_SELALL); return;        /* ^A */
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
        case '}':  brace(); break;                   /* then typed, below */
    }
    if (window_key(k)) return;
    switch (k) {
    case KF(1):  run_cmd(C_HELP); break;
    case KF(2):  run_cmd(C_SAVE); break;
    case KF(3):  run_cmd(C_NEXT); break;
    case KF(4):  run_cmd(shift ? C_MPREV : C_MNEXT); break;
    case KF(6):  run_cmd(shift ? C_PREVF : C_NEXTF); break;
    case KF(9):  run_cmd(ctrl ? C_RUN : C_MAKE); break;
    case KF(10): run_cmd(menu(0)); break;
    }
}

static void fresh(void)                               /* the first file: no messages yet */
{
    buf_fresh();
    nerr = 0; nwarn = 0; ecur = 0xFFFFu; info[0] = 0; mtop = 0;
}

void main(void)
{
    uint8_t k, na = rom_args(), j = 0, sys = 0; const char *a = *(const char **)0xF0;
    for (;;) {                                        /* PROG [-s] [name] */
        while (na && *a == ' ') { a++; na--; }
        if (na >= 2 && a[0] == '-' && (a[1] == 's' || a[1] == 'S') && (na == 2 || a[2] == ' ')) { sys = 1; a += 2; na -= 2; continue; }
        break;
    }
    while (j < na && j < NAMEMAX - 1 && a[j] != ' ') { name[j] = a[j]; j++; }
    name[j] = 0;
    ui_init();
    ui_titles = mtitle; ui_menus = menus; ui_nmenu = 6; ui_marked = marked; ui_dirtab = 0x07F00000UL; ui_name = "PROG ";
    eh = (uint8_t)(rows - 5 - MSGH);
    wy = 2; th = eh; tw = (uint8_t)(cols - 2);
    scheme(sys);
    ed_maxlines = 16384u;                                 /* a file's lines in its 4 MB */
    ed_cur = 0; ed_nbuf = 1;
    ed_slots = bufbase(0); ed_undo = ed_slots + 0x00600000UL;
    ed_rc_tabw();                                         /* VI's set ts=N, one tab width for all three editors */
    load_file();
    fresh();
    if (is_k4p(name)) {                                   /* PROG GAME/PROJECT.K4P: the project, sources and all */
        char p[NAMEMAX]; uint8_t i;
        for (i = 0; name[i]; i++) p[i] = name[i];
        p[i] = 0;
        name[0] = 0; nlines = 1; cy = 0; ln[0] = 0; line_out(0); dirty = 0;   /* an empty first tab, which open_path reuses */
        open_path(p);
    }
    if (!name[0]) note = "No file yet -- type, then Ctrl+S names it; Ctrl+O opens one";
    REG(TERM + 4) = 1; cursor_show(1);
    ptr_on();
    while (running) {
        draw();
        k = event();
        if (kcode != 2 || mev == 1) note = "";
        do_key(k);
    }
    ptr_off();
    put(27); put('['); put('2'); put(' '); put('q');     /* the block back for the shell */
    REG(TERM + 0x0E) = 0; REG(TERM + 4) = 1; REG(TERM + 4) = 2;
    rom_video();
}
