/* core/sidebars.c -- the sidebars the machine has.  See sidebars.h. */
#include "sidebars.h"
#include "zip.h"
#include "ui/settings.h"
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

/* the emulator's own drawings, by the names draw = builtin NAME uses */
static const char *const builtin_keys[SIDEBAR_COUNT] = {
    "border", "gradient", "knot", "registers", "halloween", "christmas", "space", "river", "dreamfall", "tetris", "antfarm", "matrix", "doom" };
#define MAXSB 64
static sidebar_info list[MAXSB];
static const char *labels[MAXSB];
static int n;
static char root[1024] = "fs";
typedef struct { char k[24], v[64]; } kv_t;
typedef struct { kv_t kv[24]; int n; time_t mtime; off_t size; int ready; } cfg_t;
static cfg_t opts[MAXSB], gcfg;                  /* each sidebar's OPTIONS.CFG; SIDEBARS.CFG */

static int builtin_of(const char *s)
{
    for (int i = 0; i < SIDEBAR_COUNT; i++) if (!strcasecmp(s, builtin_keys[i])) return i;
    return -1;
}
static char *trim(char *s)
{
    size_t l;
    while (*s == ' ' || *s == '\t') s++;
    l = strlen(s);
    while (l && (s[l - 1] == ' ' || s[l - 1] == '\t' || s[l - 1] == '\r')) s[--l] = 0;
    return s;
}
/* SIDEBAR.INF: name = value lines, # comments.  0 when it has a name and a
 * drawing this emulator knows. */
static int parse_inf(const uint8_t *text, uint32_t len, sidebar_info *si)
{
    char *buf = malloc(len + 1), *line, *save = NULL;
    if (!buf) return -1;
    memcpy(buf, text, len); buf[len] = 0;
    si->builtin = -1;
    for (line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *h = strchr(line, '#'), *eq, *k, *v;
        if (h) *h = 0;
        if (!(eq = strchr(line, '='))) continue;
        *eq = 0; k = trim(line); v = trim(eq + 1);
        if (!strcmp(k, "name")) snprintf(si->name, sizeof si->name, "%s", v);
        else if (!strcmp(k, "about")) snprintf(si->about, sizeof si->about, "%s", v);
        else if (!strcmp(k, "version")) si->version = atoi(v);
        else if (!strcmp(k, "game")) snprintf(si->game, sizeof si->game, "%s", v);
        else if (!strcmp(k, "season")) {
            for (char *p = v; *p; ) { int m = (int) strtol(p, &p, 10); if (m >= 1 && m <= 12) si->months |= 1u << (m - 1); while (*p && !isdigit((unsigned char) *p)) p++; }
        } else if (!strcmp(k, "draw") && !strncmp(v, "builtin", 7) && (v[7] == ' ' || v[7] == '\t'))
            si->builtin = builtin_of(trim(v + 8));
    }
    free(buf);
    return si->name[0] && si->builtin >= 0 ? 0 : -1;
}
/* the menu's order: the emulator's own drawings as they always came, then by name */
static int by_builtin(const void *a, const void *b)
{
    const sidebar_info *x = a, *y = b;
    return x->builtin != y->builtin ? x->builtin - y->builtin : strcmp(x->key, y->key);
}
static int read_one(const char *path, const char *fname, sidebar_info *si)
{
    FILE *f = fopen(path, "rb"); long sz; uint8_t *b, *inf; uint32_t il; zip_t *z; int st;
    size_t kl = strlen(fname) - 4;
    if (!f) return -1;
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > (64L << 20) || !(b = malloc((size_t) sz))) { fclose(f); return -1; }
    if (fread(b, 1, (size_t) sz, f) != (size_t) sz) { fclose(f); free(b); return -1; }
    fclose(f);
    if (zip_open_mem(b, (uint32_t) sz, &z)) return -1;          /* b is the zip's now, or freed */
    st = zip_fetch(z, "SIDEBAR.INF", &inf, &il);
    zip_close(z);
    if (st) return -1;
    memset(si, 0, sizeof *si);
    for (size_t i = 0; i < kl && i < sizeof si->key - 1; i++) si->key[i] = (char) tolower((unsigned char) fname[i]);
    snprintf(si->file, sizeof si->file, "%s", fname);
    st = parse_inf(inf, il, si);
    free(inf);
    return st;
}
int sidebars_scan(const char *fsroot)
{
    char dir[1100], path[1400]; DIR *d; struct dirent *e; int found = 0;
    snprintf(root, sizeof root, "%s", fsroot ? fsroot : "fs");
    snprintf(dir, sizeof dir, "%s/SYSTEM/SIDEBARS", root);
    if (!(d = opendir(dir))) return n;
    while ((e = readdir(d)) && found < MAXSB) {
        size_t l = strlen(e->d_name);
        if (l < 5 || l - 4 >= sizeof list[0].key || strcasecmp(e->d_name + l - 4, ".zip")) continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        if (!read_one(path, e->d_name, &list[found])) found++;
    }
    closedir(d);
    if (!found) return n;                   /* nothing readable: the built-in names stand */
    n = found;
    memset(opts, 0, sizeof opts); memset(&gcfg, 0, sizeof gcfg);
    qsort(list, (size_t) n, sizeof *list, by_builtin);
    for (int i = 0; i < n; i++) labels[i] = list[i].key;
    { int def = sidebars_find("border"); settings_set_labels(SET_VIDEO_SIDEBARS, labels, n, def >= 0 ? def : 0); }
    sidebars_poll();                        /* SIDEBARS.CFG, if there is one already */
    return n;
}
int sidebars_count(void) { return n; }
const sidebar_info *sidebars_info(int i) { return i >= 0 && i < n ? &list[i] : NULL; }
int sidebars_find(const char *key)
{
    for (int i = 0; i < n; i++) if (!strcasecmp(list[i].key, key)) return i;
    return -1;
}
int sidebars_builtin(int v)
{
    if (!n) return v >= 0 && v < SIDEBAR_COUNT ? v : SIDEBAR_BORDER;   /* no zips: the value is the builtin */
    return v >= 0 && v < n ? list[v].builtin : SIDEBAR_BORDER;
}

