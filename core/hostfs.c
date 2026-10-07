/* ---- the file device ($D300): the host filesystem ---------------------
 * The machine's disk is a directory on the host (fs/, or wherever fs_root
 * points), sandboxed: no guest name climbs out of it.  Mounts graft a server
 * or a zip onto a local-looking path, and a URL is a file name. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>     /* toupper: the search path uppercases a name's stem (fs_path) */
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>
#include "io.h"
#include "io_int.h"
#include "mem.h"
#include "net.h"
#include "zip.h"       /* MOUNT GAMES.ZIP /MNT/GAMES */
#include "state.h"
char fs_root[512] = "fs";
char fs_cwd[256] = "";            /* relative to fs_root, no leading/trailing slash; "" = root */
static uint8_t fs_reg[0x18];
static uint8_t fs_cap;                    /* $D318: GETCWD's buffer size, one shot; 0 = 64 (the ROM's) */
static char fs_back_cwd[256], fs_back_remote[512];   /* where the last CHDIR came from: FS_CHDIR_BACK */
static char fs_was_cwd[256], fs_was_remote[512];     /* taken as a CHDIR starts, kept only if it lands */
static int  fs_chdir_ran;
static FILE *fs_file;
static int fs_file_w;                     /* fs_file was opened to write: its close goes to the disk at once */
/* A file the machine wrote is on the disk when the call returns (Doc,
 * 2026-10-07: "force the write on SAVE").  Without it Linux held a SAVE in
 * memory for up to half a minute, and a pulled plug lost it.  A few
 * milliseconds a file on an SSD. */
static int fs_close_durable(FILE *f)
{
    int bad = fflush(f) != 0;                 /* a full disk says so here */
    fsync(fileno(f));                         /* a filesystem that cannot (a FUSE mount) is no failure */
    return fclose(f) || bad;
}
static void fs_close_cur(void)
{
    if (!fs_file) return;
    if (fs_file_w) fs_close_durable(fs_file); else fclose(fs_file);
    fs_file = NULL; fs_file_w = 0;
}
static void fs_sync_dir(const char *path)   /* a rename's new name, on the disk too */
{
    char d[1024]; snprintf(d, sizeof d, "%s", path);
    char *sl = strrchr(d, '/'); if (sl) *sl = 0; else snprintf(d, sizeof d, ".");
    FILE *f = fopen(d[0] ? d : "/", "r");
    if (f) { fsync(fileno(f)); fclose(f); }
}
static uint8_t *fs_netbuf; static uint32_t fs_netlen, fs_netpos;   /* a fetched URL, served as the open file */
static char fs_remote[512];             /* the current directory when it is on a server: a tnfs:// URL; "" = local */
static void fs_net_drop(void) { free(fs_netbuf); fs_netbuf = NULL; fs_netlen = fs_netpos = 0; }
/* Mount points: a local-looking path (MNT/ATARI) mapped onto a server, so a
 * server can be navigated like a local subtree -- the cwd never becomes a URL,
 * so local programs (RANGER) launch through the usual search path and only the
 * filesystem ops route to the net (Doc, 2026-09-10: the atari8.us case). */
/* A mount can also be a zip file (MOUNT GAMES.ZIP /MNT/GAMES, Doc 2026-09-15:
 * the sidebars' packages first, and anything shipped as one file after): ZIP
 * is then the whole file, read into memory at MOUNT, and URL only what MOUNT
 * lists.  Its paths are "zip:N:TAIL" -- N the mount's slot, made here and
 * never typed, so no guest name can reach a zip that is not mounted. */
static struct { char at[80]; char url[240]; zip_t *zip; } fs_mnt[8]; static int fs_mnt_n;
/* REL is a root-relative path; if it is under a mount, fill URL (base + tail)
 * and return 1.  A trailing part is appended with net_url_join. */
static int fs_mount_url(const char *rel, char *url, size_t max)
{
    for (int i = 0; i < fs_mnt_n; i++) {
        size_t al = strlen(fs_mnt[i].at);
        if (strncasecmp(rel, fs_mnt[i].at, al) || (rel[al] && rel[al] != '/')) continue;
        { const char *tail = rel + al; while (*tail == '/') tail++;
          if (fs_mnt[i].zip) snprintf(url, max, "zip:%d:%s", i, tail);
          else if (*tail) net_url_join(url, max, fs_mnt[i].url, tail); else snprintf(url, max, "%s", fs_mnt[i].url); }
        return 1;
    }
    return 0;
}
static void fs_mnt_clear(void) { for (int i = 0; i < fs_mnt_n; i++) zip_close(fs_mnt[i].zip); fs_mnt_n = 0; }
/* The zip behind a "zip:N:" path, and the path inside it; NULL for anything else. */
static zip_t *fs_mnt_zip(const char *url, const char **inner)
{
    const char *c; int i;
    if (strncmp(url, "zip:", 4)) return NULL;
    i = atoi(url + 4); c = strchr(url + 4, ':');
    if (!c || i < 0 || i >= fs_mnt_n) return NULL;
    *inner = c + 1;
    return fs_mnt[i].zip;
}
/* What the file system asks of a mount, to the zip or to the network. */
static int mnt_fetch(const char *url, uint8_t **b, uint32_t *n)
{
    const char *in; zip_t *z = fs_mnt_zip(url, &in);
    if (z) { int st = zip_fetch(z, in, b, n); return st == 3 ? 2 : st; }
    return strncmp(url, "zip:", 4) ? net_fetch(url, b, n) : 1;
}
static int mnt_listdir(const char *url, net_dirent **e, int *n)
{
    const char *in; zip_t *z = fs_mnt_zip(url, &in);
    if (z) return zip_listdir(z, in, e, n) ? 2 : 0;
    return strncmp(url, "zip:", 4) ? net_listdir(url, e, n) : 2;
}
static int mnt_isdir(const char *url)
{
    const char *in; zip_t *z = fs_mnt_zip(url, &in);
    if (z) return zip_isdir(z, in);
    return strncmp(url, "zip:", 4) ? net_isdir(url) : -1;
}
void fs_set_root(const char *d) { snprintf(fs_root, sizeof fs_root, "%s", d); fs_cwd[0] = 0; }
const char *fs_get_root(void) { return fs_root; }
const char *fs_get_cwd(void) { return fs_cwd; }   /* the shell's current dir, relative to the root */
static uint32_t fs_rd32(int off) { return rd32(&fs_reg[off]); }
static void fs_wr32(int off, uint32_t v) { for (int i = 0; i < 4; i++) fs_reg[off + i] = (v >> (8 * i)) & 0xFF; }
/* The date and time of a host file at $D314/$D316, packed for the ROM to
 * print without dividing: (year-1980)<<9 | month<<5 | day, and hour<<8 |
 * minute.  Zero when there is none to give (a network entry).  DIR -l shows
 * them (Doc, 2026-09-11: "like in linux"). */
