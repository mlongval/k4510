#include <time.h>
#include <ctype.h>     /* toupper: the search path uppercases a name's stem (fs_path) */
#include <strings.h>   /* strcasecmp: the extension table there */
#include "io.h"
#include "ui/settings.h"      /* the CPU clock request at $D523 */
#include "mem.h"
#include "xemu/emutools_basicdefs.h"
#include "xemu/cpu65.h"
static void dbg_key(uint8_t k);
static uint32_t sys_frames;
static int dbg_num;
static int dbg_auto; static uint32_t dbg_auto_next;
#include "vicky.h"
#include "opl2.h"
#include "audio.h"
#include "net.h"
#include "term.h"
#include "ui/menu.h"

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
#include <string.h>
#include <stdint.h>

/* ---- keyboard: a FIFO behind two registers (Wozmon polls them) --------
 * Each entry is a byte and one bit saying what KIND of byte: a character
 * (what a key or a dead-key sequence typed, in the font's code page 437) or
 * a key code (the arrows, Home, the function keys: KEY_* in io.h, $80-$9F).
 * The two ranges overlap -- KEY_LEFT is $82 and so is é -- and for as long
 * as the queue held bare bytes the shell could not tell them apart: on a
 * French keyboard the arrows printed Ç ü é â (Doc, hdieu, 2026-09-08).  The
 * kind rides in bit 8 here and is reported through KBDST bits 5 and 6. */
static uint16_t kbd_fifo[64];
static int      kbd_head, kbd_tail;
static uint8_t  kbd_last, kbd_mods, kbd_last_key;   /* kbd_last_key: the byte last read was a key code */
#define KBD_KEY 0x100

/* Every key passes here, from the frontend's SDL loop. The F7 menu (core/ui) takes them first: its own key
 * opens it (unshifted only -- Shift+F7 still reaches BBC BASIC) and,
 * while it is open, every key is the menu's. */
static void kbd_enqueue(uint16_t ent)
{
    int next = (kbd_tail + 1) & 63;
    if (next == kbd_head) return;
    kbd_fifo[kbd_tail] = ent;
    kbd_tail = next;
}
static void kbd_in(uint16_t ent)
{
    uint8_t ascii = (uint8_t)ent;
    if (menu_is_open()) { menu_key(ascii); return; }
    if ((ent & KBD_KEY) && ascii == menu_key_code() && !(kbd_mods & 1)) { menu_open(); return; }
    dbg_key(ascii);
    kbd_enqueue(ent);
}
void kbd_push(uint8_t ascii)   { kbd_in(ascii); }                    /* a character */
void kbd_push_key(uint8_t code) { kbd_in((uint16_t)code | KBD_KEY); }  /* a KEY_* code */
void kbd_modifiers(uint8_t sh, uint8_t ct, uint8_t al) { kbd_mods = (sh ? 1 : 0) | (ct ? 2 : 0) | (al ? 4 : 0); }
static uint8_t kbd_held_mask;
void kbd_held(uint8_t mask) { kbd_held_mask = mask; }
static int mouse_x, mouse_y; static uint8_t mouse_btn; static int8_t mouse_wheel, mouse_dx, mouse_dy;
static int8_t clamp8(int v) { return (int8_t)(v > 127 ? 127 : v < -128 ? -128 : v); }
void mouse_set(int x, int y, uint8_t buttons, int wheel, int dx, int dy)
{
    mouse_x = x < 0 ? 0 : x > 639 ? 639 : x; mouse_y = y < 0 ? 0 : y > 479 ? 479 : y;
    mouse_btn = buttons; mouse_wheel = clamp8(wheel); mouse_dx = clamp8(dx); mouse_dy = clamp8(dy);
}
static int kbd_ready(void) { return kbd_head != kbd_tail; }
static uint8_t kbd_read(void)
{
    if (!kbd_ready()) return 0;
    kbd_last = (uint8_t)kbd_fifo[kbd_head]; kbd_last_key = (kbd_fifo[kbd_head] & KBD_KEY) ? 1 : 0;
    kbd_head = (kbd_head + 1) & 63;
    return kbd_last;
}

/* ---- host filesystem -------------------------------------------------- */
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
static char fs_root[512] = "fs";
static char fs_cwd[256] = "";            /* relative to fs_root, no leading/trailing slash; "" = root */
static uint8_t fs_reg[0x18];
static uint8_t fs_cap;                    /* $D318: GETCWD's buffer size, one shot; 0 = 64 (the ROM's) */
static char fs_back_cwd[256], fs_back_remote[512];   /* where the last CHDIR came from: FS_CHDIR_BACK */
static char fs_was_cwd[256], fs_was_remote[512];     /* taken as a CHDIR starts, kept only if it lands */
static int  fs_chdir_ran;
static FILE *fs_file;
static uint8_t *fs_netbuf; static uint32_t fs_netlen, fs_netpos;   /* a fetched URL, served as the open file */
static char fs_remote[512];             /* the current directory when it is on a server: a tnfs:// URL; "" = local */
static void fs_net_drop(void) { free(fs_netbuf); fs_netbuf = NULL; fs_netlen = fs_netpos = 0; }
/* Mount points: a local-looking path (MNT/ATARI) mapped onto a server, so a
 * server can be navigated like a local subtree -- the cwd never becomes a URL,
 * so local programs (RANGER) launch through the usual search path and only the
 * filesystem ops route to the net (Doc, 2026-09-10: the atari8.us case). */
static struct { char at[80]; char url[240]; } fs_mnt[8]; static int fs_mnt_n;
/* REL is a root-relative path; if it is under a mount, fill URL (base + tail)
 * and return 1.  A trailing part is appended with net_url_join. */
