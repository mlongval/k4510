/* K4510: EDIT [-s] [-u] [-v] [name] -- the editor, in the manner of MS-DOS 5's EDIT.
 *
 * Doc, 2026-10-02: "a WYSIWYG text editor similar to the one on later
 * releases of MS-DOS with keyboard shortcuts and mouse controls and screen
 * borders", replacing the small EDIT, "use DOS EDIT colours, but offer a
 * command line option to use system colors".  So: a menu bar (F10, Alt and
 * the letter, or the mouse), the text in a framed window with its name in
 * the top border, scroll bars that take the mouse, dialog boxes with
 * shadows, and the status line along the bottom -- in EDIT's blue, grey and
 * cyan, or with -s in the console's own colours.
 *
 * And for BBC BASIC, which knows its keywords only in capitals (print is a
 * variable to it, PRINT the statement): Edit > Uppercase Keywords (Ctrl+U)
 * puts every keyword in the file in capitals, and with -u each save does it
 * first, so a program may be typed in lower case.  BBC's *EDIT passes -u.
 *
 * And VI's keys, for whoever has them in their fingers (Doc, 2026-10-02:
 * "supports VI movements and actions ... off by default"): Options > VI
 * Keys, or -v.  Then EDIT starts in VI's normal mode -- counts, motions,
 * d c y, x p u and the rest are demo/vikeys.h, the very code VI runs -- i a
 * o and their kind type until Esc, and : / ? open a line on the status row.
 * The menus, the mouse, the dialogs and the Ctrl keys stay EDIT's, but for
 * Ctrl+R, Ctrl+U and Ctrl+D, which are VI's while it is in normal mode.
 *
 * Three layers, so PROG and WORD look the same: the text engine is VI's
 * (demo/ed.h), the editing window EDIT's and PROG's (demo/dosed.h), the
 * furniture -- colours, menus, dialogs, the mouse -- all three's
 * (demo/dosui.h).  What is EDIT's own is below: its menus, its commands,
 * one file.
 *
 *   row 0            the menu bar           File Edit Search Options Help
 *   row 1            the window's top: its corners, the file's name
 *   rows 2..rows-3   the text, framed left, the vertical scroll bar right
 *   row rows-2       the window's foot: the horizontal scroll bar
 *   row rows-1       the status line: what just happened, line:column
 *
 * Far memory: $08000000 its lines, $08600000 its undo, $08C00000 the Open
 * dialog's directory.  $08000000-$0BFFFFFF was used by nothing (PROG has
 * $04-$07, BOOK $0C, SPLIT $0D, VI $0E-$0F). */
#include "k4510.h"
static void ui_start(void);
#define ED_SCREEN_BACK() ui_start()
#include "ed.h"
#include "dosui.h"
#include "dosed.h"
static uint8_t vimode;                                /* VI's keys: Options, or -v */
#define DOSVI_P1 0x08D00000UL                        /* the two overlays (demo/edit-header.s): beside the Open list's */
#define DOSVI_P2 0x08D10000UL
#include "dosvi.h"

enum { C_NEW = 1, C_OPEN, C_SAVE, C_SAVEAS, C_EXIT, C_UNDO, C_REDO, C_CUT, C_COPY, C_PASTE,
       C_CLEAR, C_SELALL, C_RENUM, C_UPPER, C_FIND, C_NEXT, C_CHANGE, C_GOTO, C_DOS, C_SYS, C_VI, C_TABW, C_HELP, C_ABOUT };

/* ---- the screen ----------------------------------------------------------- */
/* the top band names the file (core/io.c's title stack, SYS+$44), as PROG */
static void band_file(void)
{
    const char *b = base_of(name); uint8_t i;
    REG(0xD544) = 0;
    for (i = 0; b[i]; i++) REG(0xD544) = (uint8_t)b[i];
}
static void draw(void)
{
    char p[16]; uint8_t i = 0;
    if (full) { band_file(); menubar(-1); frame_top(1, name[0] ? base_of(name) : (const char *)"Untitled", 1); }
    window(); full = 0;
    if (over) { p[0] = 'O'; p[1] = 'V'; p[2] = 'R'; p[3] = ' '; p[4] = ' '; i = 5; }
    where(p + i);
    if (vimode && !*note) { vi_status(); return; }
    status_line(*note ? note : (const char *)"K4510 Editor  <F1=Help>  <F10 or Alt=Menus>", p);
    window_cursor();
}

