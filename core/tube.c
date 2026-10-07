/* ---- the Tube ($D800) ---------------------------------------------------
 * The co-processor is a child process on a pty: BBC BASIC, RunCPM, the
 * host's shell for `!`, or the chess engine.  (Until 2026-09-14 there was
 * a second transport, the interpreter compiled in and run on a core of the
 * bare-metal Pi or a thread of a test build; it went with the Pi port.) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <termios.h>
#ifdef __linux__
#include <sys/prctl.h>          /* PR_SET_PDEATHSIG: the child dies with the emulator */
#endif
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include "io.h"
#include "io_int.h"
#include "mem.h"
#include "vicky.h"
#include "term.h"
static pid_t tube_pid; static int tube_fd = -1;
int tube_prog_now;                             /* the Tube program started last, while it lives (io_title) */
int tube_prog_at = 1;                          /* the stack depth it started at: its name goes after that entry */
static uint8_t tube_exit;                      /* the last Tube child's exit status ($D80A): `!`'s result code */
static uint8_t tube_ring[4096]; static unsigned tube_w, tube_r;
/* k4510-menu.cfg "linux = locked", set by the frontend: the Tube's host shell
 * (`!`, and SSH through it) is refused, the machine's compilers are not. */
int io_lock_linux;
/* A refused start is a session of one line: the ROM waits for "alive" before
 * it reads anything (cmd_bbcbasic), so tube_status says alive until the
 * refusal has been read, then the session is over like any other. */
static int tube_refused;
/* The host shell (program 4, `!` at the prompt) is fitted everywhere: the
 * Linux beside the machine is the user's, on a desktop as on the appliance
 * (Doc, 2026-09-08: "drop restrictions on host access"). */
static uint8_t tube_cmd[4], tube_rows, tube_cols;     /* $D804-7 the command string's address, $D808/9 the window */
/* Program 5: a UCI chess engine (Stockfish) on the pty, for CHESS.PRG.  Fitted
 * where a binary is found -- K4510_UCI names one, else the usual places -- and
 * said so in $D800 bit 3.  Not gated like the shell: it is one program, not a
 * door.  Where there is none, CHESS plays its own engine, or one over the
 * network. */
static const char *uci_path(void)
{
    static const char *found; static int looked;
    if (!looked) {
        static const char *const places[] = { "/usr/games/stockfish", "/usr/bin/stockfish", "/usr/local/bin/stockfish", NULL };
        const char *e = getenv("K4510_UCI");
        looked = 1;
        if (e && *e && access(e, X_OK) == 0) found = e;
        else for (int i = 0; places[i]; i++) if (access(places[i], X_OK) == 0) { found = places[i]; break; }
        if (!found) { static char home[600]; const char *h = getenv("HOME");
            if (h) { snprintf(home, sizeof home, "%.500s/opt/stockfish-bin", h); if (access(home, X_OK) == 0) found = home; } }
    }
    return found;
}

/* ---- the Tube ULA ------------------------------------------------------- 
 * On a real BBC Micro the Tube ULA was the FIFO chip between host and
 * co-processor. Ours does a little more: it watches the byte stream coming
 * up from BBC BASIC and executes the machine-specific escapes itself --
 * ESC]K4G;...BEL (the graphics VDU codes bbccos.c forwards: CLG, GCOL,
 * palette, PLOT, origin, mode) go straight to the VICKY blitter, and
 * ESC]K4S;...BEL (SOUND) to the sound sequencer. Those sequences never
 * reach the console ROM (except MODE, which is executed here AND passed
 * on, because the console must change its text geometry too); everything
 * else flows through untouched. BBC coordinates (1280x1024, origin bottom
 * left) land on a 640x480 8bpp bitmap at $200000 (EhBASIC's GRAPHICS
 * surface), VICKY layer 1; colour 0 stays transparent so the text screen
 * shows through, and BBC logical colours live in palette entries 16-31. */
#define TULA_GFXB 0x200000u
#define TULA_W 640
#define TULA_H 480
static int tula_x[3], tula_y[3], tula_ox, tula_oy;
static uint8_t tula_fg = 17, tula_bg = 16, tula_on;
static uint8_t ula_buf[256]; static unsigned ula_n; static int ula_st;

