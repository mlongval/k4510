#!/usr/bin/env python3
"""RunCPM as the in-process Tube child (program 3). Generates
cpm/src/abstraction_k4510.h from abstraction_posix.h -- same file, four
seams replaced -- and wires the build. Run from k4510/."""
import sys, re

def patch(path, edits):
    s = open(path).read()
    for old, new in edits:
        if new in s and s.count(old) == 0: continue
        if s.count(old) != 1: sys.exit(f"{path}: anchor ({s.count(old)}x): {old[:70]!r}")
        s = s.replace(old, new)
    open(path, "w").write(s); print("patched", path)

# ---------------------------------------------------------------- the abstraction
s = open("cpm/src/abstraction_posix.h").read()
def rep(old, new):
    global s
    if s.count(old) != 1: sys.exit(f"abstraction: anchor ({s.count(old)}x): {old[:70]!r}")
    s = s.replace(old, new)

rep("""#include <ctype.h>
#include <errno.h>
#include <glob.h>
#include <libgen.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#define millis() clock() / 1000
""",
"""/* [K4510] abstraction_k4510.h -- RunCPM as the K4510's in-process Tube
 * co-processor (core 3 on the Pi, a thread in the desktop test build).
 * GENERATED from abstraction_posix.h by patch_cpm.py: identical except at
 * four seams -- no termios/poll/glob/system, the console is the Tube's
 * rings (core/tube_cp.h), the files go through the co-processor's path
 * layer (its cwd is /CPM, so "A/0/NAME" lands in fs/CPM/A/0), the
 * directory search walks opendir() instead of glob(), and the machine's
 * kill ($D803 = 2) longjmps out of the Z80. Edit the generator, not this. */
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <setjmp.h>
#include <time.h>
#include <unistd.h>
#include "tube_cp.h"
#define millis() tube_cp_ticks()
#define usleep(us) tube_cp_usleep(us)
#define fopen tube_cp_fopen
#define remove tube_cp_remove
#define rename tube_cp_rename
#define mkdir tube_cp_mkdir
#define stat(p, b) tube_cp_stat((p), (b))
#define truncate tube_cp_truncate
#define access(p, m) (tube_cp_stat((p), &_k4_st) ? -1 : 0)
static struct stat _k4_st;
jmp_buf tube_cpm_env;                       /* tube_cpm_main's landing for the kill */
static void _k4_check_kill(void) { if (tube_cp_killed()) longjmp(tube_cpm_env, 1); }
""")
rep("""#ifdef STREAMIO
    #include <termios.h>
#endif
""", "")
rep("""#define FOLDERCHAR '/'
#define FILEBASE "./"
""", """#define FOLDERCHAR '/'
#define FILEBASE ""                          /* [K4510] the path layer adds fs/CPM/ */
""")
# the directory search: opendir instead of glob, sorted like glob was
rep("""glob_t pglob;
int dirPos;
""", """int dirPos;
/* [K4510] A directory listing gathered with opendir(), sorted as glob()
 * sorted, over "D/U" or -- for the all-users search "D/?" -- every user
 * folder 0-9, A-F. Names are "D/U/NAME" like glob's paths were. */
static char _k4_names[512][32]; static int _k4_count;
static int _k4_cmp(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }
static void _k4_scan_dir(const char *dir3)
{
    DIR *d = tube_cp_opendir(dir3); struct dirent *e;
    if (!d) return;
    while ((e = readdir(d)) && _k4_count < 512) {
        if (e->d_name[0] == '.' || strlen(e->d_name) > 27) continue;
        snprintf(_k4_names[_k4_count++], 32, "%s/%s", dir3, e->d_name);
    }
    closedir(d);
}
static int _k4_glob(const char *dir)          /* dir is "D/U/*" or "D/?/*" */
{
    char d3[4] = { dir[0], '/', dir[2], 0 };
    _k4_count = 0;
    if (dir[2] == '?') { for (const char *u = "0123456789ABCDEF"; *u; u++) { d3[2] = *u; _k4_scan_dir(d3); } }
    else _k4_scan_dir(d3);
    qsort(_k4_names, _k4_count, sizeof _k4_names[0], _k4_cmp);
    return _k4_count == 0;
}
""")
rep("""        if (!glob((char *)fullpath, 0, NULL, &pglob)) {
            for (i = dirPos; i < pglob.gl_pathc; ++i) {
                ++dirPos;
                strncpy(findNextDirName, pglob.gl_pathv[i], sizeof(findNextDirName) - 1);""",
"""        if (!_k4_glob((char *)fullpath)) {
            for (i = dirPos; i < _k4_count; ++i) {
                ++dirPos;
                strncpy(findNextDirName, _k4_names[i], sizeof(findNextDirName) - 1);""")
