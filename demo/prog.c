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
 * Stage 4 of four (BUILD-LOG 2026-09-14): projects (PROJECT.K4P) were
 * stage 3; the mouse and the selection are stage 4 (their sections below). */
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
#define KF(n)   (0x8F + (n))            /* F1 = $90 ... F12 = $9B; F12 is the host's (menu; Shift+F12 pause) */
#define MSGH    4                       /* message rows */
#define TEXT0   2                       /* the first row of text: under the menu bar and the files */
#define MOUSEX  0xD108u                 /* the mouse, in the mode's pixels (core/io.c) */
#define MOUSEY  0xD10Au
#define MOUSEB  0xD10Cu                 /* bit0 left, bit1 right, bit2 middle */
#define MOUSEW  0xD10Du                 /* the wheel's turn in the last frame, signed: + is away from you */
#define SPRTAB  0x123000UL              /* the pointer: sprite 0, drawn as MOUSETEST draws it */
#define SPRDATA 0x123100UL
#define KMOUSE  0xFF                    /* event()'s answer for the mouse: kcode 2, what happened in mev */

enum { C_NONE, C_OPEN, C_SAVE, C_SAVEAS, C_QUIT, C_UNDO, C_REDO, C_CUT, C_COPY, C_PASTE,
       C_FIND, C_NEXT, C_REPL, C_GOTO, C_MAKE, C_RUN, C_MNEXT, C_MPREV, C_RENUM, C_HELP, C_ABOUT,
       C_NEW, C_CLOSE, C_NEXTF, C_PREVF, C_FINDF, C_NEWPROJ, C_SELALL };

static uint8_t running = 1, eh, over, msgs_due = 1, kmod, kcode, wantx;
static unsigned tgline = 0xFFFFu;        /* the line a run of typing is on: one undo for all of it */
static uint8_t hoff, lasthoff = 0xFF;
static unsigned lasttop = 0xFFFF, lastcy = 0xFFFF, mtop;
static char ibuf[NAMEMAX];               /* what a prompt is editing */
static char gline[140];                  /* find in files' command line: BSS, where the ROM can read it */
static uint8_t nameeq(const char *a, const char *b);
static uint8_t mev, mrow, mcol, mheld, dragging, chh = 8;   /* the mouse: mev 1 press, 2 drag, 3 release, 4 wheel */
static int8_t mwheel;
static uint8_t selon, selshown;          /* a selection is up; one was, when last drawn */
static unsigned sely, qy1, qy2;          /* its anchor (the cursor is the other end); both ends in order */
static uint8_t selx, qx1, qx2;

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

/* ---- keys --------------------------------------------------------------- */
static uint8_t key(void)
{
    uint8_t k;
    do { k = rom_getin(); } while (!k);
    kmod = REG(KSTAT);
    kcode = (uint8_t)((kmod & 0x40) ? 1 : 0);
    return k;
}

/* ---- the mouse -------------------------------------------------------------
 * The machine draws no pointer, so PROG draws one: sprite 0, MOUSETEST's
 * arrow.  event() waits for a key or for the mouse to do something, looking
 * once a frame -- the wheel register holds one frame's turn, and reading it
 * faster would count a notch twice. */
