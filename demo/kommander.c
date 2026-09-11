/* K4510: KOMMANDER -- a two-panel file commander, the Norton Commander of
 * this machine. Two directory panels side by side; Tab switches which is live,
 * the arrows walk it, Enter descends a directory or views a file, and the
 * function keys copy, move, make and delete.
 *
 * A .prg cannot simply jsr another .prg -- both load at $6000, so running EDIT
 * over ourselves would crash on its return. File operations therefore go
 * straight to the storage device at $D300 (the same chip LOAD and DIR ride on),
 * and View is our own pager. But EDIT and VI we DO reach, through the shell's
 * SWAP: it DMAs our whole $0000-$FFFF and the screen out to far memory, runs
 * the editor over the top, and DMAs us back -- the same trick that lets EhBASIC
 * run a program and return. So F4 hands the selected file to VI or EDIT and
 * comes home with the panels intact.
 *
 * The screen is drawn straight into VICKY's text32 map at $030000: four bytes
 * a cell (glyph low, glyph high, foreground, background), so every cell gets
 * its own colour and the panels can carry the blue-and-cyan look a file
 * commander is supposed to have. The frame glyphs are the console font's CP437
 * line-drawing set, the same ones the F7 menu draws its borders with.
 *
 *   Tab            switch the active panel        Enter   descend / view
 *   arrows         move the selection             Esc     leave
 *   F3 View        page through a file            F4 Edit  VI or EDIT (via SWAP)
 *   Enter          directory: descend;  .prg or .com: leave and run it;
 *                  any other file: view it
 *   F5 Copy        to the other panel             F6 Move  move / rename
 *   F9 MkDir       make a directory               F10 Del  delete
 *   F2 Refresh     re-read both panels            .        show/hide dotfiles
 */
#include "k4510.h"

/* ---- the storage device at $D300 (core/io.h) --------------------------- */
#define FS        0xD300u
#define FS_CMD    (FS + 0x00)
#define FS_ST     (FS + 0x01)
#define FS_NAME   (FS + 0x04)      /* 28-bit pointer to a NUL-terminated name  */
#define FS_ADDR   (FS + 0x08)      /* 28-bit RAM address for READ/DIRNEXT/dest */
#define FS_LEN    (FS + 0x0C)      /* 32-bit bytes requested / done            */
#define FS_SIZE   (FS + 0x10)      /* 32-bit size; $FFFFFFFF means a directory */
#define C_OPEN_R  1
#define C_READ    3
#define C_CLOSE   5
#define C_DIR1    6
#define C_DIRN    7
#define C_STAT    8
#define C_LOAD    9
#define C_CHDIR   11
#define C_MKDIR   12
#define C_RM      13
#define C_RMDIR   14
#define C_GETCWD  15
#define C_RENAME  16
#define C_COPY    17
#define C_DIRALL  18

/* ---- JIM, the VT100 at $DA00: only its geometry and the exit-clear ------ */
#define TERM      0xDA00u

/* ---- key codes (core/io.h) --------------------------------------------- */
#define K_ENTER 0x0D
#define K_BS    0x08
#define K_TAB   0x09
#define K_ESC   0x1B
#define K_UP    0x80
#define K_DOWN  0x81
#define K_LEFT  0x82
#define K_RIGHT 0x83
#define K_HOME  0x84
#define K_END   0x85
#define K_PGUP  0x86
#define K_PGDN  0x87
#define K_DEL   0x89
#define K_F1    0x90               /* F1..F12 = $90..$9B; F7/F8 never arrive  */

/* ---- the text32 screen ------------------------------------------------- */
#define SCREEN  0x00030000UL