static void fs_when(const char *path)
{
    struct stat sb; uint16_t d = 0, t = 0;
    if (path && stat(path, &sb) == 0) {
        struct tm *m = localtime(&sb.st_mtime);
        if (m) { int y = m->tm_year + 1900 - 1980; if (y < 0) y = 0; if (y > 127) y = 127;
                 d = (uint16_t)((y << 9) | ((m->tm_mon + 1) << 5) | m->tm_mday); t = (uint16_t)((m->tm_hour << 8) | m->tm_min); }
    }
    fs_reg[0x14] = (uint8_t) d; fs_reg[0x15] = (uint8_t)(d >> 8); fs_reg[0x16] = (uint8_t) t; fs_reg[0x17] = (uint8_t)(t >> 8);
}
/* Resolve a guest name against the cwd inside the sandbox: "/" is the root,
 * "." and ".." work, ".." never climbs above the root. rel gets the
 * root-relative path ("" for the root), out the host path. */
int fs_resolve(const char *name, char *rel, size_t relmax, char *out, size_t outmax)
{
    char buf[512]; size_t n = 0;
    if (name[0] == '/' || name[0] == '\\') { buf[0] = 0; name++; } else snprintf(buf, sizeof buf, "%s", fs_cwd);
    n = strlen(buf);
    while (*name) {
        const char *e = name; size_t l;
        while (*e && *e != '/' && *e != '\\') e++;
        l = (size_t)(e - name);
        if (l == 0 || (l == 1 && name[0] == '.')) { /* skip */ }
        else if (l == 2 && name[0] == '.' && name[1] == '.') { while (n && buf[n - 1] != '/') n--; if (n) n--; buf[n] = 0; }
        else { if (n + l + 2 >= sizeof buf) return 5; if (n) buf[n++] = '/'; memcpy(buf + n, name, l); n += l; buf[n] = 0; }
        name = *e ? e + 1 : e;
    }
    snprintf(rel, relmax, "%s", buf);
    if (n) snprintf(out, outmax, "%s/%s", fs_root, buf); else snprintf(out, outmax, "%s", fs_root);
    return 0;
}
/* If the host path does not exist, look for a case-insensitive match of its
 * last component in its directory (the guest upper-cases names). */
void fs_casefix(char *path, size_t max)
{
    struct stat sb; char dir[768], *base; DIR *d; struct dirent *e;
    if (!stat(path, &sb)) return;
    snprintf(dir, sizeof dir, "%s", path); base = strrchr(dir, '/'); if (!base) return;
    *base++ = 0;
    if (!(d = opendir(dir))) return;
    while ((e = readdir(d))) if (!strcasecmp(e->d_name, base)) { snprintf(path, max, "%.500s/%.255s", dir, e->d_name); break; }
    closedir(d);
}
int fs_guest_str(uint32_t p, char *name, size_t max)
{
    size_t n = 0;
    p &= K4510_PHYS_MASK;
    for (; n < max - 1; n++) { name[n] = k4510_ram[(p + n) & K4510_PHYS_MASK]; if (!name[n]) break; }
    if (n >= max - 1) return 5;
    name[n] = 0;
    return 0;
}
static int fs_guest_name(char *name, size_t max)
{
    uint32_t p = fs_rd32(4) & K4510_PHYS_MASK; size_t n = 0;
    for (; n < max - 1; n++) { name[n] = k4510_ram[(p + n) & K4510_PHYS_MASK]; if (!name[n]) break; }
    if (n >= max - 1) return 5;
    name[n] = 0;
    return 0;
}
/* The Meatloaf rule: a URL is a file name -- and while the current directory
 * is on a server, so is a bare name. Returns 1 and the URL in out, else 0. */
static int fs_url_for(const char *name, char *out, size_t max)
{
    char rel[512], loc[768];
    if (net_is_url(name)) { snprintf(out, max, "%s", name); return 1; }
    if (fs_remote[0] && strcmp(name, "-") != 0) { net_url_join(out, max, fs_remote, name); return 1; }
    if (fs_mnt_n && strcmp(name, "-") != 0 && !fs_resolve(name, rel, sizeof rel, loc, sizeof loc) && fs_mount_url(rel, out, max)) return 1;
    return 0;
}
void (*io_radio_hook)(const char *cmd, char *reply, size_t max);
static char radio_reply[1024]; static int radio_lines;
/* A machine path ("/SYSTEM/DOC/IMG/BOOT.PNG", or a name beside the current
 * directory) as the host's, for JIM's pictures: Kitty's t=f names a FILE, and
 * on this machine a program's files are the machine's.  The same sandbox as
 * every other name: it cannot climb out of the root.  1 when it resolves. */
