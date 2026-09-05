/* The in-process Tube co-processor: rings, handshake, the co-processor's
 * little OS. See tube_cp.h for the picture. */
#include "tube_cp.h"
#include "sndq.h"
void k4510_audio_pump(void);   /* sdl/main.c: the frontend owns the ring */
/* GCC's builtins rather than <stdatomic.h>: the Pi kernel compiles with
 * -nostdinc and its libc's headers, which do not carry it. */
#define atomic_load(p)     __atomic_load_n((p), __ATOMIC_SEQ_CST)
#define atomic_store(p, v) __atomic_store_n((p), (v), __ATOMIC_SEQ_CST)
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "net.h"

/* ---- the rings ---------------------------------------------------------
 * Single producer, single consumer each. The writer owns w, the reader owns
 * r; the reader is the only side that ever stores r, so the writer cannot
 * corrupt it, and vice versa. Resets happen only while the co-processor is
 * not running, from the machine's side. */
#define OUT_SZ 16384                      /* co-processor -> machine (its screen) */
#define IN_SZ  256                        /* machine -> co-processor (its keyboard) */
static uint8_t out_ring[OUT_SZ]; static volatile unsigned out_w, out_r;
static uint8_t in_ring[IN_SZ];   static volatile unsigned in_w, in_r;

static volatile int cp_req;                /* program to start (1), 0 = none pending */
static volatile int cp_alive;
static volatile int cp_kill;

/* ---- the machine's side ------------------------------------------------ */
#ifndef K4510_PI
#include <pthread.h>
#include <time.h>
static pthread_t cp_thread; static int cp_thread_up;
static void *cp_thread_main(void *arg) { (void) arg; tube_cp_run(); return NULL; }
unsigned tube_cp_ticks(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (unsigned)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000); }
void tube_cp_usleep(unsigned us) { usleep(us); }
#endif

/* How often the co-processor's core looks for work. It is idle from power-on
 * until the ROM runs BBC or CPM, which on most sessions is never, so on the Pi
 * -- where this core is a real core and the wait is a spin -- it should look
 * rarely. 20 ms is imperceptible against starting an interpreter. */
#ifdef K4510_PI
#define TUBE_IDLE_US 20000
#else
#define TUBE_IDLE_US 1000
#endif

int tube_cp_start(int prog)
{
    if (prog != 1 && prog != 3) return -1;   /* 1 = BBC BASIC, 3 = CP/M */
    /* If the co-processor's core is holding the sound, take it back BEFORE
     * letting it run an interpreter -- it cannot do both, and this is the
     * quiescent moment the handover wants: the ROM is between commands. */
    if (sndq_owner() != SNDQ_OWNER_CPU) sndq_request(SNDQ_OWNER_CPU);
#ifndef K4510_PI
    if (!cp_thread_up) { if (pthread_create(&cp_thread, NULL, cp_thread_main, NULL)) return -1; cp_thread_up = 1; }
#endif
    if (!atomic_load(&cp_alive)) {        /* nobody writing: a clean slate */
        atomic_store(&out_w, 0); atomic_store(&out_r, 0);
        atomic_store(&in_w, 0);  atomic_store(&in_r, 0);
    }
    atomic_store(&cp_kill, 0);
    atomic_store(&cp_req, prog);
#if defined(K4510_PI) && defined(__aarch64__)
    __asm__ volatile("sev" ::: "memory");     /* wake the co-processor's core */
#endif          /* the co-processor picks this up (after quitting, if it is still running) */
    return 0;
}
void tube_cp_stop(void)
{
    atomic_store(&cp_req, 0);
    if (atomic_load(&cp_alive)) atomic_store(&cp_kill, 1);
    else { atomic_store(&out_w, 0); atomic_store(&out_r, 0); atomic_store(&in_w, 0); atomic_store(&in_r, 0); }
}
int tube_cp_alive(void) { return atomic_load(&cp_alive); }
int tube_cp_read(uint8_t *buf, int max)
{
    unsigned r = atomic_load(&out_r), w = atomic_load(&out_w); int n = 0;
    while (n < max && r != w) buf[n++] = out_ring[r++ % OUT_SZ];
    atomic_store(&out_r, r);
    return n;
}
void tube_cp_write(uint8_t b)
{
    unsigned w = atomic_load(&in_w);
    if (w - atomic_load(&in_r) >= IN_SZ) return;      /* the co-processor is not listening; drop, like a full pty would not... but a keyboard buffer does */
    in_ring[w % IN_SZ] = b;
    atomic_store(&in_w, w + 1);
}