#pragma code-name (push, "LOCODE")                   /* at $1A00 (demo/edit.cfg): BBC BASIC's keywords */
#pragma rodata-name (push, "LOCODE")
/* ---- BBC BASIC's keywords, in capitals --------------------------------------
 * Only a whole word that IS a keyword changes (BBC BASIC's own table,
 * tube/src/bbmain.c): print -> PRINT, left$( -> LEFT$(, procdraw -> PROCdraw,
 * but total, count% and name$ stay the variables they are.  Strings, the
 * rest of a REM or DATA, a star command and an assembler comment are left
 * as typed. */
static const char kw[] = " AND ABS ACS ADVAL ASC ASN ATN BGET BPUT BY COLOUR COLOR CALL CASE CHAIN CHR$ CLEAR CLOSE CLG CLS COS COUNT"
    " CIRCLE DATA DEG DEF DIV DIM DRAW ENDPROC ENDWHILE ENDCASE ENDIF END ENVELOPE ELSE EVAL ERL ERROR EOF EOR ERR EXIT EXP EXT"
    " ELLIPSE FOR FALSE FILL FN GOTO GET$ GET GOSUB GCOL HIMEM INPUT IF INKEY$ INKEY INT INSTR( INSTALL LINE LOMEM LOCAL LEFT$( LEN"
    " LET LOG LN MID$( MODE MOD MOVE MOUSE NEXT NOT ON OFF OF ORIGIN OR OPENIN OPENOUT OPENUP OSCLI OTHERWISE PRINT PAGE PRIVATE PTR"
    " PI PLOT POINT( PROC POS QUIT RETURN REPEAT REPORT READ REM RUN RAD RESTORE RIGHT$( RND RECTANGLE STEP SGN SIN SQR SPC STR$"
    " STRING$( SOUND STOP SUM SWAP SYS TAN TAB( THEN TIME TINT TO TRACE TRUE UNTIL USR VDU VAL VPOS WHILE WHEN WAIT WIDTH ";
