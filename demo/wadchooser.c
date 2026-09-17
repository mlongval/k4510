/* K4510: WADCHOOSER -- which game DOOM plays, and fetching the ones that may
 * be fetched.
 *
 * Doc, 2026-09-17: "there is a github with doom wads.  Can you make a
 * WADCHOOSER.prg in /APPS/DOOM that can download the .wad files available
 * there."
 *
 * What it offers comes from two places:
 *
 *   /APPS/DOOM/WADS.CFG   the catalogue: games that can be DOWNLOADED, one a
 *                         line,  NAME.WAD|MB|what it is|URL|path in the zip
 *   any .WAD in /DISK/DOOM   whatever is already there, catalogued or not
 *
 * The catalogue as shipped lists only what its owners let anybody pass on:
 * Freedoom (BSD) and id Software's DOOM shareware episode.  The repository
 * Doc pointed at also carries the full commercial games -- DOOM, DOOM II,
 * Final DOOM, Heretic, Hexen, Strife -- which are still sold and which that
 * repository has no licence to hand out, so this does not fetch them, and the
 * K4510's public repo does not point at them.  It does not need to: the
 * second source is the answer.  A WAD you own, copied into /DISK/DOOM, is
 * listed here and chosen like any other.  (Heretic, Hexen and Strife would
 * not run anyway: the co-processor is the DOOM engine only.)
 *
 * Nothing is downloaded INTO the machine.  The storage device does it all on
 * the host: MOUNT takes a zip from a URL, COPYFILE copies out of it (or
 * straight from a URL), and the 45GS10 never sees a byte of the 28 MB.
 *
 * The choice is one line in /DISK/DOOM/DOOM.CFG, "wad = NAME", which the
 * emulator reads when DOOM is started (core/io.c).
 */
#include "k4510.h"

#define FS       0xD300u
#define FS_CMD   (FS + 0x00)
#define FS_ST    (FS + 0x01)
#define FS_NAME  (FS + 0x04)
#define FS_ADDR  (FS + 0x08)
#define FS_LEN   (FS + 0x0C)
#define FS_SIZE  (FS + 0x10)
#define C_DIR1    6
#define C_DIRN    7
#define C_STAT    8
#define C_LOAD    9
#define C_SAVE   10
#define C_CHDIR  11
#define C_MKDIR  12
#define C_RMDIR  14
#define C_GETCWD 15
#define C_COPY   17
#define C_MOUNT  19
#define C_UMOUNT 20

/* The games live on /DISK, not beside the program.  Doc, 2026-09-17: "the
 * premise of this system is that it is loaded and runs from RAM ... however
 * loading a bunch of doom wads into ram for a slim to none chance of being
 * played is nonsensical".  /APPS ships in the layer and the layer is RAM;
 * /DISK is the part of the machine that stays on the disk and is read when
 * somebody asks (fs/DISK/README.TXT).  The catalogue is small and ships. */
#define HOME  "/DISK/DOOM"
#define CAT   "/APPS/DOOM/WADS.CFG"
#define CFG   HOME "/DOOM.CFG"
#define DL    HOME "/DL"                  /* where a zip is mounted while one file is copied out of it */

#define KY(k)   (0x100u | (k))
#define K_UP    KY(0x80)
#define K_DOWN  KY(0x81)

void __fastcall__ rom_chrout(unsigned char c);

#define MAXI 16
static char cat[2048];                    /* WADS.CFG, cut into strings where it stands */
static char extra[8][32];                 /* WADs on the disk that the catalogue does not know */
static struct { const char *name, *mb, *what, *url, *inner; uint8_t have; } it[MAXI];
static uint8_t n_it, n_extra, sel;
static char chosen[32], cwd[160], ent[64], path[160], cfgbuf[48];

static uint8_t fs_do(uint8_t c)       { REG(FS_CMD) = c; return REG(FS_ST); }
static void    fs_name(const char *s) { w32(FS_NAME, (uint32_t)(uint16_t) s); }
static void    fs_addr(const char *s) { w32(FS_ADDR, (uint32_t)(uint16_t) s); }