rep("""            globfree(&pglob);
        }""", """        }""")
# the console
i = s.index("#ifndef RUNVT_EMBED\nstatic struct termios _old_term, _new_term;")
j = s.index("#endif // RUNVT_EMBED")
s = s[:i] + """/* [K4510] the console is the Tube: bytes down to the machine's ROM
 * console, keys up from it. A wait for a key is where the kill is felt. */
static int _k4_pending = -1;

void _console_init(void) { _k4_pending = -1; }
void _console_reset(void) { }

int _kbhit(void) {
    _k4_check_kill();
    if (_k4_pending < 0) _k4_pending = tube_cp_getc();
    return _k4_pending >= 0;
}

uint8 _getch(void) {
    uint8 ch;
    while (!_kbhit()) tube_cp_usleep(2000);
    ch = (uint8)_k4_pending; _k4_pending = -1;
    return ch;
}

void _putch(uint8 ch) {
    char c = (char)ch;
    _k4_check_kill();
    tube_cp_puts(&c, 1);
}

uint8 _getche(void) {
    uint8 ch = _getch();
    _putch(ch);
    return ch;
}

void _clrscr(void) {
    tube_cp_puts("\\033[H\\033[J", 6);       /* the K4510 console understands ESC[J */
}
""" + s[j + len("#endif // RUNVT_EMBED"):]
open("cpm/src/abstraction_k4510.h", "w").write(s)
print("generated cpm/src/abstraction_k4510.h")

# ---------------------------------------------------------------- main.c: pick it
patch("cpm/src/main.c", [(
"""    #ifdef RUNVT_EMBED
        #include "abstraction_runvt.h"
    #elif defined(_WIN32)""",
"""    #ifdef RUNVT_EMBED
        #include "abstraction_runvt.h"
    #elif defined(K4510_TUBE)
        #include "abstraction_k4510.h" // [K4510] the in-process Tube co-processor
    #elif defined(_WIN32)"""),
(
"""int main(int argc, char *argv[]) {

    #ifdef DEBUGLOG""",
"""int main(int argc, char *argv[]) {
    #ifdef K4510_TUBE
    if (setjmp(tube_cpm_env)) return 2;  // [K4510] the machine stopped the Tube
    #endif

    #ifdef DEBUGLOG"""),
])