/* VIC-II palette indices */
#define CBG      6                 /* blue: the panel field                   */
#define FILEFG  15                 /* light grey: a file                      */
#define DIRFG    1                 /* white: a directory                      */
#define FRFG     1                 /* white: the frame                        */
#define HDR_AFG  0                 /* active panel title: black on cyan       */
#define HDR_ABG  3
#define HDR_IFG  1                 /* idle panel title: white on blue         */
#define HDR_IBG  6
#define BAR_AFG  0                 /* active selection: black on cyan         */
#define BAR_ABG  3
#define BAR_IFG  0                 /* idle selection: black on grey           */
#define BAR_IBG 12
#define FN_LFG   0                 /* function bar: black label on cyan       */
#define FN_BG    3
#define FN_KFG   7                 /* the key itself in yellow                */

void __fastcall__ rom_chrout(unsigned char c);
unsigned char rom_getin(void);
static unsigned char rom_args(void) { return ((unsigned char (*)(void))0xFF95)(); }
static void rom_video(void) { ((void (*)(void))0xFF92)(); }

/* geometry, read from JIM once run_at has seeded it with the console's */
static uint8_t cols, rows, ox, oy, pcols;

static uint32_t rr32(uint16_t r) { return (uint32_t)REG(r) | ((uint32_t)REG(r+1)<<8) | ((uint32_t)REG(r+2)<<16) | ((uint32_t)REG(r+3)<<24); }

/* two far_poke16s lay a whole cell: glyph (high byte 0) then fg|bg<<8 */
static void putcell(uint8_t x, uint8_t y, uint8_t g, uint8_t fg, uint8_t bg)
{
    uint32_t a = SCREEN + ((uint32_t)((uint16_t)(oy + y)) * pcols + ox + x) * 4;
    far_poke16(a, g);
    far_poke16(a + 2, (uint16_t)fg | ((uint16_t)bg << 8));
}
/* a string clipped/padded to exactly w cells */
static void draw_str(uint8_t x, uint8_t y, const char *s, uint8_t w, uint8_t fg, uint8_t bg)
{
    uint8_t i;
    for (i = 0; i < w; i++) {
        char c = *s;
        if (!c) { for (; i < w; i++) putcell(x + i, y, ' ', fg, bg); return; }
        putcell(x + i, y, (uint8_t)c, fg, bg); s++;
    }
}

/* ---- the storage device ------------------------------------------------ */
static uint8_t fs_do(uint8_t cmd) { REG(FS_CMD) = cmd; return REG(FS_ST); }
static void fs_name(const char *s) { w32(FS_NAME, (uint32_t)(uint16_t)s); }
static void fs_addr(uint32_t a)    { w32(FS_ADDR, a); }

/* ---- the two panels ---------------------------------------------------- */
#define MAXENT 192
#define NAMEW  30
static char     names[2][MAXENT][NAMEW];
static uint32_t sizes[2][MAXENT];
static int      count[2], cur[2], top[2];
static char     ppath[2][192];
static uint8_t  active, show_hidden;
static char     tbuf[256];                 /* scratch for one entry name / cwd */

static void u32dec(uint32_t v, char *out)
{
    char t[12]; uint8_t n = 0, i = 0;
    if (!v) { out[0] = '0'; out[1] = 0; return; }
    while (v) { t[n++] = (char)('0' + (uint8_t)(v % 10)); v /= 10; }
    while (n) out[i++] = t[--n];
    out[i] = 0;
}
static uint8_t is_dir(uint8_t p, int i) { return sizes[p][i] == 0xFFFFFFFFUL; }
static uint8_t is_updir(uint8_t p, int i) { return names[p][i][0] == '.' && names[p][i][1] == '.' && !names[p][i][2]; }

static void set_cwd(uint8_t p) { fs_name(ppath[p]); fs_do(C_CHDIR); }

/* read the current directory into panel p. Assumes -- and leaves -- the
 * device's cwd on ppath[p]. */
