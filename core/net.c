/* The network: URLs, the TNFS client, the N: device. Platform underneath:
 * net_plat.h. See net.h for the picture. */
#include "net.h"
#include "net_plat.h"
#include "mem.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

/* Skip an optional FujiNet device prefix: N:TNFS://host is the same address as
 * tnfs://host.  FujiNet (and the 8-bit world) writes every network address as
 * N:PROTOCOL://...; K4510 accepts that spelling as well as the bare scheme,
 * so its docs and muscle memory carry over (the FujiNet Guide to Operations,
 * Doc, 2026-09-10). */
static const char *net_deprefix(const char *s)
{
    /* N:  N1:..N8:  (the FujiNet device name and optional unit/channel digit);
     * the firmware strips it before URL parsing and so do we (channels are one
     * thing on K4510, so the digit is accepted and ignored). */
    if ((s[0] == 'N' || s[0] == 'n')) {
        if (s[1] == ':') return s + 2;
        if (s[1] >= '1' && s[1] <= '8' && s[2] == ':') return s + 3;
    }
    return s;
}
int net_is_url(const char *name)
{
    name = net_deprefix(name);
    return !strncasecmp(name, "http://", 7) || !strncasecmp(name, "https://", 8) || !strncasecmp(name, "tnfs://", 7);
}

/* ---- URLs ---------------------------------------------------------------- */
typedef struct { char scheme[8], user[64], pass[64], host[128], path[512]; int port; } url_t;
static int url_parse(const char *s, url_t *u)
{
    const char *p, *e, *c; size_t n;
    s = net_deprefix(s);
    p = strstr(s, "://");
    if (!p) return -1;
    n = (size_t)(p - s); if (n >= sizeof u->scheme) return -1;
    memcpy(u->scheme, s, n); u->scheme[n] = 0;
    for (size_t i = 0; i < n; i++) u->scheme[i] = (char) ((u->scheme[i] | 0x20));
    p += 3;
    u->user[0] = u->pass[0] = 0;
    e = strchr(p, '/'); if (!e) e = p + strlen(p);
    { const char *at = memchr(p, '@', (size_t)(e - p)); const char *co;   /* user:password@ (Chapter 2.3) */
      if (at) {
          co = memchr(p, ':', (size_t)(at - p));
          { size_t un = (size_t)((co ? co : at) - p); if (un >= sizeof u->user) un = sizeof u->user - 1; memcpy(u->user, p, un); u->user[un] = 0; }
          if (co) { size_t pn = (size_t)(at - co - 1); if (pn >= sizeof u->pass) pn = sizeof u->pass - 1; memcpy(u->pass, co + 1, pn); u->pass[pn] = 0; }
          p = at + 1;
      } }
    n = (size_t)(e - p); if (n >= sizeof u->host) return -1;
    memcpy(u->host, p, n); u->host[n] = 0;
    u->port = !strcmp(u->scheme, "tnfs") ? 16384 : !strcmp(u->scheme, "https") ? 443 : 80;
    if ((c = strrchr(u->host, ':')) && c[1] >= '0' && c[1] <= '9') { u->port = atoi(c + 1); *((char *) c) = 0; }
    snprintf(u->path, sizeof u->path, "%s", *e ? e : "/");
    return 0;
}
/* base URL + a relative name (or an absolute /path, or another URL), with . and .. folded */
void net_url_join(char *out, size_t max, const char *base, const char *name)
{
    url_t u; char path[512]; size_t n;
    if (net_is_url(name)) { snprintf(out, max, "%s", name); return; }
    if (url_parse(base, &u)) { snprintf(out, max, "%s", name); return; }
    if (name[0] == '/') snprintf(path, sizeof path, "%s", name);
    else snprintf(path, sizeof path, "%.300s/%.200s", u.path, name);
    /* fold */
    { char tmp[512]; size_t o = 0; const char *s = path;
      while (*s) {
          while (*s == '/') s++;
          if (!*s) break;
          const char *e = s; while (*e && *e != '/') e++;
          n = (size_t)(e - s);
          if (n == 1 && s[0] == '.') { }
          else if (n == 2 && s[0] == '.' && s[1] == '.') { while (o && tmp[o - 1] != '/') o--; if (o) o--; }
          else { if (o + n + 2 < sizeof tmp) { tmp[o++] = '/'; memcpy(tmp + o, s, n); o += n; } }
          s = e;
      }
      tmp[o] = 0;
      snprintf(out, max, "%s://%s:%d%s", u.scheme, u.host, u.port, o ? tmp : "/"); }
}