static uint8_t kwb[24], upflag;
static uint8_t kw_is(uint8_t n)                       /* kwb[0..n) one of them? */
{
    const char *k;
    uint8_t i;
    for (k = kw; k[1]; k++) {
        if (*k != ' ') continue;
        for (i = 0; i < n && (uint8_t)k[1 + i] == kwb[i]; i++) ;
        if (i == n && k[1 + n] == ' ') return 1;
    }
    return 0;
}
static uint8_t is_al(uint8_t c) { c = rn_up(c); return (uint8_t)(c >= 'A' && c <= 'Z'); }
static uint8_t is_an(uint8_t c) { return (uint8_t)(is_al(c) || (c >= '0' && c <= '9') || c == '_'); }
#pragma code-name (push, "HICODE")                   /* the pass at $E000: $1A00 is full */
static uint8_t up_line(uint8_t *l)                    /* l: a length, then the text; answers how many words changed */
{
    uint8_t i = 1, e = l[0], s, n, m, c, p, q = 0, asmb = 0, st = 1, hits = 0;
    while (i <= e) {
        c = l[i];
        if (c == '"') { q ^= 1; i++; continue; }
        if (q) { i++; continue; }
        if (c == ':') { st = 1; i++; continue; }
        if (c == '*' && st && !asmb) break;            /* a star command: the OS's, as typed */
        if (c == '[') asmb = 1; else if (c == ']') asmb = 0;
        if (asmb && (c == ';' || c == '\\')) { while (i <= e && l[i] != ':') i++; continue; }
        if (c == '&') { do i++; while (i <= e && is_an(l[i])); st = 0; continue; }   /* hex */
        if (!is_al(c)) { if (c != ' ' && (c < '0' || c > '9')) st = 0; i++; continue; }
        for (s = i; i <= e && is_an(l[i]); i++) ;
        n = (uint8_t)(i - s); st = 0;
        if (n > 20 || (i <= e && (l[i] == '%' || l[i] == '&'))) continue;   /* an integer variable */
        for (m = 0; m < n; m++) kwb[m] = rn_up(l[s + m]);
        if (i <= e && l[i] == '$') kwb[m++] = '$';
        p = 0;
        if (s + m <= e && l[s + m] == '(') { kwb[m] = '('; if (kw_is((uint8_t)(m + 1))) p = n; }
        if (!p && kw_is(m)) p = n;
        if (!p) {                                     /* PROCname, FNname, DEFPROC..., DEFFN... */
            c = (uint8_t)(kwb[0] == 'D' && kwb[1] == 'E' && kwb[2] == 'F' ? 3 : 0);
            if (kwb[c] == 'P' && kwb[c + 1] == 'R' && kwb[c + 2] == 'O' && kwb[c + 3] == 'C' && n > c + 4) p = (uint8_t)(c + 4);
            else if (kwb[c] == 'F' && kwb[c + 1] == 'N' && n > c + 2) p = (uint8_t)(c + 2);
        }
        for (c = 0, m = 0; m < p; m++) if (l[s + m] != kwb[m]) { l[s + m] = kwb[m]; c = 1; }
        hits += c;
        if (p == n && ((n == 3 && kwb[0] == 'R' && kwb[1] == 'E' && kwb[2] == 'M') || (n == 4 && kwb[0] == 'D' && kwb[1] == 'A' && kwb[2] == 'T' && kwb[3] == 'A'))) break;
    }
    return hits;
}
#pragma code-name (pop)
static char unote[40];
static void up_all(void)                              /* the whole file, one undo */
{
    unsigned n, hits = 0; uint8_t h;
    t_end(); line_out(cy); u_begin();
    for (n = 0; n < nlines; n++) {
        far_get(SLOT(n), tmp, 256);
        far_get(SLOT(n), ln, 256);
        if ((h = up_line(ln)) != 0) { u_push(1, n, tmp); far_put(ln, SLOT(n), 256); hits += h; }
    }
    u_end(); line_in(cy);
    if (hits) { dirty = 1; full = 1; }
    nb_reset(); nb_n(hits); nb_s(hits == 1 ? " keyword put in capitals" : " keywords put in capitals");
    for (h = 0; nbuf[h] && h < sizeof unote - 1; h++) unote[h] = nbuf[h];
    unote[h] = 0; note = unote;
}

#pragma rodata-name (pop)
#pragma code-name (pop)


/* ---- files (at $E000, as PROG's) ------------------------------------------ */
#pragma code-name (push, "HICODE")
static char fbuf[NAMEMAX];
static void fresh(void) { ujp = ujn = 0; useq = 0; tgline = 0xFFFFu; cx = 0; top = 0; hoff = 0; dirty = 0; full = 1; wantx = 0; selon = 0; }
static uint8_t saved(void)
{
    if (note[0] == 'w') { note = "Saved"; return 1; }
    note = "The file was not saved"; return 0;
}
static uint8_t save_as(void)
{
    uint8_t i;
    for (i = 0; name[i] && i < NAMEMAX - 1; i++) fbuf[i] = name[i];
    fbuf[i] = 0;
    if (!form1("Save As", "File Name:", fbuf, NAMEMAX, "OK") || !fbuf[0]) return 0;
    for (i = 0; fbuf[i]; i++) name[i] = fbuf[i];
    name[i] = 0;
    if (upflag) up_all();
    t_end(); save_file();
    full = 1;
    return saved();
}
static uint8_t save(void)
{
    if (!name[0]) return save_as();
    if (upflag) up_all();
    t_end(); save_file();
    return saved();
}
static uint8_t may_leave(void)                        /* 1 if the text in front may go */
{
    uint8_t a;
    if (!dirty) return 1;
    a = ask(0, "Loaded file is not saved.  Save it now?", "Yes", "No", "Cancel");
    if (a == 0) return save();
    return (uint8_t)(a == 1);
}
static void load_name(const char *nm)
{
    uint8_t i;
    for (i = 0; nm[i] && i < NAMEMAX - 1; i++) name[i] = nm[i];
    name[i] = 0;
    nlines = 1; cy = 0;
    load_file();
    fresh();
    if (note[0] == 'n') note = "A new file";
}
#pragma code-name (pop)