static void relist(uint8_t p)
{
    int n = 0;
    set_cwd(p);
    if (!(ppath[p][0] == '/' && !ppath[p][1])) {         /* not root: offer .. */
        names[p][0][0] = '.'; names[p][0][1] = '.'; names[p][0][2] = 0;
        sizes[p][0] = 0xFFFFFFFFUL; n = 1;
    }
    fs_addr((uint32_t)(uint16_t)tbuf);
    if (!fs_do(show_hidden ? C_DIRALL : C_DIR1)) {
        while (n < MAXENT) {
            fs_addr((uint32_t)(uint16_t)tbuf);
            if (fs_do(C_DIRN)) break;                    /* status 4: end of dir */
            { uint8_t i = 0; while (tbuf[i] && i < NAMEW - 1) { names[p][n][i] = tbuf[i]; i++; } names[p][n][i] = 0; }
            sizes[p][n] = rr32(FS_SIZE);
            n++;
        }
    }
    count[p] = n;
    if (cur[p] >= n) cur[p] = n ? n - 1 : 0;
    if (cur[p] < 0) cur[p] = 0;
    if (top[p] > cur[p]) top[p] = cur[p];
}
static void relist_both(void) { relist(0); relist(1); set_cwd(active); }

static void getcwd_into(uint8_t p)
{
    REG(FS + 0x18) = sizeof ppath[0];                          /* GETCWD's CAP: all ppath can hold */
    fs_addr((uint32_t)(uint16_t)tbuf);
    fs_do(C_GETCWD);
    { uint8_t i = 0; while (tbuf[i] && i < (uint8_t)(sizeof ppath[0] - 1)) { ppath[p][i] = tbuf[i]; i++; } ppath[p][i] = 0; }
}

/* ---- drawing ----------------------------------------------------------- */
static char rowbuf[96];
static void make_row(uint8_t p, int idx, uint8_t iw)
{
    const char *nm = names[p][idx];
    char sb[12]; uint8_t i, sl; int room;
    for (i = 0; i < iw; i++) rowbuf[i] = ' ';
    rowbuf[iw] = 0;
    room = (int)iw - 9; if (room < 1) room = (int)iw - 1; if (room < 0) room = 0;
    for (i = 0; nm[i] && (int)i < room; i++) rowbuf[1 + i] = nm[i];
    if (is_dir(p, idx)) { sb[0] = '<'; strcpy(sb + 1, is_updir(p, idx) ? "UP>" : "DIR>"); }
    else u32dec(sizes[p][idx], sb);
    sl = (uint8_t)strlen(sb);
    if (iw > sl + 1) for (i = 0; i < sl; i++) rowbuf[iw - 1 - sl + i] = sb[i];
}