static void tula_vw16(uint8_t r, int v) { vicky_write(r, v & 0xFF); vicky_write(r + 1, (v >> 8) & 0xFF); }
static void tula_vw32(uint8_t r, uint32_t v) { for (int i = 0; i < 4; i++) vicky_write(r + i, (v >> (8 * i)) & 0xFF); }
static int tula_sx(int x) { return x >> 1; }
static int tula_sy(int y) { return (TULA_H - 1) - ((y * 15) >> 5); }
static uint8_t tula_pix(uint8_t c) { return c == 16 ? 0 : c; }   /* logical black -> transparent */
static void tula_pal(int l, int r, int g, int b)
{ vicky_write(6, 16 + (l & 15)); vicky_write(7, r); vicky_write(8, g); vicky_write(9, b); }
static void tula_pphys(int l, int p)             /* a BBC physical colour: primaries from the bits */
{ p &= 7; tula_pal(l, (p & 1) ? 255 : 0, (p & 2) ? 255 : 0, (p & 4) ? 255 : 0); }
static void tula_defpal(int mode)                /* the mode's default logical->physical map */
{
    static const uint8_t four[4] = { 0, 1, 3, 7 };
    for (int i = 0; i < 16; i++)
        tula_pphys(i, mode == 2 ? (i & 7) : (mode == 1 || mode == 5) ? four[i & 3] : (i & 1) ? 7 : 0);
}
static void tula_pt(int i, int x, int y) { tula_vw16(0x84 + i * 4, tula_sx(x)); tula_vw16(0x86 + i * 4, tula_sy(y)); }
static void tula_blt(uint8_t op, uint8_t c)
{
    tula_vw32(0x70, tula_pix(c));
    tula_vw32(0x74, TULA_GFXB); tula_vw16(0x78, TULA_W); tula_vw16(0x7A, TULA_H); tula_vw16(0x7E, TULA_W);
    vicky_write(0x80, op); vicky_write(0x82, 1);
}
static void tula_rect(int x0, int y0, int x1, int y1, uint8_t c)   /* pixel coords, any order */
{
    int t;
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
    if (x1 < 0 || y1 < 0 || x0 >= TULA_W || y0 >= TULA_H) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= TULA_W) x1 = TULA_W - 1;
    if (y1 >= TULA_H) y1 = TULA_H - 1;
    tula_vw32(0x70, tula_pix(c));
    tula_vw32(0x74, TULA_GFXB + (uint32_t)y0 * TULA_W + x0);
    tula_vw16(0x78, x1 - x0 + 1); tula_vw16(0x7A, y1 - y0 + 1); tula_vw16(0x7E, TULA_W);
    vicky_write(0x80, 2); vicky_write(0x82, 1);
}
static void tula_clg(void) { memset(k4510_ram + TULA_GFXB, tula_pix(tula_bg), TULA_W * TULA_H); }
static void tula_circle(int cx, int cy, int ex, int ey, uint8_t c, int fill)
{
    double fdx = ex - cx, fdy = ey - cy, r2 = fdx * fdx + fdy * fdy;
    int r = (int)(sqrt(r2) + 0.5);
    if (r > 4096) r = 4096;                      /* four screens across; a bigger radius would be 10^9 blits */
    for (int d = -r; d <= r; d += 2) {           /* scanlines, 2 BBC units apart ~= one pixel row */
        int s = (int)(sqrt(r2 - (double)d * d) + 0.5);
        if (fill)
            tula_rect(tula_sx(cx - s), tula_sy(cy + d), tula_sx(cx + s), tula_sy(cy + d), c);
        else {
            tula_rect(tula_sx(cx - s), tula_sy(cy + d), tula_sx(cx - s), tula_sy(cy + d), c);
            tula_rect(tula_sx(cx + s), tula_sy(cy + d), tula_sx(cx + s), tula_sy(cy + d), c);
            tula_rect(tula_sx(cx + d), tula_sy(cy - s), tula_sx(cx + d), tula_sy(cy - s), c);
            tula_rect(tula_sx(cx + d), tula_sy(cy + s), tula_sx(cx + d), tula_sy(cy + s), c);
        }
    }
}
/* Sprites, the Acorn way. RISC OS reached its sprites through VDU 23,27
 * (select) and PLOT &E8-&EF (plot the selected one); ours land on VICKY's
 * 128 hardware sprites, so a plotted sprite is a register write and moving
 * it costs nothing. There is no sprite file format: a sprite is CAPTURED
 * from the bitmap, which BBC BASIC has just drawn with the words it knows.
 *   VDU 23,27,0,n|         select sprite n (0-127) for PLOT
 *   VDU 23,27,1,n,w,h|     capture n from the bitmap: w x h pixels (8/16/32/64),
 *                          bottom-left corner at the graphics cursor (MOVE x,y first)
 *   VDU 23,27,2,n|         hide n
 *   VDU 23,27,3,n,f|       flip: f bit0 horizontal, bit1 vertical
 *   VDU 23,27,4,n,z|       depth: drawn after layer z (0 text .. 3); default 1, over the bitmap
 *   VDU 23,27,5,n,m|       n shows sprite m's picture (one capture, many sprites)
 *   PLOT 237,x,y           show the selected sprite with its bottom-left at x,y
 * Attribute table at $260000, pictures at $261000 + n * 4 KB (8 bpp, 64x64 max). */