/* ---- help ---------------------------------------------------------------
 * The keys page and About at $E000, lines and all (2026-10-05: the main image
 * was full once the furniture drew through JIM).  Each text is one array --
 * lines ended by NUL, the whole by $FF -- because a string literal goes to
 * the main image whatever the pragma says, and a named array does not. */
#pragma code-name (push, "HICODE")
#pragma rodata-name (push, "HICODE")
static const char helpall[] =
    "Moving        arrows, Home, End, PgUp, PgDn; with Ctrl: words, the ends\0"
    "Selecting     Shift with a moving key; drag, Shift+click; Ctrl+A all\0"
    "Clipboard     Ctrl+X Ctrl+C Ctrl+V, or Shift+Del Ctrl+Ins Shift+Ins\0"
    "              (with nothing selected: the line)\0"
    "Undo, redo    Ctrl+Z, Ctrl+Y           Insert   insert / overwrite\0"
    "Tab           spaces to the next stop; Tab, Shift+Tab indent a selection\0"
    "              (Options > Tab Width: how many spaces; never a tab character)\0"
    "Files         Ctrl+N new, Ctrl+O open, Ctrl+S save, Ctrl+Q exit\0"
    "Search        Ctrl+F find, F3 again, Ctrl+G go to a line\0"
    "BASIC         Ctrl+R renumbers (10, 20, 30 and every GOTO)\0"
    "              Ctrl+U puts BBC BASIC's keywords in capitals\0"
    "Menus         F10, or Alt and the letter; Esc closes\0"
    "Mouse         click the text, a menu, a scroll bar; the wheel scrolls\0"
    "\0"
    "EDIT -s FILE  starts in the console's own colours (Options)\0"
    "EDIT -u FILE  keywords in capitals at every save (BBC's *EDIT)\0"
    "EDIT -v FILE  VI's keys (Options): Esc, hjkl w b e, d c y, x p u, :w :q\0"
    "\xFF";
static const char aboutall[] =
    "K4510 Editor\0"
    "\0"
    "MS-DOS EDIT's manner, VI's engine.\0"
    "\xFF";
static void help(uint8_t about)
{
    const char *l[24], *p = about ? aboutall : helpall; uint8_t n = 0;
    while (*p != '\xFF' && n < 23) { l[n++] = p; while (*p++) ; }
    l[n] = 0;
    text_box(about ? "About" : "Keyboard", l);
}
#pragma rodata-name (pop)
#pragma code-name (pop)

/* ---- the commands ---------------------------------------------------------
 * Every key and every menu entry is a command number, run by one switch, so
 * a menu can never do something its key does not (PROG's rule). */
static void run_cmd(uint8_t c)
{
    if (c != C_CUT && c != C_COPY && c != C_PASTE && c != C_CLEAR && c != C_NONE) sel_clear();
    switch (c) {
    case C_NEW:    if (may_leave()) { name[0] = 0; load_name(""); note = ""; } break;
    case C_OPEN:   if (may_leave() && open_dialog(fbuf)) load_name(fbuf); full = 1; break;
    case C_SAVE:   save(); break;
    case C_SAVEAS: save_as(); break;
    case C_EXIT:   if (may_leave()) running = 0; break;
    case C_UNDO:   t_end(); u_apply(0); break;
    case C_REDO:   t_end(); u_apply(1); break;
    case C_CUT:    do_cut(); break;
    case C_COPY:   do_copy(); break;
    case C_PASTE:  do_paste(); break;
    case C_CLEAR:  do_clear(); break;
    case C_SELALL: select_all(); break;
    case C_RENUM:  t_end(); do_renum(""); break;
    case C_UPPER:  up_all(); break;
    case C_FIND:   find_dlg(); break;
    case C_NEXT:   find_next(); break;
    case C_CHANGE: change_dlg(); break;
    case C_GOTO:   goto_dlg(); break;
    case C_DOS:    scheme(0); full = 1; break;
    case C_SYS:    scheme(1); full = 1; break;
    case C_VI:     vi_toggle(); break;
    case C_TABW:   tabw_dlg(); break;
    case C_HELP:   help(0); break;
    case C_ABOUT:  help(1); break;
    }
    wantx = cx;
}

