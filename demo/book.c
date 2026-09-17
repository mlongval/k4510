/* K4510: BOOK -- the handbook, read on the machine it describes.
 *
 *   BOOK            the contents
 *   BOOK 2          chapter 2; BOOK SHELL, the chapter whose title says SHELL
 *   BOOK FILE.GMI   any Gemini page, from /SYSTEM/DOC or a path
 *
 * The pages are Gemini text (gemtext), made from the handbook's LaTeX by
 * doc/guide/mkgem.py into /SYSTEM/DOC, in the machine's character set and
 * already wrapped to 78 columns -- so a line of the file is a line of the
 * screen, and TYPE shows them readably too.  A line's first characters say
 * what it is: # ## ### headings, => a link, * an item, > a quote, and ```
 * fences a block shown as typed.  That is the whole format.
 *
 * Keys: the arrows, PgUp/PgDn (Space too), Home/End move; Tab and Shift+Tab
 * choose a link, Enter follows it, Backspace (or Left) comes back; / finds,
 * n finds again; Q or Esc leaves.  A link to a .PIC is a screenshot: Enter
 * shows it full screen, as the machine drew it, and any key comes back.
 *
 * Memory: the page, its line index and a picture live in far memory
 * ($0C000000 up), so a chapter of any length fits; only the line on its way
 * to the screen comes down.  A name handed to the ROM is built in page 3
 * ($0300), which belongs to programs: during a system call the ROM is banked
 * in over $A000-$FFFF, and this program's data may lie up there. */
#include <string.h>
#include "k4510.h"

void __fastcall__ rom_chrout(unsigned char c);
unsigned char rom_getin(void);
static unsigned char rom_args(void)  { return ((unsigned char (*)(void))0xFF95)(); }
static unsigned char rom_load(void)  { return ((unsigned char (*)(void))0xFF89)(); }
static void rom_video(void)          { ((void (*)(void))0xFF92)(); }

#define TERM     0xDA00u
#define KBDST    0xD101u
#define VIC      0xD000u
#define SHELL_RC (*(volatile uint8_t *)0x03FF)
#define PATHBUF  ((char *)0x0300)          /* below $A000, for the ROM */

#define DOC      0x0C000000UL             /* the page */
#define IDX      0x0C800000UL             /* its display lines: 4-byte offsets */
#define TYP      0x0C900000UL             /* a type byte each */
#define PIC      0x0CA00000UL             /* a picture being shown */
#define RAWIDX   0x0CC00000UL             /* every line's offset, before the display lines are made of them */
#define BITMAP   0x00200000UL             /* VICKY layer 1's bitmap (as LOGO's) */

#define K_UP     0x80
#define K_DOWN   0x81
#define K_LEFT   0x82
#define K_RIGHT  0x83
#define K_HOME   0x84
#define K_END    0x85
#define K_PGUP   0x86
#define K_PGDN   0x87

enum { T_TEXT, T_H1, T_H2, T_H3, T_LINK, T_PRE, T_QUOTE, T_LIST, T_IMG };   /* T_IMG: a row a picture is drawn over, under its link */

#define DOCDIR   "/SYSTEM/DOC/"
#define NAMELEN  48
#define MAXLINKS 400
#define HIST     8

static char name[NAMELEN];               /* the page, as a path */
static uint32_t size;
static uint16_t nlines, top, nlinks;
static int sel;                          /* the chosen link, -1 none */
static uint16_t links[MAXLINKS];         /* display line of each link */
static uint8_t rows, cols, page;
/* Pictures in the page itself.  Doc, 2026-09-17: "I would like BOOK to display
 * images inline".  JIM draws them (the Kitty graphics protocol, core/jimgfx.h):
 * BOOK leaves IMG_ROWS blank rows under a picture's link and asks JIM to put
 * IMG/NAME.PNG there, IMG_COLS wide -- half size, which is what fits in a page
 * of text -- cropped when the picture is cut by the top or the foot of the
 * page.  Enter on the link still shows it whole.  An emulator whose JIM cannot
 * draw never answers the question main() asks, and the page is as it was. */