static void draw_panel(uint8_t p)
{
    uint8_t half = cols / 2;
    uint8_t px = p ? half : 0;
    uint8_t pw = p ? (uint8_t)(cols - half) : half;
    uint8_t ph = (uint8_t)(rows - 1);            /* the last row is the function bar */
    uint8_t iw = (uint8_t)(pw - 2);
    uint8_t vis = (uint8_t)(ph - 2);
    uint8_t x, y, hfg, hbg; int i;

    /* frame */
    putcell(px, 0, 0xDA, FRFG, CBG); putcell((uint8_t)(px + pw - 1), 0, 0xBF, FRFG, CBG);
    putcell(px, (uint8_t)(ph - 1), 0xC0, FRFG, CBG); putcell((uint8_t)(px + pw - 1), (uint8_t)(ph - 1), 0xD9, FRFG, CBG);
    for (x = 1; x < pw - 1; x++) { putcell((uint8_t)(px + x), 0, 0xC4, FRFG, CBG); putcell((uint8_t)(px + x), (uint8_t)(ph - 1), 0xC4, FRFG, CBG); }
    for (y = 1; y < ph - 1; y++) { putcell(px, y, 0xB3, FRFG, CBG); putcell((uint8_t)(px + pw - 1), y, 0xB3, FRFG, CBG); }

    /* the path, in a highlighted title bar between the top corners */
    hfg = (p == active) ? HDR_AFG : HDR_IFG; hbg = (p == active) ? HDR_ABG : HDR_IBG;
    for (x = 1; x < pw - 1; x++) putcell((uint8_t)(px + x), 0, ' ', hfg, hbg);
    { uint8_t fl = (uint8_t)(pw - 4), plen = (uint8_t)strlen(ppath[p]);
      if (plen <= fl) draw_str((uint8_t)(px + 2), 0, ppath[p], plen, hfg, hbg);
      else { putcell((uint8_t)(px + 2), 0, '<', hfg, hbg);
             draw_str((uint8_t)(px + 3), 0, ppath[p] + (plen - (fl - 1)), (uint8_t)(fl - 1), hfg, hbg); } }

    /* keep the selection on screen */
    if (cur[p] < top[p]) top[p] = cur[p];
    if (cur[p] >= top[p] + vis) top[p] = cur[p] - vis + 1;

    for (i = 0; i < vis; i++) {
        int idx = top[p] + i;
        y = (uint8_t)(1 + i);
        if (idx < count[p]) {
            uint8_t fg, bg;
            make_row(p, idx, iw);
            if (idx == cur[p]) {
                if (p == active) { fg = BAR_AFG; bg = BAR_ABG; } else { fg = BAR_IFG; bg = BAR_IBG; }
            } else { fg = is_dir(p, idx) ? DIRFG : FILEFG; bg = CBG; }
            draw_str((uint8_t)(px + 1), y, rowbuf, iw, fg, bg);
        } else draw_str((uint8_t)(px + 1), y, "", iw, FILEFG, CBG);
    }

    /* item count, centred in the bottom border */
    { char cb[24]; uint8_t l; u32dec((uint32_t)count[p], tbuf); strcpy(cb, " "); strcat(cb, tbuf); strcat(cb, " items ");
      l = (uint8_t)strlen(cb);
      if (l + 2 < pw) draw_str((uint8_t)(px + (pw - l) / 2), (uint8_t)(ph - 1), cb, l, FRFG, CBG); }
}

static void draw_bar(void)
{
    static const char *k[] = { "Tab", "F3", "F4", "F5", "F6", "F9", "F10", "Esc" };
    static const char *l[] = { "Panel", "View", "Edit", "Copy", "Move", "MkDir", "Del", "Quit" };
    uint8_t y = (uint8_t)(rows - 1), x, i;
    int total = 0, startx;
    for (x = 0; x < cols; x++) putcell(x, y, ' ', FN_LFG, FN_BG);
    for (i = 0; i < 8; i++) total += (int)strlen(k[i]) + 1 + (int)strlen(l[i]) + 1;   /* KEY:Label<sp> */
    startx = ((int)cols - total) / 2; if (startx < 0) startx = 0;
    x = (uint8_t)startx;
    for (i = 0; i < 8; i++) {
        uint8_t kl = (uint8_t)strlen(k[i]), ll = (uint8_t)strlen(l[i]);
        if (x + kl + 1 + ll >= cols) break;
        draw_str(x, y, k[i], kl, FN_KFG, FN_BG); x += kl;
        putcell(x++, y, ':', FN_LFG, FN_BG);
        draw_str(x, y, l[i], ll, FN_LFG, FN_BG); x += ll + 1;
    }
}

static void draw_all(void)
{
    uint8_t x, y;
    for (y = 0; y < rows; y++) for (x = 0; x < cols; x++) putcell(x, y, ' ', FILEFG, CBG);
    draw_panel(0); draw_panel(1); draw_bar();
}

/* ---- modal boxes ------------------------------------------------------- */
static uint8_t getkey(void) { uint8_t k; do { k = rom_getin(); } while (!k); return k; }