static int fs_mount_url(const char *rel, char *url, size_t max)
{
    for (int i = 0; i < fs_mnt_n; i++) {
        size_t al = strlen(fs_mnt[i].at);
        if (strncasecmp(rel, fs_mnt[i].at, al) || (rel[al] && rel[al] != '/')) continue;
        { const char *tail = rel + al; while (*tail == '/') tail++;
          if (*tail) net_url_join(url, max, fs_mnt[i].url, tail); else snprintf(url, max, "%s", fs_mnt[i].url); }
        return 1;
    }
    return 0;
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
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
/* Resolve a guest name against the cwd inside the sandbox: "/" is the root,
 * "." and ".." work, ".." never climbs above the root. rel gets the
 * root-relative path ("" for the root), out the host path. */
static int fs_resolve(const char *name, char *rel, size_t relmax, char *out, size_t outmax)
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
static void fs_casefix(char *path, size_t max)
{
    struct stat sb; char dir[768], *base; DIR *d; struct dirent *e;
    if (!stat(path, &sb)) return;
    snprintf(dir, sizeof dir, "%s", path); base = strrchr(dir, '/'); if (!base) return;
    *base++ = 0;
    if (!(d = opendir(dir))) return;
    while ((e = readdir(d))) if (!strcasecmp(e->d_name, base)) { snprintf(path, max, "%.500s/%.255s", dir, e->d_name); break; }
    closedir(d);
}
static int fs_guest_str(uint32_t p, char *name, size_t max)
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
#include <stdlib.h>
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
              if (cmd == FS_STAT && net_isdir(url) == 1) { fs_wr32(0x10, 0xFFFFFFFFu); break; }
              if ((st = net_fetch(url, &b, &n))) { st = st == 6 ? 1 : st;
                  if (st == 1 && bare) goto local_fs;     /* not on the server: a local program by this name */
                  break; }
              if (cmd == FS_STAT) { fs_wr32(0x10, n); free(b); break; }
              if (fs_file) { fclose(fs_file); fs_file = NULL; }
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
        if (fs_file) { fclose(fs_file); fs_file = NULL; }
        fs_net_drop();
        fs_file = fopen(path, (cmd == FS_OPEN_WRITE || cmd == FS_SAVE) ? "wb" : "rb");
        if (!fs_file) { st = 1; break; }
        if (cmd == FS_OPEN_READ || cmd == FS_LOAD) { fseek(fs_file, 0, SEEK_END); long sz = ftell(fs_file); fseek(fs_file, 0, SEEK_SET); fs_wr32(0x10, (uint32_t)sz); }
        if (cmd == FS_LOAD)  {                /* LEN, when the caller set one, is the buffer: LOAD used to run to EOF over it (review 2026-09-12); 0 = whole file */
            uint32_t got = fs_read_span(fs_file, addr, len ? len : K4510_PHYS_SIZE); fs_wr32(12, got);
            if (len && got == len && fgetc(fs_file) != EOF) st = 6;
            fclose(fs_file); fs_file = NULL; }
        if (cmd == FS_SAVE)  { fs_write_span(fs_file, addr, len); fclose(fs_file); fs_file = NULL; }
        break; }
    case FS_READ: {
        uint32_t done = 0;
        if (fs_netbuf) { while (done < len && fs_netpos < fs_netlen) k4510_ram[(addr + done++) & K4510_PHYS_MASK] = fs_netbuf[fs_netpos++]; fs_wr32(12, done); break; }
        if (!fs_file) { st = 2; break; }
        fs_wr32(12, fs_read_span(fs_file, addr, len)); break; }
    case FS_WRITE: { if (!fs_file) { st = 2; break; } fs_write_span(fs_file, addr, len); break; }
    case FS_CLOSE: if (fs_file) { fclose(fs_file); fs_file = NULL; } fs_net_drop(); break;
    case FS_DIR_FIRST: case FS_DIR_ALL: {
        char durl[512]; const char *lurl = fs_remote[0] ? fs_remote : (fs_mount_url(fs_cwd, durl, sizeof durl) ? durl : NULL);
        if (lurl) {                           /* a listing from the server (CD tnfs:// or a mount) */
            net_dirent *e; int n;
            if ((st = net_listdir(lurl, &e, &n))) { st = st == 6 ? 2 : st; break; }
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
              if ((st = net_fetch(url, &nb, &nn))) { st = st == 6 ? 1 : st; break; }
          } }
        if (!nb && (st = fs_path(path, sizeof path, 1))) break;                /* source, searched + case-fixed */
        if ((st = fs_guest_str(fs_rd32(8), n2, sizeof n2))) break;
        if ((st = fs_resolve(n2, rel, sizeof rel, dst, sizeof dst))) break;    /* destination, as given */
        { char durl[256]; if (fs_mount_url(rel, durl, sizeof durl)) { if (nb) free(nb); st = 2; break; } }   /* a mount is read-only */
        if (cmd == FS_RENAME)
            st = rename(path, dst) ? 2 : 0;
        else {
            FILE *a = nb ? NULL : fopen(path, "rb"), *b = NULL;
            if (nb) { if (!(b = fopen(dst, "wb"))) { free(nb); st = 2; break; } if (fwrite(nb, 1, nn, b) != nn) st = 2; fclose(b); free(nb); break; }
            if (!a) { st = 1; break; }
            if (!(b = fopen(dst, "wb"))) { fclose(a); st = 2; break; }
            { char buf[4096]; size_t k; while ((k = fread(buf, 1, sizeof buf, a)) > 0) if (fwrite(buf, 1, k, b) != k) { st = 2; break; } }
            fclose(a); fclose(b);
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
            if (net_isdir(url) == 1) snprintf(fs_cwd, sizeof fs_cwd, "%s", rel); else st = 1;
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
    case FS_MOUNT: {                       /* NAMEPTR = URL, reg 8 -> PATH */
        char url[256], pathn[256], rel[256], loc[768];
        if ((st = fs_guest_name(url, sizeof url))) break;
        if ((st = fs_guest_str(fs_rd32(8), pathn, sizeof pathn))) break;
        if (!net_is_url(url) || !pathn[0]) { st = 3; break; }
        if ((st = fs_resolve(pathn, rel, sizeof rel, loc, sizeof loc))) break;
        if (!rel[0]) { st = 2; break; }                                    /* the root cannot be a mount */
        { int i; for (i = 0; i < fs_mnt_n; i++) if (!strcasecmp(fs_mnt[i].at, rel)) break;
          if (i == fs_mnt_n) { if (fs_mnt_n >= 8) { st = 2; break; } fs_mnt_n++; }
          snprintf(fs_mnt[i].at, sizeof fs_mnt[i].at, "%s", rel);
          { size_t n; snprintf(fs_mnt[i].url, sizeof fs_mnt[i].url, "%s", url); n = strlen(fs_mnt[i].url); while (n > 8 && fs_mnt[i].url[n - 1] == '/') fs_mnt[i].url[--n] = 0; } }
        mkdir(loc, 0777);                     /* a real (empty) directory, so the mount shows in a DIR of its parent */
        break; }
    case FS_UMOUNT: {                      /* NAMEPTR = PATH */
        char name[256], rel[256], loc[768]; int i, found = 0;
        if ((st = fs_guest_name(name, sizeof name))) break;
        if ((st = fs_resolve(name, rel, sizeof rel, loc, sizeof loc))) break;
        for (i = 0; i < fs_mnt_n; i++) if (!strcasecmp(fs_mnt[i].at, rel)) { found = 1; fs_mnt[i] = fs_mnt[--fs_mnt_n]; break; }
        if (!found) st = 1;
        else rmdir(loc);                      /* remove the placeholder directory MOUNT made (only if it is empty) */
        break; }
    case FS_MOUNTS: {                     /* list mounts: LEN = index -> "/path  url" at ADDR */
        uint32_t idx = fs_rd32(12); char b[360]; int j;
        if (idx >= (uint32_t)fs_mnt_n) { st = 4; break; }
        snprintf(b, sizeof b, "/%s  %s", fs_mnt[idx].at, fs_mnt[idx].url);
        for (j = 0; b[j] && j < 300; j++) k4510_ram[(addr + j) & K4510_PHYS_MASK] = (uint8_t)b[j];
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
    if (fs_chdir_ran) {                    /* a CHDIR that landed: remember where it came from */
        fs_chdir_ran = 0;
        if (!st) { memcpy(fs_back_cwd, fs_was_cwd, sizeof fs_back_cwd); memcpy(fs_back_remote, fs_was_remote, sizeof fs_back_remote); }
    }
    fs_reg[1] = (uint8_t)st; fs_reg[0] = 0;
}

/* ---- DMA ---------------------------------------------------------------- */
static uint8_t dma_reg[16];     /* SRC[4] DST[4] LEN[4] CMD STATUS .. */



static void dma_run(uint8_t cmd)
{
    uint32_t src = rd32(&dma_reg[0]) & K4510_PHYS_MASK;
    uint32_t dst = rd32(&dma_reg[4]) & K4510_PHYS_MASK;
    uint32_t len = rd32(&dma_reg[8]) & K4510_PHYS_MASK;
    dma_reg[13] = cmd;
    switch (cmd) {
    case 1:   /* copy, memmove semantics (overlap-safe), wraps at 256 MB */
        if (src + len <= K4510_PHYS_SIZE && dst + len <= K4510_PHYS_SIZE) {
            memmove(k4510_ram + dst, k4510_ram + src, len);
        } else {
            for (uint32_t i = 0; i < len; i++)       /* rare: wrap */
                k4510_ram[(dst + i) & K4510_PHYS_MASK] = k4510_ram[(src + i) & K4510_PHYS_MASK];
        }
        break;
    case 2:   /* fill with SRC byte 0 */
        if (dst + len <= K4510_PHYS_SIZE) memset(k4510_ram + dst, dma_reg[0], len);
        else for (uint32_t i = 0; i < len; i++) k4510_ram[(dst + i) & K4510_PHYS_MASK] = dma_reg[0];
        break;
    case 3:   /* swap */
        for (uint32_t i = 0; i < len; i++) {
            uint8_t *a = &k4510_ram[(src + i) & K4510_PHYS_MASK], *b = &k4510_ram[(dst + i) & K4510_PHYS_MASK];
            uint8_t t = *a; *a = *b; *b = t;
        }
        break;
    default:
        dma_reg[13] = 0xFF;
    }
    dma_reg[12] = 0;   /* instant: idle again before the CPU sees the next instruction */
}

/* ---- dispatch ------------------------------------------------------------ */

/* ---- MATH $D700 --------------------------------------------------------- */
#include <math.h>
static uint8_t math_reg[0x80];       /* $D700-$D77F image; F0..F7 at 0, FI at $24, integer unit at $68.. */
static float  mf_get(int n) { float f; memcpy(&f, &math_reg[n * 4], 4); return f; }
static void   mf_set(int n, float f) { memcpy(&math_reg[n * 4], &f, 4); }
static uint32_t m32(int off) { return (uint32_t)math_reg[off] | ((uint32_t)math_reg[off + 1] << 8) | ((uint32_t)math_reg[off + 2] << 16) | ((uint32_t)math_reg[off + 3] << 24); }
static void m32w(int off, uint32_t v) { for (int i = 0; i < 4; i++) math_reg[off + i] = (uint8_t)(v >> (8 * i)); }
static void math_int_update(void)
{
    uint32_t a = m32(0x70), b = m32(0x74);
    uint64_t p = (uint64_t)a * b;
    m32w(0x78, (uint32_t)p); m32w(0x7C, (uint32_t)(p >> 32));
    if (b == 0) { m32w(0x6C, 0xFFFFFFFFu); m32w(0x68, 0xFFFFFFFFu); }
    else { uint64_t q = ((uint64_t)a << 32) / b; m32w(0x6C, (uint32_t)(q >> 32)); m32w(0x68, (uint32_t)q); }
}
/* MS-BASIC float-to-ASCII, EhBASIC-style: 6 significant digits, fixed
 * format for 0.01 <= |v| < 1e6, otherwise d.dddddE+xx; ".5" fractions,
 * trailing zeros stripped -- measured against Lee's own FOUT output. */
static void ms_ftoa(float vf, char *out, int lead)
{
    char *p = out, digits[12]; double v = vf; int e10 = 0, dp, i, n;
    if (lead) *p++ = vf < 0 ? '-' : ' ';
    else if (vf < 0) *p++ = '-';
    if (v < 0) v = -v;
    if (v == 0) { *p++ = '0'; *p = 0; return; }
    if (isnan(v)) { strcpy(p, "NAN"); return; }
    if (isinf(v)) { strcpy(p, "1E+38"); return; }         /* MS BASIC has no inf: overflow prints as its largest */
    while (v >= 999999.5) { v /= 10; e10++; }
    while (v < 99999.95)  { v *= 10; e10--; }
    snprintf(digits, sizeof digits, "%06lu", (unsigned long)(v + 0.5));
    if (digits[6]) { digits[6] = 0; e10++; }              /* 999999.5+ rounded up a digit */
    dp = 6 + e10;                                         /* value = 0.digits * 10^dp */
    n = 6; while (n > 1 && digits[n - 1] == '0') n--;     /* strip trailing zeros */
    if (dp >= -1 && dp <= 6) {                            /* fixed: 0.01 <= v < 1e6 */
        if (dp <= 0) { *p++ = '.'; for (i = 0; i < -dp; i++) *p++ = '0'; for (i = 0; i < n; i++) *p++ = digits[i]; }
        else {
            for (i = 0; i < dp; i++) *p++ = i < n ? digits[i] : '0';
            if (n > dp) { *p++ = '.'; for (i = dp; i < n; i++) *p++ = digits[i]; }
        }
    } else {                                                /* E format */
        *p++ = digits[0];
        if (n > 1) { *p++ = '.'; for (i = 1; i < n; i++) *p++ = digits[i]; }
        *p++ = 'E'; *p++ = dp - 1 < 0 ? '-' : '+';
        i = dp - 1 < 0 ? 1 - dp : dp - 1;
        *p++ = (char)('0' + i / 10); *p++ = (char)('0' + i % 10);
    }
    *p = 0;
}
static void math_fop(uint8_t op)
{
    int d = (math_reg[0x21] >> 4) & 7, sidx = math_reg[0x21] & 7;
    float a = mf_get(d), b = mf_get(sidx), r = a; int store = 1;
    switch (op & 0x1F) {
    case MATH_MOV: r = b; break;       case MATH_ADD: r = a + b; break;   case MATH_SUB: r = a - b; break;
    case MATH_MUL: r = a * b; break;   case MATH_DIV: r = a / b; break;
    case MATH_SQRT: r = sqrtf(b); break; case MATH_SIN: r = sinf(b); break; case MATH_COS: r = cosf(b); break;
    case MATH_TAN: r = tanf(b); break; case MATH_ATAN: r = atanf(b); break; case MATH_ATAN2: r = atan2f(a, b); break;
    case MATH_EXP: r = expf(b); break; case MATH_LOG: r = logf(b); break;  case MATH_POW: r = powf(a, b); break;
    case MATH_ABS: r = fabsf(b); break; case MATH_NEG: r = -b; break;     case MATH_FLOOR: r = floorf(b); break;
    case MATH_ROUND: r = roundf(b); break; case MATH_FMOD: r = fmodf(a, b); break;
    case MATH_CMP: r = a - b; store = 0; break;
    case MATH_ITOF: r = (float)(int32_t)m32(0x24); break;
    case MATH_FTOI: { float t = truncf(b); int32_t i = (t > 2147483520.0f) ? INT32_MAX : (t < -2147483648.0f) ? INT32_MIN : (int32_t)t; m32w(0x24, (uint32_t)i); r = b; store = 0; break; }
    case MATH_FTOA: case MATH_FTOAR: {                    /* number output on the MATH unit: the exact */
        char buf[24]; uint32_t p = m32(0x30); int i;       /* MS-BASIC 9-digit format EhBASIC always used */
        ms_ftoa(b, buf, op == MATH_FTOA);
        for (i = 0; buf[i]; i++) k4510_ram[(p + i) & K4510_PHYS_MASK] = (uint8_t)buf[i];
        k4510_ram[(p + i) & K4510_PHYS_MASK] = 0;
        store = 0; break; }
    default: store = 0; break;
    }
    if (store) mf_set(d, r);
    math_reg[0x22] = (uint8_t)((r == 0.0f ? 1 : 0) | (r < 0.0f ? 2 : 0) | (isnan(r) || isinf(r) ? 4 : 0));
}
static void math_list_run(void)
{
    uint32_t pc = m32(0x28) & K4510_PHYS_MASK;
    uint16_t cnt = (uint16_t)(math_reg[0x2E] | (math_reg[0x2F] << 8));
    uint8_t status = 0xFF;
    for (int guard = 0; guard < 65536; guard++) {
        uint8_t op = k4510_ram[pc & K4510_PHYS_MASK], arg = k4510_ram[(pc + 1) & K4510_PHYS_MASK];
        pc += 2;
        if (op < 0x20) { math_reg[0x21] = arg; math_fop(op); continue; }
        int fl = math_reg[0x22];
        switch (op) {
        case ML_END:     status = 0; goto done;
        case ML_STOPNEG: if (fl & 2)   { status = 1; goto done; } break;
        case ML_STOPPOS: if (!(fl & 2)) { status = 1; goto done; } break;
        case ML_STOPZERO: if (fl & 1)  { status = 1; goto done; } break;
        case ML_STOPNZ:  if (!(fl & 1)) { status = 1; goto done; } break;
        case ML_JUMP:    pc += (int8_t)arg * 2; break;
        case ML_DJNZ:    if (--cnt) pc += (int8_t)arg * 2; break;
        case ML_STOPFIGE: { int32_t fi = (int32_t)m32(0x24); if (fi >= (int32_t)arg) { status = 1; goto done; } break; }
        case ML_LDF:     for (int i = 0; i < 4; i++) math_reg[((arg >> 4) & 7) * 4 + i] = k4510_ram[(pc + i) & K4510_PHYS_MASK]; pc += 4; break;
        case ML_LDI:     for (int i = 0; i < 4; i++) math_reg[0x24 + i] = k4510_ram[(pc + i) & K4510_PHYS_MASK]; pc += 4; break;
        case ML_LDMS: {  uint32_t a = ((uint32_t)k4510_ram[pc & K4510_PHYS_MASK] | ((uint32_t)k4510_ram[(pc + 1) & K4510_PHYS_MASK] << 8)
                         | ((uint32_t)k4510_ram[(pc + 2) & K4510_PHYS_MASK] << 16) | ((uint32_t)k4510_ram[(pc + 3) & K4510_PHYS_MASK] << 24)) & K4510_PHYS_MASK;
                         uint8_t e = k4510_ram[a], m1 = k4510_ram[(a + 1) & K4510_PHYS_MASK], m2 = k4510_ram[(a + 2) & K4510_PHYS_MASK], m3 = k4510_ram[(a + 3) & K4510_PHYS_MASK];
                         float f = 0.0f;
                         if (e >= 2) { uint32_t bits = ((uint32_t)(m1 & 0x80) << 24) | ((uint32_t)(e - 2) << 23) | ((uint32_t)(m1 & 0x7F) << 16) | ((uint32_t)m2 << 8) | m3; memcpy(&f, &bits, 4); }
                         mf_set((arg >> 4) & 7, f); pc += 4; break; }
        default:         status = 0xFF; goto done;
        }
    }
done:
    math_reg[0x2D] = status; math_reg[0x2E] = (uint8_t)cnt; math_reg[0x2F] = (uint8_t)(cnt >> 8);
}
static uint8_t math_read(uint8_t r) { return r < sizeof math_reg ? math_reg[r] : 0xFF; }
static void math_write(uint8_t r, uint8_t v)
{
    if (r >= sizeof math_reg) return;
    if (r == 0x20) { math_fop(v); return; }
    if (r == 0x2C) { math_list_run(); return; }
    math_reg[r] = v;
    if (r >= 0x70 && r < 0x78) math_int_update();
}

/* ---- SYS $D500 ---------------------------------------------------------- */
#include <time.h>
extern uint32_t mem_rom_base;
static uint8_t  sys_reg[0x10];
/* The build, not the generation.  The Makefile stamps git describe in here, so
 * a BUG report names the exact commit it came from; without that both this and
 * ROM_VERSION only say which era the machine is from.  16 bytes, NUL included. */
#include "build.h"        /* generated by the Makefile from git; regenerated only when
                           * the commit changes, so io.o rebuilds exactly when it must */
#ifndef K4510_BUILD
#define K4510_BUILD "k4510 0.3"
#endif
static const char sys_version[16] = K4510_BUILD;
/* $D522: what is beneath the machine.  The guest cannot otherwise tell a
 * desktop window from the K4510 Linux -- the ROM bytes are the same --
 * and BUG and INFO have to say which one an issue came from.  The frontend
 * sets it: 0 = a desktop (or a container on one), 1 = the K4510 Linux. */
int io_host_kind;
/* ---- the sound sequencer ($D5E0-$D5E3) ---------------------------------
 * The BBC Micro's four queued sound channels, in K4510 silicon. Write CH
 * ($D5E0: low nibble = channel, bit 4 = flush that channel's queue first,
 * bit 7 = silence everything now), AMP (signed, 0 to -15; an envelope
 * number > 0 plays at a fixed loudness), PITCH (quarter semitones, 53 =
 * middle C) and DUR ($D5E3: 20ths of a second, 255 = hold forever); the
 * DUR write queues the note. The channels are OPL2 voices 0-3: channel 0
 * is the Beeb's noise channel, a heavily fed-back FM patch here (the OPL2
 * has no noise generator outside rhythm mode); 1-3 are a plain two-operator
 * tone.  Each channel holds a playing note plus 63 queued ones -- much
 * deeper than the Beeb's four, because the Beeb blocked BASIC when the
 * queue filled and a one-way Tube cannot; a whole tune fits instead.  A
 * note arriving on a full queue is dropped.
 *
 * Until 2026-09-05 the channels were SID voices; the SIDs are gone.  The
 * patch is written on every note because a program is free to zero the
 * chip (OPL2.PRG does, on its way out) and the sequencer must still sound
 * after it. */
#define SEQ_DEPTH 64
typedef struct { uint16_t freq; uint8_t amp, dur; } seq_note;   /* freq: OPL2 F-number | block << 10 */
static seq_note seq_q[4][SEQ_DEPTH];
static uint8_t  seq_head[4], seq_len[4], seq_reg[3];
static int      seq_left[4];             /* frames left of the playing note; 0 idle, -1 forever */

static const uint8_t seq_slot[4] = { 0, 1, 2, 8 };   /* the modulator slot of OPL2 voices 0-3; the carrier is 3 on */
static void seq_off(int ch)
{
    opl2_write_reg((uint8_t)(0xB0 + ch), 0);                      /* key off */
    seq_left[ch] = 0;
}
static void seq_start(int ch, const seq_note *n)
{
    uint8_t m = seq_slot[ch], c = (uint8_t)(m + 3), noise = ch == 0;
    uint8_t tl = (uint8_t)((15 - (n->amp > 15 ? 15 : n->amp)) * 3);   /* carrier level: 0 loudest, 63 silent */
    opl2_write_reg(0x01, 0x20);                                   /* waveform select on */
    opl2_write_reg((uint8_t)(0x20 + m), noise ? 0x0F : 0x21);     /* modulator: mult 15 for noise, else 1 + sustain */
    opl2_write_reg((uint8_t)(0x40 + m), noise ? 0x00 : 0x18);
    opl2_write_reg((uint8_t)(0x60 + m), 0xF0);                    /* attack instant, no decay */
    opl2_write_reg((uint8_t)(0x80 + m), 0x0F);                    /* sustain full, release fast */
    opl2_write_reg((uint8_t)(0xE0 + m), noise ? 0x00 : 0x01);
    opl2_write_reg((uint8_t)(0x20 + c), 0x21);                    /* carrier: mult 1, sustain */
    opl2_write_reg((uint8_t)(0x40 + c), tl);
    opl2_write_reg((uint8_t)(0x60 + c), 0xF0);
    opl2_write_reg((uint8_t)(0x80 + c), 0x0F);
    opl2_write_reg((uint8_t)(0xE0 + c), 0x00);
    opl2_write_reg((uint8_t)(0xC0 + ch), noise ? 0x0E : 0x00);   /* noise: feedback 7, FM */
    opl2_write_reg((uint8_t)(0xB0 + ch), 0);                      /* key off first: a retrigger needs the envelope let go */
    if (n->amp) {
        opl2_write_reg((uint8_t)(0xA0 + ch), (uint8_t)(n->freq & 0xFF));
        opl2_write_reg((uint8_t)(0xB0 + ch), (uint8_t)(0x20 | ((n->freq >> 8) & 0x1F)));
    }
    seq_left[ch] = (n->dur == 255) ? -1 : (n->dur ? n->dur * 3 : 1);   /* 20ths at 60 fps */
}
static void seq_next(int ch)
{
    if (seq_len[ch]) { seq_start(ch, &seq_q[ch][seq_head[ch]]); seq_head[ch] = (seq_head[ch] + 1) % SEQ_DEPTH; seq_len[ch]--; }
    else seq_off(ch);
}
static void seq_tick(void)
{
    for (int ch = 0; ch < 4; ch++)
        if (seq_left[ch] > 0 && --seq_left[ch] == 0) seq_next(ch);
}
static void seq_write(uint8_t r, uint8_t v)
{
    if (r < 3) {
        if (r == 0 && (v & 0x80)) for (int c = 0; c < 4; c++) { seq_len[c] = 0; seq_off(c); }
        seq_reg[r] = v;
        return;
    }
    {
        static const double semiq[48] = {    /* 2^(i/48): a quarter-semitone ladder */
            1.000000000, 1.014545335, 1.029302237, 1.044273782, 1.059463094, 1.074873340,
            1.090507733, 1.106369533, 1.122462048, 1.138788635, 1.155352697, 1.172157689,
            1.189207115, 1.206504531, 1.224053543, 1.241857812, 1.259921050, 1.278247024,
            1.296839555, 1.315702520, 1.334839854, 1.354255547, 1.373953647, 1.393938263,
            1.414213562, 1.434783772, 1.455653183, 1.476826146, 1.498307077, 1.520100455,
            1.542210825, 1.564642798, 1.587401052, 1.610490332, 1.633915453, 1.657681301,
            1.681792831, 1.706255071, 1.731073122, 1.756252160, 1.781797436, 1.807714277,
            1.834008086, 1.860684348, 1.887748625, 1.915206561, 1.943063882, 1.971326397 };
        int ch = seq_reg[0] & 3, q = (int)seq_reg[2] - 5;         /* pitch 5 = C3, 130.81 Hz */
        signed char a = (signed char)seq_reg[1];
        double hz, f; int block;
        seq_note n;
        n.amp = a < 0 ? (uint8_t)(-a > 15 ? 15 : -a) : (a > 0 ? 13 : 0);
        hz = 130.8127827;
        if (q < 0) hz = hz * semiq[q + 48] / 2.0;
        else       hz = hz * semiq[q % 48] * (double)(1 << (q / 48));
        /* the OPL2's F-number: hz * 2^(20-block) / 49716, in the lowest block that holds it */
        for (block = 0; block < 7; block++) if (hz * (double)(1 << (20 - block)) / 49716.0 < 1024.0) break;
        f = hz * (double)(1 << (20 - block)) / 49716.0;
        n.freq = (uint16_t)((f > 1023.0 ? 1023 : (int)f) | (block << 10));
        n.dur = v;
        if (seq_reg[0] & 0x10) { seq_len[ch] = 0; seq_left[ch] = 0; }          /* flush: this note now */
        if (seq_left[ch] == 0) seq_start(ch, &n);
        else if (seq_len[ch] < SEQ_DEPTH) { seq_q[ch][(seq_head[ch] + seq_len[ch]) % SEQ_DEPTH] = n; seq_len[ch]++; }
    }
}

void io_frame_tick(void) { sys_frames++; seq_tick(); term_tick(); if (dbg_auto && sys_frames >= dbg_auto_next) { dbg_auto_next = sys_frames + 900; dbg_dump("auto, 15 s"); } }
static unsigned sys_cpu_khz = 40500;
void io_set_cpu_khz(unsigned khz) { sys_cpu_khz = khz; }   /* the frontend, from the CPU clock setting */
static void sys_latch(void)
{
    time_t t = time(NULL); struct tm *m = localtime(&t);
    sys_reg[0] = (uint8_t)(sys_cpu_khz & 0xFF); sys_reg[1] = (uint8_t)(sys_cpu_khz >> 8);
    sys_reg[2] = 256 & 0xFF;   sys_reg[3] = 256 >> 8;
    sys_reg[5] = m->tm_sec; sys_reg[6] = m->tm_min; sys_reg[7] = m->tm_hour;
    sys_reg[8] = m->tm_mday; sys_reg[9] = m->tm_mon + 1;
    sys_reg[10] = (m->tm_year + 1900) & 0xFF; sys_reg[11] = (m->tm_year + 1900) >> 8;
    sys_reg[12] = m->tm_wday;
}
static uint8_t sys_opts;                 /* the menu's switches, readable by the guest */
uint16_t io_audio_gaps;                   /* the frontend counts: audio callbacks that found nothing to play */
static int mode_acked;
void io_set_opts(uint8_t v) { sys_opts = v; }
static uint8_t sys_band_top = 1, sys_band_bot = 2, sys_clockfmt;
void io_set_bands(uint8_t top, uint8_t bot, uint8_t clockfmt)
{ sys_band_top = top; sys_band_bot = bot; sys_clockfmt = clockfmt; }
int  io_mode_acked(void) { int a = mode_acked; mode_acked = 0; return a; }
/* SETUP measures the machine and then asks to keep the answer.  The guest
 * cannot write k4510.cfg -- it is outside the machine's filesystem -- and has
 * no business knowing what a host fingerprint is, so it writes SYS+$28 and the
 * frontend does both.  Reading SYS+$28 says whether this host has been
 * measured at all, which is what the banner tells the user about. */
static int clock_measured, adopt_req;
void io_set_clock_measured(int yes) { clock_measured = yes ? 1 : 0; }
int  io_clock_measured(void) { return clock_measured; }   /* asked back: the governor says a different
                                                           * thing when SETUP's answer is the one standing */
int  io_adopt_requested(void) { int a = adopt_req; adopt_req = 0; return a; }
/* SETUP sweeps the ladder and starves the sound on purpose at the top of it.
 * The governor's whole job is to step down when the sound starves, so left
 * running it changes the clock under the program measuring it -- which it did,
 * 2026-08-27: the sweep came out non-monotonic and nothing reached 60 fps.
 * SYS+$29 says a measurement is in progress and the governor stands down. */
static int measuring;
int  io_measuring(void) { return measuring; }
/* What "choppy" actually is, since 952daa6.  The gap counter asks whether the
 * ring ran dry, and that fix stops it running dry by clocking the sound on
 * without the CPU when the machine is late -- so gaps read 0 across the whole
 * band where a host is merely losing, and Doc could hear drops on rows both
 * BENCH and SETUP called clean.  This counts the samples that came from that
 * top-up rather than from a frame the machine ran: it is the sound the
 * machine did not make, which is the sound he is hearing. */
uint16_t io_audio_fill;

/* A real-time millisecond counter at SYS+$36..$39 (32-bit, little endian).
 *
 * The frame counter at SYS+$0D counts frames the machine RENDERED, which is
 * the right clock for anything that wants to be in step with the picture and
 * the wrong one for anything that wants to be in step with the world: when
 * the host runs long, frames stretch and anything paced by them stretches
 * with them.  OPLPLAY's music did exactly that on the Pi.
 *
 * It is read through the host's own clock at the moment the guest asks,
 * rather than latched once a frame -- latching would hand back the same
 * granularity the frame counter already has and fix nothing. */
static uint32_t (*ms_source)(void);
void io_set_ms_source(uint32_t (*fn)(void)) { ms_source = fn; }
static uint32_t sys_ms(void) { return ms_source ? ms_source() : 0; }

static uint8_t sys_read(uint8_t r)
{
    if (r == 4) { sys_latch(); return 0; }
    if (r < 4) { sys_latch(); return sys_reg[r]; }
    if (r < 0x0D) return sys_reg[r];
    if (r < 0x10) return (uint8_t)(sys_frames >> ((r - 0x0D) * 8));
    if (r < 0x20) return (uint8_t)sys_version[r - 0x10];
    if (r == 0x20) return (uint8_t)(mem_rom_base >> 8);
    if (r == 0x21) return sys_opts;
    if (r == 0x2D) return sys_band_top;      /* rows in the top band */
    if (r == 0x2E) return sys_band_bot;      /* rows in the bottom band */
    if (r == 0x2F) return sys_clockfmt;      /* bit0 24-hour; bits1-2 the date order */
    if (r == 0x22) return (uint8_t)io_host_kind;  /* what is beneath the machine, for BUG and INFO */
    /* The clock's index in the frontend's ladder.  Deliberately NOT documented
     * as a fixed table: the ladder is reordered when steps are added, and a
     * guest that knows a clock by its position gets it wrong the day that
     * happens (BENCH did, 2026-08-27).  Write past the end to learn the
     * highest index -- the write clamps -- and read SYS+0/1/0x26 to see what
     * a step actually became. */
    if (r == 0x23) return (uint8_t)settings_get(SET_CPU_CLOCK);
    if (r == 0x24) return (uint8_t)(io_audio_gaps & 0xFF);       /* audio callbacks that found the ring empty, since last cleared */
    if (r == 0x25) return (uint8_t)(io_audio_gaps >> 8);
    if (r == 0x27) return (uint8_t)settings_choices(SET_CPU_CLOCK);
    if (r == 0x28) return (uint8_t)clock_measured;                /* 0: no measured clock for this host yet -- run SETUP */
    if (r == 0x29) return (uint8_t)measuring;
    if (r == 0x2A) return (uint8_t)(io_audio_fill & 0xFF);       /* filled samples since last cleared, saturating */
    if (r == 0x2B) return (uint8_t)(io_audio_fill >> 8);
    if (r == 0x26) return (uint8_t)(sys_cpu_khz >> 16);   /* the clock in kHz needs a third byte: SYS+0/1 alone stop at 65.5 MHz, and the ladder goes to 202500 */
    if (r >= 0x30 && r <= 0x33) return (uint8_t)(dbg_watch_addr >> (8 * (r - 0x30)));
    if (r >= 0x36 && r <= 0x39) return (uint8_t)(sys_ms() >> (8 * (r - 0x36)));   /* the wall clock, in ms */
    if (r == 0x34) return dbg_watch_ctl;
    if (r == 0x35) return dbg_watch_hits;
    if (r == 0xF0) return (uint8_t)dbg_num;
    if (r == 0xF2) return (uint8_t)dbg_auto;
    return 0xFF;
}

/* ---- the Tube ($D800) ---------------------------------------------------
 * Two transports carry the co-processor: on the desktop it is a child
 * process on a pty (BBC BASIC or RunCPM, below); with K4510_TUBE_INPROC --
 * the Pi, and the desktop test build -- it is the interpreter compiled in
 * and running on a core (or thread) of its own, through core/tube_cp.c.
 * The Tube ULA between them is the same code either way. */
#if defined(K4510_TUBE_INPROC)
#include "tube_cp.h"
static int tube_was_alive;
#elif !defined(K4510_NOPROC)
#include <pty.h>
#include <termios.h>
#ifdef __linux__
#include <sys/prctl.h>          /* PR_SET_PDEATHSIG: the child dies with the emulator */
#endif
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
static pid_t tube_pid; static int tube_fd = -1;
#endif
static uint8_t tube_ring[4096]; static unsigned tube_w, tube_r;
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
#include <math.h>
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
#if defined(K4510_TUBE_INPROC)
static void tube_pump(void)
{
    uint8_t buf[256]; int n, alive = tube_cp_alive();
    while (tube_w - tube_r < sizeof tube_ring - 600 && (n = tube_cp_read(buf, sizeof buf)) > 0)
        for (int i = 0; i < n; i++) tula_in(buf[i]);
    if (alive) tube_was_alive = 1;
    else if (tube_was_alive) { tube_was_alive = 0; tula_close(); }   /* the co-processor ended (*QUIT) */
}
static void tube_start(int prog) { tube_cp_start(prog); }             /* 1 = BBC BASIC, 3 = CP/M, both in-process */
static void tube_stop(void)
{
    tube_cp_stop();
    tube_w = tube_r = 0; tube_was_alive = 0;
    tula_close();
}
static uint8_t tube_status(void) { tube_pump(); return (tube_cp_alive() ? 1 : 0) | (tube_w != tube_r ? 0x80 : 0); }
static uint8_t tube_read(void) { tube_pump(); return tube_w != tube_r ? tube_ring[tube_r++ & 4095] : 0; }
static void tube_write(uint8_t v) { tube_cp_write(v); }
#elif !defined(K4510_NOPROC)
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
    if (!full && tube_pid && waitpid (tube_pid, NULL, WNOHANG) == tube_pid) { tube_pid = 0; close (tube_fd); tube_fd = -1; tula_close(); }
}
static void tube_start(int prog)                  /* 1 = BBC BASIC, 3 = CP/M (RunCPM), 4 = the host shell */
{
    struct winsize ws = { 29, 79, 0, 0 };
    char cmd[256] = "";
    pid_t parent = getpid ();                     /* NOT 1: in a container the emulator IS pid 1, and "getppid() == 1" then killed every child (2026-09-07) */
    if (tube_pid) return;
    if (prog == 5 && !uci_path()) return;
    if (prog == 4) {
        fs_guest_str((uint32_t)tube_cmd[0] | (uint32_t)tube_cmd[1] << 8 | (uint32_t)tube_cmd[2] << 16 | (uint32_t)tube_cmd[3] << 24, cmd, sizeof cmd);
        if (tube_rows) ws.ws_row = tube_rows;          /* the console window as the ROM has it, bands and margin taken out */
        if (tube_cols) ws.ws_col = tube_cols;
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
            setenv ("TERM", term && *term ? term : "xterm-color", 1);
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
    if (tube_pid < 0) { tube_pid = 0; tube_fd = -1; return; }
    fcntl (tube_fd, F_SETFL, O_NONBLOCK);
}
static void tube_stop(void)
{
    if (tube_pid) { kill (-tube_pid, SIGKILL); kill (tube_pid, SIGKILL); waitpid (tube_pid, NULL, 0); tube_pid = 0; }   /* the session: a `!nohup x &` too */
    if (tube_fd >= 0) { close (tube_fd); tube_fd = -1; }
    tube_w = tube_r = 0;
    tula_close();
}
static uint8_t tube_status(void) { tube_pump(); return (tube_pid ? 1 : 0) | 4 | (uci_path() ? 8 : 0) | (tube_w != tube_r ? 0x80 : 0); }
static uint8_t tube_read(void) { tube_pump(); return tube_w != tube_r ? tube_ring[tube_r++ & 4095] : 0; }
static void tube_write(uint8_t v) { if (tube_fd >= 0) { ssize_t n = write (tube_fd, &v, 1); (void) n; } }
#else
static uint8_t tube_status(void) { return 0; }
static uint8_t tube_read(void) { return 0; }
static void tube_write(uint8_t v) { (void) v; }
static void tube_start(int prog) { (void) prog; }
static void tube_stop(void) {}
#endif

/* ---- WATCH: a write watchpoint on physical RAM (core/io.h) -------------- */
uint32_t dbg_watch_addr;
uint8_t  dbg_watch_ctl, dbg_watch_hits;
void dbg_watch_hit(void)
{
    if (dbg_watch_hits != 255) dbg_watch_hits++;
    dbg_watch_ctl = 0;                       /* one bug, one dump */
    dbg_dump("watch");
}

/* ---- debug recorder and DUMP ------------------------------------------- */
#define DBG_PCS 4096
#define DBG_KEYS 256
#define DBG_LOG 8192
static uint16_t dbg_pcs[DBG_PCS]; static uint32_t dbg_pci;
static uint8_t dbg_keys[DBG_KEYS]; static uint32_t dbg_keyi;
static char dbg_log[DBG_LOG]; static uint32_t dbg_logi;
void dbg_pc(uint16_t pc) { dbg_pcs[dbg_pci++ & (DBG_PCS - 1)] = pc; }
extern int dbg_rec;
static void dbg_key(uint8_t k) { dbg_keys[dbg_keyi++ & (DBG_KEYS - 1)] = k; }
static void dbg_logc(uint8_t c) { dbg_log[dbg_logi++ & (DBG_LOG - 1)] = (char)c; }
extern uint8_t vicky_read(uint8_t r);
int dbg_dump(const char *why)
{
    char name[64]; FILE *f; time_t t = time(NULL); struct tm *m = localtime(&t);
    mkdir("dumps", 0777);
    if (++dbg_num > 100) dbg_num = 1;                  /* rotate: at most 100 files */
    snprintf(name, sizeof name, "dumps/dump-%03d.txt", dbg_num);
    if (!(f = fopen(name, "w"))) { dbg_num--; return -1; }
    fprintf(f, "K4510 dump %d  %04d-%02d-%02d %02d:%02d:%02d  (%s)\n", dbg_num, m->tm_year + 1900, m->tm_mon + 1, m->tm_mday, m->tm_hour, m->tm_min, m->tm_sec, why);
    fprintf(f, "frame %u  cwd /%s\n\n", (unsigned)sys_frames, fs_cwd);
    { uint8_t pf = cpu65_get_pf(); char fl[8]; const char *names = "NVEBDIZC";
      for (int b = 0; b < 8; b++) fl[b] = (pf & (0x80 >> b)) ? names[b] : '-';
      fprintf(f, "CPU  PC=%04X A=%02X X=%02X Y=%02X Z=%02X SP=%04X  P=%02X (%.8s)  B=%04X  inhibit=%d\n",
            cpu65.pc, cpu65.a, cpu65.x, cpu65.y, cpu65.z, cpu65.s | cpu65.sphi, pf, fl, cpu65.bphi, cpu65.cpu_inhibit_interrupts); }
    { const k4510_map_t *mp = mem_map_state();
      fprintf(f, "MAP  mask=%02X off_lo=%05X off_hi=%05X mb_lo=%X mb_hi=%X   BANKS mask=%02X", mp->mask, mp->offset_low, mp->offset_high, mp->mb_low >> 20, mp->mb_high >> 20, mem_bank_mask());
      for (int b = 0; b < 8; b++) if (mem_bank_get(b) != BANK_OFF) fprintf(f, " %d=%07X", b, mem_bank_get(b));
      fprintf(f, "   FAR table=%07X depth=%d err=%d\n", far_table, far_depth, far_err); }
    fprintf(f, "VICKY ctrl=%02X bg=%02X irqst=%02X irqmask=%02X\n", vicky_read(0), vicky_read(1), vicky_read(4), vicky_read(5));
    for (int n = 0; n < 4; n++) { fprintf(f, "  layer %d:", n); for (int i = 0; i < 16; i++) fprintf(f, " %02X", vicky_read(0x10 + n * 16 + i)); fprintf(f, "\n"); }
    fprintf(f, "  sprites=%02X sheila=%02X list=", vicky_read(0x0E), vicky_read(0x64)); for (int i = 3; i >= 0; i--) fprintf(f, "%02X", vicky_read(0x60 + i)); fprintf(f, "\n");
    fprintf(f, "FS   reg:"); for (int i = 0; i < (int)sizeof fs_reg; i++) fprintf(f, " %02X", fs_reg[i]); fprintf(f, "   DMA:"); for (int i = 0; i < 14; i++) fprintf(f, " %02X", dma_reg[i]); fprintf(f, "\n");
    fprintf(f, "MATH F0..F7:"); for (int i = 0; i < 8; i++) fprintf(f, " %g", mf_get(i)); fprintf(f, "  FI=%d flags=%02X mlstat=%02X\n", (int)m32(0x24), math_reg[0x22], math_reg[0x2D]);
    fprintf(f, "\nSCREEN (text layer at $030000, 80 columns):\n");
    for (int y = 0; y < 60; y++) { char r[81]; int last = -1; for (int x = 0; x < 80; x++) { uint8_t ch = k4510_ram[0x30000 + (y * 80 + x) * 4]; r[x] = (ch >= 0x20 && ch < 0x7F) ? ch : (ch ? '.' : ' '); if (r[x] != ' ') last = x; } r[last + 1] = 0; if (last >= 0) fprintf(f, "%2d|%s\n", y, r); }
    fprintf(f, "\nSHELL LOG (command lines and DUMP notes, oldest first):\n");
    { uint32_t n = dbg_logi < DBG_LOG ? dbg_logi : DBG_LOG, start = dbg_logi - n; for (uint32_t i = 0; i < n; i++) fputc(dbg_log[(start + i) & (DBG_LOG - 1)], f); fprintf(f, "\n"); }
    fprintf(f, "\nKEYS (last %u, oldest first, hex):", dbg_keyi < DBG_KEYS ? dbg_keyi : DBG_KEYS);
    { uint32_t n = dbg_keyi < DBG_KEYS ? dbg_keyi : DBG_KEYS, start = dbg_keyi - n; for (uint32_t i = 0; i < n; i++) { uint8_t k = dbg_keys[(start + i) & (DBG_KEYS - 1)]; if (k >= 0x20 && k < 0x7F) fprintf(f, " %c", k); else fprintf(f, " %02X", k); } fprintf(f, "\n"); }
    fprintf(f, "\nPC HISTORY (last %u opcode fetches, oldest first; runs of consecutive PCs collapsed as a-b):\n", dbg_pci < DBG_PCS ? dbg_pci : DBG_PCS);
    { uint32_t n = dbg_pci < DBG_PCS ? dbg_pci : DBG_PCS, start = dbg_pci - n; int col = 0; uint16_t run0 = 0, prev = 0; int inrun = 0;
      for (uint32_t i = 0; i <= n; i++) {
          uint16_t pc = i < n ? dbg_pcs[(start + i) & (DBG_PCS - 1)] : 0; int seq = i < n && inrun && pc > prev && pc - prev <= 3;
          if (i == 0) { run0 = pc; prev = pc; inrun = 1; continue; }
          if (seq) { prev = pc; continue; }
          if (run0 == prev) col += fprintf(f, "%04X ", run0); else col += fprintf(f, "%04X-%04X ", run0, prev);
          if (col > 90) { fprintf(f, "\n"); col = 0; }
          run0 = pc; prev = pc;
      }
      fprintf(f, "\n"); }
    fprintf(f, "\nZERO PAGE:\n"); for (int i = 0; i < 256; i += 32) { fprintf(f, "%02X:", i); for (int j = 0; j < 32; j++) fprintf(f, " %02X", k4510_ram[i + j]); fprintf(f, "\n"); }
    fprintf(f, "STACK $0100-$01FF:\n"); for (int i = 0x100; i < 0x200; i += 32) { fprintf(f, "%04X:", i); for (int j = 0; j < 32; j++) fprintf(f, " %02X", k4510_ram[i + j]); fprintf(f, "\n"); }
    fprintf(f, "$0300-$04FF (EhBASIC vectors, input buffer, K4510 glue state):\n"); for (int i = 0x300; i < 0x500; i += 32) { fprintf(f, "%04X:", i); for (int j = 0; j < 32; j++) fprintf(f, " %02X", k4510_ram[i + j]); fprintf(f, "\n"); }
    fclose(f);
    fprintf(stderr, "K4510: %s written (%s)\n", name, why);
    return dbg_num;
}

void io_reset(void)
{
    sys_frames = 0;
    tube_stop(); fs_cap = 0;                     /* a power cycle left BBC BASIC running and $D800 saying so (review 2026-09-12) */
    net_reset(); fs_remote[0] = 0; fs_cwd[0] = 0; fs_mnt_n = 0; fs_net_drop(); term_reset();   /* cwd too: a power cycle from a subdirectory came back in it, where there is no STARTUP.BAT (Doc) */
    /* The prompt starts in /HOME where the disk has one (fs/HOME/README.TXT);
     * the ROM reads /STARTUP.BAT by its absolute name, so boot is unaffected. */
    { char home[600]; struct stat sb; snprintf(home, sizeof home, "%s/HOME", fs_root);
      if (!stat(home, &sb) && S_ISDIR(sb.st_mode)) snprintf(fs_cwd, sizeof fs_cwd, "HOME"); }
    /* Off by default on BOTH now (Doc, 2026-08-27): auto-dump every 15 s was
       intrusive, and dbg_rec -- the PC recorder -- costs a store per emulated
       instruction whether or not a dump is ever written.  DUMP ON re-arms both
       when you actually want to debug; DUMP writes state on demand without it. */
    dbg_auto = 0; dbg_rec = 0;
    memset(math_reg, 0, sizeof math_reg); math_int_update();
    memset(seq_q, 0, sizeof seq_q); memset(seq_head, 0, sizeof seq_head); memset(seq_len, 0, sizeof seq_len);
    memset(seq_reg, 0, sizeof seq_reg); memset(seq_left, 0, sizeof seq_left);
    kbd_head = kbd_tail = 0; kbd_last = 0;
    memset(dma_reg, 0, sizeof dma_reg);
    vicky_reset();
    audio_reset();
}

/* ---- I/O profile: how often the CPU touches the I/O page, at what cost, and
 * where.  Read by the frontend for SYSTEM/PERF.TXT.  On the Pi the clock is
 * the ARM generic timer (a register); elsewhere only the counts are kept. */
uint32_t io_prof_reads, io_prof_writes, io_prof_hist[256];   /* hist: (addr >> 4) & 255, 16-byte groups */
uint64_t io_prof_cycles;
int io_prof_on;                                  /* set by the frontend only while its PERF window is open:
                                                  * the counters (and on the Pi two timer reads) cost every
                                                  * single I/O access, so they run only when someone is looking */
static inline uint64_t io_prof_clk(void) { return 0; }
void io_prof_reset(void) { io_prof_reads = io_prof_writes = 0; io_prof_cycles = 0; memset(io_prof_hist, 0, sizeof io_prof_hist); }
static uint8_t io_read_inner(uint16_t addr);
uint8_t io_read(uint16_t addr)
{
    if (io_prof_on) {
        uint64_t t = io_prof_clk(); uint8_t v = io_read_inner(addr);
        io_prof_cycles += io_prof_clk() - t; io_prof_reads++; io_prof_hist[(addr >> 4) & 255]++;
        return v;
    }
    return io_read_inner(addr);
}
static uint8_t io_read_inner(uint16_t addr)
{
    switch (addr & 0xFF00) {
    case IO_VICKY:
        return vicky_read(addr & 0xFF);
    case IO_SOUND:
        if (addr >= IO_FM && (addr - IO_FM) < 3) return opl2_read((uint8_t)(addr - IO_FM));   /* the OPL2: STATUS, data readback, ID */
        return 0xFF;                                                       /* $D400-$D47F: nothing there (the SIDs, until 2026-09-05) */
    case IO_SYS:
        return sys_read(addr & 0xFF);
    case IO_MATH:
        return math_read(addr & 0xFF);
    case IO_BANK: {
        uint8_t r = addr & 0xFF;
        if (r < 0x20) { uint32_t v = mem_bank_get(r >> 2) == BANK_OFF ? (mem_bank_base(r >> 2) | 0xFF000000u) : mem_bank_base(r >> 2); return (uint8_t)(v >> (8 * (r & 3))); }   /* off: base with byte 3 = $FF */
        if (r == 0x20) return mem_bank_mask();
        if (r == 0x21) return mem_map_state()->mask;
        return 0xFF;
    }
    case IO_TUBE:
        if ((addr & 0xFF) == 0) return tube_status();
        if ((addr & 0xFF) == 1) return tube_read();
        return 0xFF;
    case IO_FAR: {
        uint8_t r = addr & 0xFF;
        if (r >= 0x80 && r < 0x84) return (uint8_t)(far_table >> (8 * (r - 0x80)));
        if (r == 0x84) return far_depth;
        if (r == 0x85) return far_err;
        return 0xFF;
    }
    case IO_INPUT:
        if (addr == IO_KBD)   return kbd_read();
        if (addr == IO_KBDST) return (kbd_ready() ? 0x80 : 0x00) | (kbd_last_key ? 0x40 : 0x00)
                                   | (kbd_ready() && (kbd_fifo[kbd_head] & KBD_KEY) ? 0x20 : 0x00) | kbd_mods;
        if (addr == IO_KBDST + 1) return kbd_ready() ? (uint8_t)kbd_fifo[kbd_head] : 0;   /* peek: next key, not popped */
        if (addr == IO_KBDHELD) return menu_is_open() ? 0 : kbd_held_mask;     /* the keys down now; none while the menu has them */
        if (addr >= IO_MOUSEX && addr <= IO_MOUSEDY) {                        /* the mouse; the menu keeps its clicks */
            /* The host reports the glass (640x480); the program wants the pixels
             * of the mode VICKY is in (core/vicky.h CTRL): halve or quarter the
             * columns, halve the lines, take the 200-line field's top off. */
            uint8_t ctrl = vicky_read(0); int xs = (ctrl & 16) ? 2 : (ctrl & 2) ? 1 : 0, ys = (ctrl & 4) ? 1 : 0;
            int x = mouse_x >> xs, y = mouse_y - ((ctrl & 8) ? 40 : 0), ymax = ((ctrl & 8) ? 400 : 480) - 1;
            y = (y < 0 ? 0 : y > ymax ? ymax : y) >> ys;
            switch (addr - IO_MOUSEX) {
            case 0: return (uint8_t) x;  case 1: return (uint8_t)(x >> 8);
            case 2: return (uint8_t) y;  case 3: return (uint8_t)(y >> 8);
            case 4: return menu_is_open() ? 0 : mouse_btn;
            case 5: return menu_is_open() ? 0 : (uint8_t) mouse_wheel;
            case 6: return menu_is_open() ? 0 : (uint8_t)(mouse_dx >> xs);
            default: return menu_is_open() ? 0 : (uint8_t)(mouse_dy >> ys);
            }
        }
        if (addr == IO_KBDST + 2) {                                              /* break pending: an ESC or Ctrl-C anywhere in the queue is removed and returned */
            for (int i = kbd_head; i != kbd_tail; i = (i + 1) & 63) {
                uint8_t k = kbd_fifo[i];
                if (k == 0x03 || k == 0x1B) { for (int j = i; j != kbd_tail; j = (j + 1) & 63) kbd_fifo[j] = kbd_fifo[(j + 1) & 63]; kbd_tail = (kbd_tail + 63) & 63; return k; }
            }
            return 0;
        }
        return 0xFF;
    case IO_STORAGE:
        if ((addr & 0xFF) < sizeof fs_reg) return fs_reg[addr & 0xFF];
        if (addr == IO_FS_CAP) return fs_cap;
        return 0xFF;
    case IO_NET:
        return net_read(addr & 0xFF);
    case IO_TERM:
        return term_read(addr & 0xFF);
    case IO_DMA:
        if ((addr & 0xFF) < 16) return dma_reg[addr & 0xFF];
        return 0xFF;
    default:
        return 0xFF;
    }
}

void io_write(uint16_t addr, uint8_t v)
{
    switch (addr & 0xFF00) {
    case IO_VICKY:
        vicky_write(addr & 0xFF, v); return;
    case IO_SOUND:
        if (addr >= IO_FM && (addr - IO_FM) < 2) opl2_write((uint8_t)(addr - IO_FM), v);   /* the OPL2: ADDR, DATA */
        return;
    case IO_MATH:
        math_write(addr & 0xFF, v); return;
    case IO_SYS:
        if ((addr & 0xFF) >= 0xE0 && (addr & 0xFF) <= 0xE3) seq_write((uint8_t)((addr & 0xFF) - 0xE0), v);
        if ((addr & 0xFF) >= 0x30 && (addr & 0xFF) <= 0x33) { uint8_t sh = 8 * ((addr & 0xFF) - 0x30);
            dbg_watch_addr = (dbg_watch_addr & ~(0xFFu << sh)) | ((uint32_t)v << sh); dbg_watch_addr &= K4510_PHYS_MASK; }
        if ((addr & 0xFF) == 0x34) { dbg_watch_ctl = v ? 1 : 0; dbg_watch_hits = 0; dbg_rec = dbg_watch_ctl ? 1 : dbg_rec; }
        if ((addr & 0xFF) == 0xF0) { dbg_rec = 1; dbg_dump("DUMP register"); }
        if ((addr & 0xFF) == 0xF1) dbg_logc(v);
        if ((addr & 0xFF) == 0xF2) { dbg_auto = v ? 1 : 0; dbg_rec = dbg_auto ? 1 : dbg_rec; dbg_auto_next = sys_frames + 900; }
        if ((addr & 0xFF) == 0x23) settings_set(SET_CPU_CLOCK, v);   /* a program asks for a clock; the frontend applies it next frame (BENCH sweeps them) */
        if ((addr & 0xFF) == 0x24) { io_audio_gaps = 0; io_audio_fill = 0; }   /* any write clears both audio counts */
        if ((addr & 0xFF) == 0x28) adopt_req = 1;                     /* SETUP: keep the clock in force as this host's measured clock */
        if ((addr & 0xFF) == 0x29) measuring = v ? 1 : 0;              /* SETUP: hold the governor off while the ladder is swept */
        if ((addr & 0xFF) == 0x21) { sys_opts &= (uint8_t)~(SYSOPT_MODEREQ | SYSOPT_MODE); mode_acked = 1; }
                                   /* the guest acknowledging a video-mode request: it has performed it,
                                    * so the request goes away at once instead of standing for frames
                                    * while every key poll performs it again */
        return;
    case IO_BANK: {
        uint8_t r = addr & 0xFF, b = r >> 2, i = r & 3;
        if (r >= 0x20) return;
        { uint32_t cur = (mem_bank_base(b) & ~(0xFFu << (8 * i))) | ((uint32_t)v << (8 * i));
          if (i == 3) { if (v & 0x80) { mem_bank_setbase(b, cur); mem_bank_off(b); } else mem_bank_set(b, cur); }   /* byte 3 decides on/off */
          else mem_bank_setbase(b, cur); }                                                                           /* bytes 0-2: the base only */
        return;
    }
    case IO_TUBE:
        if ((addr & 0xFF) == 2) tube_write(v);
        if ((addr & 0xFF) == 3) { if (v == 1 || v == 3 || v == 4 || v == 5) tube_start(v); else if (v == 2) tube_stop(); }
        if ((addr & 0xFF) >= 4 && (addr & 0xFF) < 8) tube_cmd[(addr & 0xFF) - 4] = v;
        if ((addr & 0xFF) == 8) tube_rows = v;
        if ((addr & 0xFF) == 9) tube_cols = v;
        return;
    case IO_FAR: {
        uint8_t r = addr & 0xFF;
        if (r >= 0x80 && r < 0x84) { far_table = (far_table & ~(0xFFu << (8 * (r - 0x80)))) | ((uint32_t)v << (8 * (r - 0x80))); far_table &= K4510_PHYS_MASK; }
        if (r == 0x85) far_err = 0;
        return;
    }
    case IO_DMA:
        if ((addr & 0xFF) < 12) { dma_reg[addr & 0xFF] = v; return; }
        if (addr == IO_DMA_CMD) { dma_run(v); return; }
        return;
    case IO_STORAGE:
        if (addr == IO_FS_CMD) { fs_run(v); return; }
        if ((addr & 0xFF) < sizeof fs_reg) fs_reg[addr & 0xFF] = v;
        if (addr == IO_FS_CAP) fs_cap = v;
        return;
    case IO_NET:
        net_write(addr & 0xFF, v);
        return;
    case IO_TERM:
        term_write(addr & 0xFF, v);
        return;
    case IO_INPUT:
        /* Type-ahead: a write to KBD pushes a key into the queue, so a program
         * can type at the shell -- the C64's keyboard buffer, which is how a
         * program there handed a command back to BASIC.  It goes straight into
         * the FIFO, not through kbd_push: a program is not allowed to open the
         * menu, and the debugger's key log is for keys a person pressed. */
        if ((addr & 0xFF) == 0) kbd_enqueue(v);
        return;
    default:
        return;
    }
}

/* ---- save states (core/state.h) ------------------------------------------ */
#include "state.h"
void io_state_save(FILE *f)
{
    state_put(f, "SYSF", &sys_frames, sizeof sys_frames);
    state_put(f, "KBDQ", kbd_fifo, sizeof kbd_fifo);
    state_put(f, "KBDH", &kbd_head, sizeof kbd_head);
    state_put(f, "KBDT", &kbd_tail, sizeof kbd_tail);
    state_put(f, "FSCW", fs_cwd, sizeof fs_cwd);
    state_put(f, "FSRG", fs_reg, sizeof fs_reg);
    state_put(f, "DMA ", dma_reg, sizeof dma_reg);
    state_put(f, "MATH", math_reg, sizeof math_reg);
    state_put(f, "SYSR", sys_reg, sizeof sys_reg);
    state_put(f, "SEQQ", seq_q, sizeof seq_q);
    state_put(f, "SEQH", seq_head, sizeof seq_head);
    state_put(f, "SEQL", seq_len, sizeof seq_len);
    state_put(f, "SEQR", seq_reg, sizeof seq_reg);
    state_put(f, "SEQF", seq_left, sizeof seq_left);
    state_put(f, "TULX", tula_x, sizeof tula_x); state_put(f, "TULY", tula_y, sizeof tula_y);
    state_put(f, "TULO", &tula_ox, sizeof tula_ox); state_put(f, "TULP", &tula_oy, sizeof tula_oy);
    state_put(f, "TULF", &tula_fg, 1); state_put(f, "TULB", &tula_bg, 1); state_put(f, "TULN", &tula_on, 1);
    state_put(f, "TSPC", &tula_spr_cur, sizeof tula_spr_cur); state_put(f, "TSPO", &tula_spr_on, sizeof tula_spr_on);
    state_put(f, "TSPW", tula_spr_w, sizeof tula_spr_w); state_put(f, "TSPH", tula_spr_h, sizeof tula_spr_h);
}
int io_state_load(FILE *f)
{
    if (state_get(f, "SYSF", &sys_frames, sizeof sys_frames) || state_get(f, "KBDQ", kbd_fifo, sizeof kbd_fifo)
        || state_get(f, "KBDH", &kbd_head, sizeof kbd_head) || state_get(f, "KBDT", &kbd_tail, sizeof kbd_tail)
        || state_get(f, "FSCW", fs_cwd, sizeof fs_cwd) || state_get(f, "FSRG", fs_reg, sizeof fs_reg)
        || state_get(f, "DMA ", dma_reg, sizeof dma_reg) || state_get(f, "MATH", math_reg, sizeof math_reg)
        || state_get(f, "SYSR", sys_reg, sizeof sys_reg)
        || state_get(f, "SEQQ", seq_q, sizeof seq_q) || state_get(f, "SEQH", seq_head, sizeof seq_head)
        || state_get(f, "SEQL", seq_len, sizeof seq_len) || state_get(f, "SEQR", seq_reg, sizeof seq_reg) || state_get(f, "SEQF", seq_left, sizeof seq_left)
        || state_get(f, "TULX", tula_x, sizeof tula_x) || state_get(f, "TULY", tula_y, sizeof tula_y)
        || state_get(f, "TULO", &tula_ox, sizeof tula_ox) || state_get(f, "TULP", &tula_oy, sizeof tula_oy)
        || state_get(f, "TULF", &tula_fg, 1) || state_get(f, "TULB", &tula_bg, 1) || state_get(f, "TULN", &tula_on, 1)
        || state_get(f, "TSPC", &tula_spr_cur, sizeof tula_spr_cur) || state_get(f, "TSPO", &tula_spr_on, sizeof tula_spr_on)
        || state_get(f, "TSPW", tula_spr_w, sizeof tula_spr_w) || state_get(f, "TSPH", tula_spr_h, sizeof tula_spr_h)) return -2;
    /* the file closed, the network dropped, the Tube stopped.  The OPL2's
     * registers are not carried: a state loads with the chip reset, and the
     * sequencer's playing notes re-sound as their queues advance. */
    math_int_update();
    kbd_head %= 64; kbd_tail %= 64; fs_cwd[sizeof fs_cwd - 1] = 0;        /* a corrupt .k4s must not index out of bounds */
    for (int c = 0; c < 4; c++) { seq_head[c] %= SEQ_DEPTH; if (seq_len[c] > SEQ_DEPTH) seq_len[c] = SEQ_DEPTH; }
    if (fs_file) { fclose(fs_file); fs_file = 0; }
    fs_net_drop(); fs_remote[0] = 0; fs_mnt_n = 0; net_reset(); tube_stop(); fs_cap = 0;
    return 0;
}