static uint8_t gfx, img_rows;
static volatile uint8_t sink;            /* where a register read for its side effect goes */
#define IMG_COLS 40
static char ln[164];                     /* the line being drawn */
static char msg[64];
static char find[40];
static char arg[40];
static char hname[HIST][NAMELEN];
static uint16_t htop[HIST];
static int hsel[HIST];
static uint8_t hn;

/* ---- the terminal (JIM) ----------------------------------------------- */
static uint8_t sx, lim;                  /* column, and where a line stops */
static void raw(uint8_t c) { REG(TERM) = c; }
static void rawstr(const char *s) { while (*s) raw((uint8_t)*s++); }
static void put(uint8_t c) { if (sx >= lim) return; sx++; raw(c); }
static void says(const char *s) { while (*s) put((uint8_t)*s++); }
static void num(uint16_t v)
{
    char b[6]; uint8_t i = 5;
    b[5] = 0;
    do { b[--i] = (char)('0' + v % 10); v /= 10; } while (v);
    says(b + i);
}
static void esc(void) { raw(27); raw('['); }
static void at(uint8_t r, uint8_t c)
{
    uint8_t s = lim; lim = 255;
    esc(); num(r + 1); raw(';'); num(c + 1); raw('H');
    lim = s; sx = c;
}
static void sgr(const char *s) { esc(); rawstr(s); raw('m'); }
static void eeol(void) { esc(); raw('K'); }

/* ---- zero page for the system calls ----------------------------------- */
static void zp16(uint8_t a, uint16_t v) { REG(a) = (uint8_t)v; REG(a + 1) = (uint8_t)(v >> 8); }
static void zp32(uint8_t a, uint32_t v)
{
    REG(a) = (uint8_t)v; REG(a + 1) = (uint8_t)(v >> 8);
    REG(a + 2) = (uint8_t)(v >> 16); REG(a + 3) = (uint8_t)(v >> 24);
}
static uint32_t zpr32(uint8_t a)
{
    return (uint32_t)REG(a) | ((uint32_t)REG(a + 1) << 8) | ((uint32_t)REG(a + 2) << 16) | ((uint32_t)REG(a + 3) << 24);
}
static uint8_t load(const char *path, uint32_t dest)   /* 0 ok; size in `size`-style out */
{
    strncpy(PATHBUF, path, 0xFE); PATHBUF[0xFE] = 0;
    zp16(0xF0, (uint16_t)PATHBUF); zp32(0xF2, dest);
    return rom_load();
}

/* ---- the page ---------------------------------------------------------- */
static uint32_t off_of(uint16_t n)
{
    uint32_t o;
    dma_copy(IDX + ((uint32_t)n << 2), (uint32_t)(uint16_t)&o, 4);
    return o;
}
static void set_off(uint16_t n, uint32_t o) { dma_copy((uint32_t)(uint16_t)&o, IDX + ((uint32_t)n << 2), 4); }
static uint32_t raw_of(uint16_t n) { uint32_t o; dma_copy(RAWIDX + ((uint32_t)n << 2), (uint32_t)(uint16_t)&o, 4); return o; }
static void set_raw(uint16_t n, uint32_t o) { dma_copy((uint32_t)(uint16_t)&o, RAWIDX + ((uint32_t)n << 2), 4); }
static void fetch_at(uint32_t o)
{
    uint32_t left = size > o ? size - o : 0;
    uint8_t want = left > 160 ? 160 : (uint8_t)left, i;
    if (want) dma_copy(DOC + o, (uint32_t)(uint16_t)ln, want);
    for (i = 0; i < want && ln[i] != '\n' && ln[i] != '\r'; i++)
        ;
    ln[i] = 0;
}
static void fetch(uint16_t n) { fetch_at(off_of(n)); }

/* Index the page: every line's offset (pass 1), then the display lines --
 * the ``` fences dropped, each line typed by its first characters (pass 2).
 * Two regions since pictures arrived: a picture ADDS display lines, so the
 * kept index could overtake the one being read if they shared one. */
