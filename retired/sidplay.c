/* K4510 SID player. PSID tunes from /SID, played by their own 6502 code
 * on SID 0, the way a C64 would: the player lives under the ROM at $E000
 * (K-05) so the tune may own $0400-$CFFF; the zero page is swapped around
 * every call into the tune; play() runs at the tune's rate (50 Hz PAL,
 * 60 Hz NTSC or CIA) from the frame counter. VICKY registers a tune may
 * poke (it thinks they are the VIC-II) are put back every frame.
 *   chooser: cursor keys, PgUp/PgDn, Enter plays, Esc leaves
 *   playing: +/- next/previous song, space next file, Esc back to the list */
#include "k4510.h"
#include "far.h"

void __fastcall__ tune_call(unsigned addr);
extern unsigned char tune_a;

#define SCREEN   0x00030000UL        /* the ROM's text map, 80 cells x 4 bytes */
#define TERM     0xDA00u             /* $DA07/$DA08: the console's origin, OX and OY */
#define LISTBUF  0x00300000UL        /* file names, 32 bytes each */
#define FILEBUF  0x00310000UL        /* the loaded .sid */
#define MAXFILES 400
/* The console's origin is NOT fixed at (1,1): the F7 menu can turn the
 * one-cell margin off, and then it is (0,0).  This was hardcoded, so with the
 * margin off the program drew one cell in from the edge and left column 0 and
 * row 0 holding the previous screen -- a stripe of junk down the side.  The
 * ROM publishes the real origin at $DA07/$DA08 (video_init), so read it; and
 * clear the whole PHYSICAL screen rather than our own window.  But the whole
 * screen was too much: with the status bar on (F7) the console is a window
 * between two static bands, and wiping every cell took the bands with it --
 * they were still gone when the shell came back.  And 79x29 was not the size
 * of that window either, so the list ran into the bottom band.  So the whole
 * geometry is read from JIM ($DA05-$DA08: COLS ROWS OX OY) and nothing is
 * drawn or cleared outside it. */
static uint8_t s_ox = 1, s_oy = 1, s_cols = 79, s_rows = 29;
static uint8_t page = 24, ptot = 48;      /* the list: rows per column, entries per page */
#define C_WHITE 1
#define C_YEL 7
#define C_GREY 12
#define C_LBLUE 14
#define C_GREEN 13
#define C_BLUE 6
#define KEY_UP 0x80
#define KEY_DOWN 0x81
#define KEY_LEFT 0x82
#define KEY_RIGHT 0x83
#define KEY_HOME 0x84
#define KEY_END 0x85
#define KEY_PGUP 0x86
#define KEY_PGDN 0x87

static void put(uint8_t x, uint8_t y, uint8_t ch, uint8_t fg)
{
    uint32_t c;
    if (x >= s_cols || y >= s_rows) return;         /* the window is the whole world */
    c = SCREEN + ((uint32_t)(y + s_oy) * 80 + x + s_ox) * 4;
    far_poke(c, ch); far_poke(c + 1, 0); far_poke(c + 2, fg); far_poke(c + 3, C_BLUE);
}
static void text(uint8_t x, uint8_t y, const char *s, uint8_t fg) { while (*s) put(x++, y, (uint8_t)*s++, fg); }
static void textn(uint8_t x, uint8_t y, uint32_t fa, uint8_t n, uint8_t fg) { uint8_t i; for (i = 0; i < n; i++) { uint8_t ch = far_peek(fa + i); put(x + i, y, ch ? ch : ' ', fg); } }
/* JIM knows the window -- it is the same one -- so let it do the clearing:
 * it never touches a cell outside, which is exactly what the bands need. The
 * colours are set first, because the attributes are whatever the last program
 * to write a stream left behind. */