/* ---- what changes: the options, SIDEBARS.CFG, STATE.DAT (step 5) ---------- */
static const char gcfg_default[] =
    "# /SYSTEM/SIDEBARS/SIDEBARS.CFG -- for all the sidebars (docs/SIDEBAR-FORMAT.md).\n"
    "# Which one is shown is F12 -> Video -> Sidebar; each one's own options are\n"
    "# in its folder here, KEY/OPTIONS.CFG (F12 -> Video -> Edit options).\n"
    "\n"
    "right   = same     # the right-hand side: same, or another sidebar's name (tetris)\n"
    "change  = never    # never, or how often the next one comes: 10m, 1h, 1d\n"
    "seasons = on       # on: while changing, halloween only in October, christmas in December\n";

static void upper_key(int i, char *buf, size_t sz)
{
    size_t k = 0;
    for (; list[i].key[k] && k < sz - 1; k++) buf[k] = (char) toupper((unsigned char) list[i].key[k]);
    buf[k] = 0;
}
static void kv_parse(char *text, cfg_t *c)
{
    char *line, *save = NULL;
    c->n = 0;
    for (line = strtok_r(text, "\n", &save); line && c->n < (int)(sizeof c->kv / sizeof *c->kv); line = strtok_r(NULL, "\n", &save)) {
        char *h = strchr(line, '#'), *eq, *k, *v;
        if (h) *h = 0;
        if (!(eq = strchr(line, '='))) continue;
        *eq = 0; k = trim(line); v = trim(eq + 1);
        if (!*k) continue;
        snprintf(c->kv[c->n].k, sizeof c->kv[0].k, "%s", k); snprintf(c->kv[c->n].v, sizeof c->kv[0].v, "%s", v);
        c->n++;
    }
}
static const char *kv_get(const cfg_t *c, const char *k)
{
    for (int i = c->n - 1; i >= 0; i--) if (!strcasecmp(c->kv[i].k, k)) return c->kv[i].v;   /* the last one given wins */
    return NULL;
}
/* read PATH into C if it changed since last time; 1 when it was read */
static int cfg_refresh(const char *path, cfg_t *c)
{
    struct stat sb; FILE *f; char *b; long sz;
    if (stat(path, &sb)) { int had = c->ready && c->n; c->n = 0; c->ready = 1; c->mtime = 0; c->size = -1; return had; }
    if (c->ready && sb.st_mtime == c->mtime && sb.st_size == c->size) return 0;
    c->mtime = sb.st_mtime; c->size = sb.st_size; c->ready = 1; c->n = 0;
    if (!(f = fopen(path, "rb"))) return 1;
    sz = sb.st_size > 65536 ? 65536 : (long) sb.st_size;
    if ((b = malloc((size_t) sz + 1))) { size_t got = fread(b, 1, (size_t) sz, f); b[got] = 0; kv_parse(b, c); free(b); }
    fclose(f);
    return 1;
}
static int write_file(const char *path, const void *b, size_t len)
{
    char tmp[1400]; FILE *f;
    snprintf(tmp, sizeof tmp, "%s.NEW", path);
    if (!(f = fopen(tmp, "wb"))) return -1;
    if (fwrite(b, 1, len, f) != len) { fclose(f); remove(tmp); return -1; }
    if (fclose(f) || rename(tmp, path)) { remove(tmp); return -1; }
    return 0;
}
static void gcfg_path(char *buf, size_t sz) { snprintf(buf, sz, "%s/SYSTEM/SIDEBARS/SIDEBARS.CFG", root); }
static void side_path(int i, const char *leaf, char *buf, size_t sz)
{
    char up[24]; upper_key(i, up, sizeof up);
    snprintf(buf, sz, "%s/SYSTEM/SIDEBARS/%s%s%s", root, up, leaf ? "/" : "", leaf ? leaf : "");
}
int sidebars_prepare(int i)
{
    char p[1400]; struct stat sb;
    if (i < 0 || i >= n) return -1;
    gcfg_path(p, sizeof p);
    if (stat(p, &sb)) { write_file(p, gcfg_default, sizeof gcfg_default - 1); cfg_refresh(p, &gcfg); }
    side_path(i, NULL, p, sizeof p);
    mkdir(p, 0777);
    side_path(i, "OPTIONS.CFG", p, sizeof p);
    if (stat(p, &sb)) {                             /* the first time: the zip's own copy, the defaults explained */
        char zp[1400]; FILE *f; long sz; uint8_t *b = NULL, *o; uint32_t ol; zip_t *z;
        snprintf(zp, sizeof zp, "%s/SYSTEM/SIDEBARS/%s", root, list[i].file);
        if ((f = fopen(zp, "rb"))) {
            fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
            if (sz > 0 && sz <= (64L << 20) && (b = malloc((size_t) sz)) && fread(b, 1, (size_t) sz, f) == (size_t) sz) {
                fclose(f); f = NULL;
                if (!zip_open_mem(b, (uint32_t) sz, &z)) {
                    if (!zip_fetch(z, "OPTIONS.CFG", &o, &ol)) { write_file(p, o, ol); free(o); }
                    zip_close(z);
                }
            } else free(b);
            if (f) fclose(f);
        }
        if (stat(p, &sb)) { static const char none[] = "# no options of its own\n"; write_file(p, none, sizeof none - 1); }
    }
    cfg_refresh(p, &opts[i]);
    return 0;
}
int sidebars_poll(void)
{
    char p[1400]; int changed = 0;
    gcfg_path(p, sizeof p); changed |= cfg_refresh(p, &gcfg);
    for (int i = 0; i < n; i++) if (opts[i].ready) { side_path(i, "OPTIONS.CFG", p, sizeof p); changed |= cfg_refresh(p, &opts[i]); }
    return changed;
}
const char *sidebars_opt(int i, const char *key) { return i >= 0 && i < n ? kv_get(&opts[i], key) : NULL; }
const char *sidebars_cfg(const char *key) { return kv_get(&gcfg, key); }
double sidebars_speed(int i)
{
    const char *v = sidebars_opt(i, "speed"); char *e; double d;
    if (!v) return 1.0;
    d = strtod(v, &e);
    if (e == v || d <= 0) return 1.0;               /* not a number: as designed */
    return d < 0.25 ? 0.25 : d > 4.0 ? 4.0 : d;
}
long sidebars_seconds(const char *s)
{
    char *e; double v;
    if (!s) return 0;
    v = strtod(s, &e);
    if (e == s || v <= 0) return 0;                 /* never, real, nothing */
    while (*e == ' ') e++;
    switch (tolower((unsigned char) *e)) { case 'm': v *= 60; break; case 'h': v *= 3600; break; case 'd': v *= 86400; break; default: break; }
    return (long) v;
}
int sidebars_shown(int v, int side, long now, int month)
{
    const char *right = sidebars_cfg("right"), *seasons = sidebars_cfg("seasons");
    long per = sidebars_seconds(sidebars_cfg("change"));
    int el[MAXSB], k = 0;
    if (!n || v < 0 || v >= n || list[v].builtin == SIDEBAR_REGISTERS) return v;   /* the register panel stays put */
    if (side == 1 && right && *right && strcasecmp(right, "same")) {
        int r = sidebars_find(right);
        if (r >= 0 && list[r].builtin != SIDEBAR_REGISTERS) return r;
    }
    if (per <= 0) return v;
    for (int i = 0; i < n; i++) {
        if (list[i].builtin == SIDEBAR_REGISTERS) continue;
        if (list[i].game[0]) continue;                       /* a gamebar belongs to its game: chosen by hand or shown with it, never drawn from the hat */
        if (!(seasons && !strcasecmp(seasons, "off")) && list[i].months && month >= 1 && month <= 12 && !(list[i].months & (1u << (month - 1)))) continue;
        el[k++] = i;
    }
    return k ? el[(now / per) % k] : v;
}
void sidebars_options_path(int i, char *buf, size_t sz)
{
    char up[24];
    if (i < 0 || i >= n) { snprintf(buf, sz, "/SYSTEM/SIDEBARS/SIDEBARS.CFG"); return; }
    upper_key(i, up, sizeof up);
    snprintf(buf, sz, "/SYSTEM/SIDEBARS/%s/OPTIONS.CFG", up);
}
uint8_t *sidebars_state_read(int i, size_t *len)
{
    char p[1400], line[160], key[24] = ""; int ver = -1; size_t sz = 0; FILE *f; uint8_t *b;
    *len = 0;
    if (i < 0 || i >= n) return NULL;
    side_path(i, "STATE.DAT", p, sizeof p);
    if (!(f = fopen(p, "rb"))) return NULL;
    if (!fgets(line, sizeof line, f) || sscanf(line, "K4510 sidebar state: %23s %d %zu", key, &ver, &sz) != 3
        || strcmp(key, list[i].key) || ver != list[i].version || sz == 0 || sz > (64u << 20)) {
        char old[1400];                              /* another version's: kept, out of the way, never read */
        fclose(f); side_path(i, "STATE.OLD", old, sizeof old); rename(p, old);
        return NULL;
    }
    if (!(b = malloc(sz)) || fread(b, 1, sz, f) != sz) { free(b); fclose(f); return NULL; }
    fclose(f);
    *len = sz;
    return b;
}
int sidebars_state_write(int i, const uint8_t *buf, size_t len)
{
    char p[1400], hdr[160]; uint8_t *all; int hl, st;
    if (i < 0 || i >= n || !buf || !len) return -1;
    side_path(i, NULL, p, sizeof p); mkdir(p, 0777);
    side_path(i, "STATE.DAT", p, sizeof p);
    hl = snprintf(hdr, sizeof hdr, "K4510 sidebar state: %s %d %zu\n", list[i].key, list[i].version, len);
    if (!(all = malloc((size_t) hl + len))) return -1;
    memcpy(all, hdr, (size_t) hl); memcpy(all + hl, buf, len);
    st = write_file(p, all, (size_t) hl + len);
    free(all);
    return st;
}
