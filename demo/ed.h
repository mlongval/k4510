/* demo/ed.h -- the editing engine VI and PROG share.
 *
 * Moved out of VI whole (2026-09-14, for PROG: "not limited to just one
 * file at a time"), so a fix lands in both editors.  Everything here works
 * on THE CURRENT BUFFER: the text is 256-byte line slots in far memory at
 * ed_slots, its undo journal at ed_undo, and the globals below (ln, cy, cx,
 * nlines, top, name, dirty) are that buffer's.  VI has one buffer and never
 * moves them; PROG switches buffers by saving and restoring them.
 *
 * In order: the constants and the ROM's calls; the screen, through JIM; the
 * lines; the register; undo; files; editing; search and substitute; put and
 * join; renumber (renum.h); make (MAKE.ERR, the error list, :run).
 * #include "k4510.h" first. */

#define TERM   0xDA00u
#define SLOTS  ed_slots                   /* one 256-byte slot per line (the current buffer) */
#define FLAT   0x0E800000UL                 /* the file, flat, for LOAD and SAVE */
#define SLOT(n) (SLOTS + ((uint32_t)(n) << 8))
#define REGS   0x0E900000UL                 /* the unnamed register: whole lines, same slot shape */
#define RSLOT(n) (REGS + ((uint32_t)(n) << 8))
#define UNDO   ed_undo                    /* the journal: 512 bytes an entry, header + one slot */
#define USLOT(n) (UNDO + ((uint32_t)(n) << 9))
#define UNDOMAX 3000u
#define MAXLINES ed_maxlines                /* VI 32000; PROG 16384, a file in 4 MB */
#define REGMAX  2000u
#define NAMEMAX 64
static uint32_t ed_slots = 0x0E000000UL, ed_undo = 0x0F000000UL;   /* VI's places, as they always were */
static unsigned ed_maxlines = 32000u;

void __fastcall__ rom_chrout(unsigned char c);
unsigned char rom_getin(void);
/* The ROM's calls give a byte in A and leave X as $FF (crt0.s zp_out's loop
 * ends there).  A C function returning unsigned char through a cast is
 * tested as A|X by cc65, so every save read as a failure: VI said "not
 * saved" after each good write and never cleared [+] (found 2026-09-14,
 * when :make trusted it).  Called as int, and cut to the byte: X is gone. */
static unsigned char rom_args(void) { return (unsigned char)(((unsigned (*)(void))0xFF95)() & 0xFF); }
static unsigned char rom_load(void) { return (unsigned char)(((unsigned (*)(void))0xFF89)() & 0xFF); }
static unsigned char rom_save(void) { return (unsigned char)(((unsigned (*)(void))0xFF8C)() & 0xFF); }
static void rom_video(void) { ((void (*)(void))0xFF92)(); }
static void zp16(uint8_t a, uint16_t v) { REG(a) = v; REG(a + 1) = v >> 8; }
static void zp32(uint8_t a, uint32_t v) { REG(a)=v; REG(a+1)=v>>8; REG(a+2)=v>>16; REG(a+3)=v>>24; }
static uint32_t zpr32(uint8_t a) { return (uint32_t)REG(a) | ((uint32_t)REG(a+1)<<8) | ((uint32_t)REG(a+2)<<16) | ((uint32_t)REG(a+3)<<24); }
static void far_get(uint32_t p, void *d, unsigned n) { dma_copy(p, (uint32_t)(uint16_t)d, n); }
static void far_put(const void *s, uint32_t p, unsigned n) { dma_copy((uint32_t)(uint16_t)s, p, n); }

static uint8_t ln[256], tmp[256];           /* the cursor's line; a line being drawn */
static char name[NAMEMAX];
static unsigned nlines = 1, cy, top;
static uint8_t cx, cols, rows, dirty;
static const char *note = "";
static char pat[NAMEMAX]; static uint8_t patlen, lastdir = 1;
static unsigned reglines;                   /* what the register holds */
static uint8_t reglinewise;
static uint8_t curshape;                    /* the cursor shape JIM has; 0 after a command drew over us */

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
/* Tab: spaces to the next stop, never a tab character.  The width is
 * VI's `set ts=N` (in /SYSTEM/ETC/VI.RC, or typed as :set ts=N), one value
 * for both editors: PROG reads the same line with ed_rc_tabw.  Doc,
 * 2026-09-14: "insert 2 or 4 spaces, not TAB characters". */
