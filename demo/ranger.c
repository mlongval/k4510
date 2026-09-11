/* K4510: RANGER -- a miller-column file browser, after the Unix `ranger`.
 *
 * KOMMANDER is this machine's Norton Commander: two panels, function keys,
 * you drive it with your eyes on the panels.  RANGER is the other tradition.
 * Three columns -- where you came from, where you are, what you are pointing
 * at -- and vi's fingers: hjkl to walk, yy/dd/pp to move files about, and no
 * function keys at all.  Neither replaces the other; they are different ideas
 * about what a file manager is for, and this machine can afford both.
 *
 * The three columns are ranger's whole argument.  The middle one is the
 * directory you are in.  The left is its parent, with the directory you came
 * through highlighted, so going up is never a leap in the dark.  The right is
 * a preview of whatever the bar is on: a directory's contents if it is a
 * directory, the first lines of the file if it is a file.  You can therefore
 * see three levels of the tree at once and move through it without ever
 * opening anything.
 *
 *   h  Left        up to the parent      j k  Down Up   move the bar
 *   l  Right       into a directory      gg G          top / bottom
 *   Enter          directory: descend;   .prg or .com: leave and run it;
 *                  any other file: edit it in VI (through SWAP)
 *   Space          mark / unmark         .             show or hide dotfiles
 *   yy             yank (copy)           dd            cut (move)
 *   pp             paste here            DD            delete to /.TRASH
 *   r              rename                m             make a directory
 *   q  Esc         leave -- and the shell is left in the directory you ended
 *                  in, which is the point of the whole exercise
 *
 * Two-key sequences are ranger's, not decoration: yy/dd/pp/DD/gg all want a
 * deliberate second press, and DD in particular should never be one keystroke
 * away from a full directory.
 *
 * Nothing here is destructive in the way a delete usually is.  DD does not
 * remove anything: it RENAMEs it into /.TRASH, which is one directory move on
 * the device and is instantly undone by walking in there and moving it back.
 * A name already taken in the trash gets ~1, ~2 appended rather than
 * overwriting what is there -- a trash that eats the thing you deleted
 * yesterday is not a trash.
 *
 * The screen is drawn straight into VICKY's text32 map, four bytes a cell, the
 * same way KOMMANDER does it -- and, like KOMMANDER, the console's origin is
 * read from JIM at $DA07/$DA08 rather than assumed, because the F7 menu can
 * turn the one-cell margin off underneath us.
 */
#include "k4510.h"

/* ---- the storage device at $D300 (core/io.h) --------------------------- */
#define FS        0xD300u
#define FS_CMD    (FS + 0x00)
#define FS_ST     (FS + 0x01)
#define FS_NAME   (FS + 0x04)
#define FS_ADDR   (FS + 0x08)
#define FS_LEN    (FS + 0x0C)
#define FS_SIZE   (FS + 0x10)
#define C_OPEN_R  1
#define C_READ    3
#define C_CLOSE   5
#define C_DIR1    6
#define C_DIRN    7
#define C_STAT    8
#define C_SAVE    10
#define C_CHDIR   11
#define C_MKDIR   12
#define C_GETCWD  15
#define C_RENAME  16
#define C_COPY    17
#define C_DIRALL  18

#define TERM      0xDA00u
#define SCREEN    0x00030000UL
#define TRASH     "/.TRASH"

/* ---- keys -------------------------------------------------------------- */
#define K_ENTER 0x0D
#define K_BS    0x08
#define K_ESC   0x1B
#define K_UP    0x80
#define K_DOWN  0x81
#define K_LEFT  0x82
#define K_RIGHT 0x83
#define K_HOME  0x84
#define K_END   0x85
#define K_PGUP  0x86
#define K_PGDN  0x87

/* ---- colours (VIC-II palette indices) ---------------------------------- */
#define CBG       6      /* blue: the field                        */
#define FILEFG   15      /* light grey: a file                     */
#define DIRFG     1      /* white: a directory                     */
#define DIMFG    12      /* grey: the parent column, preview text  */
#define MARKFG    7      /* yellow: a marked entry                 */
#define BAR_FG    0      /* the bar: black on cyan                 */
#define BAR_BG    3
#define HDRFG     0      /* header/status: black on cyan           */
#define HDRBG     3
#define KEYFG     7      /* a key name in the hint line            */

void __fastcall__ rom_chrout(unsigned char c);
unsigned char rom_getin(void);
static void rom_video(void) { ((void (*)(void))0xFF92)(); }
static unsigned char rom_args(void) { return ((unsigned char (*)(void))0xFF95)(); }

