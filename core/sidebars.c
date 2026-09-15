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

/* the emulator's own drawings, by the names draw = builtin NAME uses */
static const char *const builtin_keys[SIDEBAR_COUNT] = {
    "border", "gradient", "knot", "registers", "halloween", "christmas", "space", "river", "dreamfall", "tetris", "antfarm" };
#define MAXSB 64
static sidebar_info list[MAXSB];
static const char *labels[MAXSB];
static int n;

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
    st = parse_inf(inf, il, si);
    free(inf);
    return st;
}
int sidebars_scan(const char *fsroot)
{
    char dir[1024], path[1400]; DIR *d; struct dirent *e; int found = 0;
    snprintf(dir, sizeof dir, "%s/SYSTEM/SIDEBARS", fsroot ? fsroot : "fs");
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
    qsort(list, (size_t) n, sizeof *list, by_builtin);
    for (int i = 0; i < n; i++) labels[i] = list[i].key;
    { int def = sidebars_find("border"); settings_set_labels(SET_VIDEO_SIDEBARS, labels, n, def >= 0 ? def : 0); }
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