static uint8_t ed_tabw = 4;
static void ed_tab(void)
{
    do { if (ln[0] >= 255) return; ins_ch(' '); } while (cx % ed_tabw);
}
static void ed_set_tabw(const char *v)                /* "4", from set ts=4 */
{
    uint8_t n = 0;
    while (*v >= '0' && *v <= '9') n = (uint8_t)(n * 10 + (*v++ - '0'));
    if (n >= 1 && n <= 16) ed_tabw = n;
}
/* the tab width from VI.RC, for a front end that does not run VI.RC itself:
 * a line "set ts=N" or "set tabstop=N" */
static const char ed_rcname[] = "/SYSTEM/ETC/VI.RC";
static char ed_rl[40];
static void ed_rc_line(void)                          /* one VI.RC line in ed_rl: only "set ts=" matters here */
{
    if (memcmp(ed_rl, "set ", 4)) return;
    if (!memcmp(ed_rl + 4, "ts=", 3)) ed_set_tabw(ed_rl + 7);
    else if (!memcmp(ed_rl + 4, "tabstop=", 8)) ed_set_tabw(ed_rl + 12);
}
static void ed_rc_tabw(void)
{
    uint32_t l, off = 0; unsigned chunk, i; uint8_t n = 0;
    zp16(0xF0, (uint16_t)ed_rcname); zp32(0xF2, FLAT);
    if (rom_load()) return;                           /* no VI.RC: the default stands */
    l = zpr32(0xF6);
    while (off < l) {
        chunk = (l - off) > 128 ? 128 : (unsigned)(l - off);
        far_get(FLAT + off, tmp, chunk);
        for (i = 0; i < chunk; i++) {
            if (tmp[i] == '\n') { ed_rl[n] = 0; ed_rc_line(); n = 0; }
            else if (tmp[i] != '\r' && n < sizeof ed_rl - 1) ed_rl[n++] = (char)tmp[i];
        }
        off += chunk;
    }
    ed_rl[n] = 0; ed_rc_line();                       /* a last line with no newline */
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

/* ---- renumber ------------------------------------------------------------
 * :renum [start [step]] -- demo/renum.h does the BASIC; the lines are ours.
 * The table of old numbers lives in far memory like everything else, and
 * the change is one undo group, so u puts every number back. */
#define RNTAB 0x0EA00000UL                  /* renum's table: each numbered line's old number */
static unsigned rn_t;
static void rn_tab_put(unsigned i, unsigned v) { rn_t = v; far_put(&rn_t, RNTAB + ((uint32_t)i << 1), 2); }
static unsigned rn_tab_get(unsigned i) { far_get(RNTAB + ((uint32_t)i << 1), &rn_t, 2); return rn_t; }
#define RN_TAB_PUT rn_tab_put
#define RN_TAB_GET rn_tab_get
#include "renum.h"
static uint8_t rbuf[256];
static unsigned rn_arg(const char **c, unsigned dflt)
{
    unsigned v = 0; uint8_t any = 0;
    while (**c == ' ') (*c)++;
    while (**c >= '0' && **c <= '9') { v = v * 10 + (unsigned)(**c - '0'); (*c)++; any = 1; }
    return any ? v : dflt;
}
static void do_renum(const char *c)
{
    unsigned l, j, start, step; uint8_t same;
    start = rn_arg(&c, 10); step = rn_arg(&c, 10);
    line_out(cy);
    rn_begin();
    for (l = 0; l < nlines; l++) { far_get(SLOT(l), tmp, 256); rn_scan(tmp); }
    note = rn_check(rn_lang_of(name), start, step);
    if (note) { line_in(cy); return; }
    for (l = 0; l < nlines; l++) {                 /* a dry run first: nothing changes unless all of it can */
        far_get(SLOT(l), tmp, 256);
        if (!rn_line(tmp, rbuf)) { note = "renum: a line would pass 255 characters"; line_in(cy); return; }
    }
    rn_rewind(); u_begin();
    for (l = 0; l < nlines; l++) {
        far_get(SLOT(l), tmp, 256); rn_line(tmp, rbuf);
        same = (uint8_t)(rbuf[0] == tmp[0]);
        for (j = 1; same && j <= tmp[0]; j++) if (rbuf[j] != tmp[j]) same = 0;
        if (!same) { u_push(1, l, tmp); far_put(rbuf, SLOT(l), 256); dirty = 1; }
    }
    u_end(); line_in(cy);
    if (cx > ln[0]) cx = ln[0] ? (uint8_t)(ln[0] - 1) : 0;
    full = 1;
    note = rn_report();
}

/* ---- make ------------------------------------------------------------------
 * :make compiles the file with the machine's own CC or PAS -- the compilers
 * on the Linux beside it, reached through the ROM's SHELL call -- and reads
 * what they said from /SYSTEM/LOG/MAKE.ERR, where tools/k4510-errfmt has put
 * every compiler's messages into one form, FILE:LINE:COL:KIND:TEXT.  So VI
 * knows no compiler's dialect: a new language is a script, not a new VI.
 * The list lives in far memory, 128 bytes an entry:
 *   [0,1] line  [2] column (0: none)  [3] E or W  [4] 1 if it is this file
 *   [5] text length  [6..] text ("FILE:LINE: " in front when not this file)
 * :run is :make and then the program, under SWAP -k.  The file is read back
 * afterwards: SWAP keeps VI's 64 KB, not the far memory the text lives in,
 * and a program is free to use that. */
#define ERRRAW 0x0EB00000UL                 /* MAKE.ERR as loaded */
#define ERRTAB 0x0EC00000UL                 /* the list */
#define ERRMAX 200u
static const char errfile[] = "/SYSTEM/LOG/MAKE.ERR";
static unsigned nerr, nwarn, ecur = 0xFFFFu; /* ecur + 1 is where :cn goes */
static uint8_t ebuf[128];
static char nbuf[96], info[64];
static char ed_mkdir[NAMEMAX];
/* A front end may say how to build and what to run, instead of "the
 * compiler for this file, on this file" and "this file without its
 * extension": PROG in a project folder -- "CC -p /GAME/PROJECT.K4P" and
 * "/GAME/game".  Empty: the file's own way, as VI always does it. */
static char ed_mkline[NAMEMAX + 12], ed_runname[NAMEMAX];               /* the directory the last MAKE.ERR's file names are in ("" = here) */
static uint8_t nbn;
/* rom_shell (k4510.h, $FF8F) runs the line, and the ROM copies it through
 * the CPU's view -- in which, during a system call, $A000-$CFFF is the
 * ROM's (blocks 5-7), not ours.  VI's code, constants and C stack are all
 * up there, so a line built on the stack read as ROM bytes, and the shell
 * refused it (rc 1, no compile: found on the Dell, 2026-09-14).  The lines
 * are built in shline, which is BSS, which vi.cfg puts at $0800.  (Names
 * for LOAD and SAVE may live anywhere: the file device reads physical
 * memory, not the CPU's view.) */
static char shline[NAMEMAX + 16];

static void nb_reset(void) { nbn = 0; nbuf[0] = 0; }
static void nb_s(const char *s) { while (*s && nbn < sizeof nbuf - 1) nbuf[nbn++] = *s++; nbuf[nbn] = 0; }
static void nb_t(const uint8_t *t, uint8_t n) { uint8_t i; for (i = 0; i < n && nbn < sizeof nbuf - 1; i++) nbuf[nbn++] = (char)t[i]; nbuf[nbn] = 0; }
static void nb_n(unsigned v) { char b[6]; uint8_t k = 0; do { b[k++] = (char)('0' + v % 10); v /= 10; } while (v); while (k && nbn < sizeof nbuf - 1) nbuf[nbn++] = b[--k]; nbuf[nbn] = 0; }

static const char *base_of(const char *p) { const char *b = p; for (; *p; p++) if (*p == '/') b = p + 1; return b; }
static uint8_t is_this_file(const uint8_t *f, unsigned fl)
{
    const char *b = base_of(name); unsigned i;
    for (i = 0; i < fl; i++) if (!b[i] || rn_up((uint8_t)b[i]) != rn_up(f[i])) return 0;
    return (uint8_t)(b[fl] == 0);
}
static unsigned digits(const uint8_t *l, unsigned a, unsigned b) { unsigned v = 0; for (; a < b; a++) if (l[a] >= '0' && l[a] <= '9') v = v * 10 + (l[a] - '0'); return v; }

static void err_add(const uint8_t *l)               /* one line of MAKE.ERR, l[0] its length */
{
    unsigned f[4], i, k = 0, v, t = 0;
    for (i = 1; i <= l[0] && k < 4; i++) if (l[i] == ':') f[k++] = i;
    if (k < 4) return;
    if (l[f[2] + 1] == 'I') {                        /* information: what was made */
        for (i = f[3] + 1; i <= l[0] && t < sizeof info - 1; i++) info[t++] = (char)l[i];
        info[t] = 0; return;
    }
    v = digits(l, f[0] + 1, f[1]);
    ebuf[0] = (uint8_t)v; ebuf[1] = (uint8_t)(v >> 8);
    ebuf[2] = (uint8_t)digits(l, f[1] + 1, f[2]);
    ebuf[3] = l[f[2] + 1] == 'W' ? 'W' : l[f[2] + 1] == 'F' ? 'F' : 'E';   /* F: found (PROG's find in files) */
    ebuf[4] = (uint8_t)(l[1] != '-' && is_this_file(l + 1, f[0] - 1));
    for (i = f[3] + 1; i <= l[0] && t < 94; i++) ebuf[6 + t++] = l[i];
    ebuf[5] = (uint8_t)t;
    k = (uint8_t)(l[1] == '-' ? 0 : f[0] - 1);       /* [102] the file's name length, [103..127] the name: */
    if (k > 25) k = 0;                                /* where PROG goes for a message about another file */
    ebuf[102] = (uint8_t)k;
    for (i = 0; i < k; i++) ebuf[103 + i] = l[1 + i];
    if (ebuf[3] == 'W') nwarn++;
    far_put(ebuf, ERRTAB + ((uint32_t)nerr << 7), 128);
    nerr++;
}
/* the entry in ebuf is about the file in front -- asked when it is shown,
 * not when MAKE.ERR was read: in PROG the file in front changes (Doc,
 * 2026-09-14: the header's own messages still said PGH.H:1:) */
static uint8_t ent_same(void)
{
    const char *b = base_of(name); uint8_t k = ebuf[102], i;
    if (!k || !b[0]) return 0;
    for (i = 0; i < k; i++) if (!b[i] || rn_up((uint8_t)b[i]) != rn_up(ebuf[103 + i])) return 0;
    return (uint8_t)(b[k] == 0);
}
/* where the entry is, as it is shown: "line 12: " here, "UNIT.PAS:12: " in
 * another file, nothing for a message that names no file */
static void ent_where(char *o)
{
    unsigned l = (unsigned)ebuf[0] | ((unsigned)ebuf[1] << 8); uint8_t k = ebuf[102], i, n = 0, m = 0; char d[6];
    o[0] = 0;
    if (!l && !k) return;
    if (!k || ent_same()) { const char *s = "line "; while (*s) o[n++] = *s++; }
    else { for (i = 0; i < k && i < 25; i++) o[n++] = (char)ebuf[103 + i]; o[n++] = ':'; }
    if (l) { do { d[m++] = (char)('0' + l % 10); l /= 10; } while (l); while (m) o[n++] = d[--m]; }
    else if (n && o[n - 1] == ':') n--;
    o[n++] = ':'; o[n++] = ' '; o[n] = 0;
}
static void err_load(void)
{
    uint32_t l, off = 0; unsigned chunk, i;
    nerr = 0; nwarn = 0; info[0] = 0; ecur = 0xFFFFu;
    zp16(0xF0, (uint16_t)errfile); zp32(0xF2, ERRRAW);
    if (rom_load()) return;
    l = zpr32(0xF6);
    rbuf[0] = 0;
    while (off < l) {
        chunk = (l - off) > 128 ? 128 : (unsigned)(l - off);
        far_get(ERRRAW + off, tmp, chunk);
        for (i = 0; i < chunk; i++) {
            if (tmp[i] == '\n') { if (rbuf[0]) err_add(rbuf); rbuf[0] = 0; if (nerr >= ERRMAX) return; }
            else if (tmp[i] != '\r' && rbuf[0] < 255) { rbuf[0]++; rbuf[rbuf[0]] = tmp[i]; }
        }
        off += chunk;
    }
    if (rbuf[0] && nerr < ERRMAX) err_add(rbuf);
}
static void err_go(unsigned i)                       /* to entry i: its line and column, its text below */
{
    unsigned l;
    far_get(ERRTAB + ((uint32_t)i << 7), ebuf, 128);
    ecur = i;
    l = (unsigned)ebuf[0] | ((unsigned)ebuf[1] << 8);
    if (ent_same() && l) {
        goline(l - 1);
        cx = ebuf[2] ? (uint8_t)(ebuf[2] - 1) : 0;
        if (cx >= ln[0]) cx = ln[0] ? (uint8_t)(ln[0] - 1) : 0;
    }
    nb_reset(); nb_s(ebuf[3] == 'W' ? "warning " : ebuf[3] == 'F' ? "found " : "error "); nb_n(i + 1); nb_s(" of "); nb_n(nerr); nb_s(": ");
    { char w[40]; ent_where(w); nb_s(w); }
    nb_t(ebuf + 6, ebuf[5]);
    note = nbuf;
}
static void screen_back(void)                        /* after a command has drawn over us */
{
    REG(TERM + 4) = 1;                               /* JIM's attributes too: an error in red left all of VI red */
    REG(TERM + 4) = 2; REG(TERM + 0x0E) = 1;
    curshape = 0; full = 1;             /* the front end redraws all of it */
}
static const char *compiler(void)                    /* the machine's word for this file's language */
{
    const char *d = 0, *s;
    for (s = name; *s; s++) if (*s == '.') d = s;
    if (!d) return 0;
    if (rn_up((uint8_t)d[1]) == 'C' && !d[2]) return "CC";
    if (rn_up((uint8_t)d[1]) == 'P' && rn_up((uint8_t)d[2]) == 'A' && rn_up((uint8_t)d[3]) == 'S' && !d[4]) return "PAS";
    if (rn_up((uint8_t)d[1]) == 'R' && rn_up((uint8_t)d[2]) == 'X' && !d[3]) return "RX";    /* REXX: nothing to compile, only to run */
    if (rn_up((uint8_t)d[1]) == 'B' && rn_up((uint8_t)d[2]) == 'A' && rn_up((uint8_t)d[3]) == 'S' && !d[4]) return "MSBASIC";   /* BASIC and LOGO: */
    if (rn_up((uint8_t)d[1]) == 'L' && rn_up((uint8_t)d[2]) == 'G' && rn_up((uint8_t)d[3]) == 'O' && !d[4]) return "LOGO";      /* run as REXX is */
    return 0;
}
static uint8_t interpreted(const char *t)              /* REXX, MSBASIC, LOGO: an interpreter runs the file; CC and PAS compile */
{
    return (uint8_t)(t && (t[0] == 'R' || t[0] == 'M' || t[0] == 'L'));
}
static void mkdir_of_name(void)                      /* the file's directory: MAKE.ERR's names are there */
{
    const char *e = base_of(name), *s; uint8_t k = 0;
    for (s = name; s < e && k < NAMEMAX - 1; ) ed_mkdir[k++] = *s++;
    ed_mkdir[k] = 0;
}
static uint8_t do_make(void)                         /* 1 if it compiled without an error */
{
    char *c = shline; const char *tool = compiler(), *s; uint8_t i = 0, rc; unsigned e;
    if (!name[0]) { note = "make: the file has no name -- :w NAME first"; return 0; }
    if (!tool && !ed_mkline[0]) { note = "make: no compiler for this file (.C .PAS .RX .BAS .LGO)"; return 0; }
    if (dirty) {                                     /* only a changed file: re-saving an unchanged one made it */
        save_file();                                 /* newer than its object, and a project recompiled it for */
        if (note[0] != 'w') return 0;                /* nothing (the Dell: "2 compiled, 0 kept", 2026-09-14) */
    }
    if (interpreted(tool)) { note = "saved -- nothing to compile: run it (^F9, :run)"; return 1; }
    if (ed_mkline[0]) {                              /* the front end's build: it has set ed_mkdir too */
        for (s = ed_mkline; *s && i < sizeof shline - 1; ) c[i++] = *s++;
        c[i] = 0;
    } else {
        for (s = tool; *s; ) c[i++] = *s++;
        c[i++] = ' ';
        for (s = name; *s && i < sizeof shline - 1; ) c[i++] = *s++;
        c[i] = 0;
        mkdir_of_name();
    }
    rc = rom_shell(c);
    screen_back();
    err_load();
    if (!rc && nerr == nwarn) {
        nb_reset(); nb_s(info[0] ? info : "compiled");
        if (nwarn) { nb_s(", "); nb_n(nwarn); nb_s(nwarn == 1 ? " warning (:cn)" : " warnings (:cn)"); }
        note = nbuf;
        return 1;
    }
    for (e = 0; e < nerr; e++) { far_get(ERRTAB + ((uint32_t)e << 7) + 3, &i, 1); if (i == 'E') break; }
    if (e < nerr) err_go(e);
    else if (nerr) err_go(0);
    else { nb_reset(); nb_s("make: failed, rc "); nb_n(rc); nb_s(" -- nothing in MAKE.ERR"); note = nbuf; }
    return 0;
}
static void do_run(void)
{
    char *c = shline; const char *s, *dot = 0; uint8_t i = 0, rc; unsigned keepy = cy; uint8_t keepx = cx;
    const char *tool = compiler(); uint8_t rx = (uint8_t)(tool != 0 && tool[0] == 'R'), interp = interpreted(tool);
    if (!do_make()) return;
    for (s = "SWAP -k "; *s; ) c[i++] = *s++;
    if (interp) {                                    /* REXX, BASIC, LOGO: the interpreter runs the file (REXX's die() writes MAKE.ERR) */
        for (s = tool; *s; ) c[i++] = *s++;
        c[i++] = ' ';
        for (s = name; *s && i < sizeof shline - 1; ) c[i++] = *s++;
        mkdir_of_name();
    }
    else if (ed_runname[0]) for (s = ed_runname; *s && i < sizeof shline - 1; ) c[i++] = *s++;   /* the project's program */
    else {
        for (s = name; *s; s++) if (*s == '.') dot = s;
        for (s = name; *s && s != dot && i < sizeof shline - 1; ) c[i++] = *s++;
    }
    c[i] = 0;
    rc = rom_shell(c);
    at((uint8_t)(rows - 1), 0); sgr("7"); say(" -- a key returns -- "); sgr("0");
    while (!rom_getin()) ;
    rom_video();
    cols = REG(TERM + 5); rows = REG(TERM + 6);
    if (!cols) cols = 80;
    if (!rows) rows = 30;
    screen_back();
    load_file();                                     /* the program may have used the far memory the text was in */
    ujp = ujn = 0; reglines = 0;
    goline(keepy); cx = keepx;
    if (cx >= ln[0]) cx = ln[0] ? (uint8_t)(ln[0] - 1) : 0;
    dirty = 0;
    if (rx) { err_load(); if (nerr) { err_go(0); return; } }   /* "RX: line 12: ..." -- to line 12 */
    if (rc) { nb_reset(); nb_s("run: rc "); nb_n(rc); nb_s(" (from inside a SWAP? leave the editor and run it)"); note = nbuf; }
    else note = "ran it; the file is as saved";
}

/* ---- buffers ---------------------------------------------------------------
 * PROG's tabs: up to NBUF files open, one current.  The current file is the
 * globals above; the others wait in ed_bufs, each with its own far memory
 * (its lines at .slots, its undo at .undo) -- so switching is a copy of a
 * few words and a line, not of the text.  VI has one file and never calls
 * these. */
#define NBUF 8
struct edbuf { char name[NAMEMAX]; uint32_t slots, undo; unsigned nlines, cy, top, ujp, ujn; uint8_t cx, dirty, useq; };
static struct edbuf ed_bufs[NBUF];
static uint8_t ed_cur, ed_nbuf = 1;
static void ed_buf_store(void)                        /* the current file into its record */
{
    struct edbuf *b = &ed_bufs[ed_cur];
    line_out(cy); u_end();
    memcpy(b->name, name, NAMEMAX);
    b->slots = ed_slots; b->undo = ed_undo; b->nlines = nlines; b->cy = cy; b->top = top;
    b->ujp = ujp; b->ujn = ujn; b->cx = cx; b->dirty = dirty; b->useq = useq;
}
static void ed_buf_fetch(uint8_t i)                   /* file i becomes the current one */
{
    struct edbuf *b = &ed_bufs[i];
    ed_cur = i;
    memcpy(name, b->name, NAMEMAX);
    ed_slots = b->slots; ed_undo = b->undo; nlines = b->nlines; cy = b->cy; top = b->top;
    ujp = b->ujp; ujn = b->ujn; cx = b->cx; dirty = b->dirty; useq = b->useq;
    line_in(cy); full = 1;
}