static void box(uint8_t bx, uint8_t by, uint8_t bw, uint8_t bh, const char *title)
{
    uint8_t x, y;
    for (y = 0; y < bh; y++) for (x = 0; x < bw; x++) putcell((uint8_t)(bx + x), (uint8_t)(by + y), ' ', FRFG, CBG);
    putcell(bx, by, 0xDA, FRFG, CBG); putcell((uint8_t)(bx + bw - 1), by, 0xBF, FRFG, CBG);
    putcell(bx, (uint8_t)(by + bh - 1), 0xC0, FRFG, CBG); putcell((uint8_t)(bx + bw - 1), (uint8_t)(by + bh - 1), 0xD9, FRFG, CBG);
    for (x = 1; x < bw - 1; x++) { putcell((uint8_t)(bx + x), by, 0xC4, FRFG, CBG); putcell((uint8_t)(bx + x), (uint8_t)(by + bh - 1), 0xC4, FRFG, CBG); }
    for (y = 1; y < bh - 1; y++) { putcell(bx, (uint8_t)(by + y), 0xB3, FRFG, CBG); putcell((uint8_t)(bx + bw - 1), (uint8_t)(by + y), 0xB3, FRFG, CBG); }
    if (title) { uint8_t tl = (uint8_t)strlen(title); if (tl + 2 < bw) draw_str((uint8_t)(bx + (bw - tl) / 2), by, title, tl, HDR_AFG, HDR_ABG); }
}

static void message(const char *m)
{
    uint8_t bw = (uint8_t)(strlen(m) + 6); uint8_t bx, by;
    if (bw > cols - 2) bw = (uint8_t)(cols - 2);
    bx = (uint8_t)((cols - bw) / 2); by = (uint8_t)(rows / 2 - 1);
    box(bx, by, bw, 3, 0);
    draw_str((uint8_t)(bx + 3), (uint8_t)(by + 1), m, (uint8_t)(bw - 4), FRFG, CBG);
    getkey();
    draw_all();
}
static uint8_t confirm(const char *m)
{
    uint8_t bw = (uint8_t)(strlen(m) + 6); uint8_t bx, by, k;
    if (bw < 22) bw = 22; if (bw > cols - 2) bw = (uint8_t)(cols - 2);
    bx = (uint8_t)((cols - bw) / 2); by = (uint8_t)(rows / 2 - 2);
    box(bx, by, bw, 4, " Confirm ");
    draw_str((uint8_t)(bx + 3), (uint8_t)(by + 1), m, (uint8_t)(bw - 4), FRFG, CBG);
    draw_str((uint8_t)(bx + 3), (uint8_t)(by + 2), "Y = yes, any other = no", (uint8_t)(bw - 4), FILEFG, CBG);
    k = getkey();
    draw_all();
    return k == 'y' || k == 'Y';
}
/* edit buf in place; returns 1 on Enter, 0 on Esc */
static uint8_t prompt(const char *title, char *buf, uint8_t maxlen)
{
    uint8_t bw = (uint8_t)(cols - 8), bx = 4, by = (uint8_t)(rows / 2 - 2);
    uint8_t fx = (uint8_t)(bx + 2), fw = (uint8_t)(bw - 4), len = (uint8_t)strlen(buf);
    box(bx, by, bw, 4, title);
    for (;;) {
        uint8_t k, off = len >= fw ? (uint8_t)(len - fw + 1) : 0;
        draw_str(fx, (uint8_t)(by + 1), buf + off, fw, HDR_AFG, 15);        /* black on light grey field */
        if (len - off < fw) putcell((uint8_t)(fx + (len - off)), (uint8_t)(by + 1), '_', HDR_AFG, 15);
        k = getkey();
        if (k == K_ENTER) { draw_all(); return 1; }
        if (k == K_ESC)   { draw_all(); return 0; }
        if (k == K_BS)    { if (len) buf[--len] = 0; continue; }
        if (k >= 0x20 && k < 0x7F && len < maxlen) { buf[len++] = (char)k; buf[len] = 0; }
    }
}

/* ---- the file viewer (F3) ---------------------------------------------- */
#define VBUF ((char *)0x0800)
#define VMAX 0x5000UL                   /* $0800-$57FF, everything under the program */
static unsigned v_bol(char *b, unsigned o) { while (o && b[o - 1] != '\n') o--; return o; }
static unsigned v_eol(char *b, unsigned n, unsigned o) { while (o < n && b[o] != '\n') o++; return o; }