static char *link_target(char **label);
static uint8_t is_pic(const char *t);
static void index_page(void)
{
    static uint8_t buf[256];
    uint32_t o = 0;
    uint16_t nraw = 0, i, n, j, k = 0;
    uint8_t pre = 0, t;
    set_raw(nraw++, 0);
    while (o < size) {
        n = size - o > 256 ? 256 : (uint16_t)(size - o);
        dma_copy(DOC + o, (uint32_t)(uint16_t)buf, n);
        for (i = 0; i < n; i++)
            if (buf[i] == '\n' && o + i + 1 < size) set_raw(nraw++, o + i + 1);
        o += n;
    }
    nlinks = 0;
    for (j = 0; j < nraw; j++) {
        o = raw_of(j);
        fetch_at(o);
        if (ln[0] == '`' && ln[1] == '`' && ln[2] == '`') { pre = !pre; continue; }
        if (pre) t = T_PRE;
        else if (ln[0] == '#') t = ln[1] != '#' ? T_H1 : ln[2] != '#' ? T_H2 : T_H3;
        else if (ln[0] == '=' && ln[1] == '>') t = T_LINK;
        else if (ln[0] == '>') t = T_QUOTE;
        else if (ln[0] == '*' && ln[1] == ' ') t = T_LIST;
        else t = T_TEXT;
        set_off(k, o);
        far_poke(TYP + k, t);
        if (t == T_LINK && nlinks < MAXLINKS) links[nlinks++] = k;
        k++;
        if (t == T_LINK && gfx) {                          /* a picture: its rows follow, each pointing back at the link's line */
            char *lab;
            if (is_pic(link_target(&lab))) for (i = 0; i < img_rows; i++) { set_off(k, o); far_poke(TYP + k, T_IMG); k++; }
        }
    }
    nlines = k;
}

static uint8_t open_page(const char *file)
{
    char path[NAMELEN];
    if (file[0] == '/') { strncpy(path, file, NAMELEN - 1); path[NAMELEN - 1] = 0; }
    else { strcpy(path, DOCDIR); strncat(path, file, NAMELEN - sizeof DOCDIR); }
    if (load(path, DOC)) return 1;
    size = zpr32(0xF6);
    strcpy(name, path);
    index_page();
    top = 0; sel = -1;
    return 0;
}

/* a link line: "=> target label" -- the target is cut out of ln in place */
static char *link_target(char **label)
{
    char *p = ln + 2, *t;
    while (*p == ' ') p++;
    t = p;
    while (*p && *p != ' ') p++;
    if (*p) { *p++ = 0; while (*p == ' ') p++; }
    *label = *p ? p : t;
    return t;
}
static uint8_t is_pic(const char *t)
{
    uint8_t l = (uint8_t)strlen(t);
    return l > 4 && !strcmp(t + l - 4, ".PIC");
}

/* ---- drawing ----------------------------------------------------------- */
static void show(uint16_t n, uint8_t t)
{
    const char *p = ln;
    char *lab, *tgt;
    switch (t) {
    case T_H1: sgr("1;37"); while (*p == '#') p++; while (*p == ' ') p++; break;
    case T_H2: sgr("1;36"); while (*p == '#') p++; while (*p == ' ') p++; break;
    case T_H3: sgr("36");   while (*p == '#') p++; while (*p == ' ') p++; break;
    case T_LINK:
        tgt = link_target(&lab);
        sgr(sel >= 0 && links[sel] == n ? "7" : "1;36");
        put(0xAF); put(' ');                          /* >> */
        if (is_pic(tgt)) says("[picture] ");
        else if (strstr(tgt, "://")) says("[internet] ");
        says(lab);
        return;
    case T_PRE:   sgr("1;37"); put(' '); put(' '); break;
    case T_QUOTE: sgr("32"); put(0xB3); put(' '); p++; if (*p == ' ') p++; break;   /* | */
    case T_LIST:  put(0xF9); put(' '); p += 2; break;                                /* . */
    }
    says(p);
}

