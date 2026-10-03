/* demo/dosvi.h -- VI's keys in the DOS editors, EDIT and PROG (2026-10-02).
 * vikeys.h is VI's normal mode, the code VI itself runs; this is how an
 * editor in EDIT's window feeds it: which keys go to it, the maps (:imap jk
 * <Esc>, and VI.RC's), the : line on the status row, and p and P, which put
 * characters back as well as lines (the clipboard is the same register, so
 * Ctrl+C then p works).
 *
 * The editor reads keys with vi_event() instead of event(), gives each one
 * to vi_key() first (1: taken), draws the status line with vi_status() when
 * it has no note to show, and calls vi_init() once.  What a : command asks
 * of the editor itself -- save, leave, compile -- comes back in vi_act (and
 * a name in vi_arg), for the editor to do when vi_key returns.
 *
 * Overlays: all of this is 9 KB of code, which neither EDIT nor PROG has
 * room for.  So it is two overlays, both linked for $E000 and loaded into
 * far memory (the editor's K4SG header puts them at DOSVI_P1 and DOSVI_P2):
 * VIO1 is vikeys.h, VIO2 the glue below.  A call goes through the far-call
 * gate ($DF00 + 4n, core/mem.c), which banks block 7 onto the overlay and
 * puts it back on the return -- so the editor's own code at $E000 (its
 * HICODE) is out of sight while an overlay runs, and nothing here may call
 * it; that is why a : command answers in vi_act instead of saving.  The
 * calls INTO an overlay, from anywhere, are the vi_* macros below; within
 * VIO2 the five it needs of VIO1 are slots too.  vi_setup() writes the gate
 * table; the editor calls it each time round its loop (a program run from
 * it may have written its own).
 *
 *   vi_act   'w' save (to vi_arg if it is not empty)   'q' leave, asking
 *            'Q' leave, not asking   'x' save, then leave
 *            'm' :make   'r' :run   'n' :cn   'p' :cp
 *
 * #include "k4510.h", "ed.h", "dosui.h" and "dosed.h" first, and declare
 * `static uint8_t vimode;' (1: VI's keys on). */

static uint8_t vik_key, vi_act;                 /* the editor declares vimode, before it draws */
static const char *vi_arg;
#define vik_page (th - 1)
#define VIK_CHARREG
#define VIK_MAPMAX 8                            /* BSS is short in both: eight maps, sixteen keys a side */
#define VIK_MAPRHS 16
#pragma code-name (push, "VIO1")
static void vik_put(uint8_t after)
{
    t_end();
    if (reglinewise || !reglines) { do_put(after); return; }
    if (after && cx < ln[0]) cx++;
    put_chars();
    if (cx) cx--;
}
#include "vikeys.h"
#pragma code-name (pop)

static void vi_clamp(void) { if (mode != 1 && ln[0] && cx >= ln[0]) cx = (uint8_t)(ln[0] - 1); }

/* ---- the gate ---------------------------------------------------------------- */
#define VIG(n) (0xDF00u + 4 * (n))
#define vi_key(k)       ((uint8_t (__fastcall__ *)(uint8_t))VIG(0))(k)
#define vi_status()     ((void (*)(void))VIG(1))()
#define vi_event()      ((uint8_t (*)(void))VIG(2))()
#define vi_init()       ((void (*)(void))VIG(3))()
#define vi_toggle()     ((void (*)(void))VIG(4))()
#define vi_normal(k)    ((void (__fastcall__ *)(uint8_t))VIG(5))(k)
#define map_feed(k)     ((uint8_t (__fastcall__ *)(uint8_t))VIG(6))(k)
#define map_timeout()   ((void (*)(void))VIG(7))()
#define do_map(c, m)    ((void (*)(const char *, uint8_t))VIG(8))(c, m)
#define do_sub(c)       ((void (__fastcall__ *)(const char *))VIG(9))(c)
#pragma code-name (push, "VIO2")

/* ---- keys, through the maps ------------------------------------------------
 * Only characters are mapped, and only with VI's keys on; a key code or the
 * mouse arriving while a match waits sends the waiting keys first and then
 * itself (held: event() is not called again until it has gone). */
static uint8_t vh_k, vh_kc, vh_mod, vh_held;
static uint8_t vi_event_o(void)
{
    uint8_t k;
    for (;;) {
        if (qi < qn) { kcode = 0; kmod = 0; return qbuf[qi++]; }
        if (vh_held) { vh_held = 0; kcode = vh_kc; kmod = vh_mod; return vh_k; }
        if (!vimode || mode == 2) return event();
        ev_wait = (uint8_t)(pbn ? 60 : 0);            /* a second, as vim's timeoutlen */
        k = event();
        ev_wait = 0;
        if (!k && !kcode) { map_timeout(); continue; }
        if (kcode) {
            if (!pbn) return k;
            vh_k = k; vh_kc = kcode; vh_mod = kmod; vh_held = 1; map_timeout(); continue;
        }
        if (map_feed(k)) return k;
    }
}