/* ---- the menus ------------------------------------------------------------ */
static const char *const mtitle[] = { "File", "Edit", "Search", "Options", "Help" };
static const struct item m_file[]   = { { "New", 0, C_NEW, "Ctrl+N" }, { "Open...", 0, C_OPEN, "Ctrl+O" }, { "Save", 0, C_SAVE, "Ctrl+S" },
                                        { "Save As...", 5, C_SAVEAS, "" }, { "", 0, C_SEP, "" }, { "Exit", 1, C_EXIT, "Ctrl+Q" }, { 0, 0, 0, 0 } };
static const struct item m_edit[]   = { { "Undo", 0, C_UNDO, "Ctrl+Z" }, { "Redo", 0, C_REDO, "Ctrl+Y" }, { "", 0, C_SEP, "" },
                                        { "Cut", 2, C_CUT, "Shift+Del" }, { "Copy", 0, C_COPY, "Ctrl+Ins" }, { "Paste", 0, C_PASTE, "Shift+Ins" },
                                        { "Clear", 2, C_CLEAR, "Del" }, { "Select All", 7, C_SELALL, "Ctrl+A" }, { "", 0, C_SEP, "" },
                                        { "Renumber BASIC", 2, C_RENUM, "Ctrl+R" },
                                        { "Uppercase Keywords", 10, C_UPPER, "Ctrl+U" }, { 0, 0, 0, 0 } };
static const struct item m_search[] = { { "Find...", 0, C_FIND, "Ctrl+F" }, { "Repeat Last Find", 0, C_NEXT, "F3" },
                                        { "Change...", 0, C_CHANGE, "" }, { "Go To Line...", 0, C_GOTO, "Ctrl+G" }, { 0, 0, 0, 0 } };
static const struct item m_opt[]    = { { "DOS Colours", 0, C_DOS, "" }, { "System Colours", 0, C_SYS, "" }, { "", 0, C_SEP, "" },
                                        { "VI Keys", 0, C_VI, "" }, { "Tab Width...", 0, C_TABW, "" }, { 0, 0, 0, 0 } };
static const struct item m_help[]   = { { "Keyboard", 0, C_HELP, "F1" }, { "About...", 0, C_ABOUT, "" }, { 0, 0, 0, 0 } };
static const struct item *const menus[] = { m_file, m_edit, m_search, m_opt, m_help };
static uint8_t marked(uint8_t c) { return (uint8_t)((c == C_DOS && !sysc) || (c == C_SYS && sysc) || (c == C_VI && vimode)); }