static void view_file(void)
{
    unsigned n, vtop = 0, o;
    uint8_t st, y, running = 1;
    fs_name(names[active][cur[active]]);
    fs_addr(0x00000800UL);
    w32(FS_LEN, VMAX);
    st = fs_do(C_LOAD);
    if (st) { message("Cannot open file"); return; }
    n = (unsigned)rr32(FS_LEN);
    if ((uint32_t)n > VMAX) n = (unsigned)VMAX;
    while (running) {
        /* header */
        for (y = 0; y < cols; y++) putcell(y, 0, ' ', HDR_AFG, HDR_ABG);
        draw_str(1, 0, names[active][cur[active]], (uint8_t)(cols - 20), HDR_AFG, HDR_ABG);
        draw_str((uint8_t)(cols - 18), 0, "Esc/Q: back", 17, HDR_AFG, HDR_ABG);
        /* body */
        o = vtop;
        for (y = 1; y < rows; y++) {
            uint8_t x = 0;
            unsigned e = v_eol(VBUF, n, o);
            if (o <= n) {
                while (o + x < e && x < cols) {
                    char c = VBUF[o + x];
                    putcell(x, y, (c >= 0x20 && c < 0x7F) ? (uint8_t)c : (c == '\t' ? ' ' : '.'), FILEFG, CBG);
                    x++;
                }
            }
            for (; x < cols; x++) putcell(x, y, ' ', FILEFG, CBG);
            o = e < n ? e + 1 : n + 1;
        }
        { uint8_t k = getkey();
          switch (k) {
          case K_ESC: case 'q': case 'Q': running = 0; break;
          case K_DOWN: { unsigned e = v_eol(VBUF, n, vtop); if (e < n) vtop = e + 1; } break;
          case K_UP:   if (vtop) vtop = v_bol(VBUF, vtop - 1); break;
          case K_PGDN: { uint8_t i; for (i = 0; i < rows - 2; i++) { unsigned e = v_eol(VBUF, n, vtop); if (e < n) vtop = e + 1; else break; } } break;
          case K_PGUP: { uint8_t i; for (i = 0; i < rows - 2; i++) if (vtop) vtop = v_bol(VBUF, vtop - 1); } break;
          case K_HOME: vtop = 0; break;
          case K_END:  { while (v_eol(VBUF, n, vtop) < n) vtop = v_eol(VBUF, n, vtop) + 1; } break;
          } }
    }
    draw_all();
}

/* ---- edit, through the shell's SWAP (F4) ------------------------------- */
/* SWAP saves our whole $0000-$FFFF and the screen to far memory, runs the
 * editor over the top, and restores us. The selected file resolves against the
 * device cwd, which we hold on the active panel, so a bare name is enough.
 *
 * The command line must NOT live in our own image: SWAP loads the editor over
 * $6000, and the shell leaves args_tail pointing at the file name inside the
 * line -- if that were our RAM, the editor's own bytes would land there and it
 * would open a garbage name. So the line is built at $0300, the low page the
 * ROM reserves for programs (k4510.cfg: "$0300-$043F belongs to programs"):
 * below the editor's load, untouched by it, and carried across the swap. */