static void clear_all(void)
{
    REG(TERM + 0x0B) = C_YEL; REG(TERM + 0x0C) = C_BLUE;
    REG(TERM + 4) = 2;
}
static void dec(uint8_t x, uint8_t y, uint16_t v, uint8_t fg) { char b[6]; uint8_t i = 5; b[i] = 0; do { b[--i] = '0' + v % 10; v /= 10; } while (v); text(x, y, b + i, fg); }
static void dec2(uint8_t x, uint8_t y, uint8_t v, uint8_t fg) { put(x, y, '0' + v / 10, fg); put(x + 1, y, '0' + v % 10, fg); }
static void hex4(uint8_t x, uint8_t y, uint16_t v) { static const char h[] = "0123456789ABCDEF"; uint8_t i; for (i = 0; i < 4; i++) { put(x + 3 - i, y, h[v & 15], C_GREY); v >>= 4; } }

/* ---- the file list ------------------------------------------------------ */
static uint16_t nfiles;
static char fsname[36];
static char oldcwd[36];
static uint8_t fs_cmd(uint8_t c) { REG(0xD300) = c; return REG(0xD301); }
static void fs_setname(const char *n) { far_w32(0xD304, (uint16_t)n); }
static void list_dir(void)
{
    nfiles = 0;
    fs_setname("/SID"); if (fs_cmd(11)) { fs_setname("/"); fs_cmd(11); }
    if (fs_cmd(6)) return;
    for (;;) {
        uint8_t i, len; char *e;
        far_w32(0xD308, (uint16_t)fsname);
        if (fs_cmd(7)) break;
        if (far_r32(0xD310) == 0xFFFFFFFFUL) continue;                 /* a directory */
        len = 0; while (fsname[len]) len++;
        if (len < 5) continue; e = fsname + len - 4;
        if (e[0] != '.' || (e[1] | 0x20) != 's' || (e[2] | 0x20) != 'i' || (e[3] | 0x20) != 'd') continue;
        for (i = 0; i < 31; i++) far_poke(LISTBUF + (uint32_t)nfiles * 32 + i, i < len ? fsname[i] : 0);
        far_poke(LISTBUF + (uint32_t)nfiles * 32 + 31, 0);
        if (++nfiles == MAXFILES) break;
    }
}
static void get_name(uint16_t n, char *out) { uint8_t i; for (i = 0; i < 31; i++) out[i] = far_peek(LISTBUF + (uint32_t)n * 32 + i); out[31] = 0; }
static uint32_t r32far(uint32_t a) { return (uint32_t)far_peek(a) | ((uint32_t)far_peek(a + 1) << 8) | ((uint32_t)far_peek(a + 2) << 16) | ((uint32_t)far_peek(a + 3) << 24); }

/* ---- the chooser -------------------------------------------------------- */
static void draw_row(uint16_t top, uint16_t i, uint8_t sel)
{
    uint8_t r = (uint8_t)(i - top), x, y;
    if (i >= nfiles || r >= ptot) return;
    x = (r < page) ? 2 : 41; y = (uint8_t)(2 + r % page);
    put(x - 2, y, sel ? 0x10 : ' ', C_WHITE);
    textn(x, y, LISTBUF + (uint32_t)i * 32, 31, sel ? C_WHITE : C_YEL);
}
static void draw_list(uint16_t top, uint16_t cur)
{
    uint16_t i;
    clear_all();
    text(0, 0, "K4510 SID player", C_WHITE); text(22, 0, "/SID", C_GREY);
    dec(30, 0, nfiles, C_GREY); text(35, 0, "tunes", C_GREY);
    text(0, s_rows - 1, "cursor keys, PgUp/PgDn, Enter plays, Esc leaves", C_GREY);
    for (i = top; i < nfiles && i < top + ptot; i++) draw_row(top, i, i == cur);
}

/* ---- the tune ----------------------------------------------------------- */
static uint16_t init_addr, play_addr, load_addr, nsongs, song, end_addr; static uint8_t is_rsid, rate, clocksel, collides;
static uint8_t vregs[0x50];
static void save_video(void) { uint8_t i; for (i = 0; i < 0x50; i++) vregs[i] = REG(0xD000 + i); }
static void restore_video(void) { uint8_t i; for (i = 0x10; i < 0x50; i++) REG(0xD000 + i) = vregs[i]; REG(0xD000) = vregs[0]; REG(0xD001) = vregs[1]; }
static void sid_silence(void) { uint8_t i; for (i = 0; i < 25; i++) { REG(0xD400 + i) = 0; REG(0xD420 + i) = 0; } }