/* ---- screen ------------------------------------------------------------ */
static uint8_t cols, rows, ox, oy, pcols;

static uint32_t rr32(uint16_t r) { return (uint32_t)REG(r) | ((uint32_t)REG(r+1)<<8) | ((uint32_t)REG(r+2)<<16) | ((uint32_t)REG(r+3)<<24); }

static void putcell(uint8_t x, uint8_t y, uint8_t g, uint8_t fg, uint8_t bg)
{
    uint32_t a = SCREEN + ((uint32_t)((uint16_t)(oy + y)) * pcols + ox + x) * 4;
    far_poke16(a, g);
    far_poke16(a + 2, (uint16_t)fg | ((uint16_t)bg << 8));
}
static void draw_str(uint8_t x, uint8_t y, const char *s, uint8_t w, uint8_t fg, uint8_t bg)
{
    uint8_t i;
    for (i = 0; i < w; i++) {
        char c = *s;
        if (!c) { for (; i < w; i++) putcell(x + i, y, ' ', fg, bg); return; }
        putcell(x + i, y, (uint8_t)c, fg, bg); s++;
    }
}

/* ---- the device -------------------------------------------------------- */
static uint8_t fs_do(uint8_t cmd) { REG(FS_CMD) = cmd; return REG(FS_ST); }
static void fs_name(const char *s) { w32(FS_NAME, (uint32_t)(uint16_t)s); }
static void fs_addr(uint32_t a)    { w32(FS_ADDR, a); }

/* ---- state ------------------------------------------------------------- */
/* The three columns are not the same shape, and sizing them alike wasted most
 * of a .prg.  The middle one is the one you scroll, so it carries full names
 * and as many entries as KOMMANDER.  The parent column is a sixth of the
 * screen wide, so short names do; it still needs every entry, because the
 * directory we came through may be anywhere in it.  The preview is never
 * scrolled -- you only ever see the top of it -- so a screenful is all it can
 * ever show, and all it therefore stores. */
#define MAXENT 192                         /* the current directory      */
#define NAMEW   28
#define PMAX   192                         /* the parent column          */
#define PNAMEW  16
#define VMAX    32                         /* the preview: one screenful */
#define VNAMEW  24
#define PATHW  160
#define MAXREG  32
#define PREVL   28
#define PREVW   40
#define PREVBUF 1024                       /* head of a file, cut into lines */

static char     names[MAXENT][NAMEW];      /* the current directory  */
static uint32_t sizes[MAXENT];
static uint8_t  marks[MAXENT];
static int      count, cur, top;

static char     pnames[PMAX][PNAMEW];      /* the parent directory   */
static uint32_t psizes[PMAX];
static int      pcount, pcur;

static char     vnames[VMAX][VNAMEW];      /* the preview, when it is a directory */
static uint32_t vsizes[VMAX];
static int      vcount;
static char     vtext[PREVL][PREVW];       /* the preview, when it is a file      */
static char     vbuf[PREVBUF];
static int      vlines;
static uint8_t  vis_dir;

static char     path[PATHW] = "/";
static char     ppath[PATHW];
static char     tbuf[PATHW];
static char     tbuf2[PATHW];
static uint8_t  show_hidden;
static uint8_t  ncols = 3;                 /* columns shown: 3, 2 (no parent) or 1 (no preview) */

static char     reg[MAXREG][NAMEW];        /* the yank register  */
static char     regdir[PATHW];
static int      regn;
static uint8_t  regcut;

static uint8_t  is_dir_sz(uint32_t s) { return s == 0xFFFFFFFFUL; }

static void u32dec(uint32_t v, char *out)
{
    char t[12]; uint8_t n = 0, i = 0;
    if (!v) { out[0] = '0'; out[1] = 0; return; }
    while (v) { t[n++] = (char)('0' + (uint8_t)(v % 10)); v /= 10; }
    while (n) out[i++] = t[--n];
    out[i] = 0;
}

