#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pty.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <signal.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#include "io.h"
#include "io_int.h"
#include "term.h"
/* ---- the second screen's session (2026-10-05) ---------------------------------
 * Doc: JIM shows "the current K/OS K4510 program, or ... one terminal
 * connection.  Via that terminal connection to the host or a distant machine,
 * TMUX can be run there which gives the illusion of MULTIPLE connections".
 * This is that connection: a pty the emulator owns, drawn by JIM's second
 * terminal (core/term.c), alive whatever the machine is doing -- the 45GS10 is
 * never asked.  What runs is /SYSTEM/ETC/TERMINAL.CFG's first line that is not
 * a comment (an ssh to a tmux, say), or a login shell on the Linux beneath.
 * Alt+1 / Alt+2 (the frontend), F12 > Screen, the TERMINAL command and
 * ESC ] 4510 ; kos BEL from the session switch between the two.  Locked
 * machines (k4510-menu.cfg) have no second screen: it is a way into Linux. */
#include <time.h>
int io_lat_on; unsigned long long io_lat_read_ns;
static pid_t s2_pid;
static int s2_fd = -1, s2_dead;
static int s2_jiggle;                        /* frames into a redraw asked of the session (io_screen2_redraw) */
/* What the pty would not take yet (a paste into a busy session, EAGAIN): kept
 * in order and offered again each frame, up to S2_OUTQ bytes (the review,
 * 2026-10-09: the rest of a paste used to be dropped). */
#define S2_OUTQ 65536
static uint8_t s2_outq[S2_OUTQ]; static size_t s2_outn;
#define S2_INQ 65536                         /* what was read and not yet fed (s2_feedable, below) */
static uint8_t s2_in[S2_INQ + 8192]; static size_t s2_inn; static int s2_in_age;
static void s2_winsize(void)
{
    struct winsize ws; int c = 80, r = 25;
    term2_size(&c, &r);
    if (s2_jiggle && s2_jiggle < 6 && r > 2) r--;   /* a row short for a few frames, then right again */
    memset(&ws, 0, sizeof ws); ws.ws_col = (unsigned short) c; ws.ws_row = (unsigned short) r;
    ws.ws_xpixel = (unsigned short)(c * 8); ws.ws_ypixel = (unsigned short)(r * term_cell_h());
    if (s2_fd >= 0) ioctl(s2_fd, TIOCSWINSZ, &ws);
}
static void s2_command(char *cmd, size_t n)
{
    char path[1024]; FILE *f; const char *e = getenv("K4510_TERMINAL");   /* the environment first: the tests, a one-off */
    cmd[0] = 0;
    if (e) { snprintf(cmd, n, "%s", e); return; }
    snprintf(path, sizeof path, "%s/SYSTEM/ETC/TERMINAL.CFG", fs_root);
    fs_casefix(path, sizeof path);
    if (!(f = fopen(path, "r"))) return;
    while (fgets(cmd, (int) n, f)) {
        char *p = cmd; size_t l;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '#' || *p == '\n' || *p == '\r') { cmd[0] = 0; continue; }
        memmove(cmd, p, strlen(p) + 1);
        l = strlen(cmd); while (l && (cmd[l - 1] == '\n' || cmd[l - 1] == '\r' || cmd[l - 1] == ' ')) cmd[--l] = 0;
        break;
    }
    fclose(f);
}
static int s2_spawn(void)
{
    struct winsize ws; char cmd[512]; int c = 80, r = 25;
    pid_t parent = getpid();
    term2_open();
    term2_size(&c, &r);
    memset(&ws, 0, sizeof ws); ws.ws_col = (unsigned short) c; ws.ws_row = (unsigned short) r;
    s2_command(cmd, sizeof cmd);
    s2_pid = forkpty(&s2_fd, NULL, NULL, &ws);
    if (s2_pid == 0) {
        const char *term = getenv("K4510_TERM"), *sh = getenv("SHELL"), *home = getenv("HOME");
#ifdef PR_SET_PDEATHSIG
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        if (getppid() != parent) _exit(0);
#endif
        for (int fd = 3; fd < 4096; fd++) close(fd);
        { struct termios tio; if (tcgetattr(0, &tio) == 0) { tio.c_cc[VERASE] = 0x08; tcsetattr(0, TCSANOW, &tio); } }   /* the machine's backspace, as `!` has it */
        setenv("TERM", term && *term ? term : "xterm-color", 1);
        if (!getenv("LANG") && !getenv("LC_ALL")) setenv("LANG", "C.UTF-8", 1);
        setenv("K4510_SCREEN", "2", 1);
        if (!sh || !*sh || access(sh, X_OK) != 0) sh = access("/bin/bash", X_OK) == 0 ? "/bin/bash" : "/bin/sh";
        if (home && chdir(home) != 0) { }
        if (cmd[0]) execl(sh, sh, "-c", cmd, (char *) NULL);
        else        execl(sh, sh, "-l", (char *) NULL);
        _exit(127);
    }
    if (s2_pid < 0) { s2_pid = 0; s2_fd = -1; term2_say("terminal: no pty for a session\r\n"); return -1; }
    fcntl(s2_fd, F_SETFL, O_NONBLOCK);
    fcntl(s2_fd, F_SETFD, FD_CLOEXEC);          /* not inherited by what the emulator starts next (a `!` session, a helper) */
    s2_dead = 0; s2_outn = 0; s2_inn = 0; s2_in_age = 0;
    return 0;
}
int io_screen2_allowed(void) { return !io_lock_linux; }
int io_screen2_fd(void) { return s2_pid && s2_fd >= 0 && term_screen() == 1 ? s2_fd : -1; }
void io_screen_show(int n)
{
    if (n && io_lock_linux) { term_screen_show(0); return; }
    if (n && !s2_pid && !s2_dead) s2_spawn();
    term_screen_show(n);
}
int io_screen(void) { return term_screen(); }
/* After a power cycle (Doc, 2026-10-07): the machine's RAM is zeroed, and the
 * second screen's map lives in it, so what the session had drawn there was
 * gone -- and a session redraws only what changes (mosh, tmux), leaving holes.
 * The map is blanked and the session made to draw everything again: its
 * window a row short for a few frames and back, which every full-screen
 * program answers with a whole repaint. */
