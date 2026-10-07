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
static pid_t s2_pid;
static int s2_fd = -1, s2_dead;
static int s2_jiggle;                        /* frames into a redraw asked of the session (io_screen2_redraw) */
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
    s2_dead = 0;
    return 0;
}
int io_screen2_allowed(void) { return !io_lock_linux; }
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
static void s2_write(const uint8_t *b, size_t n)
{
    while (s2_fd >= 0 && n) { ssize_t w = write(s2_fd, b, n); if (w <= 0) break; b += w; n -= (size_t) w; }
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
void s2_pump(void)
{
    int c, r, req;
    uint8_t buf[8192]; ssize_t n; int rounds = 8;
    if ((req = term_screen_request()) >= 0) io_screen_show(req);    /* ESC ] 4510 ; kos / term, from either screen */
    if (term2_fit(&c, &r)) s2_winsize();                            /* a MODE change: the session's size follows */
    if (s2_jiggle) { if (s2_jiggle == 1 || s2_jiggle == 6) s2_winsize(); if (++s2_jiggle > 6) s2_jiggle = 0; }
    if (!s2_pid) return;
    while (rounds-- && (n = read(s2_fd, buf, sizeof buf)) > 0) {
        size_t m; uint8_t rep[256];
        term2_feed(buf, (size_t) n);
        if ((m = term2_replies(rep, sizeof rep))) s2_write(rep, m);  /* cursor reports, DA: the session asked */
    }
    if (waitpid(s2_pid, NULL, WNOHANG) == s2_pid) {
        while ((n = read(s2_fd, buf, sizeof buf)) > 0) term2_feed(buf, (size_t) n);
        close(s2_fd); s2_fd = -1; s2_pid = 0; s2_dead = 1;
        term2_say("\r\n\x1b[0m[the session has ended: Enter starts another, Alt+1 is K/OS]\r\n");
    }
}
