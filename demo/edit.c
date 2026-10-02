/* K4510: EDIT [-s] [name] -- the editor, in the manner of MS-DOS 5's EDIT.
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
#include "ed.h"
#include "dosui.h"
#include "dosed.h"

enum { C_NEW = 1, C_OPEN, C_SAVE, C_SAVEAS, C_EXIT, C_UNDO, C_REDO, C_CUT, C_COPY, C_PASTE,
       C_CLEAR, C_SELALL, C_RENUM, C_FIND, C_NEXT, C_CHANGE, C_GOTO, C_DOS, C_SYS, C_HELP, C_ABOUT };

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
    status_line(*note ? note : (const char *)"K4510 Editor  <F1=Help>  <F10 or Alt=Menus>", p);
    window_cursor();
}

/* ---- files -------------------------------------------------------------- */
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
    t_end(); save_file();
    full = 1;
    return saved();
}
static uint8_t save(void)
{
    if (!name[0]) return save_as();
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

/* ---- help --------------------------------------------------------------- */
static const char *const helptext[] = {
    "Moving        arrows, Home, End, PgUp, PgDn; with Ctrl: words, the ends",
    "Selecting     Shift with a moving key; drag, Shift+click; Ctrl+A all",
    "Clipboard     Ctrl+X Ctrl+C Ctrl+V, or Shift+Del Ctrl+Ins Shift+Ins",
    "              (with nothing selected: the line)",
    "Undo, redo    Ctrl+Z, Ctrl+Y           Insert   insert / overwrite",
    "Tab           spaces to the next stop; Tab, Shift+Tab indent a selection",
    "Files         Ctrl+N new, Ctrl+O open, Ctrl+S save, Ctrl+Q exit",
    "Search        Ctrl+F find, F3 again, Ctrl+G go to a line",
    "BASIC         Ctrl+R renumbers (10, 20, 30 and every GOTO)",
    "Menus         F10, or Alt and the letter; Esc closes",
    "Mouse         click the text, a menu, a scroll bar; the wheel scrolls",
    "",
    "EDIT -s FILE  starts in the console's own colours (Options)",
    0 };
static const char *const abouttext[] = { "K4510 Editor", "", "MS-DOS EDIT's manner, VI's engine.", 0 };

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
    case C_FIND:   find_dlg(); break;
    case C_NEXT:   find_next(); break;
    case C_CHANGE: change_dlg(); break;
    case C_GOTO:   goto_dlg(); break;
    case C_DOS:    scheme(0); full = 1; break;
    case C_SYS:    scheme(1); full = 1; break;
    case C_HELP:   text_box("Keyboard", helptext); break;
    case C_ABOUT:  text_box("About", abouttext); break;
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
                                        { "Renumber BASIC", 2, C_RENUM, "Ctrl+R" }, { 0, 0, 0, 0 } };
static const struct item m_search[] = { { "Find...", 0, C_FIND, "Ctrl+F" }, { "Repeat Last Find", 0, C_NEXT, "F3" },
                                        { "Change...", 0, C_CHANGE, "" }, { "Go To Line...", 0, C_GOTO, "Ctrl+G" }, { 0, 0, 0, 0 } };
static const struct item m_opt[]    = { { "DOS Colours", 0, C_DOS, "" }, { "System Colours", 0, C_SYS, "" }, { 0, 0, 0, 0 } };
static const struct item m_help[]   = { { "Keyboard", 0, C_HELP, "F1" }, { "About...", 0, C_ABOUT, "" }, { 0, 0, 0, 0 } };
static const struct item *const menus[] = { m_file, m_edit, m_search, m_opt, m_help };
static uint8_t marked(uint8_t c) { return (uint8_t)((c == C_DOS && !sysc) || (c == C_SYS && sysc)); }

/* ---- the keys and the mouse ------------------------------------------------- */
static void do_key(uint8_t k)
{
    uint8_t i;
    if (kcode == 2) {
        if (window_mouse()) return;
        if (mrow == 0 && mev == 1) { i = title_at(mcol); if (i < ui_nmenu) run_cmd(menu(i)); }
        return;
    }
    if (kcode && k >= KALT && k < KALT + 26) { i = title_of(k); if (i < ui_nmenu) run_cmd(menu(i)); return; }
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
    for (;;) {                                        /* EDIT [-s] [name] */
        while (na && *a == ' ') { a++; na--; }
        if (na >= 2 && a[0] == '-' && (a[1] == 's' || a[1] == 'S') && (na == 2 || a[2] == ' ')) { sys = 1; a += 2; na -= 2; continue; }
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