/* ---- a picture in the page ------------------------------------------------ */
static void rawnum(uint16_t v) { char b[6]; uint8_t i = 5; b[5] = 0; do { b[--i] = (char)('0' + v % 10); v /= 10; } while (v); rawstr(b + i); }
static void raw64(const char *t)                     /* base64, which is how the protocol carries a file's name */
{
    static const char A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint8_t n = (uint8_t)strlen(t), i; uint32_t v;
    for (i = 0; i < n; i += 3) {
        v = (uint32_t)(uint8_t)t[i] << 16; if (i + 1 < n) v |= (uint32_t)(uint8_t)t[i + 1] << 8; if (i + 2 < n) v |= (uint8_t)t[i + 2];
        raw(A[(v >> 18) & 63]); raw(A[(v >> 12) & 63]); raw(i + 1 < n ? A[(v >> 6) & 63] : '='); raw(i + 2 < n ? A[v & 63] : '=');
    }
}
/* Display line n is a picture's row, drawn on screen row r.  The picture is
 * sent once for the rows of it that are on the page: from its first row when
 * that is in sight, from the top of the page when it is not. */
static void inline_picture(uint16_t n, uint8_t r)
{
    static char path[NAMELEN]; static uint8_t hd[24];
    char *lab, *tgt; uint8_t kk, left, l; uint16_t h, sy, sh;
    for (kk = 1; kk <= n && far_peek(TYP + n - kk) == T_IMG; kk++)
        ;
    if (kk != 1 && r != 0) return;                     /* a later row of a picture already begun above */
    fetch(n); tgt = link_target(&lab);
    strcpy(path, DOCDIR); strncat(path, tgt, NAMELEN - sizeof DOCDIR - 1);
    l = (uint8_t)strlen(path); if (l < 4) return;
    path[l - 2] = 'N'; path[l - 1] = 'G';             /* IMG/NAME.PIC is shown whole; IMG/NAME.PNG is its twin for JIM */
    if (load(path, PIC)) return;                       /* no twin on this disk: the link alone, as before */
    dma_copy(PIC, (uint32_t)(uint16_t)hd, 24);
    if (hd[1] != 'P' || hd[2] != 'N' || hd[3] != 'G') return;
    h = ((uint16_t)hd[22] << 8) | hd[23];
    left = (uint8_t)(img_rows - (kk - 1));
    if (r + left > page) left = (uint8_t)(page - r);
    sy = (uint16_t)((uint32_t)(kk - 1) * h / img_rows);
    sh = (uint16_t)((uint32_t)left * h / img_rows);
    if (!left || !sh) return;
    at(r, 2);
    rawstr("\033_Ga=T,f=100,t=f,i=1,q=2,C=1,c="); rawnum(IMG_COLS); rawstr(",r="); rawnum(left);
    rawstr(",y="); rawnum(sy); rawstr(",h="); rawnum(sh); raw(';'); raw64(path); rawstr("\033\\");
}

static void status(void)
{
    uint16_t pct;
    const char *s = strrchr(name, '/');
    at(rows - 1, 0); lim = cols - 1;                  /* never the last cell: it would scroll */
    sgr("7");
    if (msg[0]) { put(' '); says(msg); msg[0] = 0; }
    else {
        says(" BOOK  "); says(s ? s + 1 : (const char *)name); says("  ");
        pct = nlines <= page ? 100 : (uint16_t)(((uint32_t)(top + page) * 100) / nlines);
        num(pct > 100 ? 100 : pct); says("%   Tab link  Enter open  Bksp back  / find  Q quit");
    }
    while (sx < lim) put(' ');
    sgr("0");
}

static void draw(void)
{
    uint8_t r;
    uint16_t n;
    if (gfx) rawstr("\033_Ga=d,q=2\033\\");            /* the last page's pictures off the glass */
    for (r = 0; r < page; r++) {
        n = top + r;
        at(r, 0); lim = cols; sgr("0");
        if (n < nlines && far_peek(TYP + n) != T_IMG) { fetch(n); show(n, far_peek(TYP + n)); }
        sgr("0"); eeol();
    }
    if (gfx) for (r = 0; r < page; r++) if (top + r < nlines && far_peek(TYP + top + r) == T_IMG) inline_picture(top + r, r);
    status();
}