static void say(const char *s) { while (*s) rom_chrout(*s++); }
static void sayln(const char *s) { say(s); rom_chrout('\n'); }

static uint16_t getkey(void)
{
    uint8_t st = REG(KBDST), k;
    if (!(st & 0x80)) return 0;
    k = REG(KBD);
    return (st & 0x20) ? KY(k) : k;
}
static uint16_t waitkey(void) { uint16_t k; while ((k = getkey()) == 0) ; return k; }

static char up(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }
static uint8_t same(const char *a, const char *b)     /* names, whatever their case */
{
    while (*a && *b) { if (up(*a) != up(*b)) return 0; a++; b++; }
    return *a == *b;
}
static uint8_t is_wad(const char *s)
{
    uint8_t l = (uint8_t) strlen(s);
    return l > 4 && same(s + l - 4, ".WAD");
}
static char *trim(char *s)
{
    char *e;
    while (*s == ' ' || *s == '\t') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = 0;
    return s;
}

/* WADS.CFG: five fields a line, '|' between them; '#' starts a comment line */
static void read_catalogue(void)
{
    char *p = cat, *line, *f[5];
    uint8_t i;
    fs_name(CAT); fs_addr(cat); w32(FS_LEN, sizeof cat - 1);
    if (fs_do(C_LOAD) != 0) return;                   /* none, or too long: only what is on the disk is offered */
    cat[(uint16_t) (REG(FS_LEN) | (uint16_t) REG(FS_LEN + 1) << 8)] = 0;
    while (*p && n_it < MAXI) {
        line = p;
        while (*p && *p != '\n') p++;
        if (*p) *p++ = 0;
        line = trim(line);
        if (!*line || *line == '#') continue;
        for (i = 0; i < 5; i++) {
            f[i] = line;
            while (*line && *line != '|') line++;
            if (*line) *line++ = 0;
            f[i] = trim(f[i]);
        }
        if (!is_wad(f[0]) || !*f[3]) continue;
        it[n_it].name = f[0]; it[n_it].mb = f[1]; it[n_it].what = f[2]; it[n_it].url = f[3]; it[n_it].inner = f[4];
        n_it++;
    }
}
/* every .WAD in the folder: mark the catalogue's, add the rest */
static void read_folder(void)
{
    uint8_t i;
    for (i = 0; i < n_it; i++) it[i].have = 0;
    while (n_it && !it[n_it - 1].url) n_it--;         /* the extras of a previous pass */
    n_extra = 0;
    fs_addr(ent);
    if (fs_do(C_DIR1)) return;
    for (;;) {
        fs_addr(ent);
        if (fs_do(C_DIRN)) break;
        if (!is_wad(ent)) continue;
        for (i = 0; i < n_it; i++) if (it[i].url && same(it[i].name, ent)) { it[i].have = 1; break; }
        if (i < n_it || n_extra >= 8 || n_it >= MAXI || strlen(ent) > 31) continue;
        strcpy(extra[n_extra], ent);
        it[n_it].name = extra[n_extra]; it[n_it].mb = ""; it[n_it].what = "on the disk"; it[n_it].url = 0; it[n_it].inner = ""; it[n_it].have = 1;
        n_extra++; n_it++;
    }
}
static void read_choice(void)
{
    char *p;
    chosen[0] = 0;
    fs_name(CFG); fs_addr(cfgbuf); w32(FS_LEN, sizeof cfgbuf - 1);
    if (fs_do(C_LOAD) != 0) return;
    cfgbuf[REG(FS_LEN)] = 0;
    if (!(p = strchr(cfgbuf, '='))) return;
    { char *e = strchr(p, '\n'); if (e) *e = 0; }
    p = trim(p + 1);
    if (strlen(p) < sizeof chosen) strcpy(chosen, p);
}
static uint8_t write_choice(const char *name)
{
    strcpy(cfgbuf, "wad = "); strcat(cfgbuf, name); strcat(cfgbuf, "\n");
    fs_name(CFG); fs_addr(cfgbuf); w32(FS_LEN, (uint32_t) strlen(cfgbuf));
    if (fs_do(C_SAVE)) return 0;
    strcpy(chosen, name);
    return 1;
}