int io_fs_hostpath(const char *name, char *out, size_t max)
{
    char rel[256], part[256]; const char *p; struct stat sb;
    if (fs_resolve(name, rel, sizeof rel, out, max)) return 0;
    if (!stat(out, &sb)) return 1;
    /* Not there as spelt: every part of it in whatever case the disk has it
     * (fs_casefix mends only the last one, which is all a guest's upper-cased
     * name ever needs; a path typed by a program may be lower case throughout). */
    snprintf(out, max, "%s", fs_root);
    for (p = rel; *p; ) {
        size_t l = strcspn(p, "/"), n = strlen(out);
        if (l >= sizeof part || n + l + 2 >= max) return 0;
        memcpy(part, p, l); part[l] = 0;
        snprintf(out + n, max - n, "/%s", part);
        fs_casefix(out, max);
        p += l; while (*p == '/') p++;
    }
    return !stat(out, &sb);
}
/* host path for NAMEPTR; for reads, a bare name (no directory part) that is
 * not where we are is looked for along the disk's shape (fs/HOME/README.TXT):
 *   /SYSTEM/BIN/name              the tools
 *   /APPS/STEM/name               a program's own folder  (SKYFIRE -> /APPS/SKYFIRE/skyfire.prg)
 *   /LANG/STEM/name               a language's            (EHBASIC -> /LANG/EHBASIC/ehbasic.prg)
 *   /HOME/PROJECTS/STEM/name      yours
 *   and by extension: .BAS -> /LANG/EHBASIC/EX, .BBC -> /LANG/BBCBASIC/EX, .RX -> /LANG/RX, .PAS -> /LANG/PASCAL
 * where STEM is the name without its extension, uppercased -- the name IS the
 * folder, so this is one stat per step and never a walk. */
static int fs_path(char *out, size_t max, int search)
{
    char name[128], rel[256]; struct stat sb; int st;
    if ((st = fs_guest_name(name, sizeof name))) return st;
    if ((st = fs_resolve(name, rel, sizeof rel, out, max))) return st;
    fs_casefix(out, max);
    if (search && stat(out, &sb) && !strchr(name, '/') && !strchr(name, '\\')) {
        char stem[64]; const char *dot = strrchr(name, '.'); size_t sl = dot ? (size_t)(dot - name) : strlen(name);
        char dirs[8][40]; int nd = 0;
        if (sl >= sizeof stem) sl = sizeof stem - 1;
        for (size_t i = 0; i < sl; i++) stem[i] = (char)toupper((unsigned char)name[i]);
        stem[sl] = 0;
        snprintf(dirs[nd++], 40, "/SYSTEM/BIN");
        if (sl) { snprintf(dirs[nd++], 40, "/APPS/%.30s", stem); snprintf(dirs[nd++], 40, "/LANG/%.30s", stem); snprintf(dirs[nd++], 40, "/HOME/PROJECTS/%.24s", stem); }
        if (dot) {
            static const struct { const char *ext, *dir; } by_ext[] = {
                { ".BAS", "/LANG/EHBASIC/EX" }, { ".BBC", "/LANG/BBCBASIC/EX" }, { ".RX", "/LANG/RX" }, { ".PAS", "/LANG/PASCAL" }, { ".C", "/LANG/C" },
                { ".PRG", "/LANG/PASCAL" }, { ".PRG", "/LANG/C" } };   /* the compiled examples, by their bare names */
            for (size_t i = 0; i < sizeof by_ext / sizeof *by_ext; i++)
                if (!strcasecmp(dot, by_ext[i].ext)) snprintf(dirs[nd++], 40, "%s", by_ext[i].dir);
        }
        for (int i = 0; i < nd; i++) {
            char alt[512]; snprintf(alt, sizeof alt, "%s/%.127s", dirs[i], name);
            if (fs_resolve(alt, rel, sizeof rel, out, max)) continue;
            fs_casefix(out, max);
            if (!stat(out, &sb)) return 0;
        }
        fs_resolve(name, rel, sizeof rel, out, max); fs_casefix(out, max);   /* not found: the plain path, for the error */
    }
    return 0;
}
/* directory listing: read, sort, serve.
 *
 * TWO THINGS HERE ARE ABOUT SPEED, and both were measured on a Pi 3B+ with
 * an 826-file directory (Doc, 2026-09-01: "the 800+ entries in the OPL
 * directory kinda kill the DIR command... takes about 10 seconds before it
 * starts").  Ten seconds before the FIRST name, which is the part that
 * matters: a listing that streams is usable at any length.
 *
 *   1. The sort was an insertion sort, which is n-squared -- 340,000
 *      strcasecmp calls for 826 names, against about 8,000 for qsort.
 *   2. Every entry was stat()ed here, before the first name was served, to
 *      learn its size.  On a card that is 826 round trips through FAT for a
 *      number the guest has not asked for yet.  The stat now happens in
 *      FS_DIR_NEXT, on the one entry being served: the same work in total,
 *      but paid a name at a time and only for as far as the listing is read
 *      (Esc out of a long DIR and the rest is never paid at all).
 *
 * That is why the directory's own path is kept here: by the time an entry is
 * served, the guest may have been told anything about where it is. */