static uint16_t maxtop(void) { return nlines > page ? nlines - page : 0; }
static void go(int d)
{
    long t = (long)top + d;
    if (t < 0) t = 0;
    if (t > (long)maxtop()) t = maxtop();
    top = (uint16_t)t;
}
static void reveal(void)
{
    uint16_t l;
    if (sel < 0) return;
    l = links[sel];
    if (l < top) top = l;
    else if (l >= top + page) { top = l > page / 2 ? l - page / 2 : 0; if (top > maxtop()) top = maxtop(); }
}

/* Tab: the next link after the chosen one (or after the top of the screen,
 * when the chosen one has scrolled away); Shift+Tab the one before. */
static void tab(uint8_t back)
{
    int i;
    long pos;
    if (!nlinks) { strcpy(msg, "no links on this page"); return; }
    if (sel >= 0 && links[sel] >= top && links[sel] < top + page) pos = links[sel];
    else pos = back ? (long)top + page : (long)top - 1;
    if (!back) {
        for (i = 0; i < (int)nlinks && (long)links[i] <= pos; i++)
            ;
        sel = i < (int)nlinks ? i : 0;
    } else {
        for (i = (int)nlinks - 1; i >= 0 && (long)links[i] >= pos; i--)
            ;
        sel = i >= 0 ? i : (int)nlinks - 1;
    }
    reveal();
}

static void push(void)
{
    if (hn == HIST) { memmove(hname[0], hname[1], sizeof hname[0] * (HIST - 1)); memmove(htop, htop + 1, sizeof htop[0] * (HIST - 1)); memmove(hsel, hsel + 1, sizeof hsel[0] * (HIST - 1)); hn--; }
    strcpy(hname[hn], name); htop[hn] = top; hsel[hn] = sel; hn++;
}
static void back(void)
{
    if (!hn) { strcpy(msg, "this is where you started"); return; }
    hn--;
    if (open_page(hname[hn])) { strcpy(msg, "cannot open that page again"); return; }
    top = htop[hn]; sel = hsel[hn];
}

/* ---- a picture --------------------------------------------------------- */
/* IMG/NAME.PIC (mkgem.py): "K4PC", width, height, colours (16-bit LE), a
 * format byte, five zero bytes, the palette (3 bytes a colour), then the
 * pixels -- format 0 as runs of (count 1-255, index), one DMA fill a run;
 * format 1 raw, one DMA copy.  Shown on VICKY's layer 1 with the text layer
 * off, as LOGO sets it up; the VIDEO call puts the ROM's screen and palette
 * back. */
