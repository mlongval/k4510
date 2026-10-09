/* jimbench -- what JIM costs on a real program's output (the JIM review,
 * 2026-10-09).  Streams recorded by test/jim/rec.py (nvim, tmux, mosh on a pty
 * the Terminal screen's size) are fed into JIM's second screen through
 * term2_feed, chunk by chunk as the pty handed them over, the way core/screen2.c
 * does.  Per phase of the recording (open, Ctrl-D, j held, insert, :split...):
 * bytes, MB/s, and the cost of each keystroke's update -- everything the program
 * wrote between one key and the next -- as median / 95th percentile / worst.
 *
 *   test/jimbench [--loops N] [--commit] [--vterm] STREAM.jst[.gz]...
 *   (every recording: the .jst.gz files in test/jim/streams)
 *
 * --commit  a 640x480 text layer is up and the second screen is shown, so an
 *           ESC[?2026l would repaint the whole frame at once (vicky_commit).
 *           Since 2026-10-09 the second screen's never does -- s2_pump feeds
 *           it before the raster, which draws the finished update -- and this
 *           shows that cost gone (nvim's j held: 1.8 ms an update before)
 * --vterm   the same bytes through libvterm (with its screen layer), if the
 *           host has libvterm.so.0: a reference for "is JIM slow?"
 *
 * Nothing here is timed against the glass: what a frame costs to draw is
 * test/vidbench's, and when the bytes are read (once a frame) is sdl/main.c's. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dlfcn.h>
#include "../core/xemu/emutools_basicdefs.h"
#include "../core/xemu/cpu65.h"
#include "../core/mem.h"
#include "../core/io.h"
#include "../core/term.h"
#include "../core/vicky.h"

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
typedef struct { char tag; uint32_t t; uint32_t n; const uint8_t *b; } rec_t;
typedef struct { int cols, rows, nrec; rec_t *r; uint8_t *data; } stream_t;
static int load(const char *path, stream_t *s)
{
    /* .jst or .jst.gz (the recordings in test/jim/streams are kept gzipped) */
    size_t l = strlen(path), cap = 1 << 20, n = 0, i, got;
    int gz = l > 3 && !strcmp(path + l - 3, ".gz");
    char cmd[1200]; FILE *f;
    if (gz) { snprintf(cmd, sizeof cmd, "gzip -dc '%s'", path); f = popen(cmd, "r"); } else f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    s->data = malloc(cap + 1);
    while ((got = fread(s->data + n, 1, cap - n, f)) > 0) { n += got; if (n == cap) { cap *= 2; s->data = realloc(s->data, cap + 1); } }
    if (gz) { if (pclose(f) != 0) { fprintf(stderr, "%s: gzip failed\n", path); return -1; } } else fclose(f);
    s->data[n] = 0;
    if (sscanf((char *) s->data, "JST1 %d %d", &s->cols, &s->rows) != 2) { fprintf(stderr, "%s: not a JST1 stream\n", path); return -1; }
    i = (size_t)((uint8_t *) memchr(s->data, '\n', n) - s->data) + 1;
    s->r = malloc(sizeof(rec_t) * (n / 9 + 1)); s->nrec = 0;
    while (i + 9 <= n) {
        rec_t *r = &s->r[s->nrec++];
        r->tag = (char) s->data[i]; memcpy(&r->t, s->data + i + 1, 4); memcpy(&r->n, s->data + i + 5, 4);
        r->b = s->data + i + 9; i += 9 + r->n;
    }
    return 0;
}

/* libvterm, by dlopen: the host has the library (nvim's) but not its header */
typedef void *(*vt_new_f)(int, int);
static struct { void *(*new_)(int, int); void (*utf8)(void *, int); size_t (*write)(void *, const char *, size_t);
                void *(*screen)(void *); void (*sreset)(void *, int); void (*free_)(void *); size_t (*oread)(void *, char *, size_t); } vt;
static int vt_load(void)
{
    void *h = dlopen("libvterm.so.0", RTLD_NOW);
    if (!h) return 0;
    vt.new_ = (void *(*)(int, int)) dlsym(h, "vterm_new"); vt.utf8 = (void (*)(void *, int)) dlsym(h, "vterm_set_utf8");
    vt.write = (size_t (*)(void *, const char *, size_t)) dlsym(h, "vterm_input_write");
    vt.screen = (void *(*)(void *)) dlsym(h, "vterm_obtain_screen"); vt.sreset = (void (*)(void *, int)) dlsym(h, "vterm_screen_reset");
    vt.free_ = (void (*)(void *)) dlsym(h, "vterm_free"); vt.oread = (size_t (*)(void *, char *, size_t)) dlsym(h, "vterm_output_read");
    return vt.new_ && vt.utf8 && vt.write && vt.screen && vt.sreset && vt.free_ && vt.oread;
}

static int dcmp(const void *a, const void *b) { double x = *(const double *) a, y = *(const double *) b; return x < y ? -1 : x > y; }
static uint8_t fb[VICKY_WIDTH * VICKY_HEIGHT];