static char (*fs_list)[64]; static int fs_list_n, fs_list_i;
static char fs_list_dir[768];
/* Sizes are normally NOT held: a local listing stats an entry as it serves it
 * (see above).  A listing from a TNFS server is the exception -- the sizes
 * arrive with the names and there is nothing local to stat -- so it fills
 * this, and a non-NULL fs_list_size means "the sizes are already known". */
static uint32_t *fs_list_size;
static int fs_cmp(const void *a, const void *b) { return strcasecmp((const char *)a, (const char *)b); }
static int fs_dir_first(int all)
{
    char rel[256]; DIR *d; struct dirent *e; int n = 0, cap = 64;
    fs_resolve("", rel, sizeof rel, fs_list_dir, sizeof fs_list_dir);
    if (!(d = opendir(fs_list_dir))) return 2;
    free(fs_list); free(fs_list_size); fs_list_size = NULL;
    fs_list = malloc(cap * sizeof *fs_list);
    if (!fs_list) { closedir(d); return 2; }
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.' && (!all || !e->d_name[1] || (e->d_name[1] == '.' && !e->d_name[2]))) continue;
        if (n == cap) {
            char (*nl_)[64] = realloc(fs_list, 2 * cap * sizeof *fs_list);
            if (!nl_) break;                         /* out of memory: serve what we have */
            fs_list = nl_; cap *= 2;
        }
        snprintf(fs_list[n], 64, "%.63s", e->d_name);
        n++;
    }
    closedir(d);
    if (n > 1) qsort(fs_list, n, sizeof *fs_list, fs_cmp);
    fs_list_n = n; fs_list_i = 0;
    return 0;
}
/* the size of one listed entry, asked for only when it is served.
 * 0xFFFFFFFF means a directory, which is what the ROM prints as <DIR>. */
static uint32_t fs_list_size_of(const char *name)
{
    char full[1024]; struct stat sb;
    snprintf(full, sizeof full, "%s/%s", fs_list_dir, name);
    return stat(full, &sb) ? 0 : S_ISDIR(sb.st_mode) ? 0xFFFFFFFFu : (uint32_t)sb.st_size;
}
/* Does the pending guest NAME resolve under a mount?  Used to refuse writes
 * (a mount is read-only) without a network round trip. */
static int fs_name_mounted(void)
{
    char name[128], rel[256], loc[768], url[256];
    if (!fs_mnt_n || fs_guest_name(name, sizeof name)) return 0;
    if (net_is_url(name)) return 0;
    if (fs_resolve(name, rel, sizeof rel, loc, sizeof loc)) return 0;
    return fs_mount_url(rel, url, sizeof url);
}
/* Bulk copies between a FILE and guest RAM, in the largest contiguous spans
 * (the address wraps at the top of RAM): fread/fwrite, not a call per byte. */