/* ---- TNFS ---------------------------------------------------------------- *
 * Datagrams: session (2, LE) sequence (1) command (1) then data; replies
 * carry a status byte first (0 ok, 0x21 EOF). One session per server, kept
 * open; a lost reply is retried three times. */
typedef struct { char host[128]; int port; int h; uint16_t sid; uint8_t seq; int up; unsigned last; } tnfs_t;
static tnfs_t sess[4]; static int sess_n;
#define TNFS_TO 1500
#define TNFS_MAX 1024

static int tnfs_xfer(tnfs_t *t, uint8_t cmd, const uint8_t *req, int reqn, uint8_t *rep, int repmax)
{
    uint8_t pkt[TNFS_MAX + 4]; int n;
    for (int retry = 0; retry < 3; retry++) {
        pkt[0] = t->sid & 255; pkt[1] = t->sid >> 8; pkt[2] = t->seq; pkt[3] = cmd;
        memcpy(pkt + 4, req, (size_t) reqn);
        if (plat_udp_send(t->h, pkt, reqn + 4) < 0) return -1;
        for (;;) {
            n = plat_udp_recv(t->h, pkt, sizeof pkt, TNFS_TO);
            if (n <= 0) break;
            if (n >= 4 && pkt[2] == t->seq && pkt[3] == cmd) {
                t->seq++;
                if (cmd == 0x00) t->sid = pkt[0] | (pkt[1] << 8);
                n -= 4; if (n > repmax) n = repmax;
                memcpy(rep, pkt + 4, (size_t) n);
                return n;
            }
        }
        if (n < 0) return -1;
    }
    return -1;
}
static tnfs_t *tnfs_session(const url_t *u)
{
    tnfs_t *t = NULL; uint8_t req[160], rep[16]; int n, k;
    for (int i = 0; i < sess_n; i++) if (!strcasecmp(sess[i].host, u->host) && sess[i].port == u->port) t = &sess[i];
    if (t && t->up) return t;
    if (!t) { if (sess_n == 4) { sess_n = 0; }   /* the oldest goes */
              t = &sess[sess_n++]; memset(t, 0, sizeof *t); snprintf(t->host, sizeof t->host, "%s", u->host); t->port = u->port; t->h = -1; }
    if (t->h < 0 && (t->h = plat_udp_open(u->host, u->port)) < 0) return NULL;
    /* version 1.0, mount "/", then user and password (Chapter 2.3 -- empty for
     * a public server, filled from user:password@ in the devicespec) */
    req[0] = 0x00; req[1] = 0x01; req[2] = '/'; req[3] = 0; k = 4;
    { const char *c;
      for (c = u->user; *c && k < 100; c++) req[k++] = (uint8_t)*c;
      req[k++] = 0;
      for (c = u->pass; *c && k < 156; c++) req[k++] = (uint8_t)*c;
      req[k++] = 0; }
    t->sid = 0; t->seq = 0;
    n = tnfs_xfer(t, 0x00, req, k, rep, sizeof rep);
    if (n < 1 || rep[0] != 0) return NULL;
    t->up = 1;
    return t;
}
static void tnfs_path(const url_t *u, char *out, size_t max)   /* the server wants an absolute path; a trailing slash is not part of a name */
{
    size_t n; snprintf(out, max, "%s", u->path[0] ? u->path : "/");
    n = strlen(out); while (n > 1 && out[n - 1] == '/') out[--n] = 0;
}
static int tnfs_stat(tnfs_t *t, const char *path, uint32_t *size, int *isdir)
{
    uint8_t rep[64]; int n = tnfs_xfer(t, 0x24, (const uint8_t *) path, (int) strlen(path) + 1, rep, sizeof rep);
    if (n < 1) return 2;
    if (rep[0]) return 1;
    if (n < 21) return 2;
    *isdir = (rep[1] | (rep[2] << 8)) & 0x4000 ? 1 : 0;
    *size = rep[7] | (rep[8] << 8) | (rep[9] << 16) | ((uint32_t) rep[10] << 24);
    return 0;
}
static int tnfs_fetch(const url_t *u, uint8_t **buf, uint32_t *len)
{
    tnfs_t *t = tnfs_session(u); char path[512]; uint8_t req[520], rep[TNFS_MAX]; int n, fd;
    uint32_t cap = 65536, got = 0; uint8_t *b;
    if (!t) return 6;
    tnfs_path(u, path, sizeof path);
    req[0] = 0x01; req[1] = 0x00; req[2] = 0; req[3] = 0;               /* O_RDONLY, mode 0 */
    n = (int) strlen(path) + 1; memcpy(req + 4, path, (size_t) n);
    n = tnfs_xfer(t, 0x29, req, n + 4, rep, sizeof rep);
    if (n < 1) return 2;
    if (rep[0]) return 1;
    fd = rep[1];
    if (!(b = malloc(cap))) return 2;
    for (;;) {
        req[0] = (uint8_t) fd; req[1] = 512 & 255; req[2] = 512 >> 8;
        n = tnfs_xfer(t, 0x21, req, 3, rep, sizeof rep);
        if (n < 1) { free(b); return 2; }
        if (rep[0] == 0x21) break;                                        /* EOF */
        if (rep[0] || n < 3) { free(b); return 2; }
        { int k = rep[1] | (rep[2] << 8); if (k > n - 3) k = n - 3;
          if (got + (uint32_t) k > cap) { uint8_t *nb; cap *= 2; if (!(nb = realloc(b, cap))) { free(b); return 2; } b = nb; }
          memcpy(b + got, rep + 3, (size_t) k); got += (uint32_t) k;
          if (k == 0) break; }
    }
    req[0] = (uint8_t) fd; tnfs_xfer(t, 0x23, req, 1, rep, sizeof rep);
    *buf = b; *len = got;
    return 0;
}
static int ent_cmp(const void *a, const void *b)
{
    const net_dirent *x = a, *y = b;
    if (x->isdir != y->isdir) return y->isdir - x->isdir;
    return strcasecmp(x->name, y->name);
}
static int tnfs_listdir(const url_t *u, net_dirent **ents, int *count)
{
    tnfs_t *t = tnfs_session(u); char path[512]; uint8_t req[8], rep[TNFS_MAX]; int n, h, cap = 64, cnt = 0;
    net_dirent *e;
    if (!t) return 6;
    tnfs_path(u, path, sizeof path);
    n = tnfs_xfer(t, 0x10, (const uint8_t *) path, (int) strlen(path) + 1, rep, sizeof rep);
    if (n < 1) return 2;
    if (rep[0]) return 1;
    h = rep[1];
    if (!(e = malloc(sizeof *e * (size_t) cap))) return 2;
    for (;;) {
        req[0] = (uint8_t) h;
        n = tnfs_xfer(t, 0x11, req, 1, rep, sizeof rep);
        if (n < 1) { free(e); return 2; }
        if (rep[0]) break;                                                /* 0x21: no more */
        rep[n < (int) sizeof rep ? n : (int) sizeof rep - 1] = 0;
        if (!strcmp((char *) rep + 1, ".") || !strcmp((char *) rep + 1, "..")) continue;
        if (cnt == cap) { net_dirent *ne; cap *= 2; if (!(ne = realloc(e, sizeof *e * (size_t) cap))) { free(e); return 2; } e = ne; }
        memset(&e[cnt], 0, sizeof e[cnt]);
        snprintf(e[cnt].name, sizeof e[cnt].name, "%.63s", (char *) rep + 1);
        { char full[1024]; uint32_t sz = 0; int d = 0;
          snprintf(full, sizeof full, "%s%s%s", path, path[strlen(path) - 1] == '/' ? "" : "/", e[cnt].name);
          if (!tnfs_stat(t, full, &sz, &d)) { e[cnt].size = sz; e[cnt].isdir = d; } }
        cnt++;
        if (cnt >= 512) break;
    }
    req[0] = (uint8_t) h; tnfs_xfer(t, 0x12, req, 1, rep, sizeof rep);
    qsort(e, (size_t) cnt, sizeof *e, ent_cmp);
    *ents = e; *count = cnt;
    return 0;
}