static void draw(const char *note)
{
    uint8_t i;
    say("\033[2J\033[H\033[1m WADCHOOSER\033[0m -- which game DOOM plays\n\n");
    for (i = 0; i < n_it; i++) {
        say(i == sel ? " \033[7m " : "  ");
        say(same(it[i].name, chosen) ? "* " : "  ");
        say(it[i].name);
        { uint8_t l = (uint8_t) strlen(it[i].name); while (l++ < 15) rom_chrout(' '); }
        say(it[i].have ? "here      " : "download  ");
        if (!it[i].have && *it[i].mb) { say(it[i].mb); say(" MB  "); }
        say(it[i].what);
        say(i == sel ? " \033[0m\n" : "\n");
    }
    if (!n_it) sayln("  nothing: no WADS.CFG, and no .WAD in " HOME);
    say("\n Up/Down or K/J: move   Enter: choose, fetching it if need be   Q: leave\n");
    say(" * is what DOOM plays.  A WAD of your own, put in " HOME ", is listed too.\n\n ");
    if (note) say(note);
}

/* fetch item i into the folder; 0 when it arrived */
static uint8_t fetch(uint8_t i)
{
    uint8_t st, zip, l = (uint8_t) strlen(it[i].url);
    zip = l > 4 && same(it[i].url + l - 4, ".zip");
    strcpy(path, HOME "/"); strcat(path, it[i].name);           /* where it lands */
    if (!zip) { fs_name(it[i].url); fs_addr(path); return fs_do(C_COPY); }
    if (strlen(it[i].inner) > 120) return 5;                     /* src[] below; the device's own limit is 127 */
    fs_name(it[i].url); fs_addr(DL);
    if ((st = fs_do(C_MOUNT)) != 0) return st;
    { static char src[160]; strcpy(src, DL "/"); strcat(src, it[i].inner); fs_name(src); fs_addr(path); st = fs_do(C_COPY); }
    fs_name(DL); fs_do(C_UMOUNT);
    fs_name(DL); fs_do(C_RMDIR);                                 /* the mount point MOUNT made */
    return st;
}

void main(void)
{
    uint16_t k;
    const char *note = 0;
    w32(FS_ADDR, (uint32_t)(uint16_t) cwd); fs_do(C_GETCWD);
    fs_name("/DISK"); fs_do(C_MKDIR);                            /* both may be there already; CHDIR below is the test */
    fs_name(HOME); fs_do(C_MKDIR);
    fs_name(HOME);
    if (fs_do(C_CHDIR)) { sayln("WADCHOOSER: cannot make " HOME); return; }
    read_catalogue(); read_folder(); read_choice();
    if (!chosen[0]) strcpy(chosen, "FREEDOOM1.WAD");             /* what DOOM plays when nothing says otherwise */
    for (sel = 0; sel < n_it && !same(it[sel].name, chosen); sel++) ;
    if (sel >= n_it) sel = 0;
    for (;;) {
        draw(note); note = 0;
        k = waitkey();
        if (k == 'q' || k == 'Q' || k == 27) break;
        if ((k == K_UP || k == 'k') && sel) sel--;                   /* k and j too, as RANGER has them */
        if ((k == K_DOWN || k == 'j') && sel + 1 < n_it) sel++;
        if ((k == 13 || k == 10) && n_it) {
            if (!it[sel].have) {
                say("\r fetching "); say(it[sel].name); say(" -- the machine stands still until it is here...");
                if (fetch(sel)) { note = "it did not arrive: no network, or the address has moved (WADS.CFG)"; read_folder(); continue; }
                read_folder();
            }
            note = write_choice(it[sel].name) ? "chosen.  Q, then DOOM, to play it." : "could not write " CFG;
        }
    }
    fs_name(cwd); fs_do(C_CHDIR);                                /* the shell's directory, as it was */
    say("\033[2J\033[H");
    if (chosen[0]) { say("DOOM plays "); say(chosen); sayln(".  Type DOOM."); }
}