void io_screen2_redraw(void)
{
    term2_wipe();
    if (s2_pid) s2_jiggle = 1;
}
static void s2_flush(void)
{
    size_t done = 0;
    while (s2_fd >= 0 && done < s2_outn) { ssize_t w = write(s2_fd, s2_outq + done, s2_outn - done); if (w <= 0) break; done += (size_t) w; }
    if (s2_fd < 0) done = s2_outn;
    if (done) { memmove(s2_outq, s2_outq + done, s2_outn - done); s2_outn -= done; }
}
static void s2_write(const uint8_t *b, size_t n)
{
    if (s2_fd < 0) return;
    if (!s2_outn) while (n) { ssize_t w = write(s2_fd, b, n); if (w <= 0) break; b += w; n -= (size_t) w; }
    if (n) { if (n > S2_OUTQ - s2_outn) n = S2_OUTQ - s2_outn; memcpy(s2_outq + s2_outn, b, n); s2_outn += n; }   /* behind what waits already: the order is kept */
}
void s2_key(uint16_t ent)
{
    uint8_t k = (uint8_t) ent, out[256]; size_t n;
    if (s2_dead) {                              /* the session ended: Enter starts another */
        if (k == 0x0D && !(ent & KBD_KEY)) { s2_dead = 0; term2_say("\r\n"); s2_spawn(); }
        return;
    }
    if (!(ent & KBD_KEY) && k >= 0x80) {        /* an accented letter: UTF-8, as the `!` session sends it */
        char u[4]; int l = term_cp437_utf8(k, u); s2_write((const uint8_t *) u, (size_t) l); return;
    }
    term2_key(k);                               /* JIM's own translation: arrows, F-keys, Alt as Meta */
    n = term2_replies(out, sizeof out);
    s2_write(out, n);
}
/* What the session said, read but not yet given to JIM: the start of a
 * synchronized update (ESC [ ? 2026 h) whose end has not come.  JIM is fed up
 * to it, so the frame shows the last whole update with no hold on, and the
 * rest waits for its ESC [ ? 2026 l -- no repaint of the whole frame for
 * every update that ends (they came three to a frame: the review, 2026-10-09).
 * Kept half a second at most, as JIM's own hold is, and 64 KB at most. */