/* ---- paths ------------------------------------------------------------- */
/* join dir and name into out, without doubling the slash at the root */
static void join(char *out, const char *dir, const char *nm)
{
    uint8_t l;
    strcpy(out, dir); l = (uint8_t)strlen(out);
    if (l && out[l - 1] != '/') { out[l] = '/'; out[l + 1] = 0; }
    strcat(out, nm);
}
/* out = the parent of p; returns 0 if p is the root (which has none) */
static uint8_t parent_of(char *out, const char *p)
{
    int i, last = -1;
    if (p[0] == '/' && !p[1]) return 0;
    strcpy(out, p);
    for (i = 0; out[i]; i++) if (out[i] == '/') last = i;
    if (last <= 0) { strcpy(out, "/"); return 1; }
    out[last] = 0;
    return 1;
}
/* the last component of p */
static const char *base_of(const char *p)
{
    int i, last = -1;
    for (i = 0; p[i]; i++) if (p[i] == '/') last = i;
    return p + last + 1;
}
static uint8_t chdir_to(const char *p) { fs_name(p); return fs_do(C_CHDIR); }
/* Does this path already name something?  STAT answers 0 when it does.
 * Everything that writes a file has to ask FIRST: the device's RENAME and COPY
 * take the host's semantics and overwrite an existing name without a word, so
 * "did the operation fail?" is not a collision test.  The trash learned this
 * the hard way -- two files deleted under one name, and the first was gone. */
static uint8_t exists(const char *p) { fs_name(p); return fs_do(C_STAT) == 0; }

/* ---- listing ----------------------------------------------------------- */
/* Read directory dir into nm/sz, at most MAXENT entries.  Leaves the device's
 * cwd on dir: every caller either wants that or puts it back itself. */
static int listdir(const char *dir, char *nm, uint8_t stride, uint32_t *sz, int max)
{
    int n = 0;
    if (chdir_to(dir)) return 0;
    fs_addr((uint32_t)(uint16_t)tbuf);
    if (!fs_do(show_hidden ? C_DIRALL : C_DIR1)) {
        while (n < max) {
            char *d = nm + (uint16_t)n * stride;
            fs_addr((uint32_t)(uint16_t)tbuf);
            if (fs_do(C_DIRN)) break;
            { uint8_t i = 0; while (tbuf[i] && i < stride - 1) { d[i] = tbuf[i]; i++; } d[i] = 0; }
            sz[n] = rr32(FS_SIZE);
            n++;
        }
    }
    return n;
}

static void clear_marks(void) { int i; for (i = 0; i < MAXENT; i++) marks[i] = 0; }

static void relist(void)
{
    count = listdir(path, names[0], NAMEW, sizes, MAXENT);
    if (cur >= count) cur = count ? count - 1 : 0;
    if (cur < 0) cur = 0;
    clear_marks();
}

/* The parent column, with the bar sitting on the directory we came through. */
static void relist_parent(void)
{
    int i;
    pcount = 0; pcur = 0;
    if (!parent_of(ppath, path)) return;
    pcount = listdir(ppath, pnames[0], PNAMEW, psizes, PMAX);
    for (i = 0; i < pcount; i++)
        if (!strncmp(pnames[i], base_of(path), PNAMEW - 1)) { pcur = i; break; }
    chdir_to(path);                       /* put the cwd back where the rest of us expects it */
}

/* The preview column: a directory's entries, or a file's first lines. */
static void preview(void)
{
    vcount = 0; vlines = 0; vis_dir = 0;
    if (!count) return;
    if (is_dir_sz(sizes[cur])) {
        vis_dir = 1;
        join(tbuf2, path, names[cur]);
        vcount = listdir(tbuf2, vnames[0], VNAMEW, vsizes, VMAX);
        chdir_to(path);
        return;
    }
    /* a file: read the head of it and cut it into lines */
    fs_name(names[cur]);
    if (fs_do(C_OPEN_R)) return;
    fs_addr((uint32_t)(uint16_t)vbuf);
    w32(FS_LEN, (uint32_t)PREVBUF);
    if (!fs_do(C_READ)) {
        uint32_t got = rr32(FS_LEN);
        uint16_t i, c = 0;
        int l = 0;
        for (i = 0; i < (uint16_t)got && l < PREVL; i++) {
            uint8_t ch = (uint8_t)vbuf[i];
            if (ch == '\n' || c == PREVW - 1) { vtext[l][c] = 0; l++; c = 0; if (ch != '\n' && ch != '\r') { if (l < PREVL) vtext[l][c++] = (char)ch; } continue; }
            if (ch == '\r') continue;
            if (ch < 32 || ch > 126) ch = '.';
            vtext[l][c++] = (char)ch;
        }
        if (l < PREVL && c) { vtext[l][c] = 0; l++; }
        vlines = l;
    }
    fs_do(C_CLOSE);
}