/* ---- the shared front ---------------------------------------------------- */
int net_fetch(const char *url, uint8_t **buf, uint32_t *len)
{
    url_t u;
    if (!plat_net_ready()) return 6;
    if (url_parse(url, &u)) return 1;
    if (!strcmp(u.scheme, "tnfs")) return tnfs_fetch(&u, buf, len);
    if (!strcmp(u.scheme, "http") || !strcmp(u.scheme, "https")) return plat_http_fetch(net_deprefix(url), buf, len);
    return 1;
}
int net_listdir(const char *url, net_dirent **ents, int *n)
{
    url_t u;
    if (!plat_net_ready()) return 6;
    if (url_parse(url, &u) || strcmp(u.scheme, "tnfs")) return 1;
    return tnfs_listdir(&u, ents, n);
}
int net_isdir(const char *url)
{
    url_t u; tnfs_t *t; char path[512]; uint32_t sz; int d = 0;
    if (!plat_net_ready()) return -1;
    if (url_parse(url, &u) || strcmp(u.scheme, "tnfs")) return 0;
    if (!(t = tnfs_session(&u))) return -1;
    tnfs_path(&u, path, sizeof path);
    if (!strcmp(path, "/")) return 1;
    return tnfs_stat(t, path, &sz, &d) ? 0 : d;
}