# ---------------------------------------------------------------- the Tube: program 3
patch("core/tube_cp.h", [
("""int   tube_cp_chmod(const char *path, unsigned mode);   /* the Pi's card has no modes: 0 */""",
"""int   tube_cp_chmod(const char *path, unsigned mode);   /* the Pi's card has no modes: 0 */
struct stat;
int   tube_cp_stat(const char *path, struct stat *sb);
int   tube_cp_truncate(const char *path, long length);"""),
("""int      tube_bbc_main(void);              /* the interpreter (bbccon.c); returns BASIC's exit code */""",
"""int      tube_bbc_main(void);              /* the interpreter (bbccon.c); returns BASIC's exit code */
int      tube_cpm_main(int argc, char **argv);   /* RunCPM's main, renamed (cpm/src/main.c with -Dmain=) */"""),
])
patch("core/tube_cp.c", [
("""    if (prog != 1) return -1;             /* CP/M on the in-process Tube: later */""",
"""    if (prog != 1 && prog != 3) return -1;   /* 1 = BBC BASIC, 3 = CP/M */"""),
("""        int rc = prog == 1 ? tube_bbc_main() : -1;""",
"""        int rc;
        if (prog == 3) { static char a0[] = "runcpm"; char *av[] = { a0, NULL }; tube_cp_chdir("/CPM"); rc = tube_cpm_main(1, av); }
        else { tube_cp_chdir("/"); rc = tube_bbc_main(); }"""),
("""int tube_cp_chmod(const char *path, unsigned mode)""",
"""int tube_cp_stat(const char *path, struct stat *sb) { char p[512]; cp_path(p, sizeof p, path); return stat(p, sb); }
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
int tube_cp_chmod(const char *path, unsigned mode)"""),
])
patch("core/io.c", [
("""static void tube_start(int prog) { tube_cp_start(prog); }             /* 1 = BBC BASIC; 3 (CP/M) is not fitted in-process yet */""",
"""static void tube_start(int prog) { tube_cp_start(prog); }             /* 1 = BBC BASIC, 3 = CP/M, both in-process */"""),
])

# ---------------------------------------------------------------- builds
patch("Makefile", [
("""CORE_IP_OBJS = $(filter-out core/io.o,$(CORE_OBJS)) core/io_ip.o core/tube_cp.o""",
"""CORE_IP_OBJS = $(filter-out core/io.o,$(CORE_OBJS)) core/io_ip.o core/tube_cp.o cpm/ip_runcpm.o
cpm/ip_runcpm.o: cpm/src/main.c $(wildcard cpm/src/*.h) core/tube_cp.h
	$(CC) -O2 -Wall -Wno-unused-variable -Wno-unused-function -Icore -DK4510_TUBE -Dmain=tube_cpm_main -DCCP_INTERNAL -DCPU=\\"cpu1.h\\" -c -o $@ $<"""),
])
# (the pi/Makefile patch that stood here went with the bare-metal Pi port, 2026-09-07)

# ---------------------------------------------------------------- the test leg
patch("test/tubetest.sh", [
("""echo "tubetest: OK (PRINT, restart after *QUIT, MODE 2/GCOL/PLOT/MODE 7)\"""",
"""run 'CPM
~DIR
~EXIT
' 900 'has left' 'CP/M on the in-process Tube'
grep -q 'A0>' test/tubetest.out || { cat test/tubetest.out; echo "tubetest: FAILED: no CP/M prompt"; exit 1; }
echo "tubetest: OK (PRINT, restart after *QUIT, MODE 2/GCOL/PLOT/MODE 7, CP/M DIR/EXIT)\""""),
])
open("cpm/ALTERED.md", "w").write("""# Altered source notice (RunCPM, MIT -- see LICENSE-RunCPM.txt)

`src/` is RunCPM as vendored (VENDORED-FROM.txt), with these [K4510]
changes for the in-process Tube build (`-DK4510_TUBE -Dmain=tube_cpm_main`,
used by the Pi kernel and by the desktop `make tubetest`; the desktop's
`cpm/runcpm` on a pty is built from the unmodified POSIX abstraction):

- src/main.c: selects abstraction_k4510.h under K4510_TUBE, and lands
  the machine's kill (setjmp) so *stopping the Tube* returns from main.
- src/abstraction_k4510.h: generated from abstraction_posix.h by the
  project's patch_cpm.py -- identical except: no termios/poll/glob/
  system; console on the Tube rings (core/tube_cp.h); files through the
  co-processor's path layer with FILEBASE ""; the directory search walks
  opendir() (sorted, as glob sorted); millis() is the Tube clock.
""")
print("done")