static uint8_t load_tune(uint16_t n)
{
    uint16_t hdr, ver, flags; uint32_t size, speed; char name[32];
    get_name(n, name);
    fs_setname(name); far_w32(0xD308, FILEBUF);
    if (fs_cmd(9)) return 1;
    size = far_r32(0xD30C);
    is_rsid = far_peek(FILEBUF) == 'R';
    ver = far_peek(FILEBUF + 5);
    hdr = ((uint16_t)far_peek(FILEBUF + 6) << 8) | far_peek(FILEBUF + 7);
    load_addr = ((uint16_t)far_peek(FILEBUF + 8) << 8) | far_peek(FILEBUF + 9);
    init_addr = ((uint16_t)far_peek(FILEBUF + 10) << 8) | far_peek(FILEBUF + 11);
    play_addr = ((uint16_t)far_peek(FILEBUF + 12) << 8) | far_peek(FILEBUF + 13);
    nsongs    = ((uint16_t)far_peek(FILEBUF + 14) << 8) | far_peek(FILEBUF + 15);
    song      = ((uint16_t)far_peek(FILEBUF + 16) << 8) | far_peek(FILEBUF + 17);
    speed     = ((uint32_t)far_peek(FILEBUF + 18) << 24) | ((uint32_t)far_peek(FILEBUF + 19) << 16) | ((uint32_t)far_peek(FILEBUF + 20) << 8) | far_peek(FILEBUF + 21);
    flags = ver >= 2 ? (((uint16_t)far_peek(FILEBUF + 0x76) << 8) | far_peek(FILEBUF + 0x77)) : 0;
    if (load_addr == 0) { load_addr = far_peek(FILEBUF + hdr) | ((uint16_t)far_peek(FILEBUF + hdr + 1) << 8); hdr += 2; }
    if (size <= hdr) return 2;
    if (!song) song = 1;
    rate = ((flags >> 2) & 3) == 2 ? 60 : 50;                       /* NTSC tunes at 60, PAL at 50 */
    clocksel = ((flags >> 2) & 3) == 2 ? 2 : 1;                      /* the SID's crystal: PAL 985248 Hz, NTSC 1022730 */
    if (speed & 1) rate = 60;                                        /* CIA-timed: ~60 Hz */
    end_addr = load_addr + (uint16_t)(size - hdr);
    /* the tune must fit in the C64 window the player leaves free: $0400-$CFFF */
    collides = (load_addr < 0x0300) || (end_addr > 0xD000);
    if (collides) return 0;                                          /* shown, not loaded */
    far_copy(load_addr, FILEBUF + hdr, size - hdr);                  /* into the C64's memory: the CPU view, ROM out */
    return 0;
}