static const uint8_t SYNC_H[] = "\033[?2026h", SYNC_L[] = "\033[?2026l";
static size_t s2_last(const uint8_t *b, size_t n, const uint8_t *pat)   /* the last place of pat in b, or n */
{
    size_t l = 8;                                                    /* both are 8 bytes */
    for (size_t i = n >= l ? n - l + 1 : 0; i-- > 0; ) if (b[i] == 0x1B && !memcmp(b + i, pat, l)) return i;
    return n;
}
static size_t s2_feedable(void)                                     /* how much of s2_in JIM may have now */
{
    size_t h = s2_last(s2_in, s2_inn, SYNC_H), l;
    if (h == s2_inn) return s2_inn;                                  /* no update begun */
    l = s2_last(s2_in + h, s2_inn - h, SYNC_L);
    if (l != s2_inn - h) return s2_inn;                              /* the last one begun has ended */
    if (s2_inn >= S2_INQ || s2_in_age >= 30) return s2_inn;          /* too long unfinished: as it is */
    return h;
}
void s2_pump(void)
{
    int c, r, req;
    uint8_t buf[8192]; ssize_t n; int rounds = 8;
    if ((req = term_screen_request()) >= 0) io_screen_show(req);    /* ESC ] 4510 ; kos / term, from either screen */
    if (term2_fit(&c, &r)) s2_winsize();                            /* a MODE change: the session's size follows */
    if (s2_jiggle) { if (s2_jiggle == 1 || s2_jiggle == 6) s2_winsize(); if (++s2_jiggle > 6) s2_jiggle = 0; }
    if (!s2_pid) return;
    s2_flush();
    while (rounds-- && s2_inn < S2_INQ && (n = read(s2_fd, s2_in + s2_inn, sizeof s2_in - s2_inn)) > 0) {
        if (io_lat_on && !io_lat_read_ns) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); io_lat_read_ns = (unsigned long long) ts.tv_sec * 1000000000ull + (unsigned long long) ts.tv_nsec; }
        s2_inn += (size_t) n;
    }
    if (s2_inn) {
        size_t f = s2_feedable(), m; uint8_t rep[256];
        if (f) {
            term2_feed(s2_in, f);
            memmove(s2_in, s2_in + f, s2_inn - f); s2_inn -= f; s2_in_age = 0;
            if ((m = term2_replies(rep, sizeof rep))) s2_write(rep, m);  /* cursor reports, DA: the session asked */
        } else s2_in_age++;
    }
    if (waitpid(s2_pid, NULL, WNOHANG) == s2_pid) {
        rounds = 64;                                                /* the last words, at most 512 KB: a grandchild holding the pty open must not keep this loop going */
        if (s2_inn) term2_feed(s2_in, s2_inn);
        s2_inn = 0; s2_in_age = 0;
        while (rounds-- && (n = read(s2_fd, buf, sizeof buf)) > 0) term2_feed(buf, (size_t) n);
        close(s2_fd); s2_fd = -1; s2_pid = 0; s2_dead = 1; s2_outn = 0;
        term2_say("\r\n\x1b[0m[the session has ended: Enter starts another, Alt+1 is K/OS]\r\n");
    }
}