static void picture(const char *file)
{
    static uint8_t h[16], buf[254];
    char path[NAMELEN];
    uint16_t w, ht, nc, i, cnt, n;
    static uint8_t palsave[256 * 3];                         /* the colours the picture's palette overwrites */
    uint32_t o, end, dst;
    uint8_t ctrl, l0, bg;
    strcpy(path, DOCDIR); strncat(path, file, NAMELEN - sizeof DOCDIR);
    if (load(path, PIC)) { strcpy(msg, "that picture is not on the disk"); return; }
    end = PIC + zpr32(0xF6);
    dma_copy(PIC, (uint32_t)(uint16_t)h, 16);
    if (h[0] != 'K' || h[1] != '4' || h[2] != 'P' || h[3] != 'C') { strcpy(msg, "not a picture"); return; }
    w = h[4] | (h[5] << 8); ht = h[6] | (h[7] << 8); nc = h[8] | (h[9] << 8);
    ctrl = REG(VIC); l0 = REG(VIC + 0x10); bg = REG(VIC + 1);
    REG(VIC + 1) = 0;                                         /* colour 0 is see-through on a bitmap: the ground shows, so it must be the picture's colour 0 */
    for (i = 0x21; i <= 0x25; i++) REG(VIC + i) = 0;          /* palette offset, scroll */
    REG(VIC + 0x26) = (uint8_t)w; REG(VIC + 0x27) = (uint8_t)(w >> 8);   /* stride */
    REG(VIC + 0x28) = 0; REG(VIC + 0x29) = 0; REG(VIC + 0x2A) = 0x20; REG(VIC + 0x2B) = 0;   /* $200000 */
    dma_fill(0, BITMAP, (uint32_t)w * ht);
    REG(VIC) = (uint8_t)(ctrl & 0xD9);                        /* 640x480, no doubling -- and out of the HD family (bit 5) */
    REG(VIC + 0x10) = 0;                                      /* the text layer off */
    REG(VIC + 0x20) = 0x19;                                   /* layer 1: on, bitmap, 8 bpp */
    o = PIC + 16;
    if (nc > 256) nc = 256;
    /* Keep the colours the picture is about to overwrite, and put them back
     * after it: the ROM's VIDEO call does NOT reload the palette (so that
     * PALETTE's choice survives), and BOOK's pages came back in the picture's
     * colours (Doc, 2026-09-15, chapter 02's picture). */
    for (i = 0; i < nc; i++) { REG(0xD006) = (uint8_t)i; palsave[i * 3] = REG(0xD007); palsave[i * 3 + 1] = REG(0xD008); palsave[i * 3 + 2] = REG(0xD009); }
    for (i = 0; i < nc; i++, o += 3) {
        dma_copy(o, (uint32_t)(uint16_t)buf, 3);
        REG(0xD006) = (uint8_t)i; REG(0xD007) = buf[0]; REG(0xD008) = buf[1]; REG(0xD009) = buf[2];
    }
    dst = BITMAP;
    if (h[10] == 1) dma_copy(o, BITMAP, end - o);           /* raw */
    else while (o < end) {                                  /* runs */
        n = end - o > sizeof buf ? sizeof buf : (uint16_t)(end - o);
        dma_copy(o, (uint32_t)(uint16_t)buf, n);
        for (i = 0; i + 1 < n; i += 2) {
            cnt = buf[i];
            if (cnt) dma_fill(buf[i + 1], dst, cnt);
            dst += cnt;
        }
        o += n;
    }
    while (!rom_getin())
        ;
    REG(VIC + 0x20) = 0; REG(VIC + 0x10) = l0; REG(VIC) = ctrl; REG(VIC + 1) = bg;
    for (i = 0; i < nc; i++) { REG(0xD006) = (uint8_t)i; REG(0xD007) = palsave[i * 3]; REG(0xD008) = palsave[i * 3 + 1]; REG(0xD009) = palsave[i * 3 + 2]; }   /* B commits */
    rom_video();
    REG(TERM + 4) = 2;                                        /* clear; the page is redrawn */
}

static void follow(void)
{
    char *tgt, *lab;
    char t[NAMELEN];
    if (sel < 0) { strcpy(msg, "Tab chooses a link first"); return; }
    fetch(links[sel]);
    tgt = link_target(&lab);
    if (strstr(tgt, "://")) { strcpy(msg, "that page is on the internet, not on this disk"); return; }
    if (is_pic(tgt)) { picture(tgt); return; }
    strncpy(t, tgt, NAMELEN - 1); t[NAMELEN - 1] = 0;
    push();
    if (open_page(t)) { hn--; open_page(hname[hn]); top = htop[hn]; sel = hsel[hn]; strcpy(msg, "cannot open "); strncat(msg, t, 40); }
}