static const uint8_t arrow[12] = { 0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xF0, 0xD8, 0x98, 0x0C, 0x0C };
static uint8_t arrow_at(int8_t x, int8_t y) { return (uint8_t)(x >= 0 && x < 8 && y >= 0 && y < 12 && ((arrow[y] << x) & 0x80)); }
static void ptr_on(void)
{
    uint32_t d = SPRDATA; int8_t x, y, dx, dy; uint8_t v[2], k, edge;
    for (y = 0; y < 16; y++) for (x = 0; x < 16; x += 2) {
        for (k = 0; k < 2; k++) {
            if (arrow_at((int8_t)(x + k), y)) { v[k] = 1; continue; }
            edge = 0;
            for (dy = -1; dy <= 1; dy++) for (dx = -1; dx <= 1; dx++) if (arrow_at((int8_t)(x + k + dx), (int8_t)(y + dy))) edge = 1;
            v[k] = edge ? 2 : 0;
        }
        far_poke(d++, (uint8_t)((v[0] << 4) | v[1]));
    }
    pal(17, 255, 255, 255); pal(18, 0, 0, 0);            /* PALOFS 1: 16 + pixel */
    far_poke(SPRTAB + 4, (uint8_t)SPRDATA); far_poke(SPRTAB + 5, (uint8_t)(SPRDATA >> 8)); far_poke(SPRTAB + 6, (uint8_t)(SPRDATA >> 16)); far_poke(SPRTAB + 7, 0);
    far_poke(SPRTAB + 8, 0x31);                           /* enable, 4 bpp, over the text */
    far_poke(SPRTAB + 9, 0x05);                           /* 16 x 16 */
    far_poke(SPRTAB + 10, 1);
    w32(V_SPRTAB, SPRTAB); REG(V_SPRCTL) = 1;
    chh = (uint8_t)((REG(0xD010) & 0x60) ? 16 : 8);     /* a text row in the mode's pixels: 16 at 640x480 */
}
static void ptr_off(void) { REG(V_SPRCTL) = 0; }
static uint8_t event(void)
{
    uint8_t k, b, r, c; unsigned x, y; int8_t w;
    for (;;) {
        k = rom_getin();
        if (k) { kmod = REG(KSTAT); kcode = (uint8_t)((kmod & 0x40) ? 1 : 0); return k; }
        wait_vblank();
        x = REG(MOUSEX) | ((unsigned)REG(MOUSEX + 1) << 8); y = REG(MOUSEY) | ((unsigned)REG(MOUSEY + 1) << 8);
        far_poke(SPRTAB + 0, (uint8_t)x); far_poke(SPRTAB + 1, (uint8_t)(x >> 8));
        far_poke(SPRTAB + 2, (uint8_t)y); far_poke(SPRTAB + 3, (uint8_t)(y >> 8));
        b = (uint8_t)(REG(MOUSEB) & 1); w = (int8_t)REG(MOUSEW);
        { int ty = (int) y + (int16_t)(REG(0xD014) | (REG(0xD015) << 8));   /* the console's row: its layer is scrolled down by the HD padding */
          r = (uint8_t)(ty < 0 ? 0 : ty / chh); }
        c = (uint8_t)(x >> 3);
        kmod = REG(KSTAT); kcode = 2;
        if (w) { mwheel = w; mev = 4; return KMOUSE; }
        if (b && !mheld) { mheld = 1; mrow = r; mcol = c; mev = 1; return KMOUSE; }
        if (b && (r != mrow || c != mcol)) { mrow = r; mcol = c; mev = 2; return KMOUSE; }
        if (!b && mheld) { mheld = 0; mrow = r; mcol = c; mev = 3; return KMOUSE; }
    }
}

/* ---- drawing ------------------------------------------------------------ */
static void layout(void) { eh = (uint8_t)(rows - 4 - MSGH); }
static void pad(void) { while (sx < (uint8_t)(cols - 1)) put(' '); }
static void sel_order(void)                           /* the selection's ends in file order: qy1,qx1 .. qy2,qx2 */
{
    if (sely < cy || (sely == cy && selx <= cx)) { qy1 = sely; qx1 = selx; qy2 = cy; qx2 = cx; }
    else { qy1 = cy; qx1 = cx; qy2 = sely; qx2 = selx; }
}
static void row_text(const uint8_t *l, unsigned y)    /* line y; what of it is selected, reversed */
{
    uint8_t c, p, w = l[0], a = 255, b = 255, on = 0;
    if (selon && y >= qy1 && y <= qy2) { a = y == qy1 ? qx1 : 0; b = y == qy2 ? qx2 : 255; }
    for (c = 0; (unsigned)(c + hoff) < w && c < cols; c++) {
        p = (uint8_t)(c + hoff);
        if (!on && p >= a && p < b) { sgr("7"); on = 1; }
        else if (on && p >= b) { sgr("0"); on = 0; }
        put((char)l[1 + p]);
    }
    if (b == 255 && a != 255) { if (!on) sgr("7"); if (c < cols) put(' '); sgr("0"); }   /* the line's end is selected too */
    else if (on) sgr("0");
}
static void text_row(uint8_t r)
{
    unsigned l = top + r;
    at((uint8_t)(TEXT0 + r), 0);
    if (l < nlines) {
        if (l == cy) row_text(ln, l);
        else { far_get(SLOT(l), tmp, 256); row_text(tmp, l); }
    }
    eeol();
}

static const char *const mtitle[] = { "File", "Edit", "Search", "Build", "Help" };
struct item { const char *label, *keys; uint8_t cmd; };
static const struct item m_file[]   = { { "New", "^N", C_NEW }, { "New project...", "", C_NEWPROJ }, { "Open...", "^O", C_OPEN }, { "Save", "^S F2", C_SAVE }, { "Save as...", "", C_SAVEAS },
                                        { "Close", "^W", C_CLOSE }, { "Next file", "F6", C_NEXTF }, { "Previous file", "sh-F6", C_PREVF }, { "Quit", "^Q", C_QUIT }, { 0, 0, 0 } };
