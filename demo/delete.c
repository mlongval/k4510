/* K4510: DELETE -- remove a file the way a machine with a disk should, by
 * putting it somewhere you can get it back from.
 *
 * The shell's RM calls the device's REMOVE and the file is gone.  That is the
 * right primitive and the wrong default: on a machine whose whole filesystem
 * is a directory on somebody's laptop, the cost of keeping a deleted file
 * around until you say otherwise is nothing, and the cost of not keeping it is
 * the afternoon you spent on it.
 *
 * So DELETE does not delete.  It RENAMEs into /.TRASH -- one directory move on
 * the device, no copying however large the file is -- and DELETE -r moves it
 * back.  The same /.TRASH that RANGER's DD uses, with the same rules, because
 * two trashes would be worse than none.
 *
 *   DELETE name [name...]   move them to /.TRASH
 *   DELETE -l               list what is in the trash
 *   DELETE -r name          restore one, back to the directory you are in
 *   DELETE -e               empty it -- this one really does delete
 *
 * A name already taken in the trash gets ~1, ~2 appended rather than
 * overwriting: a trash that eats the file you deleted yesterday is not a
 * trash.  That has to be tested with STAT before the move, not inferred from
 * a failed RENAME -- the device's RENAME takes the host's semantics and
 * overwrites in silence, which is exactly how RANGER's first trash lost a
 * file (docs/BUILD-LOG.md, 2026-08-29).
 *
 * Nothing here empties the trash on its own.  A trash that quietly disposes of
 * things after a while is a trash you cannot trust either; when it is big
 * enough to bother you, DELETE -e is the sentence you have to type.
 */
#include "k4510.h"

#define FS       0xD300u
#define FS_CMD   (FS + 0x00)
#define FS_ST    (FS + 0x01)
#define FS_NAME  (FS + 0x04)
#define FS_ADDR  (FS + 0x08)
#define FS_SIZE  (FS + 0x10)
#define C_DIR1    6
#define C_DIRN    7
#define C_STAT    8
#define C_MKDIR  12
#define C_RM     13
#define C_CHDIR  11
#define C_GETCWD 15
#define C_RENAME 16
#define TRASH    "/.TRASH"

void __fastcall__ rom_chrout(unsigned char c);
static unsigned char rom_args(void) { return ((unsigned char (*)(void))0xFF95)(); }

static char cwd[160], src[192], dst[192], nm[160], ent[64];

static uint8_t fs_do(uint8_t c)      { REG(FS_CMD) = c; return REG(FS_ST); }
static void    fs_name(const char *s){ w32(FS_NAME, (uint32_t)(uint16_t)s); }
static void    fs_addr(uint32_t a)   { w32(FS_ADDR, a); }
static uint8_t exists(const char *p) { fs_name(p); return fs_do(C_STAT) == 0; }

static void say(const char *s) { while (*s) rom_chrout(*s++); }
static void sayln(const char *s) { say(s); rom_chrout('\n'); }
static void dec(uint16_t v) { char t[6]; uint8_t n = 0; if (!v) { rom_chrout('0'); return; } while (v) { t[n++] = (char)('0' + v % 10); v /= 10; } while (n) rom_chrout(t[--n]); }

static void join(char *out, const char *dir, const char *leaf)
{
    uint8_t l;
    strcpy(out, dir); l = (uint8_t)strlen(out);
    if (l && out[l - 1] != '/') { out[l] = '/'; out[l + 1] = 0; }
    strcat(out, leaf);
}

/* one whitespace-delimited word out of p; returns 0 at the end */
static const char *word(const char *p, char *out)
{
    uint8_t i = 0;
    while (*p == ' ') p++;
    if (!*p) return 0;
    while (*p && *p != ' ' && i < 63) out[i++] = *p++;
    out[i] = 0;
    return p;
}