#define TULA_SPRTAB 0x260000u
#define TULA_SPRDAT 0x261000u
/* The bitmap's room: $200000 up to the sprite table, 384 KB.  Written down
 * because it was NOT -- the bound lived only as arithmetic in whoever last
 * thought about it, and the guide's I/O chapter does not mention it.  Nothing
 * overruns it today: every writer here clamps to TULA_W x TULA_H, and VICKY's
 * own reads are masked to RAM.  The hazard is the next person who scales the
 * geometry to the GLASS instead: an HD mode is 1440x1080, which is 1.5 MB and
 * would run 1.1 MB into the sprite table.  This stops that at compile time
 * rather than on someone's screen (2026-09-17). */
#define TULA_ARENA (TULA_SPRTAB - TULA_GFXB)
typedef char tula_arena_fits[(TULA_W * TULA_H <= TULA_ARENA) ? 1 : -1];
static int tula_spr_cur, tula_spr_on;
static uint8_t tula_spr_w[128], tula_spr_h[128];
static void tula_spr_off(void) { if (tula_spr_on) { vicky_write(0x0E, 0); tula_spr_on = 0; } }
static void tula_spr_init(void)
{
    memset(k4510_ram + TULA_SPRTAB, 0, 128 * 16);
    memset(tula_spr_w, 8, sizeof tula_spr_w); memset(tula_spr_h, 8, sizeof tula_spr_h);
    tula_vw32(0x0A, TULA_SPRTAB); vicky_write(0x0E, 1); tula_spr_on = 1; tula_spr_cur = 0;
}
static uint8_t tula_spr_size(int v) { return v >= 64 ? 3 : v >= 32 ? 2 : v >= 16 ? 1 : 0; }
static void tula_spr(int op, int n, int p1, int p2, int p3, int p4)
{
    uint8_t *e; (void) p3; (void) p4;
    if (!tula_on) return;
    if (!tula_spr_on) tula_spr_init();
    n &= 127; e = k4510_ram + TULA_SPRTAB + n * 16;
    switch (op) {
    case 0: tula_spr_cur = n; return;
    case 1: {
        int wc = tula_spr_size(p1), hc = tula_spr_size(p2), w = 8 << wc, h = 8 << hc;
        int sx = tula_sx(tula_x[0]), sy = tula_sy(tula_y[0]) - (h - 1);
        uint32_t a = TULA_SPRDAT + (uint32_t)n * 4096; uint8_t *d = k4510_ram + a;
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
            int px = sx + x, py = sy + y;
            d[y * w + x] = (px >= 0 && px < TULA_W && py >= 0 && py < TULA_H) ? k4510_ram[TULA_GFXB + py * TULA_W + px] : 0;
        }
        e[4] = a & 255; e[5] = (a >> 8) & 255; e[6] = (a >> 16) & 255; e[7] = (a >> 24) & 15;
        e[8] = (e[8] & 0x01) | 0x02 | 0x10;                      /* keep shown/hidden; 8 bpp; over the bitmap */
        e[9] = wc | (hc << 2); tula_spr_w[n] = (uint8_t)w; tula_spr_h[n] = (uint8_t)h;
        tula_spr_cur = n; return; }
    case 2: e[8] &= ~0x01; return;
    case 3: e[8] = (e[8] & ~0x0C) | ((p1 & 3) << 2); return;
    case 4: e[8] = (e[8] & ~0x30) | ((p1 & 3) << 4); return;
    case 5: {
        const uint8_t *m = k4510_ram + TULA_SPRTAB + (p1 & 127) * 16;
        memcpy(e + 4, m + 4, 4); e[8] = (e[8] & 0x01) | (m[8] & ~0x01); e[9] = m[9];
        tula_spr_w[n] = tula_spr_w[p1 & 127]; tula_spr_h[n] = tula_spr_h[p1 & 127]; return; }
    }
}
static void tula_spr_plot(int x, int y)          /* PLOT 232-239: bottom-left of the selected sprite at x,y */
{
    if (!tula_spr_on) return;
    int n = tula_spr_cur, sx = tula_sx(x), sy = tula_sy(y) - (tula_spr_h[n] - 1);
    uint8_t *e = k4510_ram + TULA_SPRTAB + n * 16;
    e[0] = sx & 255; e[1] = (sx >> 8) & 255; e[2] = sy & 255; e[3] = (sy >> 8) & 255;
    e[8] |= 0x01;
    vicky_write(0x0E, 1);                        /* the console's MODE re-init may have switched sprites off */
}
static void tula_mode(int n)
{
    if (n == 3 || n == 6 || n == 7) { tula_spr_off(); if (tula_on) { vicky_write(0x20, 0); tula_on = 0; } return; }
    tula_on = 1;
    vicky_write(0x21, 0); tula_vw16(0x22, 0); tula_vw16(0x24, 0);   /* palofs, scroll */
    tula_vw16(0x26, TULA_W); tula_vw32(0x28, TULA_GFXB);            /* stride, data */
    vicky_write(0x20, 0x19);                                        /* enable | bitmap | 8 bpp */
    tula_defpal(n);
    tula_fg = 16 + (n == 2 ? 7 : (n == 1 || n == 5) ? 3 : 1);
    tula_bg = 16;
    tula_ox = tula_oy = 0;
    memset(tula_x, 0, sizeof tula_x); memset(tula_y, 0, sizeof tula_y);
    tula_clg();
}
static void tula_plot(int k, int x, int y)
{
    int c, nx, ny;
    if (k & 4) { nx = tula_ox + x; ny = tula_oy + y; }
    else       { nx = tula_x[0] + x; ny = tula_y[0] + y; }
    tula_x[2] = tula_x[1]; tula_y[2] = tula_y[1];
    tula_x[1] = tula_x[0]; tula_y[1] = tula_y[0];
    tula_x[0] = nx;        tula_y[0] = ny;
    c = k & 3;
    if (!c || !tula_on) return;          /* move only, or no bitmap up */
    c = (c == 3) ? tula_bg : tula_fg;    /* 1 = foreground, 2 = invert (drawn as fg), 3 = background */
    switch (k >> 3) {
    case 29:                             /* 232-239: the sprite plot family */
        tula_spr_plot(nx, ny); return;
    case 8:                              /* 64-71: a point */
        tula_pt(0, nx, ny); tula_pt(1, nx, ny); tula_blt(6, c); return;
    case 10:                             /* 80-87: triangle with the two previous points */
        tula_pt(0, tula_x[2], tula_y[2]); tula_pt(1, tula_x[1], tula_y[1]); tula_pt(2, nx, ny);
        tula_blt(7, c); return;
    case 12:                             /* 96-103: axis-aligned rectangle fill */
        tula_rect(tula_sx(tula_x[1]), tula_sy(tula_y[1]), tula_sx(nx), tula_sy(ny), c); return;
    case 18:                             /* 144-151: circle outline, centre = previous point */
        tula_circle(tula_x[1], tula_y[1], nx, ny, c, 0); return;
    case 19:                             /* 152-159: filled circle */
        tula_circle(tula_x[1], tula_y[1], nx, ny, c, 1); return;
    default:
        if (k >= 64) return;             /* fancier families: quietly not drawn */
        tula_pt(0, tula_x[1], tula_y[1]); tula_pt(1, nx, ny);      /* 0-63: a line */
        tula_blt(6, c); return;
    }
}
#define TULA_ARGS 12
static int tula_parse(const char *p, int *a)
{
    int n = 0;
    while (*p && n < TULA_ARGS) {
        int neg = 0, v = 0;
        if (*p == '-') { neg = 1; p++; }
        while (*p >= '0' && *p <= '9') { v = v * 10 + (*p++ - '0'); if (v > 32767) v = 32767; }   /* saturate: int overflow made CIRCLE spin for minutes */
        a[n++] = neg ? -v : v;
        if (*p == ',') p++; else break;
    }
    return n;
}
static void tula_gfx(const char *p)
{
    int a[TULA_ARGS] = { 0 }, n = tula_parse(p, a);
    switch (a[0]) {
    case 16: if (tula_on) tula_clg(); break;
    case 23: if (n >= 4 && a[1] == 27) tula_spr(a[2], a[3], a[4], a[5], a[6], a[7]); break;
    case 18: if (n > 2) { if (a[2] & 0x80) tula_bg = 16 + (a[2] & 15); else tula_fg = 16 + (a[2] & 15); } break;
    case 19: if (n >= 3) { if (a[2] < 16) tula_pphys(a[1], a[2]); else if (n >= 6) tula_pal(a[1], a[3], a[4], a[5]); } break;
    case 22: if (n > 1) tula_mode(a[1]); break;
    case 25: if (n >= 4) tula_plot(a[1], a[2], a[3]); break;
    case 29: if (n >= 3) { tula_ox = a[1]; tula_oy = a[2]; } break;
    }
}
static void tula_snd(const char *p)
{
    int a[TULA_ARGS] = { 0 };
    if (*p == 'Q' || *p == 'q') { seq_write(0, 0x80); return; }
    if (tula_parse(p, a) < 4) return;
    seq_write(0, (uint8_t)a[0]); seq_write(1, (uint8_t)a[1]);
    seq_write(2, (uint8_t)a[2]); seq_write(3, (uint8_t)a[3]);
}
static void tula_close(void)
{
    seq_write(0, 0x80);                          /* flush and silence the sequencer */
    tula_spr_off();
    if (tula_on) { vicky_write(0x20, 0); tula_on = 0; }
    ula_st = 0; ula_n = 0;
}
static void ring_put(uint8_t b) { tube_ring[tube_w++ & 4095] = b; }
static void tula_in(uint8_t b)                   /* every byte from the co-processor passes here */
{
    switch (ula_st) {
    case 0:
        if (b == 0x1B) { ula_st = 1; ula_buf[0] = b; ula_n = 1; return; }
        ring_put(b); return;
    case 1:
        if (b == ']') { ula_st = 2; ula_buf[1] = b; ula_n = 2; return; }
        ring_put(0x1B); ring_put(b); ula_st = 0; return;
    case 3:                                       /* ESC inside an OSC: ESC \ (ST) ends it as BEL does -- the machine's
                                                  * own strings are BEL-terminated, so this one is the console's */
        if (b == '\\') { for (unsigned i = 0; i < ula_n; i++) ring_put(ula_buf[i]); ring_put(0x1B); ring_put('\\'); ula_st = 0; return; }
        ula_st = 2; if (ula_n < sizeof ula_buf - 2) ula_buf[ula_n++] = 0x1B;
        /* fall through: b is the next byte of the string */
    default:
        if (b == 0x1B) { ula_st = 3; return; }
        if (b == 7) {
            ula_st = 0; ula_buf[ula_n] = 0;
            if (ula_n > 6 && !memcmp(ula_buf + 2, "K4G;", 4)) {
                tula_gfx((const char *)ula_buf + 6);
                if (ula_buf[6] != '2' || ula_buf[7] != '2') return;    /* MODE also goes to the console */
            } else if (ula_n > 6 && !memcmp(ula_buf + 2, "K4S;", 4)) {
                tula_snd((const char *)ula_buf + 6);
                return;
            }
            for (unsigned i = 0; i < ula_n; i++) ring_put(ula_buf[i]);
            ring_put(7);
            return;
        }
        if (ula_n < sizeof ula_buf - 2) { ula_buf[ula_n++] = b; return; }
        for (unsigned i = 0; i < ula_n; i++) ring_put(ula_buf[i]);     /* too long: not ours */
        ring_put(b); ula_st = 0;
        return;
    }
}
static void tube_pump(void)
{
    uint8_t buf[256]; ssize_t n; int full = 0;
    if (tube_fd < 0) return;
    /* The ROM polls $D800 every few instructions while the Tube is up, and a
     * read() per poll was ~100k syscalls a second doing nothing (review
     * 2026-09-05, 6).  Read the pty at most once per 100 us of real time --
     * clock_gettime is a vDSO call, not a syscall -- and let the polls in
     * between see the ring as it stands. */
    { static struct timespec last; struct timespec now;
      clock_gettime(CLOCK_MONOTONIC, &now);
      if ((now.tv_sec - last.tv_sec) * 1000000000L + (now.tv_nsec - last.tv_nsec) < 100000L) return;   /* empty ring included: the idle prompt was the 100k/s case */
      last = now; }
    for (;;) {
        if (tube_w - tube_r >= sizeof tube_ring - 600) { full = 1; break; }
        if ((n = read (tube_fd, buf, sizeof buf)) <= 0) break;
        for (ssize_t i = 0; i < n; i++) tula_in(buf[i]);
    }
    /* Reap only once the pty has been read dry: a `!pwd` prints and exits in
     * the same instant, and closing the master with bytes still in it lost
     * the end of the line.  With the ring full the read stopped early, so
     * the child's last words may still be in the pty -- next time. */
    { int st = 0;
      if (!full && tube_pid && waitpid (tube_pid, &st, WNOHANG) == tube_pid) {
          tube_log("pid %d ended: %s %d", (int) tube_pid, WIFSIGNALED (st) ? "signal" : "exit", WIFSIGNALED (st) ? WTERMSIG (st) : WEXITSTATUS (st));
          tube_exit = WIFSIGNALED (st) ? (uint8_t) (128 + WTERMSIG (st)) : (uint8_t) WEXITSTATUS (st);
          tube_pid = 0; close (tube_fd); tube_fd = -1; tula_close(); } }
}
/* The `!` shell talks UTF-8 (a Linux host's programs do) and JIM draws CP437, so
 * JIM decodes for the length of a shell session -- switched off once the
 * session's last byte has been read out, not at the reap, when the ROM may still
 * be pumping the tail.  CP/M and BBC BASIC send CP437 of their own: prog 4 only. */