static const struct item m_edit[]   = { { "Undo", "^Z", C_UNDO }, { "Redo", "^Y", C_REDO }, { "Cut", "^X", C_CUT }, { "Copy", "^C", C_COPY }, { "Paste", "^V", C_PASTE }, { "Select all", "^A", C_SELALL }, { 0, 0, 0 } };
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
    if (pj_name[0]) { put('['); say(pj_name); say("] "); }
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
    if (!nameeq(pj_for, name)) {                      /* another file in front: its project, if it has one */
        uint8_t i;
        if (!pj_scan()) pj_name[0] = 0;
        for (i = 0; name[i] && i < NAMEMAX - 1; i++) pj_for[i] = name[i];
        pj_for[i] = 0;
    }
    layout();
    if (cy < top) top = cy;
    while (cy >= top + eh) top++;
    hoff = (cx >= cols) ? (uint8_t)(cx - cols + 1) : 0;
    if (top != lasttop || hoff != lasthoff) full = 1;
    if (selon || selshown) { full = 1; if (selon) sel_order(); }   /* a selection redraws every row it may have touched */
    selshown = selon;
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

/* ---- the selection ---------------------------------------------------------
 * Stage 4 (Doc, 2026-09-14).  Characters, not lines: from the anchor
 * (sely, selx) to the cursor, either way round.  Shift with a movement key,
 * a drag of the mouse or a Shift-click makes one, ^A takes the whole file.
 * Typing, Enter, Backspace and Delete replace it; ^C and ^X take it into
 * the register as characters and ^V puts them in at the cursor; Tab and
 * Shift-Tab indent the lines it covers.  Any other command drops it. */