/* ---- the co-processor's side ------------------------------------------- */
int tube_cp_getc(void)
{
    unsigned r = atomic_load(&in_r);
    if (r == atomic_load(&in_w)) return -1;
    uint8_t b = in_ring[r % IN_SZ];
    atomic_store(&in_r, r + 1);
    return b;
}
void tube_cp_puts(const char *s, size_t n)
{
    while (n) {
        unsigned w = atomic_load(&out_w);
        if (w - atomic_load(&out_r) >= OUT_SZ) {       /* the machine is not draining: wait, unless we are being killed */
            if (atomic_load(&cp_kill)) return;
            tube_cp_usleep(200);
            continue;
        }
        out_ring[w % OUT_SZ] = (uint8_t)*s++; n--;
        atomic_store(&out_w, w + 1);
    }
}
int tube_cp_printf(const char *fmt, ...)
{
    char buf[512]; va_list ap; int n;
    va_start(ap, fmt); n = vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    if (n > 0) tube_cp_puts(buf, (size_t)n < sizeof buf ? (size_t)n : sizeof buf - 1);
    return n;
}
int tube_cp_flush(FILE *f) { return (f == stdout || f == stderr) ? 0 : fflush(f); }
int tube_cp_killed(void) { return atomic_load(&cp_kill); }

static void *cp_ram; static size_t cp_ram_bytes;
void *tube_cp_ram(size_t *bytes)
{
    if (!cp_ram) {
        size_t want = 0x10000000u;                     /* 256 MB, like the machine; halve until the host agrees */
        while (want >= 0x400000u && !(cp_ram = malloc(want))) want >>= 1;
        cp_ram_bytes = cp_ram ? want : 0;
    }
    *bytes = cp_ram_bytes;
    return cp_ram;
}

void tube_cp_run(void)
{
    for (;;) {
        int prog;
#if defined(K4510_PI) && defined(__aarch64__)
        /* The co-processor's core sleeps until an event: tube_cp_start sends
         * one.  A spinning core heats the SoC, and Circle pulls the clock to
         * idle above 60 C -- so the spin was making the whole machine slow.
         *
         * Unless it has been given the sound.  From power-on until the ROM
         * runs BBC or CPM -- which on most sessions is never -- this core is
         * doing nothing at all, and the OPL2's synthesis is a slice of the
         * emulator's 16.7 ms frame.  So while it owns them it renders instead
         * of sleeping, and it hands them back the moment anything asks: the
         * emulator's core asks before it starts a program here, and waits. */
        for (;;) {
            if (sndq_pending()) sndq_accept();
            if ((prog = atomic_load(&cp_req))) break;
            if (sndq_owner() == SNDQ_OWNER_OTHER) k4510_audio_pump();
            else __asm__ volatile("wfe" ::: "memory");
        }
#else
        while (!(prog = atomic_load(&cp_req))) tube_cp_usleep(TUBE_IDLE_US);
#endif
        atomic_store(&cp_req, 0);
        atomic_store(&cp_alive, 1);
        int rc;
        if (prog == 3) { static char a0[] = "runcpm"; char *av[] = { a0, NULL }; tube_cp_chdir("/CPM"); rc = tube_cpm_main(1, av); }
        else { tube_cp_chdir("/"); rc = tube_bbc_main(); }
#ifndef K4510_PI
        fprintf(stderr, "[tube: program %d exited %d]\n", prog, rc);
#endif
        atomic_store(&cp_alive, 0);
        atomic_store(&cp_kill, 0);
    }
}

/* ---- files: the co-processor's own current directory ------------------- */
static char cp_cwd[256];                               /* relative to the machine's root, no slashes at either end; "" = root */