static void show_info(uint16_t n)
{
    char name[32];
    get_name(n, name);
    clear_all();
    text(0, 0, "K4510 SID player", C_WHITE); text(22, 0, name, C_GREY);
    text(0, 2, "title   ", C_LBLUE); textn(8, 2, FILEBUF + 0x16, 32, C_WHITE);
    text(0, 3, "author  ", C_LBLUE); textn(8, 3, FILEBUF + 0x36, 32, C_YEL);
    text(0, 4, "released", C_LBLUE); textn(8, 4, FILEBUF + 0x56, 32, C_YEL);
    hex4(6, 6, load_addr); text(0, 6, "load $", C_GREY);
    text(11, 6, "init $", C_GREY); hex4(17, 6, init_addr);
    text(22, 6, "play $", C_GREY); hex4(28, 6, play_addr);
    dec(34, 6, rate, C_GREY); text(37, 6, "Hz", C_GREY); text(41, 6, clocksel == 2 ? "NTSC" : "PAL ", C_GREY);
    text(66, 7, "tune", C_GREY); text(66, 8, "real", C_GREY);
    text(0, 9,  "voice 1", C_LBLUE); text(0, 11, "voice 2", C_LBLUE); text(0, 13, "voice 3", C_LBLUE);
    text(0, s_rows - 1, "+/- song   space next tune   Esc back to the list", C_GREY);
    if (is_rsid) text(0, 16, "RSID: not supported (needs a real C64)", C_YEL);
    else if (!play_addr) text(0, 16, "play $0: tune sets its own IRQ, not supported", C_YEL);
    else if (collides) text(0, 16, "tune loads outside $0300-$CFFF (player lives there)", C_YEL);
}
static void show_song(void) { text(0, 7, "song", C_GREY); dec(5, 7, song, C_WHITE); text(9, 7, "of", C_GREY); dec(12, 7, nsongs, C_WHITE); }
static void show_time(uint8_t y, uint16_t sec) { dec2(72, y, sec / 60, C_WHITE); put(74, y, ':', C_WHITE); dec2(75, y, sec % 60, C_WHITE); }
/* Real elapsed seconds, by watching the host clock's seconds tick (SYS+4
 * latches, +5 is seconds). No multiply -- small. If "tune" lags "real", the
 * machine is losing frames; if they agree, a slow tune is pitch, not tempo. */
static uint16_t rt_secs; static uint8_t rt_ls;
static void rt_reset(void) { volatile uint8_t d = REG(SYS + 4); (void)d; rt_ls = REG(SYS + 5); rt_secs = 0; }
static void rt_tick(void) { volatile uint8_t d = REG(SYS + 4); uint8_t s = REG(SYS + 5); (void)d; if (s != rt_ls) { rt_ls = s; rt_secs++; } }
static void show_meters(void)
{
    static const char *const wn[5] = { "   ", "tri", "saw", "pul", "noi" };
    uint8_t v, i;
    for (v = 0; v < 3; v++) {
        uint16_t b = 0xD400 + v * 7; uint8_t ctl = REG(b + 4), hi = REG(b + 1), y = 9 + v * 2, n = (ctl & 1) ? (hi >> 2) + 1 : 0, w;
        w = (ctl & 0x80) ? 4 : (ctl & 0x40) ? 3 : (ctl & 0x20) ? 2 : (ctl & 0x10) ? 1 : 0;
        text(8, y, (ctl & 1) ? wn[w] : wn[0], C_GREY);
        for (i = 0; i < 64; i++) put(12 + i, y, i < n ? 0xDB : 0xFA, (ctl & 1) ? C_GREEN : C_GREY);
    }
}

static uint8_t start_song(void)
{
    sid_silence();
    tune_a = (uint8_t)(song - 1);
    tune_call(init_addr);
    restore_video();
    return 0;
}