/* ---- finding ----------------------------------------------------------- */
static char up(char c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
static uint8_t has(const char *s, const char *w)
{
    const char *a, *b;
    for (; *s; s++) {
        for (a = s, b = w; *b && up(*a) == up(*b); a++, b++)
            ;
        if (!*b) return 1;
    }
    return 0;
}
static void search(uint8_t again)
{
    uint8_t k, l = 0;
    uint16_t n;
    if (!again) {
        at(rows - 1, 0); lim = cols - 1; sgr("7"); says(" find: ");
        find[0] = 0;
        for (;;) {
            do { k = rom_getin(); } while (!k);
            if (k == 13) break;
            if (k == 27) { find[0] = 0; break; }
            if (k == 8) { if (l) { find[--l] = 0; raw(8); raw(' '); raw(8); sx--; } continue; }
            if (k >= 32 && !(REG(KBDST) & 0x40) && l < sizeof find - 1) { find[l++] = (char)k; find[l] = 0; put(k); }
        }
        sgr("0");
    }
    if (!find[0]) return;
    for (n = top + 1; n < nlines; n++) {
        fetch(n);
        if (has(ln, find)) { top = n > maxtop() ? maxtop() : n; return; }
    }
    strcpy(msg, "not found below here (n looks again)");
}

/* BOOK 2 or BOOK SHELL: a contents link whose label starts "2." or holds the word */
static void choose(const char *a)
{
    uint16_t i, l = (uint16_t)strlen(a);
    uint8_t pass;
    char *lab, *tgt;
    char t[NAMELEN];
    if (strchr(a, '.')) { push(); if (open_page(a)) { back(); strcpy(msg, "no such page"); } return; }
    /* twice: first by the chapter's number or a word of its title; then, for
     * a number, by the page's own file number -- the programmer's guide goes
     * on counting (chapter 15 is 21-IO.GMI), and BOOK 21 should not be "no
     * chapter" to someone looking at the file (Doc, 2026-09-14) */
    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < nlinks; i++) {
            fetch(links[i]);
            tgt = link_target(&lab);
            if (pass == 0 ? ((!strncmp(lab, a, l) && lab[l] == '.') || (a[0] > '9' && has(lab, a)))
                          : (a[0] <= '9' && l <= 2 && !strncmp(tgt + (l == 1), a, l) && tgt[2] == '-' && (l == 2 || tgt[0] == '0'))) {
                strncpy(t, tgt, NAMELEN - 1); t[NAMELEN - 1] = 0;
                push();
                if (open_page(t)) back();
                return;
            }
        }
    strcpy(msg, "no chapter called "); strncat(msg, a, 30);
}

int main(void)
{
    uint8_t na, k, vk, sh, i;
    const char *a;
    na = rom_args(); a = *(const char **)0xF0;
    while (na && *a == ' ') { a++; na--; }
    for (i = 0; i < na && i < sizeof arg - 1 && a[i] != ' '; i++) arg[i] = up(a[i]);
    arg[i] = 0;
    cols = REG(TERM + 5); rows = REG(TERM + 6);
    if (!cols) cols = 80;
    if (rows < 6) rows = 30;
    page = rows - 1;
    REG(TERM + 4) = 2; REG(TERM + 0x0E) = 0;                 /* clear; no cursor */
    /* Can this JIM draw?  Ask the way any program asks a Kitty terminal: a
     * one-pixel query.  JIM answers at once, in its reply register; a JIM that
     * cannot swallows the question and says nothing. */
    while (REG(TERM + 1) & 0x80) sink = REG(TERM + 2);      /* into a variable: cc65 drops a read whose value is thrown away, and this loop then never ends */
    rawstr("\033_Gi=1,s=1,v=1,a=q,t=d,f=24;AAAA\033\\");
    if (REG(TERM + 1) & 0x80) { gfx = cols >= IMG_COLS + 4; while (REG(TERM + 1) & 0x80) sink = REG(TERM + 2); }
    img_rows = rows > 40 ? 30 : 15;                          /* 240 lines of glass: fifteen 16-line rows, or thirty of 8 */
    if (open_page("INDEX.GMI")) {
        const char *e = "book: /SYSTEM/DOC/INDEX.GMI is missing\n";
        while (*e) rom_chrout((unsigned char)*e++);
        SHELL_RC = 1;
        return 0;
    }
    if (arg[0]) choose(arg);
    for (;;) {
        draw();
        do { k = rom_getin(); } while (!k);
        vk = REG(KBDST) & 0x40; sh = REG(KBDST) & 1;
        if (vk) switch (k) {
            case K_DOWN:  go(1); break;
            case K_UP:    go(-1); break;
            case K_PGDN:  go(page - 1); break;
            case K_PGUP:  go(-(int)(page - 1)); break;
            case K_HOME:  top = 0; break;
            case K_END:   top = maxtop(); break;
            case K_LEFT:  back(); break;
            case K_RIGHT: follow(); break;
        }
        else switch (k) {
            case 9:   tab(sh); break;
            case 13:  follow(); break;
            case 8:   back(); break;
            case ' ': go(page - 1); break;
            case '/': search(0); break;
            case 'n': case 'N': search(1); break;
            case 'q': case 'Q': case 27: goto out;
        }
    }
out:
    sgr("0");
    REG(TERM + 0x0E) = 0; REG(TERM + 4) = 2;
    rom_video();
    return 0;
}