#define CMDLINE ((char *)0x0300)
static void do_edit(void)
{
    int s = cur[active];
    uint8_t bw, bx, by, k;
    if (!count[active] || is_dir(active, s)) { message("Select a file to edit"); return; }
    bw = 44; if (bw > cols - 2) bw = (uint8_t)(cols - 2);
    bx = (uint8_t)((cols - bw) / 2); by = (uint8_t)(rows / 2 - 2);
    box(bx, by, bw, 4, " Edit ");
    draw_str((uint8_t)(bx + 3), (uint8_t)(by + 1), names[active][s], (uint8_t)(bw - 4), FILEFG, CBG);
    draw_str((uint8_t)(bx + 3), (uint8_t)(by + 2), "V) VI    E) EDIT    Esc) cancel", (uint8_t)(bw - 4), FN_KFG, CBG);
    k = getkey();
    if (k == 'v' || k == 'V' || k == K_ENTER) strcpy(CMDLINE, "SWAP VI ");
    else if (k == 'e' || k == 'E')            strcpy(CMDLINE, "SWAP EDIT ");
    else { draw_all(); return; }
    strcat(CMDLINE, names[active][s]);
    rom_shell(CMDLINE);                     /* out to the editor and back */
    relist_both();                          /* the file may have changed size */
    draw_all();
}

/* ---- operations -------------------------------------------------------- */
static void other_dest(char *dst, const char *nm)     /* other panel's dir + name */
{
    uint8_t o = active ^ 1, l;
    strcpy(dst, ppath[o]); l = (uint8_t)strlen(dst);
    if (l && dst[l - 1] != '/') { dst[l] = '/'; dst[l + 1] = 0; }
    strcat(dst, nm);
}
static void op_error(uint8_t st)
{
    message(st == 1 ? "Not found" : st == 5 ? "Name too long" : "Operation failed");
}

static void do_copy(void)
{
    static char dst[256];
    int s = cur[active];
    if (!count[active] || is_dir(active, s)) { message("Select a file to copy"); return; }
    other_dest(dst, names[active][s]);
    if (!prompt(" Copy to ", dst, (uint8_t)(sizeof dst - 1))) return;
    fs_name(names[active][s]); fs_addr((uint32_t)(uint16_t)dst);
    { uint8_t st = fs_do(C_COPY); if (st) op_error(st); }
    relist_both(); draw_all();
}
static void do_move(void)
{
    static char dst[256];
    int s = cur[active];
    if (!count[active] || is_updir(active, s)) { message("Nothing to move"); return; }
    other_dest(dst, names[active][s]);
    if (!prompt(" Move / rename to ", dst, (uint8_t)(sizeof dst - 1))) return;
    fs_name(names[active][s]); fs_addr((uint32_t)(uint16_t)dst);
    { uint8_t st = fs_do(C_RENAME); if (st) op_error(st); }
    relist_both(); draw_all();
}
static void do_mkdir(void)
{
    static char nm[NAMEW];
    nm[0] = 0;
    if (!prompt(" Make directory ", nm, NAMEW - 1) || !nm[0]) return;
    fs_name(nm);
    { uint8_t st = fs_do(C_MKDIR); if (st) op_error(st); }
    relist_both(); draw_all();
}
static void do_delete(void)
{
    static char q[64];
    int s = cur[active];
    if (!count[active] || is_updir(active, s)) { message("Nothing to delete"); return; }
    strcpy(q, "Delete "); strcat(q, names[active][s]); strcat(q, " ?");
    if (!confirm(q)) return;
    fs_name(names[active][s]);
    { uint8_t st = fs_do(is_dir(active, s) ? C_RMDIR : C_RM);
      if (st) message(st == 2 && is_dir(active, s) ? "Directory not empty" : "Delete failed"); }
    relist_both(); draw_all();
}

/* ---- Enter on a program: get out of the way and let the shell run it ---- *
 * A program cannot be started from in here.  A .prg loads over the top of
 * KOMMANDER, and even if it did not, its output belongs on the shell's screen
 * and not under the redraw that would follow.  So KOMMANDER leaves, and TYPES
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
 * instead (`CPM E3:`) and you type the name at the CCP.  (RANGER does all of
 * this the same way; the two are deliberately alike here.) */