static void refresh(void) { relist(); relist_parent(); preview(); }

/* ---- drawing ----------------------------------------------------------- */
/* the three column origins and widths, worked out from the console's width */
static uint8_t px, pw, cx, cw, vx, vw;
static uint8_t listh;                      /* rows available to a column */

static void layout(void)
{
    /* The columns ABUT: every cell of the row belongs to exactly one of them.
     * Leaving a one-cell gap between them looks tidier in the source and is a
     * bug on the screen -- nothing draws the gap, so it keeps whatever the
     * last program left there.  Each column's own first cell is its blank
     * left edge (names are drawn at x + 1), which is the separator.
     *
     * Three columns is the ranger view; two (no parent) is the default since 2026-09-11.  It is not always the
     * right one: in MODE 2 the console is forty columns, and three of them
     * would be thirteen characters each -- a listing you cannot read of a
     * directory you cannot see.  So the count is an option (c cycles it, or
     * give it as an argument: RANGER 2), and a narrow screen starts lower. */
    px = 0;
    pw = ncols >= 3 ? (uint8_t)(cols / 6) : 0;
    cx = pw;
    cw = ncols >= 2 ? (uint8_t)((cols - pw) * 2 / 5) : (uint8_t)(cols - pw);
    if (ncols >= 3) cw = (uint8_t)(cols / 3);
    vx = (uint8_t)(cx + cw);
    vw = ncols >= 2 ? (uint8_t)(cols - vx) : 0;
    if (!vw) cw = (uint8_t)(cols - cx);
    listh = (uint8_t)(rows - 3);           /* header, status, hints */
}

static void draw_header(void)
{
    char n[12];
    uint8_t l;
    draw_str(0, 0, " ", cols, HDRFG, HDRBG);
    draw_str(1, 0, path, (uint8_t)(cols - 2), HDRFG, HDRBG);
    if (regn) {                            /* what is on the clipboard, at the right */
        u32dec((uint32_t)regn, n);
        strcpy(tbuf2, regcut ? " cut " : " copied ");
        strcat(tbuf2, n);
        l = (uint8_t)strlen(tbuf2);
        if (l + 2 < cols) draw_str((uint8_t)(cols - l - 1), 0, tbuf2, l, HDRFG, HDRBG);
    }
}

/* one list column.  sel < 0 means "no bar in this column". */
static void draw_col(uint8_t x, uint8_t w, const char *nm, uint8_t stride, uint32_t *sz, int n,
                     int sel, int first, uint8_t dim, uint8_t showmarks)
{
    uint8_t r;
    for (r = 0; r < listh; r++) {
        int i = first + r;
        uint8_t fg, bg = CBG;
        if (i >= n) { draw_str(x, (uint8_t)(r + 1), "", w, FILEFG, CBG); continue; }
        fg = is_dir_sz(sz[i]) ? DIRFG : FILEFG;
        if (dim) fg = DIMFG;
        if (showmarks && marks[i]) fg = MARKFG;
        if (i == sel) { fg = BAR_FG; bg = BAR_BG; }
        draw_str(x, (uint8_t)(r + 1), "", w, fg, bg);
        draw_str((uint8_t)(x + 1), (uint8_t)(r + 1), nm + (uint16_t)i * stride, (uint8_t)(w - 1), fg, bg);
        if (is_dir_sz(sz[i]) && w > 2) putcell((uint8_t)(x + w - 1), (uint8_t)(r + 1), '/', fg, bg);
    }
}

static void draw_preview(void)
{
    uint8_t r;
    if (!count) { for (r = 0; r < listh; r++) draw_str(vx, (uint8_t)(r + 1), "", vw, DIMFG, CBG); return; }
    if (vis_dir) { draw_col(vx, vw, vnames[0], VNAMEW, vsizes, vcount, -1, 0, 1, 0); return; }
    for (r = 0; r < listh; r++)
        draw_str((uint8_t)(vx + 1), (uint8_t)(r + 1), r < vlines ? vtext[r] : "", (uint8_t)(vw - 1), DIMFG, CBG);
    for (r = 0; r < listh; r++) putcell(vx, (uint8_t)(r + 1), ' ', DIMFG, CBG);
}

