/* jimgfxtest -- JIM's pictures (core/jimgfx.c) against what a remote program
 * can send them.  The JIM review of 2026-10-09 found a placement that read
 * past the image (x + w overflowed int), one that ran the draw loops 10^12
 * times (c= r= unbounded), a number parse that overflowed, memory a remote
 * could grow to 2.2 GB, and file transfers that opened FIFOs and unlinked
 * outside the temporary directories.  Each is a case here; every case runs
 * under an alarm, so a hang fails the test instead of stopping `make test`.
 * `make jimfuzz` builds this and test/jimfuzz.c with ASan and UBSan. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include "../core/xemu/emutools_basicdefs.h"
#include "../core/xemu/cpu65.h"
#include "../core/mem.h"
#include "../core/io.h"
#include "../core/term.h"
#include "../core/jimgfx.h"
#include "../core/state.h"

#define W(r, v) io_write(IO_TERM + (r), (uint8_t)(v))
#define R(r) io_read(IO_TERM + (r))
static int fails;
static const char *now;
static void on_alarm(int s) { (void) s; fprintf(stderr, "jimgfxtest: FAIL %s: still running after the time limit (a hang)\n", now); _exit(1); }
static void sendn(const char *s, size_t n) { for (size_t i = 0; i < n; i++) W(0, (uint8_t) s[i]); }
static void send(const char *s) { sendn(s, strlen(s)); }
static void drain(char *o, int max) { int i = 0; while ((R(1) & 0x80) && i < max - 1) o[i++] = (char) R(2); o[i] = 0; }
static void check(int ok, const char *what, const char *got)
{
    if (!ok) { fails++; printf("FAIL %s: %s (got \"%s\")\n", now, what, got ? got : ""); }
}
static void start(const char *name, int secs) { now = name; alarm((unsigned) secs); }
static void init(void) { mem_init(); io_reset(); W(5, 80); W(6, 24); W(7, 0); W(8, 0); W(0x0D, 80); W(0x0E, 0); send("\033c\033[20l"); W(4, 2); }
static void b64enc(const char *in, char *out)                  /* a path, as t=f wants it */
{
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t n = strlen(in), o = 0;
    for (size_t k = 0; k < n; k += 3) {
        uint32_t v = (uint32_t)(uint8_t) in[k] << 16 | (k + 1 < n ? (uint32_t)(uint8_t) in[k + 1] << 8 : 0) | (k + 2 < n ? (uint8_t) in[k + 2] : 0);
        out[o++] = T[v >> 18 & 63]; out[o++] = T[v >> 12 & 63]; out[o++] = k + 1 < n ? T[v >> 6 & 63] : '='; out[o++] = k + 2 < n ? T[v & 63] : '=';
    }
    out[o] = 0;
}
/* the direct way in, with a geometry of our own (host: t=f paths are Linux's) */
static void apc(const char *s, int host, jimgfx_todo_t *todo)
{
    jimgfx_geom_t g = { 80, 24, 0, 0, 8, 16, 0, 0, host };
    jimgfx_apc((const uint8_t *) s, strlen(s), &g, todo);
    if (todo->place) jimgfx_draw(todo, &g);
}