/* Move one name into the trash.  0 on success. */
static uint8_t trash_one(const char *name)
{
    uint16_t n;
    fs_name(TRASH); fs_do(C_MKDIR);              /* already there is fine */
    join(src, cwd, name);
    if (!exists(src)) { say("delete: "); say(name); sayln(": not found"); return 1; }
    join(dst, TRASH, name);
    for (n = 1; exists(dst) && n < 1000; n++) {  /* taken: name~1, name~2, ... */
        join(dst, TRASH, name);
        strcat(dst, "~");
        { char d[6]; uint8_t i = 0, j; uint16_t v = n;
          while (v) { d[i++] = (char)('0' + v % 10); v /= 10; }
          j = (uint8_t)strlen(dst);
          while (i) dst[j++] = d[--i];
          dst[j] = 0; }
    }
    if (exists(dst)) { say("delete: "); say(name); sayln(": the trash is full of that name"); return 1; }
    fs_name(src); fs_addr((uint32_t)(uint16_t)dst);
    if (fs_do(C_RENAME)) { say("delete: "); say(name); sayln(": could not move it"); return 1; }
    return 0;
}

/* -r: bring one back to the directory we are standing in */
static uint8_t restore_one(const char *name)
{
    join(src, TRASH, name);
    if (!exists(src)) { say("delete: "); say(name); sayln(": not in the trash"); return 1; }
    join(dst, cwd, name);
    if (exists(dst)) { say("delete: "); say(name); sayln(": that name is taken here"); return 1; }
    fs_name(src); fs_addr((uint32_t)(uint16_t)dst);
    if (fs_do(C_RENAME)) { sayln("delete: could not restore it"); return 1; }
    say("restored "); sayln(name);
    return 0;
}

/* walk the trash; act = 0 list, 1 empty. Returns how many it saw. */
static uint16_t walk_trash(uint8_t act)
{
    uint16_t n = 0;
    fs_name(TRASH);
    if (fs_do(C_CHDIR)) { sayln("the trash is empty"); return 0; }
    /* DIR1 OPENS the directory -- it does not hand back the first entry.
     * Treating it as an entry counts one that is not there, which is what the
     * first version of this did.  Every entry comes from DIRNEXT. */
    fs_addr((uint32_t)(uint16_t)ent);
    if (fs_do(C_DIR1)) { fs_name(cwd); fs_do(C_CHDIR); sayln("the trash is empty"); return 0; }
    for (;;) {
        fs_addr((uint32_t)(uint16_t)ent);
        if (fs_do(C_DIRN)) break;
        n++;
        if (act) { fs_name(ent); fs_do(C_RM); }
        else { say("  "); sayln(ent); }
    }
    if (!n) sayln("the trash is empty");
    fs_name(cwd); fs_do(C_CHDIR);                /* always put the shell's cwd back */
    return n;
}

void main(void)
{
    uint8_t na = rom_args();
    const char *p = *(const char **)0xF0;
    uint16_t done = 0, bad = 0;

    REG(FS + 0x18) = sizeof cwd;                               /* GETCWD's CAP: our buffer's size */
    fs_addr((uint32_t)(uint16_t)cwd); fs_do(C_GETCWD);
    if (!cwd[0]) strcpy(cwd, "/");

    if (!na) {
        sayln("delete: move files to " TRASH " (they are not destroyed)");
        sayln("  DELETE name [name...]   DELETE -l   DELETE -r name   DELETE -e");
        return;
    }

    p = word(p, nm);
    if (nm[0] == '-' && nm[1] == 'l' && !nm[2]) {
        uint16_t n = walk_trash(0);
        if (n) { say("  "); dec(n); sayln(n == 1 ? " item in " TRASH : " items in " TRASH); }
        return;
    }
    if (nm[0] == '-' && nm[1] == 'e' && !nm[2]) {
        uint16_t n = walk_trash(1);
        if (n) { say("emptied: "); dec(n); sayln(n == 1 ? " item destroyed" : " items destroyed"); }
        return;
    }
    if (nm[0] == '-' && nm[1] == 'r' && !nm[2]) {
        if (!p || !(p = word(p, nm))) { sayln("delete: -r needs a name (DELETE -l lists them)"); return; }
        do { if (restore_one(nm)) bad++; } while (p && (p = word(p, nm)));
        return;
    }

    do {
        if (trash_one(nm)) bad++; else done++;
    } while (p && (p = word(p, nm)));

    if (done) { say("moved "); dec(done); say(done == 1 ? " item to " : " items to "); sayln(TRASH); }
    if (bad) { say("("); dec(bad); sayln(bad == 1 ? " could not be moved)" : " could not be moved)"); }
}