/* returns 0 = back to the list, 1 = next file */
static uint8_t play_file(uint16_t n)
{
    uint8_t acc = 0, last = 0, k; uint16_t frames = 0, sec = 0;
    if (load_tune(n)) { show_info(n); text(0, 16, "could not load or parse this file", C_YEL); while (!(k = key_get())) ; return k == ' ' ? 1 : 0; }
    show_info(n); show_song();
    if (is_rsid || !play_addr || collides) { while (!(k = key_get())) ; return k == ' ' ? 1 : 0; }
    REG(0xD5F3) = clocksel;                                /* the SID crystal this tune expects */
    save_video();
    start_song();
    rt_reset(); show_time(8, 0);
    for (;;) {
        uint8_t f = REG(SYS + 0x0D);
        if (f != last) {                                   /* a new frame */
            /* Advance one tune-frame per frame ELAPSED, not per pass: at a low clock
             * one pass (meters + the tune) can outlast a frame, so SYS+$0D ticks
             * more than once between reads. Counting passes lost the rest -- the
             * tune ran at 40% (tune 0:17 vs real 0:43 at 15 MHz). A loop over the
             * elapsed count catches up with no multiply. f-last wraps as a byte. */
            uint8_t d = (uint8_t)(f - last); last = f;
            while (d--) {
                acc += rate; if (acc >= 60) { acc -= 60; tune_call(play_addr); }
                if (++frames == 60) { frames = 0; ++sec; }
            }
            restore_video(); show_time(7, sec);
            rt_tick(); show_time(8, rt_secs);
            show_meters();
        }
        k = key_get();
        if (k == 0x1B || k == 'q' || k == 'Q') { sid_silence(); REG(0xD5F3) = 0; return 0; }
        if (k == ' ' || k == KEY_RIGHT || k == KEY_DOWN) { sid_silence(); REG(0xD5F3) = 0; return 1; }
        if (k == KEY_LEFT || k == KEY_UP) { sid_silence(); REG(0xD5F3) = 0; return 2; }
        if (k == '+' || k == '=' || k == KEY_PGDN) { if (song < nsongs) song++; else song = 1; start_song(); show_song(); sec = 0; show_time(7, 0); rt_reset(); show_time(8, 0); }
        if (k == '-' || k == KEY_PGUP) { if (song > 1) song--; else song = nsongs; start_song(); show_song(); sec = 0; show_time(7, 0); rt_reset(); show_time(8, 0); }
    }
}

void main(void)
{
    uint16_t cur = 0, top = 0, oldcur, oldtop; uint8_t k;
    rom_out();                                            /* $0800-$CFFF and $E000-$FEFF are ours */
    /* The whole window, before anything is drawn.  Everything below is laid
     * out in these, so the status bands -- which shrink the window -- are
     * neither drawn on nor wiped. */
    s_cols = REG(TERM + 5); s_rows = REG(TERM + 6);
    s_ox = REG(TERM + 7);   s_oy = REG(TERM + 8);
    page = (uint8_t)(s_rows - 5);             /* rows 2.. , the footer on the last one */
    ptot = s_cols >= 72 ? (uint8_t)(page * 2) : page;   /* the second list column starts at 41 */
    far_w32(0xD308, (uint16_t)oldcwd); fs_cmd(15);        /* remember the caller's directory */
    list_dir();
    draw_list(top, cur);
    for (;;) {
        while (!(k = key_get())) ;
        oldcur = cur; oldtop = top;
        /* coalesce: handle every queued key before drawing anything (holding a
         * cursor key fills the FIFO much faster than a full redraw drains it) */
        do {
            if (k == 0x1B || k == 'q' || k == 'Q') { sid_silence(); REG(TERM + 4) = 2; fs_setname(oldcwd); fs_cmd(11); return; }
            if (k == KEY_DOWN && cur + 1 < nfiles) cur++;
            else if (k == KEY_UP && cur) cur--;
            else if (k == KEY_RIGHT && cur + page < nfiles) cur += page;
            else if (k == KEY_LEFT && cur >= page) cur -= page;
            else if (k == KEY_PGDN) { cur += ptot; if (cur >= nfiles) cur = nfiles - 1; }
            else if (k == KEY_PGUP) { cur = cur >= ptot ? cur - ptot : 0; }
            else if (k == KEY_HOME) cur = 0;
            else if (k == KEY_END) cur = nfiles - 1;
            else if (k == 0x0D && nfiles) {
                uint8_t r;
                do { r = play_file(cur); if (r == 1 && cur + 1 < nfiles) cur++; else if (r == 2 && cur) cur--; else if (r) r = 0; } while (r);
                top = cur - cur % ptot;
                draw_list(top, cur);
                oldcur = cur; oldtop = top;
                break;
            }
        } while ((k = key_get()) != 0);
        if (cur < top || cur >= top + ptot) top = cur - cur % ptot;
        if (top != oldtop) draw_list(top, cur);            /* a new page: full redraw */
        else if (cur != oldcur) { draw_row(top, oldcur, 0); draw_row(top, cur, 1); }
    }
}