static void sel_clear(void) { if (selon) { selon = 0; full = 1; } }
static void sel_start(void) { if (!selon) { selon = 1; sely = cy; selx = cx; } }
static uint8_t sel_delete(void)                       /* the selection out, the cursor where it began; 0 if it would not fit */
{
    unsigned i; uint8_t n, j;
    sel_order(); selon = 0; full = 1;
    t_end(); line_out(cy);
    far_get(SLOT(qy2), tmp, 256);
    if (qx2 > tmp[0]) qx2 = tmp[0];
    n = (uint8_t)(tmp[0] - qx2);                      /* what is left of the last line */
    if ((unsigned)qx1 + n > 255) { note = "the lines would not fit on one"; return 0; }
    u_begin();
    u_line(qy1);
    for (i = qy2; i > qy1; i--) u_del(i);
    far_get(SLOT(qy2), tmp, 256);
    far_get(SLOT(qy1), ln, 256);
    for (j = 0; j < n; j++) ln[qx1 + 1 + j] = tmp[qx2 + 1 + j];
    ln[0] = (uint8_t)(qx1 + n);
    far_put(ln, SLOT(qy1), 256);
    for (i = qy2; i > qy1; i--) close_at(i);
    u_end();
    cy = qy1; cx = qx1; line_in(cy); dirty = 1; wantx = cx;
    return 1;
}
static uint8_t sel_copy(void)                         /* into the register, as characters: 0 if too much */
{
    unsigned i, n; uint8_t a, b, j;
    sel_order(); line_out(cy);
    n = qy2 - qy1 + 1;
    if (n >= REGMAX) { note = "too much to copy"; return 0; }   /* >=: put_chars parks a line after it */
    for (i = 0; i < n; i++) {
        far_get(SLOT(qy1 + i), tmp, 256);
        a = i ? 0 : qx1; b = (qy1 + i == qy2) ? qx2 : tmp[0];
        if (b > tmp[0]) b = tmp[0];
        if (a > b) a = b;
        for (j = 0; j < (uint8_t)(b - a); j++) tmp[1 + j] = tmp[1 + a + j];
        tmp[0] = (uint8_t)(b - a);
        far_put(tmp, RSLOT(i), 256);
    }
    reglines = n; reglinewise = 0;
    return 1;
}
static void put_chars(void)                           /* ^V of characters: in at the cursor, the cursor after them */
{
    unsigned i, n = reglines; uint8_t j, x;
    t_end(); line_out(cy);
    far_get(RSLOT(0), tmp, 256);
    if (n == 1) {
        if ((unsigned)ln[0] + tmp[0] > 255) { note = "the line would be too long"; return; }
        u_begin(); u_line(cy);
        far_get(RSLOT(0), tmp, 256);                  /* u_line used tmp */
        for (j = 0; j < tmp[0]; j++) ins_ch(tmp[1 + j]);
        line_out(cy); u_end(); wantx = cx; full = 1;
        return;
    }
    if ((unsigned)cx + tmp[0] > 255) { note = "the line would be too long"; return; }
    far_get(RSLOT(n - 1), tmp, 256);
    if ((unsigned)tmp[0] + (ln[0] - cx) > 255) { note = "the line would be too long"; return; }
    u_begin(); u_line(cy);
    tmp[0] = (uint8_t)(ln[0] - cx);                   /* the rest of this line, parked after the register's lines */
    for (j = 0; j < tmp[0]; j++) tmp[1 + j] = ln[cx + 1 + j];
    far_put(tmp, RSLOT(n), 256);
    far_get(RSLOT(0), tmp, 256);
    for (j = 0; j < tmp[0]; j++) ln[cx + 1 + j] = tmp[1 + j];
    ln[0] = (uint8_t)(cx + tmp[0]);
    line_out(cy);
    for (i = 1; i < n; i++) { u_ins(cy + i); open_at(cy + i); dma_copy(RSLOT(i), SLOT(cy + i), 256); }
    cy += n - 1; line_in(cy); x = ln[0];
    far_get(RSLOT(n), tmp, 256);
    for (j = 0; j < tmp[0]; j++) ln[x + 1 + j] = tmp[1 + j];
    ln[0] = (uint8_t)(x + tmp[0]);
    line_out(cy);
    u_end(); cx = x; wantx = cx; dirty = 1; full = 1;
}
static void indent(uint8_t out)                       /* Tab over lines, Shift-Tab: the lines the selection covers, or this one */
{
    unsigned y, ya = cy, yb = cy; uint8_t j, k, w = ed_tabw;
    if (selon) { sel_order(); ya = qy1; yb = qy2; if (yb > ya && !qx2) yb--; }
    t_end(); line_out(cy);
    u_begin();
    for (y = ya; y <= yb; y++) {
        u_line(y);                                    /* tmp: the line as it was */
        if (out) {
            for (k = 0; k < w && k < tmp[0] && tmp[1 + k] == ' '; k++) ;
            if (!k) continue;
            for (j = 0; (uint8_t)(j + k) < tmp[0]; j++) tmp[1 + j] = tmp[1 + j + k];
            tmp[0] = (uint8_t)(tmp[0] - k);
        } else {
            if (!tmp[0] || (unsigned)tmp[0] + w > 255) continue;
            for (j = tmp[0]; j; j--) tmp[j + w] = tmp[j];
            for (j = 1; j <= w; j++) tmp[j] = ' ';
            tmp[0] = (uint8_t)(tmp[0] + w);
        }
        far_put(tmp, SLOT(y), 256);
    }
    u_end(); dirty = 1; full = 1;
    line_in(cy);
    if (selon) { sely = ya; selx = 0; goline(yb); cx = ln[0]; }   /* the lines, whole */
    else if (cx > ln[0]) cx = ln[0];
    wantx = cx;
}
static void select_all(void) { t_end(); sely = 0; selx = 0; go(nlines - 1); cx = ln[0]; wantx = cx; selon = 1; full = 1; }

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
    if (prompt("Open: ", ibuf, NAMEMAX) && ibuf[0]) open_path(ibuf);
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
    if (!prompt("New project -- its name (letters and digits): ", ibuf, 17) || !ibuf[0]) { full = 1; return; }
    for (i = 0, k = 0; ibuf[i] && k < 16; i++) {
        char c = (char)rn_up((uint8_t)ibuf[i]);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') nm[k++] = c;
    }
    nm[k] = 0;
    if (!k) { note = "a project needs a name of letters and digits"; full = 1; return; }
    k = ask("C or Pascal? C / P ");
    if (k != 'c' && k != 'p') { full = 1; return; }
    pas = (uint8_t)(k == 'p');
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
    "  Ctrl-X  Ctrl-C  Ctrl-V       cut, copy, paste: the selection, or the line",
    "  Shift+move, a drag or Ctrl-A select; typing replaces; Tab Shift-Tab indent",
    "  Ctrl-F  F3   find, again     Ctrl-R   replace        Ctrl-G   go to line",
    "  Shift-Ctrl-F find in files: every source file in this file's directory",
    "  A PROJECT.K4P beside the file: F9 builds the project (File > New project)",
    "",
    "  F9           save what changed, compile .C (CC) or .PAS (PAS); a .RX: saved",
    "  Ctrl-F9      compile, then run it (a .RX: RX runs it); a key comes back",
    "  F4  Shift-F4 the next / previous message -- another file's opens in a tab",
    "  F10          the menu (arrows, Enter, Esc)        F1  this page",
    "  Mouse: click the text, a menu, a tab, a message; drag selects; wheel scrolls",
    "",
    "  F12 is the machine's own (its menu; Shift+F12 pauses): PROG leaves it alone.",
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
    if (c != C_CUT && c != C_COPY && c != C_PASTE && c != C_NONE) sel_clear();   /* any other command drops the selection */
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
    case C_CUT:    if (!selon) { cut_line(); break; }
                   if (sel_copy() && sel_delete()) note = "cut -- ^V puts it back";
                   break;
    case C_COPY:   if (selon) { if (sel_copy()) note = "copied -- ^V puts it in"; break; }
                   t_end(); line_out(cy); reg_take(cy, 1, 1); note = "line copied -- ^V puts it in"; break;
    case C_PASTE:  if (selon && !sel_delete()) break;
                   t_end(); if (reglinewise || !reglines) do_put(0); else put_chars();
                   break;
    case C_SELALL: select_all(); break;
    case C_FIND:   find_one(); break;
    case C_NEXT:   t_end(); search(1); break;
    case C_FINDF:  find_files(); break;
    case C_REPL:   replace(); break;
    case C_GOTO:   goto_line(); break;
    case C_MAKE:   if (!save_all()) { note = "a file would not save -- nothing compiled"; break; }
                   pj_arm(); do_make(); if (ecur != 0xFFFFu) goto_msg(ecur); msgs_due = 1; break;
    case C_RUN:    if (!save_all()) { note = "a file would not save -- nothing run"; break; }
                   pj_arm(); ptr_off(); do_run(); ptr_on(); reload_others(); if (ecur != 0xFFFFu) goto_msg(ecur); msgs_due = 1; break;
    case C_MNEXT:  t_end(); if (!nerr) note = "no messages -- F9 compiles";
                   else if (ecur + 1 < nerr || ecur == 0xFFFFu) goto_msg(ecur + 1); else note = "no more messages";
                   break;
    case C_MPREV:  t_end(); if (nerr && ecur != 0xFFFFu && ecur > 0) goto_msg(ecur - 1); else note = "no earlier message";
                   break;
    case C_RENUM:  t_end(); do_renum(""); break;
    case C_HELP:   help(); break;
    case C_ABOUT:  note = "PROG, the K4510's programmer's front end -- stage 4: the mouse and selection"; break;
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
static uint8_t title_at(uint8_t c)                    /* the menu whose title is at column c, or NMENU */
{
    uint8_t i, x;
    for (i = 0; i < NMENU; i++) { x = mx(i); if (c >= x && c < (uint8_t)(x + slen(mtitle[i]) + 2)) return i; }
    return NMENU;
}
static uint8_t menu(uint8_t m)                        /* menu m open; the command chosen, or C_NONE */
{
    uint8_t sel = 0, n, k, r, w, x;
    for (;;) {
        w = mwidth(menus[m], &n); x = mx(m);
        menu_draw(m, sel);
        k = event();
        if (kcode == 2) {                             /* the mouse: a title opens its menu, an entry runs, elsewhere closes */
            if (mev == 4) continue;
            if (!mrow) {
                if (mev == 3) continue;
                r = title_at(mcol);
                if (r < NMENU && r != m) { m = r; sel = 0; tabrow(); for (r = 0; r < eh; r++) text_row(r); }
                continue;
            }
            if (mrow <= n && mcol > x && mcol <= (uint8_t)(x + w)) {
                sel = (uint8_t)(mrow - 1);
                if (mev != 2) { full = 1; return menus[m][sel].cmd; }   /* a press, or a drag let go over it */
                continue;
            }
            if (mev == 1) break;
            continue;
        }
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

/* ---- the mouse's clicks ---------------------------------------------------- */
static uint8_t tab_at(uint8_t c)                      /* the open file whose tab is at column c, or 0xFF: tabrow's arithmetic */
{
    uint8_t i, x = 0, w; const char *nm;
    if (pj_name[0]) x = (uint8_t)(slen(pj_name) + 3);
    for (i = 0; i < ed_nbuf; i++) {
        nm = i == ed_cur ? name : ed_bufs[i].name;
        w = (uint8_t)((nm[0] ? slen(base_of(nm)) : 10) + ((i == ed_cur ? dirty : ed_bufs[i].dirty) ? 1 : 0) + 2);
        if (c >= x && c < (uint8_t)(x + w)) return i;
        x = (uint8_t)(x + w + 1);
    }
    return 0xFF;
}
static void place(uint8_t r, uint8_t c)               /* the cursor to a text row and column of the screen */
{
    go(top + (r - TEXT0));
    cx = (uint8_t)(hoff + c); if (cx > ln[0]) cx = ln[0];
    wantx = cx;
}
static void do_mouse(void)
{
    uint8_t r = mrow, i; unsigned d;
    if (mev == 4) {                                   /* the wheel: three lines a notch, the cursor kept on the screen */
        d = (unsigned)(mwheel < 0 ? -mwheel : mwheel) * 3;
        t_end();
        if (mwheel > 0) top = top > d ? top - d : 0;
        else { top += d; if (top + eh > nlines) top = nlines > eh ? nlines - eh : 0; }
        if (cy < top) go(top); else if (cy >= top + eh) go(top + eh - 1);
        if (cx > ln[0]) cx = ln[0];
        full = 1; return;
    }
    if (mev == 3) { dragging = 0; return; }
    if (mev == 2) {                                   /* a drag: the selection follows it, and pulls the text past the edges */
        if (!dragging) return;
        if (r < TEXT0) go(top ? top - 1 : 0);
        else if (r >= TEXT0 + eh) go(top + eh);
        else place(r, mcol);
        selon = (uint8_t)(cy != sely || cx != selx);
        full = 1; return;
    }
    if (!r) { i = title_at(mcol); if (i < NMENU) run_cmd(menu(i)); return; }
    if (r == 1) { i = tab_at(mcol); if (i != 0xFF && i != ed_cur) { sel_clear(); t_end(); switch_to(i); } return; }
    if (r >= TEXT0 && r < TEXT0 + eh) {
        t_end();
        if (kmod & 1) { sel_start(); place(r, mcol); }        /* Shift-click: from where the cursor was */
        else { sel_clear(); place(r, mcol); sely = cy; selx = cx; }
        dragging = 1; return;
    }
    if (r > TEXT0 + eh && r <= TEXT0 + eh + MSGH) {       /* a message: to it */
        d = mtop + (r - TEXT0 - eh - 1);
        if (d < nerr) { sel_clear(); goto_msg(d); msgs_due = 1; }
    }
}

/* ---- the keys ------------------------------------------------------------ */
static void do_key(uint8_t k)
{
    uint8_t ctrl = (uint8_t)(kmod & 2), shift = (uint8_t)(kmod & 1);
    if (kcode == 2) { do_mouse(); return; }
    if (!kcode) {
        switch (k) {
        case 0x0D: if (selon && !sel_delete()) return; enter(); return;
        case 0x08: if (selon) { sel_delete(); return; } backspace(); return;
        case 0x09:                                    /* Tab: spaces to the next stop (set ts= in VI.RC); over lines, indent */
            if (shift) { indent(1); return; }
            if (selon) { sel_order(); if (qy2 > qy1) { indent(0); return; } if (!sel_delete()) return; }
            t_begin(); ed_tab(); wantx = cx; return;
        case 0x1B: sel_clear(); return;
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
        }
        if ((k >= 0x20 && k < 0x7F) || k >= 0x80) { if (selon && !sel_delete()) return; type_ch(k); }   /* an accented letter comes as a character, not a key */
        return;
    }
    if (k >= KUP && k <= KPGDN) { if (shift) sel_start(); else sel_clear(); }   /* Shift and a movement key: select */
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
    case KDEL:   if (selon) sel_delete(); else delete_fwd(); break;
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
    if (is_k4p(name)) {                                   /* PROG GAME/PROJECT.K4P: the project, sources and all */
        char p[NAMEMAX]; uint8_t i;
        for (i = 0; name[i]; i++) p[i] = name[i];
        p[i] = 0;
        name[0] = 0; nlines = 1; cy = 0; ln[0] = 0; line_out(0); dirty = 0;   /* an empty first tab, which open_path reuses */
        open_path(p);
    }
    if (!name[0]) note = "no file yet -- type, then ^S names it; ^O opens one, ^N a new one";
    REG(TERM + 4) = 1; REG(TERM + 4) = 2; REG(TERM + 0x0E) = 1;
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