static uint32_t fs_read_span(FILE *f, uint32_t addr, uint32_t len)
{
    uint32_t done = 0;
    while (done < len) {
        uint32_t a = (addr + done) & K4510_PHYS_MASK, n = len - done; size_t r;
        if (n > K4510_PHYS_SIZE - a) n = K4510_PHYS_SIZE - a;
        r = fread(k4510_ram + a, 1, n, f); done += (uint32_t) r;
        if (r < n) break;
    }
    return done;
}
static void fs_write_span(FILE *f, uint32_t addr, uint32_t len)
{
    uint32_t done = 0;
    while (done < len) {
        uint32_t a = (addr + done) & K4510_PHYS_MASK, n = len - done;
        if (n > K4510_PHYS_SIZE - a) n = K4510_PHYS_SIZE - a;
        if (fwrite(k4510_ram + a, 1, n, f) < n) break;
        done += n;
    }
}
static void fs_run(uint8_t cmd)
{
    char path[768]; int st = 0;
    uint32_t addr = fs_rd32(8) & K4510_PHYS_MASK, len = fs_rd32(12);
    if (len > K4510_PHYS_SIZE) len = K4510_PHYS_SIZE;   /* a 32-bit LEN wrote 4 GB to the host (review 2026-09-12, 7) */
    switch (cmd) {
    case FS_OPEN_READ: case FS_OPEN_WRITE: case FS_STAT: case FS_LOAD: case FS_SAVE: {
        int rd = (cmd == FS_OPEN_READ || cmd == FS_STAT || cmd == FS_LOAD);
        { char name[128], url[512];           /* the Meatloaf rule: a URL is a file (for reading) */
          int bare = 0;
          if (!fs_guest_name(name, sizeof name) && fs_url_for(name, url, sizeof url)) {
              uint8_t *b; uint32_t n;
              bare = rd && !strchr(name, '/') && !strchr(name, '\\') && !net_is_url(name);
              if (!rd) { st = 2; break; }
              if (cmd == FS_STAT && mnt_isdir(url) == 1) { fs_wr32(0x10, 0xFFFFFFFFu); break; }
              if ((st = mnt_fetch(url, &b, &n))) { st = st == 6 ? 1 : st;
                  if (st == 1 && bare) goto local_fs;     /* not on the server: a local program by this name */
                  break; }
              if (cmd == FS_STAT) { fs_wr32(0x10, n); free(b); break; }
              fs_close_cur();
              fs_net_drop(); fs_netbuf = b; fs_netlen = n; fs_netpos = 0;
              fs_wr32(0x10, n);
              if (cmd == FS_LOAD) { uint32_t done = 0, lim = len ? len : K4510_PHYS_SIZE; while (done < n && done < lim) { k4510_ram[(addr + done) & K4510_PHYS_MASK] = b[done]; done++; } fs_wr32(12, done); if (len && n > len) st = 6; fs_net_drop(); }
              break;
          } }
        local_fs:
        if ((st = fs_path(path, sizeof path, rd))) break;
        if (cmd == FS_STAT) { struct stat sb; if (stat(path, &sb)) st = 1; else { fs_wr32(0x10, S_ISDIR(sb.st_mode) ? 0xFFFFFFFFu : (uint32_t)sb.st_size); fs_when(path); } break; }
        { struct stat sb;                     /* a directory is not a file: opening "FORTH" must fail as
                                                 not-found so the shell falls through to FORTH.prg */
          if (rd && !stat(path, &sb) && S_ISDIR(sb.st_mode)) { st = 1; break; } }
        fs_close_cur();
        fs_net_drop();
        fs_file = fopen(path, (cmd == FS_OPEN_WRITE || cmd == FS_SAVE) ? "wb" : "rb");
        if (!fs_file) { st = 1; break; }
        fs_file_w = cmd == FS_OPEN_WRITE || cmd == FS_SAVE;
        title_file(path);                             /* a .prg names the next program; a .BAS, .LGO ... the running one's file */
        if (cmd == FS_OPEN_READ || cmd == FS_LOAD) { fseek(fs_file, 0, SEEK_END); long sz = ftell(fs_file); fseek(fs_file, 0, SEEK_SET); fs_wr32(0x10, (uint32_t)sz); }
        if (cmd == FS_LOAD)  {                /* LEN, when the caller set one, is the buffer: LOAD used to run to EOF over it (review 2026-09-12); 0 = whole file */
            uint32_t got = fs_read_span(fs_file, addr, len ? len : K4510_PHYS_SIZE); fs_wr32(12, got);
            if (len && got == len && fgetc(fs_file) != EOF) st = 6;
            fclose(fs_file); fs_file = NULL; }
        if (cmd == FS_SAVE)  { fs_write_span(fs_file, addr, len); fs_file_w = 0; if (fs_close_durable(fs_file)) st = 2; fs_file = NULL; }
        break; }
    case FS_READ: {
        uint32_t done = 0;
        if (fs_netbuf) { while (done < len && fs_netpos < fs_netlen) k4510_ram[(addr + done++) & K4510_PHYS_MASK] = fs_netbuf[fs_netpos++]; fs_wr32(12, done); break; }
        if (!fs_file) { st = 2; break; }
        fs_wr32(12, fs_read_span(fs_file, addr, len)); break; }
    case FS_WRITE: { if (!fs_file) { st = 2; break; } fs_write_span(fs_file, addr, len); break; }
    case FS_CLOSE: fs_close_cur(); fs_net_drop(); break;
    case FS_DIR_FIRST: case FS_DIR_ALL: {
        char durl[512]; const char *lurl = fs_remote[0] ? fs_remote : (fs_mount_url(fs_cwd, durl, sizeof durl) ? durl : NULL);
        if (lurl) {                           /* a listing from the server (CD tnfs:// or a mount) */
            net_dirent *e; int n;
            if ((st = mnt_listdir(lurl, &e, &n))) { st = st == 6 ? 2 : st; break; }
            free(fs_list); free(fs_list_size);
            fs_list = calloc((size_t)(n ? n : 1), 64); fs_list_size = calloc((size_t)(n ? n : 1), sizeof *fs_list_size);
            if (!fs_list || !fs_list_size) { free(fs_list); free(fs_list_size); fs_list = NULL; fs_list_size = NULL; free(e); st = 2; break; }
            for (int i = 0; i < n; i++) { snprintf(fs_list[i], 64, "%s", e[i].name); fs_list_size[i] = e[i].isdir ? 0xFFFFFFFFu : e[i].size; }
            fs_list_n = n; fs_list_i = 0; free(e);
            break;
        }
        st = fs_dir_first(cmd == FS_DIR_ALL); break; }
    case FS_RENAME: case FS_COPYFILE: {
        char n2[128], rel[256], dst[768], url[512]; uint8_t *nb = NULL; uint32_t nn = 0;
        { char name[128];                     /* CP http://... local: the Meatloaf rule again */
          if (cmd == FS_COPYFILE && !fs_guest_name(name, sizeof name) && fs_url_for(name, url, sizeof url)) {
              if ((st = mnt_fetch(url, &nb, &nn))) { st = st == 6 ? 1 : st; break; }
          } }
        if (!nb && (st = fs_path(path, sizeof path, 1))) break;                /* source, searched + case-fixed */
        if ((st = fs_guest_str(fs_rd32(8), n2, sizeof n2))) { free(nb); break; }   /* nb: a whole fetched file, leaked on these two ways out until 2026-09-17 */
        if ((st = fs_resolve(n2, rel, sizeof rel, dst, sizeof dst))) { free(nb); break; }    /* destination, as given */
        { char durl[256]; if (fs_mount_url(rel, durl, sizeof durl)) { if (nb) free(nb); st = 2; break; } }   /* a mount is read-only */
        if (cmd == FS_RENAME)
            { st = rename(path, dst) ? 2 : 0; if (!st) fs_sync_dir(dst); }
        else {
            FILE *a = nb ? NULL : fopen(path, "rb"), *b = NULL;
            if (nb) { if (!(b = fopen(dst, "wb"))) { free(nb); st = 2; break; } if (fwrite(nb, 1, nn, b) != nn) st = 2; if (fs_close_durable(b)) st = 2; free(nb); break; }
            if (!a) { st = 1; break; }
            /* CP X X: opening the destination "wb" truncates the source it is
             * about to read, and the copy then succeeds at copying nothing.
             * Same device and inode is the test, so ./X and a link are caught. */
            { struct stat sa, sb; if (fstat(fileno(a), &sa) == 0 && stat(dst, &sb) == 0 && sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino) { fclose(a); st = 2; break; } }
            if (!(b = fopen(dst, "wb"))) { fclose(a); st = 2; break; }
            { char buf[4096]; size_t k; while ((k = fread(buf, 1, sizeof buf, a)) > 0) if (fwrite(buf, 1, k, b) != k) { st = 2; break; } }
            fclose(a); if (fs_close_durable(b)) st = 2;
        }
        break; }
    case FS_DIR_NEXT: {
        if (!fs_list) { st = 2; break; }
        if (fs_list_i >= fs_list_n) { st = 4; break; }
        size_t i = 0; const char *nm = fs_list[fs_list_i];
        for (; nm[i] && i < 63; i++) k4510_ram[(addr + i) & K4510_PHYS_MASK] = (uint8_t)nm[i];
        k4510_ram[(addr + i) & K4510_PHYS_MASK] = 0;
        fs_wr32(0x10, fs_list_size ? fs_list_size[fs_list_i] : fs_list_size_of(nm));
        if (fs_list_size) fs_when(NULL);                                      /* a network listing: no date to give */
        else { char p[1100]; snprintf(p, sizeof p, "%.800s/%.255s", fs_list_dir, nm); fs_when(p); }
        fs_list_i++;
        break; }
    case FS_CHDIR: {
        char name[128], rel[256], url[512]; struct stat sb;
        if ((st = fs_guest_name(name, sizeof name))) break;
        memcpy(fs_was_cwd, fs_cwd, sizeof fs_was_cwd); memcpy(fs_was_remote, fs_remote, sizeof fs_was_remote); fs_chdir_ran = 1;
        if (fs_remote[0] && !strcmp(name, "-")) { fs_remote[0] = 0; break; }          /* CD - : home from the server */
        if (net_is_url(name) || (fs_remote[0] && strcmp(name, "-"))) {                 /* the URL-cwd model: CD tnfs://host, or a name while already on a server */
            if (fs_url_for(name, url, sizeof url)) {
                if (net_isdir(url) == 1) { size_t n; snprintf(fs_remote, sizeof fs_remote, "%s", url); n = strlen(fs_remote); while (n > 8 && fs_remote[n - 1] == '/') fs_remote[--n] = 0; }
                else st = 1;
                break;
            }
        }
        if ((st = fs_resolve(name, rel, sizeof rel, path, sizeof path))) break;
        if (fs_mount_url(rel, url, sizeof url)) {                                      /* into (or within) a mount: the cwd stays local-looking */
            if (mnt_isdir(url) == 1) snprintf(fs_cwd, sizeof fs_cwd, "%s", rel); else st = 1;
            break;
        }
        fs_casefix(path, sizeof path);
        if (stat(path, &sb) || !S_ISDIR(sb.st_mode)) { st = 1; break; }
        /* keep the host's spelling of the directory in the cwd */
        snprintf(fs_cwd, sizeof fs_cwd, "%s", strlen(path) > strlen(fs_root) ? path + strlen(fs_root) + 1 : "");
        break; }
    case FS_MKDIR: if (fs_remote[0] || fs_name_mounted()) { st = 2; break; } if ((st = fs_path(path, sizeof path, 0))) break; if (mkdir(path, 0777)) st = 2; break;
    case FS_RM:    { struct stat sb; if (fs_remote[0] || fs_name_mounted()) { st = 2; break; } if ((st = fs_path(path, sizeof path, 0))) break; if (stat(path, &sb)) { st = 1; break; } if (S_ISDIR(sb.st_mode) || unlink(path)) st = 2; break; }
    case FS_RMDIR: { struct stat sb; if (fs_remote[0] || fs_name_mounted()) { st = 2; break; } if ((st = fs_path(path, sizeof path, 0))) break; if (stat(path, &sb)) { st = 1; break; }
        if (!S_ISDIR(sb.st_mode) || rmdir(path)) st = 2;
        break; }
    case FS_MOUNT: {                       /* NAMEPTR = URL or zip file, reg 8 -> PATH */
        char url[256], pathn[256], rel[256], loc[768], shown[240]; zip_t *zip = NULL;
        size_t ul;
        if ((st = fs_guest_name(url, sizeof url))) break;
        if ((st = fs_guest_str(fs_rd32(8), pathn, sizeof pathn))) break;
        if (!pathn[0]) { st = 3; break; }
        ul = strlen(url);
        /* a zip: a file on the disk (or in another mount), or a URL ending in .zip */
        if (!net_is_url(url) || (ul > 4 && !strcasecmp(url + ul - 4, ".zip"))) {
            uint8_t *b = NULL; uint32_t n = 0; char zu[512], zrel[256], zloc[768];
            if (fs_url_for(url, zu, sizeof zu)) { if ((st = mnt_fetch(zu, &b, &n))) { st = st == 6 ? 1 : st; break; } snprintf(shown, sizeof shown, "%s", url); }
            else {
                FILE *f; long sz; struct stat sb;
                if ((st = fs_resolve(url, zrel, sizeof zrel, zloc, sizeof zloc))) break;
                fs_casefix(zloc, sizeof zloc);
                if (stat(zloc, &sb) || S_ISDIR(sb.st_mode) || !(f = fopen(zloc, "rb"))) { st = 1; break; }
                fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
                if (sz < 0 || sz > (long)(512u << 20) || !(b = malloc(sz ? (size_t)sz : 1)) || fread(b, 1, (size_t)sz, f) != (size_t)sz) { free(b); fclose(f); st = 2; break; }
                fclose(f); n = (uint32_t)sz;
                snprintf(shown, sizeof shown, "/%.238s", strlen(zloc) > strlen(fs_root) ? zloc + strlen(fs_root) + 1 : zrel);
            }
            if ((st = zip_open_mem(b, n, &zip))) break;                    /* b is the zip's now, or freed */
        } else snprintf(shown, sizeof shown, "%s", url);
        if ((st = fs_resolve(pathn, rel, sizeof rel, loc, sizeof loc)) || !rel[0]) { zip_close(zip); if (!st) st = 2; break; }   /* the root cannot be a mount */
        { int i; for (i = 0; i < fs_mnt_n; i++) if (!strcasecmp(fs_mnt[i].at, rel)) break;
          if (i == fs_mnt_n) { if (fs_mnt_n >= 8) { zip_close(zip); st = 2; break; } fs_mnt_n++; fs_mnt[i].zip = NULL; }
          zip_close(fs_mnt[i].zip); fs_mnt[i].zip = zip;                   /* a mount again at the same place replaces it */
          snprintf(fs_mnt[i].at, sizeof fs_mnt[i].at, "%s", rel);
          { size_t n; snprintf(fs_mnt[i].url, sizeof fs_mnt[i].url, "%s", shown); n = strlen(fs_mnt[i].url); while (!zip && n > 8 && fs_mnt[i].url[n - 1] == '/') fs_mnt[i].url[--n] = 0; } }
        mkdir(loc, 0777);                     /* a real (empty) directory, so the mount shows in a DIR of its parent */
        break; }
    case FS_UMOUNT: {                      /* NAMEPTR = PATH */
        char name[256], rel[256], loc[768]; int i, found = 0;
        if ((st = fs_guest_name(name, sizeof name))) break;
        if ((st = fs_resolve(name, rel, sizeof rel, loc, sizeof loc))) break;
        for (i = 0; i < fs_mnt_n; i++) if (!strcasecmp(fs_mnt[i].at, rel)) { found = 1; zip_close(fs_mnt[i].zip); fs_mnt[i] = fs_mnt[--fs_mnt_n]; break; }
        if (!found) st = 1;
        else rmdir(loc);                      /* remove the placeholder directory MOUNT made (only if it is empty) */
        break; }
    case FS_RADIO: {                      /* RADIO: index 0 runs the command line at NAMEPTR and keeps its reply; then a line at a time */
        uint32_t idx = fs_rd32(12); int j, cap = fs_cap ? fs_cap : 256; const char *b = radio_reply, *e;
        fs_cap = 0;
        if (idx == 0) {
            char cmd[200];
            if (fs_guest_name(cmd, sizeof cmd)) { st = 5; break; }
            radio_reply[0] = 0; radio_lines = 0;
            if (io_radio_hook) io_radio_hook(cmd, radio_reply, sizeof radio_reply);
            else snprintf(radio_reply, sizeof radio_reply, "no radio on this host (the Navidrome sidebar is the frontend's)");
            for (const char *p = radio_reply; *p; p++) if (*p == '\n') radio_lines++;
            if (radio_reply[0] && radio_reply[strlen(radio_reply) - 1] != '\n') radio_lines++;
        }
        for (uint32_t k = 0; k < idx && *b; k++) { e = strchr(b, '\n'); b = e ? e + 1 : b + strlen(b); }
        if (!*b) { st = 4; break; }
        e = strchr(b, '\n'); if (!e) e = b + strlen(b);
        for (j = 0; b + j < e && j < cap - 1; j++) k4510_ram[(addr + j) & K4510_PHYS_MASK] = (uint8_t) b[j];
        k4510_ram[(addr + j) & K4510_PHYS_MASK] = 0; fs_wr32(0x10, (uint32_t) j);
        break; }
    case FS_SYSINFO: {                    /* STATUS: LEN = index -> a line at ADDR; index 0 takes the snapshot */
        uint32_t idx = fs_rd32(12); const char *b; int j, cap = fs_cap ? fs_cap : 256;
        fs_cap = 0;
        if (!(b = status_row((int) idx))) { st = 4; break; }
        for (j = 0; b[j] && j < cap - 1; j++) k4510_ram[(addr + j) & K4510_PHYS_MASK] = (uint8_t)b[j];
        k4510_ram[(addr + j) & K4510_PHYS_MASK] = 0; fs_wr32(0x10, (uint32_t)j);
        break; }
    case FS_SYSMOUNTS: {                  /* the machine's own storage: LEN = index -> a line at ADDR */
        uint32_t idx = fs_rd32(12); char b[160]; int j, cap = fs_cap ? fs_cap : 256;
        fs_cap = 0;
        if (!status_mount_row((int) idx, b, sizeof b)) { st = 4; break; }
        for (j = 0; b[j] && j < cap - 1; j++) k4510_ram[(addr + j) & K4510_PHYS_MASK] = (uint8_t)b[j];
        k4510_ram[(addr + j) & K4510_PHYS_MASK] = 0; fs_wr32(0x10, (uint32_t)j);
        break; }
    case FS_MOUNTS: {                     /* list mounts: LEN = index -> "/path  url" at ADDR */
        /* The caller's buffer is fs_cap bytes, as for GETCWD; 256 when it says
         * nothing.  This wrote up to 300 into the ROM's 256 until 2026-09-15. */
        uint32_t idx = fs_rd32(12); char b[360]; int j, cap = fs_cap ? fs_cap : 256;
        fs_cap = 0;
        if (idx >= (uint32_t)fs_mnt_n) { st = 4; break; }
        snprintf(b, sizeof b, "/%s  %s", fs_mnt[idx].at, fs_mnt[idx].url);
        for (j = 0; b[j] && j < cap - 1; j++) k4510_ram[(addr + j) & K4510_PHYS_MASK] = (uint8_t)b[j];
        k4510_ram[(addr + j) & K4510_PHYS_MASK] = 0; fs_wr32(0x10, (uint32_t)j);
        break; }
    case FS_GETCWD: {
        /* The caller's buffer is fs_cap bytes (64 when it says nothing -- the
         * ROM's).  A longer path keeps its tail behind "...": the end of a
         * path is the part that says where you are.  Before 2026-09-11 this
         * wrote up to 251 bytes into the ROM's 64 and a deep DIR overwrote
         * the shell's stack (review 2026-09-05, finding 1). */
        char s[520]; size_t n, cap = fs_cap ? fs_cap : 64, i; const char *src = s;
        fs_cap = 0;
        if (cap < 8) cap = 8;                  /* "..." + 4 + NUL; less made cap-4 wrap (review 2026-09-12, 3) */
        if (fs_remote[0]) snprintf(s, sizeof s, "%s", fs_remote); else snprintf(s, sizeof s, "/%s", fs_cwd);
        n = strlen(s);
        if (n > cap - 1) { src = s + n - (cap - 4); n = cap - 1;
            for (i = 0; i < 3; i++) k4510_ram[(addr + i) & K4510_PHYS_MASK] = '.';
            addr += 3; n -= 3; }
        for (i = 0; i < n; i++) k4510_ram[(addr + i) & K4510_PHYS_MASK] = (uint8_t)src[i];
        k4510_ram[(addr + i) & K4510_PHYS_MASK] = 0;
        fs_wr32(0x10, (uint32_t)strlen(s) > cap - 1 ? (uint32_t)cap - 1 : (uint32_t)strlen(s));
        break; }
    case FS_CHDIR_BACK:                    /* undo the last CHDIR, however long the path: DIR's way home */
        memcpy(fs_cwd, fs_back_cwd, sizeof fs_cwd); memcpy(fs_remote, fs_back_remote, sizeof fs_remote);
        break;
    default: st = 3;
    }
    if (cmd == FS_MOUNT && getenv("K4510_FSDEBUG")) {   /* a MOUNT: the names as the machine gave them, and where they were */
        char a[256] = "", b[256] = "";
        fs_guest_name(a, sizeof a); fs_guest_str(fs_rd32(8), b, sizeof b);
        fprintf(stderr, "fs: MOUNT '%s' @%04X '%s' @%04X -> %d (cwd '%s', %d mounted)\n",
                a, (unsigned) fs_rd32(4) & 0xFFFF, b, (unsigned) fs_rd32(8) & 0xFFFF, st, fs_cwd, fs_mnt_n);
    }
    if (fs_chdir_ran) {                    /* a CHDIR that landed: remember where it came from */
        fs_chdir_ran = 0;
        if (!st) { memcpy(fs_back_cwd, fs_was_cwd, sizeof fs_back_cwd); memcpy(fs_back_remote, fs_was_remote, sizeof fs_back_remote); }
    }
    fs_reg[1] = (uint8_t)st; fs_reg[0] = 0;
}
int fs_mount_count(void) { return fs_mnt_n; }
const char *fs_mount_at(int i) { return fs_mnt[i].at; }
const char *fs_mount_src(int i) { return fs_mnt[i].url; }
const uint8_t *hostfs_regs(size_t *n) { *n = sizeof fs_reg; return fs_reg; }
uint8_t hostfs_read(uint8_t r)
{
    if (r < sizeof fs_reg) return fs_reg[r];
    if (r == (IO_FS_CAP & 0xFF)) return fs_cap;
    return 0xFF;
}
void hostfs_write(uint8_t r, uint8_t v)
{
    if (r == (IO_FS_CMD & 0xFF)) { fs_run(v); return; }
    if (r < sizeof fs_reg) fs_reg[r] = v;
    if (r == (IO_FS_CAP & 0xFF)) fs_cap = v;
}
void hostfs_reset(void)
{
    fs_cap = 0;
    fs_remote[0] = 0; fs_cwd[0] = 0; fs_mnt_clear(); fs_net_drop();   /* cwd too: a power cycle from a subdirectory came back in it, where there is no STARTUP.BAT (Doc) */
    /* The prompt starts in /HOME where the disk has one (fs/HOME/README.TXT);
     * the ROM reads /STARTUP.BAT by its absolute name, so boot is unaffected. */
    { char home[600]; struct stat sb; snprintf(home, sizeof home, "%s/HOME", fs_root);
      if (!stat(home, &sb) && S_ISDIR(sb.st_mode)) snprintf(fs_cwd, sizeof fs_cwd, "HOME"); }
}
void hostfs_state_save(FILE *f)
{
    state_put(f, "FSCW", fs_cwd, sizeof fs_cwd);
    state_put(f, "FSRG", fs_reg, sizeof fs_reg);
}
int hostfs_state_load(FILE *f)
{
    if (state_get(f, "FSCW", fs_cwd, sizeof fs_cwd) || state_get(f, "FSRG", fs_reg, sizeof fs_reg)) return -2;
    fs_cwd[sizeof fs_cwd - 1] = 0;        /* a corrupt .k4s must not index out of bounds */
    /* ...nor walk out of the root: fs_resolve trusts fs_cwd as already clean */
    if (fs_cwd[0] == '/' || strstr(fs_cwd, "..") || strchr(fs_cwd, '\\')) fs_cwd[0] = 0;
    return 0;
}
void hostfs_after_load(void)
{
    fs_close_cur();
    fs_net_drop(); fs_remote[0] = 0; fs_mnt_clear(); fs_cap = 0;
}