/* The Tube's comings and goings, on stderr with the wall clock -- which the
 * K4510 Linux keeps in ~/k4510/DIAG/emulator-*.log while the log switch is on.
 * Put in to find out what ended an ssh session on the Dell (2026-09-12). */
void tube_log(const char *fmt, ...)
{
    struct timespec ts; struct tm tm; va_list ap;
    clock_gettime(CLOCK_REALTIME, &ts); localtime_r(&ts.tv_sec, &tm);
    fprintf(stderr, "%02d:%02d:%02d.%03ld tube: ", tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000);
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fputc('\n', stderr); fflush(stderr);
}
static int tube_utf8;
static void tube_utf8_done(void) { if (tube_utf8 && !tube_pid && tube_w == tube_r) { term_host_session(0); tube_utf8 = 0; } }
static void tube_start(int prog)                  /* 1 = BBC BASIC, 3 = CP/M (RunCPM), 4 = the host shell */
{
    struct winsize ws = { 29, 79, 0, 0 };
    char cmd[256] = "";
    pid_t parent = getpid ();                     /* NOT 1: in a container the emulator IS pid 1, and "getppid() == 1" then killed every child (2026-09-07) */
    if (tube_pid) return;
    if (prog == 5 && !uci_path()) return;
    if (prog == 4) {
        /* Refused, not run short: fs_guest_str fills the buffer and THEN reports the
         * overrun, and a shell command cut at byte 255 is a different command --
         * `rm -rf /a/long/path/build` loses its tail.  (Review, 2026-09-17.) */
        if (fs_guest_str((uint32_t)tube_cmd[0] | (uint32_t)tube_cmd[1] << 8 | (uint32_t)tube_cmd[2] << 16 | (uint32_t)tube_cmd[3] << 24, cmd, sizeof cmd)) {
            const char *m = "!: that command is too long (255 characters at most)\r\n";
            while (*m) ring_put((uint8_t) *m++);
            tube_refused = 1;
            return;
        }
        if (tube_rows) ws.ws_row = tube_rows;          /* the console window as the ROM has it, bands and margin taken out */
        if (tube_cols) ws.ws_col = tube_cols;
        ws.ws_xpixel = (unsigned short)(ws.ws_col * 8); ws.ws_ypixel = (unsigned short)(ws.ws_row * term_cell_h());   /* in pixels too: a program that draws pictures (Kitty's protocol, core/jimgfx.h) sizes them from this */
        /* Locked (k4510-menu.cfg): no way into Linux -- `!`, `!cmd`, SSH.  PAS and
         * CC come through here too, as k4510-pas/k4510-cc, and may pass.  The
         * reason goes out as the session's only output, and no session starts. */
        if (io_lock_linux && strncmp(cmd, "k4510-pas", 9) && strncmp(cmd, "k4510-cc", 8)) {
            const char *m = "Linux is locked on this machine (k4510-menu.cfg)\r\n";
            while (*m) ring_put((uint8_t) *m++);
            tube_refused = 1;
            return;
        }
    }
    tube_pid = forkpty (&tube_fd, NULL, NULL, &ws);
    if (tube_pid == 0) {
        /* Die with the emulator.  SDL turns SIGTERM into an SDL_QUIT *event*,
         * so a wedged or timed-out frontend only ever dies to SIGKILL -- which
         * runs no cleanup, and left runcpm and bbcbasic orphaned and spinning
         * at 100% for hours.  The kernel is the only thing that can be relied
         * on here, so ask it to do the killing. */
#ifdef PR_SET_PDEATHSIG
        prctl (PR_SET_PDEATHSIG, SIGKILL);
        if (getppid () != parent) _exit (0); /* the parent died between fork and here */
#endif
        /* Nothing of the emulator's crosses into a program the guest chose:
         * sockets, the open file, the trace, /dev/dri and evdev on the bare
         * Linux all lack CLOEXEC, so close everything above the pty. */
        for (int fd = 3; fd < 4096; fd++) close (fd);
        setenv ("TERM", "dumb", 1);
        /* The machine's own tools -- k4510-pas and k4510-cc, which PAS and CC
         * run through this shell -- on PATH wherever the emulator is started
         * from its checkout (tools/ beside fs/).  Doc, hdieu, 2026-09-08:
         * "k4510-pas: command not found" at the prompt of a plain desktop. */
        { char *tp = realpath ("tools", NULL); const char *op = getenv ("PATH");   /* NULL: realpath mallocs (a fixed buffer trips the fortify check) */
          if (tp && access (tp, X_OK) == 0) {
              char np[4096]; snprintf (np, sizeof np, "%s:%s", tp, op ? op : "/usr/local/bin:/usr/bin:/bin");
              setenv ("PATH", np, 1);
          }
          free (tp); }
        /* The machine's backspace key is BS ($08).  The pty's erase character is
         * DEL by default, so under dash, vi and anything without readline the key
         * echoed as ^H and erased nothing (bash's readline hid it).  Make BS the
         * erase character; DEL stays understood by the programs that read it. */
        { struct termios tio; if (tcgetattr (0, &tio) == 0) { tio.c_cc[VERASE] = 0x08; tcsetattr (0, TCSANOW, &tio); } }
        /* Resolve the co-processor's binary to an absolute path BEFORE chdir
         * (the chdir below moves the CWD, so a relative exec path would miss);
         * realpath(...,NULL) mallocs, so no fixed buffer for the fortify check. */
        if (prog == 5) {                          /* the chess engine: UCI on stdin/stdout, nothing else */
            const char *u = uci_path();
            if (u) execl (u, "stockfish", (char *) NULL);
        } else if (prog == 4) {                   /* `!`: the host's own shell, in the machine's current directory.
                                                   * JIM is a VT100 with ANSI colours.  "xterm-color" is the terminfo
                                                   * that says so (vt100's has no colour).  "ansi" is the PC's
                                                   * ANSI.SYS and garbled htop (test/ttypetest.sh, BUILD-LOG
                                                   * 2026-09-11).  K4510_TERM overrides. */
            const char *term = getenv ("K4510_TERM"), *sh = getenv ("SHELL");
            char dir[800]; snprintf (dir, sizeof dir, "%.511s%s%.255s", fs_root, fs_cwd[0] ? "/" : "", fs_cwd);
            char *rroot = realpath (fs_root, NULL);          /* the compilers find /PATH names, and MAKE.ERR, under it */
            if (rroot) setenv ("K4510_ROOT", rroot, 1);
            setenv ("TERM", term && *term ? term : "xterm-color", 1);
            if (!getenv ("LANG") && !getenv ("LC_ALL")) setenv ("LANG", "C.UTF-8", 1);   /* JIM decodes UTF-8 for this session */
            /* $SHELL as the host has it, if it exists HERE: a distrobox hands the
             * container the host's $SHELL, and Fedora's zsh is not in a Debian box. */
            if (!sh || !*sh || access (sh, X_OK) != 0) sh = access ("/bin/bash", X_OK) == 0 ? "/bin/bash" : "/bin/sh";
            if (chdir (dir) != 0) { if (chdir (fs_root) != 0) { } }
            if (cmd[0]) execl (sh, sh, "-c", cmd, (char *) NULL);   /* !ls -l   one command, then back */
            else        execl (sh, sh, (char *) NULL);              /* !        an interactive shell; exit returns */
            { const char *m = "!: the shell would not start\r\n"; ssize_t n = write (1, m, strlen (m)); (void) n; }
        } else if (prog == 3) {                   /* the Z80 second processor: CP/M's drives are fs/CPM/A .. P */
            char *bin = realpath ("cpm/runcpm", NULL);
            char dir[800]; snprintf (dir, sizeof dir, "%.511s/CPM", fs_root);   /* the configured root, as BASIC below, not ./fs */
            if (chdir (dir) != 0) { }
            if (bin) execl (bin, "runcpm", (char *) NULL);
        } else {
            /* BBC BASIC starts where the machine's shell is (fs_root/fs_cwd), so
             * LOAD needs no directory prefix; K4510_ROOT lets it show the path
             * as /... instead of the host tree above fs. */
            char *rroot = realpath (fs_root, NULL);
            if (rroot) setenv ("K4510_ROOT", rroot, 1);
            char *bin = realpath ("tube/bbcbasic", NULL);
            char dir[800]; snprintf (dir, sizeof dir, "%.511s%s%.255s", fs_root, fs_cwd[0] ? "/" : "", fs_cwd);
            if (chdir (dir) != 0) { if (chdir (fs_root) != 0) { } }
            if (bin) execl (bin, "bbcbasic", (char *) NULL);
        }
        _exit (127);
    }
    tube_exit = 0;
    if (tube_pid < 0) { tube_pid = 0; tube_fd = -1; return; }
    fcntl (tube_fd, F_SETFL, O_NONBLOCK);
    if (prog == 4) { term_host_session(1); tube_utf8 = 1; }   /* the ROM's JIM reset (tube_term) follows, and leaves it */
    tube_log("start prog %d pid %d%s%s", prog, (int) tube_pid, cmd[0] ? " cmd: " : "", cmd);
}
/* The frontend's clean exit does not come through here: it ends its frame
 * loop, tears SDL down and returns from main (sdl/main.c), so the host calls
 * this on its way out and no child outlives it. */