/* ---- the keys and the mouse ------------------------------------------------- */
static void vi_do(void)                               /* what a : command asked of EDIT itself (dosvi.h's vi_act) */
{
    uint8_t i;
    switch (vi_act) {
    case 'w': case 'x':
        if (*vi_arg) { for (i = 0; vi_arg[i] && i < NAMEMAX - 1; i++) name[i] = vi_arg[i]; name[i] = 0; full = 1; }
        if (!save() || vi_act == 'w') break;          /* :x saved: on to leaving */
    case 'q': if (dirty) { note = "Not saved -- :q! leaves anyway, :wq saves"; break; }
    case 'Q': running = 0; break;
    case 'm': case 'r': case 'n': case 'p': note = "That is PROG's: EDIT does not compile"; break;
    }
}
static void do_key(uint8_t k)
{
    uint8_t i;
    if (kcode == 2) {
        if (window_mouse()) { if (vimode) vi_clamp(); return; }
        if (mrow == 0 && mev == 1) { i = title_at(mcol); if (i < ui_nmenu) run_cmd(menu(i)); }
        return;
    }
    if (kcode && k >= KALT && k < KALT + 26) { i = title_of(k); if (i < ui_nmenu) run_cmd(menu(i)); return; }
    if (vimode && (k < KF(1) || k > KF(12) || !kcode) && vi_key(k)) { vi_do(); return; }
    if (!kcode) switch (k) {
        case 0x01: run_cmd(C_SELALL); return;        /* ^A */
        case 0x0E: run_cmd(C_NEW); return;           /* ^N */
        case 0x0F: run_cmd(C_OPEN); return;          /* ^O */
        case 0x13: run_cmd(C_SAVE); return;          /* ^S */
        case 0x11: run_cmd(C_EXIT); return;          /* ^Q */
        case 0x1A: run_cmd(C_UNDO); return;          /* ^Z */
        case 0x19: run_cmd(C_REDO); return;          /* ^Y */
        case 0x18: run_cmd(C_CUT); return;           /* ^X */
        case 0x03: run_cmd(C_COPY); return;          /* ^C */
        case 0x16: run_cmd(C_PASTE); return;         /* ^V */
        case 0x06: run_cmd(C_FIND); return;          /* ^F */
        case 0x07: run_cmd(C_GOTO); return;          /* ^G */
        case 0x12: run_cmd(C_RENUM); return;         /* ^R: renumber a BASIC file, as the old EDIT did */
        case 0x15: run_cmd(C_UPPER); return;         /* ^U: BBC BASIC's keywords in capitals */
    }
    if (window_key(k)) return;
    switch (k) {
    case KF(1):  run_cmd(C_HELP); break;
    case KF(3):  run_cmd(C_NEXT); break;
    case KF(10): run_cmd(menu(0)); break;
    }
}

void main(void)
{
    uint8_t k, na = rom_args(), j = 0, sys = 0; const char *a = *(const char **)0xF0;
    for (;;) {                                        /* EDIT [-s] [-u] [name] */
        while (na && *a == ' ') { a++; na--; }
        if (na >= 2 && a[0] == '-' && (na == 2 || a[2] == ' ')) {
            if (a[1] == 's' || a[1] == 'S') { sys = 1; a += 2; na -= 2; continue; }
            if (a[1] == 'u' || a[1] == 'U') { upflag = 1; a += 2; na -= 2; continue; }
            if (a[1] == 'v' || a[1] == 'V') { vimode = 1; a += 2; na -= 2; continue; }
        }
        break;
    }
    while (j < na && j < NAMEMAX - 1 && a[j] != ' ') { name[j] = a[j]; j++; }
    name[j] = 0;
    ui_init();
    ui_titles = mtitle; ui_menus = menus; ui_nmenu = 5; ui_marked = marked; ui_dirtab = 0x08C00000UL;
    wy = 2; th = (uint8_t)(rows - 4); tw = (uint8_t)(cols - 2);
    scheme(sys);
    ed_maxlines = 16384u;
    ed_slots = 0x08000000UL; ed_undo = 0x08600000UL;
    ed_rc_tabw();                                     /* VI's set ts=N, one tab width for all three editors */
    load_file();
    fresh();
    note = name[0] ? (note[0] == 'n' ? "A new file" : "") : "";
    vi_setup(); vi_init();                            /* VI.RC's maps (:imap jk <Esc>), for when VI's keys are on */
    ui_start(); cursor_show(1);
    ptr_on();
    while (running) {
        vi_setup();                                   /* the gate's table: VI's keys are overlays (dosvi.h) */
        draw();
        k = vi_event();
        if (kcode != 2 || mev == 1) note = "";
        do_key(k);
    }
    ptr_off();
    ui_end();                                         /* the block cursor, JIM's modes and an empty screen for the shell */
    rom_video();
}