static void draw_status(const char *msg)
{
    char n[12];
    uint8_t y = (uint8_t)(rows - 2);
    draw_str(0, y, "", cols, FILEFG, CBG);
    if (msg) { draw_str(1, y, msg, (uint8_t)(cols - 2), MARKFG, CBG); return; }
    if (count) {
        strcpy(tbuf2, names[cur]);
        if (!is_dir_sz(sizes[cur])) { strcat(tbuf2, "  "); u32dec(sizes[cur], n); strcat(tbuf2, n); strcat(tbuf2, " bytes"); }
        else strcat(tbuf2, "  <dir>");
    } else strcpy(tbuf2, "(empty)");
    draw_str(1, y, tbuf2, (uint8_t)(cols - 12), FILEFG, CBG);
    u32dec((uint32_t)count, n);
    draw_str((uint8_t)(cols - 10), y, n, 9, DIMFG, CBG);
}

static void draw_hints(void)
{
    static const char *h = "hjkl  Enter open  yy dd pp  DD trash  r name  m mkdir  c cols  . hidden  q";
    draw_str(0, (uint8_t)(rows - 1), "", cols, HDRFG, HDRBG);
    draw_str(1, (uint8_t)(rows - 1), h, (uint8_t)(cols - 2), HDRFG, HDRBG);
}

static void scroll_into_view(void)
{
    if (cur < top) top = cur;
    if (cur >= top + listh) top = cur - listh + 1;
    if (top < 0) top = 0;
}

static void draw_all(void)
{
    int ptop = pcur - listh / 2; if (ptop < 0) ptop = 0;
    scroll_into_view();
    draw_header();
    if (pw) draw_col(px, pw, pnames[0], PNAMEW, psizes, pcount, pcur, ptop, 1, 0);
    draw_col(cx, cw, names[0], NAMEW, sizes, count, cur, top, 0, 1);
    if (vw) draw_preview();
    draw_status(0);
    draw_hints();
}

static uint8_t getkey(void) { uint8_t k; do { k = rom_getin(); } while (!k); return k; }

static void message(const char *m) { draw_status(m); }

/* a one-line prompt on the status row; returns 0 if cancelled */
static uint8_t prompt(const char *label, char *buf, uint8_t max)
{
    uint8_t y = (uint8_t)(rows - 2), l = (uint8_t)strlen(label), n = (uint8_t)strlen(buf);
    for (;;) {
        uint8_t k;
        draw_str(0, y, "", cols, BAR_FG, BAR_BG);
        draw_str(1, y, label, l, BAR_FG, BAR_BG);
        draw_str((uint8_t)(1 + l), y, buf, (uint8_t)(cols - l - 2), BAR_FG, BAR_BG);
        putcell((uint8_t)(1 + l + n), y, '_', BAR_FG, BAR_BG);
        k = getkey();
        if (k == K_ENTER) return n ? 1 : 0;
        if (k == K_ESC)   return 0;
        if (k == K_BS)    { if (n) buf[--n] = 0; continue; }
        if (k >= 32 && k < 127 && n < max) { buf[n++] = (char)k; buf[n] = 0; }
    }
}
static uint8_t confirm(const char *q)
{
    uint8_t y = (uint8_t)(rows - 2), k;
    draw_str(0, y, "", cols, BAR_FG, BAR_BG);
    draw_str(1, y, q, (uint8_t)(cols - 2), BAR_FG, BAR_BG);
    k = getkey();
    return k == 'y' || k == 'Y';
}

/* ---- the yank register ------------------------------------------------- */
/* Whatever is marked, or the entry under the bar if nothing is. */
static void take(uint8_t cut)
{
    int i;
    regn = 0;
    for (i = 0; i < count && regn < MAXREG; i++)
        if (marks[i]) strcpy(reg[regn++], names[i]);
    if (!regn && count) strcpy(reg[regn++], names[cur]);
    strcpy(regdir, path);
    regcut = cut;
    clear_marks();
    draw_all();
    message(cut ? "cut" : "copied");
}

static void paste(void)
{
    int i; uint8_t bad = 0, skipped = 0;
    if (!regn) { message("nothing to paste"); return; }
    if (!strcmp(regdir, path) && regcut) { message("already here"); return; }
    for (i = 0; i < regn; i++) {
        join(tbuf,  regdir, reg[i]);       /* source, absolute        */
        join(tbuf2, path,   reg[i]);       /* destination, absolute   */
        /* Never overwrite.  The device would do it silently, and a file
         * manager that eats a file because two directories happened to agree
         * on a name is worse than one that refuses. */
        if (exists(tbuf2)) { skipped++; continue; }
        fs_name(tbuf); fs_addr((uint32_t)(uint16_t)tbuf2);
        if (fs_do(regcut ? C_RENAME : C_COPY)) bad++;
    }
    if (regcut && !skipped && !bad) regn = 0;   /* a cut is spent only once it all lands */
    chdir_to(path);
    refresh(); draw_all();
    if (skipped) message("some names are already here -- nothing was overwritten");
    else message(bad ? "some entries could not be pasted" : "pasted");
}