int main(void)
{
    char rp[512], buf[2048], dir[] = "/tmp/jimgfxtest.XXXXXX";
    jimgfx_todo_t todo;
    signal(SIGALRM, on_alarm);
    init();

    start("a 2x2 picture, shown", 5);
    send("\033_Ga=t,f=24,s=2,v=2,i=1;AAAAAAAAAAAAAAAA\033\\"); drain(rp, sizeof rp);
    check(strstr(rp, "i=1;OK") != NULL, "transmit replies OK", rp + (rp[0] != 0));
    send("\033_Ga=p,i=1\033\\"); drain(rp, sizeof rp);
    check(jimgfx_active(), "the plane is on after a=p", NULL);

    start("x + w past INT_MAX (HIGH 1: read out of bounds)", 5);
    send("\033_Ga=p,i=1,x=2147483000,w=1000\033\\"); drain(rp, sizeof rp);
    check(strstr(rp, "EINVAL") != NULL, "refused with EINVAL", rp + (rp[0] != 0));
    send("\033_Ga=p,i=1,y=2147483000,h=1000\033\\"); drain(rp, sizeof rp);
    check(strstr(rp, "EINVAL") != NULL, "y + h refused with EINVAL", rp + (rp[0] != 0));
    send("\033_Ga=p,i=1,x=1,w=2147483647\033\\"); drain(rp, sizeof rp);
    check(strstr(rp, "EINVAL") != NULL, "x=1,w=INT_MAX refused", rp + (rp[0] != 0));

    start("c= r= of 20 million (HIGH 2: the draw loops ran 10^12 times)", 5);
    send("\033_Ga=p,i=1,c=20000000,r=20000000\033\\"); drain(rp, sizeof rp);
    send("\033_Ga=p,i=1,c=20000000\033\\"); drain(rp, sizeof rp);
    send("\033_Ga=p,i=1,r=20000000\033\\"); drain(rp, sizeof rp);
    send("\033_Ga=p,i=1,c=-5,r=-5\033\\"); drain(rp, sizeof rp);
    apc("Ga=p,i=1,c=2147483647,r=2147483647", 0, &todo);
    check(todo.pw >= 1 && todo.pw <= 65536 && todo.ph >= 1 && todo.ph <= 65536, "the size is clamped", NULL);

    start("numbers past INT_MAX (MEDIUM 3: signed overflow)", 5);
    send("\033_Ga=t,f=24,s=2,v=2,i=99999999999;AAAAAAAAAAAAAAAA\033\\"); drain(rp, sizeof rp);
    check(strstr(rp, "i=100000000;") != NULL, "the id saturates at 10^8", rp + (rp[0] != 0));
    send("\033_Ga=t,f=24,s=99999999999999999999,v=2,i=7;AAAA\033\\"); drain(rp, sizeof rp);
    check(strstr(rp, "EBADPNG") != NULL, "a huge width is refused", rp + (rp[0] != 0));

    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }

    start("the images held stay under the budget (MEDIUM 4)", 30);
    { /* a 2048 x 2048 RGB file: 16 MB once it is RGBA; ten of them would be 160 MB */
      char raw[600]; FILE *f; static uint8_t row[2048 * 3];
      snprintf(raw, sizeof raw, "%s/big.rgb", dir);
      if (!(f = fopen(raw, "wb"))) { perror(raw); return 1; }
      memset(row, 0x80, sizeof row);
      for (int y = 0; y < 2048; y++) fwrite(row, 1, sizeof row, f);
      fclose(f);
      for (int i = 1; i <= 10; i++) {
          char cmd[1100], b64[1000];
          b64enc(raw, b64);
          snprintf(cmd, sizeof cmd, "Ga=t,t=f,f=24,s=2048,v=2048,i=%d;%s", 100 + i, b64);
          apc(cmd, 1, &todo);
          check(strstr(todo.reply, "OK") != NULL, "a 16 MB image is taken", todo.reply);
          check(jimgfx_held() <= JIMGFX_BUDGET, "held <= JIMGFX_BUDGET", NULL);
      }
      check(jimgfx_held() >= 3 * (16u << 20), "the last few are kept", NULL);
      unlink(raw); }

    start("t=f on a FIFO does not wait (LOW 12)", 5);
    { char fifo[600], b64[1200];
      snprintf(fifo, sizeof fifo, "%s/fifo", dir);
      if (mkfifo(fifo, 0600)) { perror("mkfifo"); return 1; }
      b64enc(fifo, b64);
      snprintf(buf, sizeof buf, "Ga=t,t=f,f=24,s=2,v=2,i=3;%s", b64);
      apc(buf, 1, &todo);
      check(strstr(todo.reply, "EBADF") != NULL, "a FIFO is EBADF", todo.reply);
      unlink(fifo); }

    start("t=t outside the temporary directories is not unlinked (LOW 12)", 5);
    { char keep[] = "jimgfxtest-tty-graphics-protocol.rgb", full[1200], b64[1700]; FILE *f;
      if (!getcwd(full, 600)) return 1;
      strcat(full, "/"); strcat(full, keep);
      if (!(f = fopen(full, "wb"))) { perror(full); return 1; }
      fwrite("\0\0\0\0\0\0\0\0\0\0\0\0", 1, 12, f); fclose(f);
      b64enc(full, b64);
      snprintf(buf, sizeof buf, "Ga=t,t=t,f=24,s=2,v=2,i=4;%s", b64);
      apc(buf, 1, &todo);
      check(strstr(todo.reply, "EBADF") != NULL, "refused", todo.reply);
      check(access(full, F_OK) == 0, "the file is still there", NULL);
      unlink(full);
      /* and one in the temporary directory is read and removed, as the protocol says */
      snprintf(full, sizeof full, "%s/tty-graphics-protocol-1.rgb", dir);
      if (!(f = fopen(full, "wb"))) { perror(full); return 1; }
      fwrite("\0\0\0\0\0\0\0\0\0\0\0\0", 1, 12, f); fclose(f);
      b64enc(full, b64);
      snprintf(buf, sizeof buf, "Ga=t,t=t,f=24,s=2,v=2,i=5;%s", b64);
      apc(buf, 1, &todo);
      check(strstr(todo.reply, "OK") != NULL, "a temporary file is read", todo.reply);
      check(access(full, F_OK) != 0, "and removed", NULL);
      unlink(full); }
    rmdir(dir);

    start("chunks (m=1) still make one picture", 5);
    jimgfx_reset(); init();
    send("\033_Ga=T,f=24,s=2,v=2,i=9,m=1;AAAAAAAA\033\\\033_Gm=0;AAAAAAAA\033\\"); drain(rp, sizeof rp);
    check(strstr(rp, "i=9;OK") != NULL, "chunked transmit replies OK", rp + (rp[0] != 0));
    check(k4510_ram[JIMGFX_PLANE] != 0, "the picture is on the plane", NULL);

    start("an APC past JIMGFX_APC_MAX, then JIM's own OSC (LOW 8)", 20);
    { /* the byte past the cap is the one dropped; what follows used to be read as
       * the stale OSC it landed in: here ESC ] 4510;kos BEL, a screen switch */
      static char chunk[1 << 20]; size_t left = JIMGFX_APC_MAX - 1;   /* 'G' and these fill it */
      memset(chunk, 'A', sizeof chunk);
      init(); term2_open();
      term2_feed((const uint8_t *) "\033_G", 3);
      while (left) { size_t n = left < sizeof chunk ? left : sizeof chunk; term2_feed((const uint8_t *) chunk, n); left -= n; }
      term2_feed((const uint8_t *) "x4510;kos\007", 10);
      check(term_screen_request() == -1, "the APC's tail is not taken for ESC ] 4510;kos", NULL); }

    start("ESC inside an APC that is not ST starts a sequence (LOW 8)", 5);
    init();
    send("\033_Gabc\033[3;5HX"); 
    check(R(9) == 5 && R(10) == 2, "ESC [ 3;5 H after the APC moved the cursor", NULL);

    start("a state saved inside an OSC loads in the ground state (LOW 10)", 5);
    { FILE *a = tmpfile(), *b = tmpfile(); long na, nb, last = -1; int ca, cb;
      init();
      send("\033]4510;"); term_state_save(a);
      send("k"); term_state_save(b);
      na = ftell(a); nb = ftell(b); rewind(a); rewind(b);
      for (long i = 0; i < na && i < nb; i++) { ca = fgetc(a); cb = fgetc(b); if (ca != cb) last = i; }
      check(na == nb && last > 8, "found T.oscn in the record", NULL);
      rewind(b);                                                        /* as saved: ESC ] 4510;k half sent */
      check(term_state_load(b) == 0, "the state loads", NULL);
      send("os\007");
      check(term_screen_request() == -1, "the OSC is not finished after the load", NULL);
      if (last > 8) {                                                   /* oscn = 200: osc_done wrote T.osc[200], past TS[0] */
          fseek(b, last, SEEK_SET); fputc(200, b); fflush(b); rewind(b);
          check(term_state_load(b) == 0, "the state loads", NULL);
          send("\007kos\007"); 
          check(term_screen_request() == -1, "no screen switch from a half OSC", NULL);
          check(k4510_ram[0x030000] == 'k', "the bytes after the load are text", NULL);
      }
      fclose(a); fclose(b); }

    alarm(0);
    if (fails) { printf("jimgfxtest: %d failed\n", fails); return 1; }
    printf("jimgfxtest: OK (the review's placements, sizes, numbers, the memory budget, FIFOs and t=t, chunks, an APC past the cap, ESC in an APC, a state saved mid-OSC)\n");
    return 0;
}