/* ---- the N: device ------------------------------------------------------- */
enum { CH_NONE, CH_TCP, CH_BUF };
static struct chan { int type, h, eof; uint8_t *buf; uint32_t len, pos; } ch[4];
static uint8_t net_reg[0x14];
static uint32_t rd32r(int off) { return net_reg[off] | (net_reg[off + 1] << 8) | (net_reg[off + 2] << 16) | ((uint32_t) net_reg[off + 3] << 24); }
static void wr32r(int off, uint32_t v) { for (int i = 0; i < 4; i++) net_reg[off + i] = (v >> (8 * i)) & 0xFF; }

static void chan_close(struct chan *c)
{
    if (c->type == CH_TCP) plat_tcp_close(c->h);
    free(c->buf);
    memset(c, 0, sizeof *c); c->h = -1;
}
void net_reset(void)
{
    for (int i = 0; i < 4; i++) { if (ch[i].type) chan_close(&ch[i]); ch[i].h = -1; }
    for (int i = 0; i < sess_n; i++) { if (sess[i].h >= 0) plat_udp_close(sess[i].h); }
    sess_n = 0;
    memset(net_reg, 0, sizeof net_reg);
}
static int guest_str(uint32_t p, char *s, size_t max)
{
    size_t n = 0; p &= K4510_PHYS_MASK;
    for (; n < max - 1; n++) { s[n] = (char) k4510_ram[(p + n) & K4510_PHYS_MASK]; if (!s[n]) break; }
    if (n >= max - 1) return 5;
    return 0;
}
static void net_run(uint8_t cmd)
{
    struct chan *c = &ch[net_reg[2] & 3]; int st = 0;
    uint32_t addr = rd32r(8) & K4510_PHYS_MASK, len = rd32r(12);
    char url[512]; uint8_t tmp[512];
    if (!plat_net_ready()) { net_reg[1] = 6; return; }
    switch (cmd) {
    case 1: {
        url_t u;
        if ((st = guest_str(rd32r(4), url, sizeof url))) break;
        if (c->type) chan_close(c);
        wr32r(0x10, 0xFFFFFFFFu);
        if (url_parse(url, &u)) { st = 1; break; }
        if (!strcmp(u.scheme, "tcp")) { c->h = plat_tcp_connect(u.host, u.port); if (c->h < 0) { st = 1; break; } c->type = CH_TCP; }
        else if ((st = net_fetch(url, &c->buf, &c->len)) == 0) { c->type = CH_BUF; c->pos = 0; wr32r(0x10, c->len); }
        break; }
    case 2: {
        uint32_t done = 0;
        if (!c->type) { st = 2; break; }
        if (c->type == CH_BUF) {
            while (done < len && c->pos < c->len) k4510_ram[(addr + done++) & K4510_PHYS_MASK] = c->buf[c->pos++];
            if (!done && c->pos >= c->len) st = 4;
        } else {
            while (done < len) {
                int r = plat_tcp_recv(c->h, tmp, len - done < sizeof tmp ? (int)(len - done) : (int) sizeof tmp);
                if (r > 0) { for (int i = 0; i < r; i++) k4510_ram[(addr + done + (uint32_t) i) & K4510_PHYS_MASK] = tmp[i]; done += (uint32_t) r; continue; }
                if (r < 0) c->eof = 1;
                break;
            }
            if (!done && c->eof) st = 4;
        }
        wr32r(12, done);
        break; }
    case 3: {
        uint32_t done = 0;
        if (c->type != CH_TCP) { st = c->type ? 3 : 2; break; }
        while (done < len) {
            int n = len - done < sizeof tmp ? (int)(len - done) : (int) sizeof tmp;
            for (int i = 0; i < n; i++) tmp[i] = k4510_ram[(addr + done + (uint32_t) i) & K4510_PHYS_MASK];
            if (plat_tcp_send(c->h, tmp, n) < 0) { st = 4; break; }
            done += (uint32_t) n;
        }
        wr32r(12, done);
        break; }
    case 4: if (c->type) chan_close(c); break;
    case 5: {
        if (!c->type) { st = 2; wr32r(0x10, 0); break; }
        if (c->type == CH_BUF) { uint32_t left = c->len - c->pos; wr32r(0x10, left); if (!left) st = 4; break; }
        { int n = c->eof ? -1 : plat_tcp_avail(c->h); if (n < 0) { c->eof = 1; st = 4; n = 0; } wr32r(0x10, (uint32_t) n); }
        break; }
    case 6: {
        uint8_t *b; uint32_t n, done = 0;
        if ((st = guest_str(rd32r(4), url, sizeof url))) break;
        if ((st = net_fetch(url, &b, &n))) break;
        wr32r(0x10, n);
        while (done < len && done < n) { k4510_ram[(addr + done) & K4510_PHYS_MASK] = b[done]; done++; }
        free(b);
        wr32r(12, done);
        break; }
    default: st = 3;
    }
    net_reg[1] = (uint8_t) st;
}
uint8_t net_read(uint8_t reg) { if (reg == 1 && !plat_net_ready()) return 6; return reg < sizeof net_reg ? net_reg[reg] : 0xFF; }
void net_write(uint8_t reg, uint8_t v)
{
    if (reg == 0) { net_run(v); return; }
    if (reg < sizeof net_reg) net_reg[reg] = v;
}