/* ---- delete to the trash ----------------------------------------------- */
/* Not a delete: a rename into /.TRASH, which is undone by walking in there
 * and moving it back.  A name already taken gets ~1, ~2 rather than
 * overwriting -- a trash that eats yesterday's file is not a trash. */
static uint8_t to_trash(const char *nm)
{
    uint8_t n;
    fs_name(TRASH); fs_do(C_MKDIR);        /* if it is already there, fine */
    join(tbuf, path, nm);
    join(tbuf2, TRASH, nm);
    for (n = 1; exists(tbuf2) && n < 100; n++) {     /* taken: nm~1, nm~2, ... */
        char d[12];
        u32dec((uint32_t)n, d);
        join(tbuf2, TRASH, nm); strcat(tbuf2, "~"); strcat(tbuf2, d);
    }
    if (exists(tbuf2)) return 1;                     /* a hundred of them already */
    fs_name(tbuf); fs_addr((uint32_t)(uint16_t)tbuf2);
    return fs_do(C_RENAME) ? 1 : 0;
}

static void do_trash(void)
{
    int i, n = 0; uint8_t bad = 0;
    for (i = 0; i < count; i++) if (marks[i]) n++;
    if (!n && !count) { message("nothing to delete"); return; }
    if (!n) {
        strcpy(tbuf2, "Move "); strcat(tbuf2, names[cur]); strcat(tbuf2, " to the trash?  y/n");
        if (!confirm(tbuf2)) { draw_all(); return; }
        bad = to_trash(names[cur]);
    } else {
        char d[12];
        u32dec((uint32_t)n, d);
        strcpy(tbuf2, "Move "); strcat(tbuf2, d); strcat(tbuf2, " marked entries to the trash?  y/n");
        if (!confirm(tbuf2)) { draw_all(); return; }
        for (i = 0; i < count; i++) if (marks[i] && to_trash(names[i])) bad = 1;
    }
    chdir_to(path);
    refresh(); draw_all();
    message(bad ? "some entries could not be moved" : "moved to " TRASH);
}

/* ---- rename and mkdir -------------------------------------------------- */
static void do_rename(void)
{
    static char nn[NAMEW];
    if (!count) { message("nothing to rename"); return; }
    strcpy(nn, names[cur]);
    if (!prompt(" rename: ", nn, (uint8_t)(NAMEW - 1))) { draw_all(); return; }
    if (!strcmp(nn, names[cur])) { draw_all(); return; }
    join(tbuf, path, names[cur]);
    join(tbuf2, path, nn);
    if (exists(tbuf2)) { draw_all(); message("that name is taken"); return; }
    fs_name(tbuf); fs_addr((uint32_t)(uint16_t)tbuf2);
    { uint8_t st = fs_do(C_RENAME);
      chdir_to(path); refresh(); draw_all();
      if (st) message("rename failed"); }
}

static void do_mkdir(void)
{
    static char nn[NAMEW];
    nn[0] = 0;
    if (!prompt(" new directory: ", nn, (uint8_t)(NAMEW - 1))) { draw_all(); return; }
    join(tbuf, path, nn);
    fs_name(tbuf);
    { uint8_t st = fs_do(C_MKDIR);
      chdir_to(path); refresh(); draw_all();
      if (st) message("could not make it"); }
}

/* ---- moving about ------------------------------------------------------ */
static void go_up(void)
{
    char was[NAMEW];
    int i;
    if (!parent_of(tbuf, path)) return;
    strcpy(was, base_of(path));
    strcpy(path, tbuf);
    cur = 0; top = 0;
    refresh();
    for (i = 0; i < count; i++) if (!strcmp(names[i], was)) { cur = i; break; }   /* land on where we were */
    preview();
    draw_all();
}

static void go_in(void)
{
    if (!count || !is_dir_sz(sizes[cur])) return;
    join(tbuf, path, names[cur]);
    strcpy(path, tbuf);
    cur = 0; top = 0;
    refresh(); draw_all();
}