/* ---- VI.RC's maps ------------------------------------------------------------
 * /SYSTEM/ETC/VI.RC's map and imap lines, so the jk that works in VI works
 * here too (ed.h reads the same file for set ts=). */
static void vi_rcline(void)
{
    if (ed_rl[0] == 'm' && ed_rl[1] == 'a' && ed_rl[2] == 'p' && ed_rl[3] == ' ') do_map(ed_rl + 4, 0);
    else if (ed_rl[0] == 'i' && ed_rl[1] == 'm' && ed_rl[2] == 'a' && ed_rl[3] == 'p' && ed_rl[4] == ' ') do_map(ed_rl + 5, 1);
}
static void vi_init_o(void)
{
    uint32_t l, off = 0; unsigned chunk, i; uint8_t n = 0; const char *keep = note;
    zp16(0xF0, (uint16_t)ed_rcname); zp32(0xF2, FLAT);   /* ed.h's: in the main image, where the file device reads it */
    if (rom_load()) return;
    l = zpr32(0xF6);
    while (off < l) {
        chunk = (l - off) > 128 ? 128 : (unsigned)(l - off);
        far_get(FLAT + off, tmp, chunk);
        for (i = 0; i < chunk; i++) {
            if (tmp[i] == '\n') { ed_rl[n] = 0; vi_rcline(); n = 0; }
            else if (tmp[i] != '\r' && n < sizeof ed_rl - 1) ed_rl[n++] = (char)tmp[i];
        }
        off += chunk;
    }
    ed_rl[n] = 0; vi_rcline();
    note = keep;
}

/* ---- the : line ------------------------------------------------------------- */
static uint8_t exw(const char *w)                     /* cmd is the word w (upper case here), alone or before a space */
{
    uint8_t i = 0;
    while (w[i]) { if (rn_up((uint8_t)cmd[i]) != (uint8_t)w[i]) return 0; i++; }
    return (uint8_t)(cmd[i] == 0 || cmd[i] == ' ');
}
static void vi_ex(void)                               /* the : line, or a / ? search */
{
    uint8_t i = 0, w = 0, q = 0, bang = 0; unsigned n = 0;
    mode = 0;
    if (cprompt != ':') {
        for (patlen = 0; cmd[patlen] && patlen < NAMEMAX - 1; patlen++) pat[patlen] = cmd[patlen];
        lastdir = (uint8_t)(cprompt == '/');
        if (patlen) search(lastdir ? 1 : -1);
        return;
    }
    if (cmd[0] >= '0' && cmd[0] <= '9') { for (; cmd[i] >= '0' && cmd[i] <= '9'; i++) n = n * 10 + (unsigned)(cmd[i] - '0'); go(n ? n - 1 : 0); cx = 0; return; }
    if (cmd[0] == '$' && !cmd[1]) { go(nlines - 1); cx = 0; return; }
    if (exw("SET")) {
        const char *v = cmd + 4;
        if (v[0] == 't' && v[1] == 's' && v[2] == '=') ed_set_tabw(v + 3);
        return;
    }
    if (exw("MAP")) { do_map(cmd + 3, 0); return; }
    if (exw("IMAP")) { do_map(cmd + 4, 1); return; }
    if (cmd[0] == 's' || (cmd[0] == '%' && cmd[1] == 's')) { t_end(); do_sub(cmd); return; }
#ifndef ED_NO_RENUM
    if (exw("RENUM")) { t_end(); do_renum(cmd + 5); return; }
#endif
    if (exw("MAKE")) { vi_act = 'm'; return; }
    if (exw("RUN")) { vi_act = 'r'; return; }
    if (exw("CN")) { vi_act = 'n'; return; }
    if (exw("CP")) { vi_act = 'p'; return; }
    for (; cmd[i] && cmd[i] != ' '; i++) {            /* w q x, either case, and a ! */
        uint8_t c = rn_up(cmd[i]);
        if (c == 'W') w = 1; else if (c == 'Q') q = 1; else if (c == 'X') { w = 1; q = 1; } else if (c == '!') bang = 1;
        else { note = "Not an editor command"; return; }
    }
    vi_arg = cmd + i; while (*vi_arg == ' ') vi_arg++;
    vi_act = (uint8_t)(w && q ? 'x' : w ? 'w' : q ? (bang ? 'Q' : 'q') : 0);
}