void io_tube_shutdown(void) { tube_stop(); }
void tube_stop(void)
{
    if (tube_pid) { tube_log("stop: killing pid %d", (int) tube_pid);
                    kill (-tube_pid, SIGKILL); kill (tube_pid, SIGKILL); waitpid (tube_pid, NULL, 0); tube_pid = 0; }   /* the session: a `!nohup x &` too */
    if (tube_fd >= 0) { close (tube_fd); tube_fd = -1; }
    tube_w = tube_r = 0; tube_refused = 0;
    tula_close();
    if (tube_utf8) { term_host_session(0); tube_utf8 = 0; }
}
uint8_t tube_status(void)
{
    tube_pump(); tube_utf8_done();
    if (tube_refused && tube_w == tube_r) tube_refused = 0;          /* its line has been read: over */
    return (tube_pid || tube_refused ? 1 : 0) | 4 | (uci_path() ? 8 : 0) | (tube_w != tube_r ? 0x80 : 0);
}
static uint8_t tube_read(void) { tube_pump(); if (tube_w != tube_r) return tube_ring[tube_r++ & 4095]; tube_utf8_done(); return 0; }
static void tube_write(uint8_t v)
{
    if (tube_fd < 0) return;
    if (tube_utf8 && v >= 0x80) {                 /* a `!` session: the ROM's raw accented letter is CP437, the host wants UTF-8 */
        char u[4]; int n = term_cp437_utf8(v, u);
        ssize_t w = write (tube_fd, u, (size_t) n); (void) w;
        return;
    }
    { ssize_t n = write (tube_fd, &v, 1); (void) n; }
}
int io_tube_kind(void) { return tube_pid ? tube_prog_now : 0; }
uint8_t tube_io_read(uint8_t r)
{
    if (r == 0) return tube_status();
    if (r == 1) return tube_read();
    if (r == 10) return tube_exit;               /* the last child's exit status: the ROM makes it `!`'s RC */
    return 0xFF;
}
void tube_io_write(uint8_t r, uint8_t v)
{
    if (r == 2) tube_write(v);
    if (r == 3) { if (v == 1 || v == 3 || v == 4 || v == 5) {
            /* Only the write that STARTED a session owns it: tube_start returns at
             * once when a co-processor is already up.  (Review, 2026-09-17.) */
            int was = tube_pid != 0;
            tube_start(v);
            if (!was) { tube_prog_now = v; tube_prog_at = title_depth_now(); } } else if (v == 2) { tube_stop(); tube_prog_now = 0; } }
    if (r >= 4 && r < 8) tube_cmd[r - 4] = v;
    if (r == 8) tube_rows = v;
    if (r == 9) tube_cols = v;
}
void tube_reset(void) { tube_prog_now = 0; tube_stop(); }
#include "state.h"
void tube_state_save(FILE *f)
{
    state_put(f, "TULX", tula_x, sizeof tula_x); state_put(f, "TULY", tula_y, sizeof tula_y);
    state_put(f, "TULO", &tula_ox, sizeof tula_ox); state_put(f, "TULP", &tula_oy, sizeof tula_oy);
    state_put(f, "TULF", &tula_fg, 1); state_put(f, "TULB", &tula_bg, 1); state_put(f, "TULN", &tula_on, 1);
    state_put(f, "TSPC", &tula_spr_cur, sizeof tula_spr_cur); state_put(f, "TSPO", &tula_spr_on, sizeof tula_spr_on);
    state_put(f, "TSPW", tula_spr_w, sizeof tula_spr_w); state_put(f, "TSPH", tula_spr_h, sizeof tula_spr_h);
}
int tube_state_load(FILE *f)
{
    if (state_get(f, "TULX", tula_x, sizeof tula_x) || state_get(f, "TULY", tula_y, sizeof tula_y)
        || state_get(f, "TULO", &tula_ox, sizeof tula_ox) || state_get(f, "TULP", &tula_oy, sizeof tula_oy)
        || state_get(f, "TULF", &tula_fg, 1) || state_get(f, "TULB", &tula_bg, 1) || state_get(f, "TULN", &tula_on, 1)
        || state_get(f, "TSPC", &tula_spr_cur, sizeof tula_spr_cur) || state_get(f, "TSPO", &tula_spr_on, sizeof tula_spr_on)
        || state_get(f, "TSPW", tula_spr_w, sizeof tula_spr_w) || state_get(f, "TSPH", tula_spr_h, sizeof tula_spr_h)) return -2;
    tula_spr_cur = (int)((unsigned) tula_spr_cur & 127);
    return 0;
}