/* ---- Enter on a program: get out of the way and let the shell run it ---- *
 * A program cannot be started from in here.  A .prg loads over the top of
 * RANGER, and even if it did not, its output belongs on the shell's screen
 * and not under the redraw that would follow.  So RANGER leaves, and TYPES
 * the command at the prompt it returns to: the keyboard has a type-ahead
 * register (a write to $D100 pushes a key), which is the C64's keyboard
 * buffer and costs the ROM nothing.  What you see afterwards is exactly what
 * you would have seen had you typed the name yourself.
 *
 * A .COM is a Z80 program on CP/M's drives.  The CCP takes one line, but it
 * auto-submits a .SUB when the command is not a .COM, so a K-RUN.SUB carrying
 * a drive change, the program, and an EXIT brings the machine back to this
 * prompt afterwards -- the trick the ROM's own try_com uses.  The submit file
 * is read on A: in whatever user area is current, so a submit may change
 * DRIVE but never USER: for a .COM outside user 0, CP/M is opened AT it
 * instead (`CPM E3:`) and you type the name at the CCP.
 */
#define KBDQ 0xD100u                       /* write: type-ahead */
static char runline[36];                   /* typed at the shell on the way out */
/* 1 = .prg, 2 = .com, 0 = an ordinary file */
static uint8_t prog_kind(const char *n)
{
    uint8_t i = 0, a, b, c;
    while (n[i]) i++;
    if (i < 5 || n[i - 4] != '.') return 0;
    a = n[i - 3] | 0x20; b = n[i - 2] | 0x20; c = n[i - 1] | 0x20;
    if (a == 'p' && b == 'r' && c == 'g') return 1;
    if (a == 'c' && b == 'o' && c == 'm') return 2;
    return 0;
}
/* /CPM/<drive>/<user>/... -- the CP/M coordinates of the directory we are in */
static uint8_t cpm_du(char *drive, uint8_t *user)
{
    const char *p = path;
    uint8_t c;
    if (p[0] != '/' || (p[1] | 0x20) != 'c' || (p[2] | 0x20) != 'p' || (p[3] | 0x20) != 'm' || p[4] != '/') return 0;
    p += 5;
    *drive = (char)(*p & 0xDF);
    if (*drive < 'A' || *drive > 'P' || p[1] != '/') return 0;
    p += 2;
    c = (uint8_t)(*p | 0x20);
    if (p[1]) return 0;                                  /* deeper than the user directory */
    if (c >= '0' && c <= '9') { *user = (uint8_t)(c - '0'); return 1; }
    if (c >= 'a' && c <= 'f') { *user = (uint8_t)(c - 'a' + 10); return 1; }
    return 0;
}
static uint8_t start_com(void)
{
    char d; uint8_t u, i = 0, j = 0;
    const char *n = names[cur];
    if (!cpm_du(&d, &u)) return 0;
    if (u) {                                             /* no submit can follow a user change */
        strcpy(runline, "CPM ");
        runline[4] = d; runline[5] = (char)(u < 10 ? '0' + u : 'A' + u - 10);
        runline[6] = ':'; runline[7] = 0;
        return 1;
    }
    if (d != 'A') { tbuf[i++] = d; tbuf[i++] = ':'; tbuf[i++] = '\r'; tbuf[i++] = '\n'; }
    while (n[j] && n[j] != '.') tbuf[i++] = n[j++];
    tbuf[i++] = '\r'; tbuf[i++] = '\n';
    strcpy(tbuf + i, "EXIT\r\n"); i += 6;
    tbuf[i++] = 26;                                      /* ^Z: CP/M's end of file */
    strcpy(tbuf2, "/CPM/A/0/K-RUN.SUB");
    fs_name(tbuf2); fs_addr((uint32_t)(uint16_t)tbuf); w32(FS_LEN, (uint32_t)i);
    if (fs_do(C_SAVE)) return 0;
    strcpy(runline, "CPM K-RUN");
    return 1;
}
static uint8_t start_prog(uint8_t kind)
{
    if (kind == 2) return start_com();
    strcpy(runline, names[cur]);
    return 1;
}
static void type_ahead(const char *s)
{
    while (*s) REG(KBDQ) = (uint8_t)*s++;
    REG(KBDQ) = 0x0D;
}

/* ---- open a file: VI, through the shell's SWAP ------------------------- */
/* SWAP saves our whole $0000-$FFFF and the screen to far memory, runs the
 * editor over the top, and restores us.  The command line must NOT live in
 * our own image -- SWAP loads the editor over $6000 and the shell leaves
 * args_tail pointing into the line, so it is built at $0300, the low page the
 * ROM reserves for programs, below the editor and carried across the swap.
 * (KOMMANDER learned this the hard way; the note is in demo/kommander.c.) */