static void cp_normalise(char *p)                      /* collapse ".", ".." and doubled slashes in a root-relative path */
{
    char out[256]; size_t o = 0; const char *s = p;
    while (*s) {
        while (*s == '/') s++;
        if (!*s) break;
        const char *e = s; while (*e && *e != '/') e++;
        size_t n = (size_t)(e - s);
        if (n == 1 && s[0] == '.') { }
        else if (n == 2 && s[0] == '.' && s[1] == '.') { while (o && out[o - 1] != '/') o--; if (o) o--; }
        else { if (o && o + 1 < sizeof out) out[o++] = '/'; if (o + n < sizeof out) { memcpy(out + o, s, n); o += n; } }
        s = e;
    }
    out[o] = 0; strcpy(p, out);
}
static void cp_path(char *out, size_t max, const char *in)
{
    char rel[256];
    if (in[0] == '/') snprintf(rel, sizeof rel, "%s", in);
    else if (cp_cwd[0]) snprintf(rel, sizeof rel, "%s/%s", cp_cwd, in);
    else snprintf(rel, sizeof rel, "%s", in);
    cp_normalise(rel);
    if (rel[0]) snprintf(out, max, "%s/%s", fs_get_root(), rel); else snprintf(out, max, "%s", fs_get_root());
}
FILE *tube_cp_fopen(const char *path, const char *mode)
{
    char p[512];
    if (net_is_url(path)) {                                /* the Meatloaf rule reaches the co-processor too */
        static uint8_t *last; uint8_t *b; uint32_t n;
        if (mode[0] != 'r' || net_fetch(path, &b, &n)) return NULL;
        free(last); last = b;                              /* one fetched file at a time: freed by the next */
        return fmemopen(b, n ? n : 1, "rb");
    }
    cp_path(p, sizeof p, path); return fopen(p, mode);
}
DIR  *tube_cp_opendir(const char *path) { char p[512]; cp_path(p, sizeof p, path); return opendir(p); }
int   tube_cp_remove(const char *path) { char p[512]; cp_path(p, sizeof p, path); return remove(p); }
int   tube_cp_rename(const char *a, const char *b) { char p[512], q[512]; cp_path(p, sizeof p, a); cp_path(q, sizeof q, b); return rename(p, q); }
int   tube_cp_mkdir(const char *path, unsigned mode) { char p[512]; cp_path(p, sizeof p, path); return mkdir(p, (mode_t)mode); }
int   tube_cp_rmdir(const char *path)
{
    char p[512]; cp_path(p, sizeof p, path);
#ifdef K4510_PI
    (void) p; return -1;                               /* circle-syscallwrap has no rmdir yet (io.c says the same) */
#else
    return rmdir(p);
#endif
}
int tube_cp_chdir(const char *path)
{
    char rel[256], p[512]; struct stat sb;
    if (path[0] == '/') snprintf(rel, sizeof rel, "%s", path);
    else if (cp_cwd[0]) snprintf(rel, sizeof rel, "%s/%s", cp_cwd, path);
    else snprintf(rel, sizeof rel, "%s", path);
    cp_normalise(rel);
    if (rel[0]) snprintf(p, sizeof p, "%s/%s", fs_get_root(), rel); else snprintf(p, sizeof p, "%s", fs_get_root());
    if (stat(p, &sb) || !S_ISDIR(sb.st_mode)) return -1;
    strcpy(cp_cwd, rel);
    return 0;
}
int tube_cp_stat(const char *path, struct stat *sb) { char p[512]; cp_path(p, sizeof p, path); return stat(p, sb); }
int tube_cp_truncate(const char *path, long length)
{
    char p[512]; cp_path(p, sizeof p, path);
#ifdef K4510_PI
    FILE *f = fopen(p, "r+b"); if (!f) return -1;      /* newlib has no truncate(); ftruncate is wrapped */
    int r = ftruncate(fileno(f), length); fclose(f); return r;
#else
    return truncate(p, length);
#endif
}
int tube_cp_chmod(const char *path, unsigned mode)
{
#ifdef K4510_PI
    (void) path; (void) mode; return 0;
#else
    char p[512]; cp_path(p, sizeof p, path); return chmod(p, (mode_t)mode);
#endif
}
char *tube_cp_getcwd(char *buf, size_t n) { snprintf(buf, n, "/%s", cp_cwd); return buf; }