int main(int argc, char **argv)
{
    int loops = 10, commit = 0, vterm = 0, a = 1;
    for (; a < argc && argv[a][0] == '-'; a++) {
        if (!strcmp(argv[a], "--loops") && a + 1 < argc) loops = atoi(argv[++a]);
        else if (!strcmp(argv[a], "--commit")) commit = 1;
        else if (!strcmp(argv[a], "--vterm")) vterm = 1;
    }
    if (a >= argc) { fprintf(stderr, "usage: %s [--loops N] [--commit] [--vterm] STREAM.jst...\n", argv[0]); return 2; }
    if (vterm && !vt_load()) { printf("(no libvterm.so.0 here: --vterm skipped)\n"); vterm = 0; }
    if (mem_init()) return 1;
    for (; a < argc; a++) {
        stream_t s; uint8_t rep[512];
        if (load(argv[a], &s)) return 1;
        io_reset();
        /* the console's window as the ROM sets it, the stream's size; the second screen follows it */
        io_write(IO_TERM + 0x0D, (uint8_t) s.cols); io_write(IO_TERM + 5, (uint8_t) s.cols); io_write(IO_TERM + 6, (uint8_t) s.rows);
        io_write(IO_TERM + 7, 0); io_write(IO_TERM + 8, 0);
        term2_open();
        if (commit) {                                   /* layer 0 a text32 map over the second screen's, 8x16 cells */
            static const int L = VR_LAYER(0);
            uint32_t map = 0x0FD40000u, font = 0x401000u;
            for (int i = 0; i < 4; i++) { io_write(IO_VICKY + L + VL_DATA + i, (uint8_t)(font >> (8 * i))); io_write(IO_VICKY + L + VL_MAP + i, (uint8_t)(map >> (8 * i))); }
            io_write(IO_VICKY + L + VL_STRIDE, (uint8_t) s.cols); io_write(IO_VICKY + L + VL_STRIDE + 1, 0);
            io_write(IO_VICKY + L + VL_CTRL, (uint8_t)(1 | (VL_MODE_TEXT32 << 1) | (1 << 5)));
            io_write(IO_VICKY + VR_CTRL, 1);
            for (int i = 0; i < 4096; i++) mem_poke(font + (uint32_t) i, (uint8_t)(i * 37));
            term_screen_show(1);
            vicky_render(fb, VICKY_WIDTH);              /* arms frame_fb: a commit repaints into it */
        }
        long total = 0; for (int i = 0; i < s.nrec; i++) if (s.r[i].tag == 'O') total += s.r[i].n;
        printf("\n%s  %dx%d  %ld bytes, %d loops%s\n", argv[a], s.cols, s.rows, total, loops, commit ? ", commits repaint a 640x480 frame" : "");
        printf("  %-10s %8s %6s %9s %10s %10s %10s %10s\n", "phase", "bytes", "keys", "MB/s", "upd med us", "p95 us", "max us", "B/upd med");
        /* per phase: updates are the output between one key (or mark) and the next */
        double *ut = malloc(sizeof(double) * (size_t)(s.nrec + 1)), *ub = malloc(sizeof(double) * (size_t)(s.nrec + 1));
        double grand_t = 0; long grand_b = 0;
        int i = 0;
        const char *phase = "start"; char pname[32];
        while (i < s.nrec) {
            int start = i, end;
            for (end = i; end < s.nrec && s.r[end].tag != 'M'; end++) ;
            /* the phase is start..end-1 */
            int nu = 0, keys = 0; long pb = 0; double pt = 0;
            for (int L = 0; L < loops; L++) {
                double tu = 0; long bu = 0; int u = 0;
                for (int k = start; k <= end; k++) {
                    if (k == end || s.r[k].tag == 'K') {          /* an update ends where the next key is typed */
                        if (bu) { if (L == loops - 1) { ut[u] = tu * 1e6; ub[u] = (double) bu; u++; } pt += tu; pb += bu; }
                        tu = 0; bu = 0;
                        if (k < end && L == 0) keys++;
                        continue;
                    }
                    if (s.r[k].tag != 'O') continue;
                    double t0 = now();
                    term2_feed(s.r[k].b, s.r[k].n);
                    term2_replies(rep, sizeof rep);
                    tu += now() - t0; bu += s.r[k].n;
                }
                nu = u;
            }
            if (pb) {
                qsort(ut, (size_t) nu, sizeof(double), dcmp); qsort(ub, (size_t) nu, sizeof(double), dcmp);
                printf("  %-10s %8ld %6d %9.1f %10.1f %10.1f %10.1f %10.0f\n", phase, pb / loops, keys, pb / pt / 1e6,
                       ut[nu / 2], ut[nu * 95 / 100 < nu ? nu * 95 / 100 : nu - 1], ut[nu - 1], ub[nu / 2]);
                grand_t += pt; grand_b += pb;
            }
            if (end < s.nrec) { snprintf(pname, sizeof pname, "%.*s", (int) s.r[end].n, (const char *) s.r[end].b); phase = pname; }
            i = end + 1;
        }
        printf("  %-10s %8ld %6s %9.1f   (JIM, all phases)\n", "total", grand_b / loops, "", grand_b / grand_t / 1e6);
        if (vterm) {
            void *v = vt.new_(s.rows, s.cols); char ob[4096];
            vt.utf8(v, 1); vt.sreset(vt.screen(v), 1);
            double t = 0; long b = 0;
            for (int L = 0; L < loops; L++)
                for (int k = 0; k < s.nrec; k++) if (s.r[k].tag == 'O') {
                    double t0 = now(); vt.write(v, (const char *) s.r[k].b, s.r[k].n); while (vt.oread(v, ob, sizeof ob)) ; t += now() - t0; b += s.r[k].n; }
            printf("  %-10s %8ld %6s %9.1f   (libvterm with its screen layer, same bytes)\n", "vterm", b / loops, "", b / t / 1e6);
            vt.free_(v);
        }
        if (commit) term_screen_show(0);
        free(ut); free(ub); free(s.r); free(s.data);
    }
    return 0;
}