#define CMDLINE ((char *)0x0300)
static void open_file(void)
{
    if (!count) return;
    chdir_to(path);                        /* VI resolves a bare name against the cwd */
    strcpy(CMDLINE, "SWAP VI ");
    strcat(CMDLINE, names[cur]);
    rom_shell(CMDLINE);
    chdir_to(path);
    refresh(); draw_all();
}

/* ---- main -------------------------------------------------------------- */
int main(void)
{
    uint8_t running = 1, pending = 0;

    { uint8_t na = rom_args(); const char *a = *(const char **)0xF0;
      while (na && *a == ' ') { a++; na--; }                 /* RANGER 2 -- how many columns */
      if (na && *a >= '1' && *a <= '3') ncols = (uint8_t)(*a - '0'); else ncols = 0; }

    cols = REG(TERM + 5); rows = REG(TERM + 6); ox = REG(TERM + 7); oy = REG(TERM + 8); pcols = REG(TERM + 0x0D);
    if (!cols) cols = 80; if (!rows) rows = 30; if (!pcols) pcols = 80;
    /* No argument: two columns -- where you are and the preview.  The parent
     * column is ranger's classic third, but Doc (the Dell, 2026-09-11) never
     * wanted the list of directories above the one he is in: `RANGER 3` or the
     * cycle key bring it back.  A very narrow console still gets one column. */
    if (!ncols) ncols = cols < 34 ? 1 : 2;
    layout();

    fs_addr((uint32_t)(uint16_t)path); fs_do(C_GETCWD);      /* start where the shell is */
    if (!path[0]) strcpy(path, "/");
    refresh();
    draw_all();

    while (running) {
        uint8_t k = getkey();
        uint8_t p = pending;
        pending = 0;

        if (p) {                                             /* the second key of a pair */
            if (p == 'g' && k == 'g') { cur = 0; top = 0; preview(); draw_all(); continue; }
            if (p == 'y' && k == 'y') { take(0); continue; }
            if (p == 'd' && k == 'd') { take(1); continue; }
            if (p == 'p' && k == 'p') { paste(); continue; }
            if (p == 'D' && k == 'D') { do_trash(); continue; }
            draw_all();                                      /* anything else cancels it */
            if (k == p) continue;
        }

        switch (k) {
        case 'h': case K_LEFT:  go_up(); break;
        case 'l': case K_RIGHT: go_in(); break;
        case 'j': case K_DOWN:  if (cur + 1 < count) { cur++; preview(); draw_all(); } break;
        case 'k': case K_UP:    if (cur) { cur--; preview(); draw_all(); } break;
        case K_PGDN: cur += listh; if (cur >= count) cur = count ? count - 1 : 0; preview(); draw_all(); break;
        case K_PGUP: cur -= listh; if (cur < 0) cur = 0; preview(); draw_all(); break;
        case 'G': case K_END:  cur = count ? count - 1 : 0; preview(); draw_all(); break;
        case K_HOME: cur = 0; preview(); draw_all(); break;
        case K_ENTER:
            if (!count) break;
            if (is_dir_sz(sizes[cur])) go_in();
            else { uint8_t kd = prog_kind(names[cur]);
                   if (kd && start_prog(kd)) running = 0; else open_file(); }
            break;
        case ' ':
            if (count) { marks[cur] = !marks[cur]; if (cur + 1 < count) cur++; preview(); draw_all(); }
            break;
        case '.': show_hidden ^= 1; refresh(); draw_all(); break;
        case 'c':                                        /* 3 -> 2 -> 1 -> 3 */
            ncols = ncols > 1 ? (uint8_t)(ncols - 1) : 3;
            layout(); scroll_into_view(); preview(); draw_all();
            break;
        case 'r': do_rename(); break;
        case 'm': do_mkdir(); break;
        case 'g': case 'y': case 'd': case 'p': case 'D': pending = k; break;
        case K_ESC: case 'q': running = 0; break;
        }
    }

    /* Leave the shell in the directory we ended up in -- the whole point of
     * driving a file manager rather than typing CD. */
    chdir_to(path);
    REG(TERM + 4) = 2;                     /* JIM: clear and home, a clean screen for the shell */
    rom_video();
    if (runline[0]) type_ahead(runline);   /* Enter on a program: the shell types it */
    return 0;
}