#define KBDQ 0xD100u                       /* write: type-ahead */
#define C_SAVE 10
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
/* /CPM/<drive>/<user> -- the CP/M coordinates of the active panel */
static uint8_t cpm_du(char *drive, uint8_t *user)
{
    const char *p = ppath[active];
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
static uint8_t start_com(const char *n)
{
    char d; uint8_t u, i = 0, j = 0;
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
    strcpy(tbuf + 64, "/CPM/A/0/K-RUN.SUB");
    fs_name(tbuf + 64); fs_addr((uint32_t)(uint16_t)tbuf); w32(FS_LEN, (uint32_t)i);
    if (fs_do(C_SAVE)) return 0;
    strcpy(runline, "CPM K-RUN");
    return 1;
}
static void type_ahead(const char *s)
{
    while (*s) REG(KBDQ) = (uint8_t)*s++;
    REG(KBDQ) = 0x0D;
}

static void enter(void)
{
    int s = cur[active];
    if (!count[active]) return;
    if (is_dir(active, s)) {
        static char updot[] = "..";
        fs_name(is_updir(active, s) ? updot : names[active][s]);
        if (fs_do(C_CHDIR)) { message("Cannot enter"); return; }
        getcwd_into(active);
        cur[active] = 0; top[active] = 0;
        relist(active);
        draw_all();
    } else {
        uint8_t kd = prog_kind(names[active][s]);
        if (kd == 1) strcpy(runline, names[active][s]);
        else if (kd == 2) start_com(names[active][s]);
        if (!runline[0]) view_file();
    }
}

/* ---- main -------------------------------------------------------------- */
void main(void)
{
    uint8_t running = 1;
    (void)rom_args();
    cols = REG(TERM + 5); rows = REG(TERM + 6); ox = REG(TERM + 7); oy = REG(TERM + 8); pcols = REG(TERM + 0x0D);
    if (!cols) cols = 80; if (!rows) rows = 30; if (!pcols) pcols = 80;

    getcwd_into(0); strcpy(ppath[1], ppath[0]);
    active = 0;
    relist(0); relist(1); set_cwd(0);
    draw_all();

    while (running) {
        uint8_t k = getkey();
        switch (k) {
        case K_TAB:   active ^= 1; set_cwd(active); draw_panel(0); draw_panel(1); break;
        case K_UP:    if (cur[active]) { cur[active]--; draw_panel(active); } break;
        case K_DOWN:  if (cur[active] + 1 < count[active]) { cur[active]++; draw_panel(active); } break;
        case K_PGUP:  cur[active] -= rows - 4; if (cur[active] < 0) cur[active] = 0; draw_panel(active); break;
        case K_PGDN:  cur[active] += rows - 4; if (cur[active] >= count[active]) cur[active] = count[active] ? count[active] - 1 : 0; draw_panel(active); break;
        case K_HOME:  cur[active] = 0; draw_panel(active); break;
        case K_END:   cur[active] = count[active] ? count[active] - 1 : 0; draw_panel(active); break;
        case K_LEFT:  if (active) { active = 0; set_cwd(0); draw_panel(0); draw_panel(1); } break;
        case K_RIGHT: if (!active) { active = 1; set_cwd(1); draw_panel(0); draw_panel(1); } break;
        case K_ENTER: enter(); if (runline[0]) running = 0; break;   /* Enter on a program: leave and run it */
        case K_F1 + 2:  view_file(); break;           /* F3  */
        case K_F1 + 3:  do_edit(); break;             /* F4  */
        case K_F1 + 4:  do_copy(); break;             /* F5  */
        case K_F1 + 5:  do_move(); break;             /* F6  */
        case K_F1 + 8:  do_mkdir(); break;            /* F9  */
        case K_F1 + 9:  do_delete(); break;           /* F10 */
        case K_F1 + 1:  relist_both(); draw_all(); break;   /* F2 refresh */
        case '.':       show_hidden ^= 1; relist_both(); draw_all(); break;
        case K_ESC: case 'q': case 'Q': running = 0; break;
        }
    }

    REG(TERM + 4) = 2;                 /* JIM: clear and home, a clean screen for the shell */
    rom_video();
    if (runline[0]) type_ahead(runline);   /* Enter on a program: the shell types it */
}