/* ---- one key: 1 if VI's mode took it, 0 for the editor ------------------------ */
static uint8_t vi_key_o(uint8_t k)
{
    vi_act = 0;
    if (mode == 2) {                                  /* the : line */
        if (kcode) return 1;
        if (k == 0x0D) vi_ex();
        else if (k == 0x1B) mode = 0;
        else if (k == 0x08) { if (cmdlen) cmd[--cmdlen] = 0; else mode = 0; }
        else if (k >= 0x20 && k < 0x7F && cmdlen < NAMEMAX - 2) { cmd[cmdlen++] = (char)k; cmd[cmdlen] = 0; }
        vi_clamp();
        return 1;
    }
    if (mode == 1) {                                  /* insert: the editor's own keys, Esc back to normal */
        if (kcode || k != 0x1B) return 0;
        t_end(); u_end(); sel_clear(); mode = 0; if (cx) cx--;
        return 1;
    }
    if (kcode) {                                      /* the arrows and their kind: the editor's, unless a d c y or a count waits */
        if (!op && !pend && !cnt) { if (!window_key(k)) return 0; vi_clamp(); return 1; }
        if (k > KPGDN) return 0;
    } else if (k >= 0x80) return 1;                   /* an accented letter means nothing in normal mode */
    else if (k < 0x20 && k != 0x1B && k != 0x0D && k != 0x08 && k != 0x12 && k != 0x15 && k != 0x04 && !op && !pend) return 0;   /* the editor's Ctrl keys */
    vik_key = (uint8_t)(kcode != 0);
    sel_clear(); t_end();
    vi_normal(k);
    vi_clamp(); wantx = cx;
    return 1;
}

/* the status line in VI's modes, and the cursor; the editor's line:column at the right */
static void vi_status_o(void)
{
    uint8_t x; const char *s;
    for (x = 0; x < cols; x++) cel(x, ' ', K_STATUS);
    if (mode == 2) {
        cel(1, cprompt, K_STATUS);
        for (x = 0; x < cmdlen; x++) cel((uint8_t)(2 + x), (uint8_t)cmd[x], K_STATUS);
        flush((uint8_t)(rows - 1), cols);
        cursor_shape('4'); cursor_at((uint8_t)(2 + cmdlen), (uint8_t)(rows - 1));
        return;
    }
    s = mode == 1 ? "-- INSERT --" : op == 'c' ? "-- c: a motion --" : op == 'y' ? "-- y: a motion --" : op == 'd' ? "-- d: a motion --"
        : "VI keys  <F1=Help>  <F10 or Alt=Menus>  :q leaves";
    for (x = 0; s[x]; x++) cel((uint8_t)(1 + x), (uint8_t)s[x], K_STATUS);
    { char p[10]; where(p); for (x = 0; x < 9; x++) cel((uint8_t)(cols - 10 + x), (uint8_t)p[x], K_STATUS); }
    flush((uint8_t)(rows - 1), cols);
    cursor_shape((uint8_t)(mode == 1 && !over ? '4' : '2'));   /* a block in normal mode, as VI's */
    cursor_at((uint8_t)(1 + cx - hoff), (uint8_t)(wy + cy - top));
}
static void vi_toggle_o(void)                           /* Options > VI Keys */
{
    vimode = (uint8_t)!vimode; mode = 0; op = pend = 0; cnt = 0; vi_clamp();
    note = vimode ? "VI keys: Esc is normal mode, i inserts, :q leaves" : "The editor's own keys";
}
#pragma code-name (pop)

/* the table: slots 0-4 VIO2's, 5-9 VIO1's (the order of the macros above) */
static struct { uint32_t base; uint8_t block, flags; uint16_t entry; } vi_tab[10];
static void vi_setup(void)
{
    uint8_t i;
    if (!vi_tab[0].entry) {
        vi_tab[0].entry = (uint16_t)vi_key_o;    vi_tab[1].entry = (uint16_t)vi_status_o; vi_tab[2].entry = (uint16_t)vi_event_o;
        vi_tab[3].entry = (uint16_t)vi_init_o;   vi_tab[4].entry = (uint16_t)vi_toggle_o;
        vi_tab[5].entry = (uint16_t)vi_normal;   vi_tab[6].entry = (uint16_t)map_feed;    vi_tab[7].entry = (uint16_t)map_timeout;
        vi_tab[8].entry = (uint16_t)do_map;      vi_tab[9].entry = (uint16_t)do_sub;
        for (i = 0; i < 10; i++) { vi_tab[i].base = i < 5 ? DOSVI_P2 : DOSVI_P1; vi_tab[i].block = 7; }
    }
    REG(0xDF80) = (uint8_t)(uint16_t)vi_tab; REG(0xDF81) = (uint8_t)((uint16_t)vi_tab >> 8); REG(0xDF82) = 0; REG(0xDF83) = 0;
}
