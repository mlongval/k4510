/* core/zip.c -- a zip file, read-only, and the inflater it needs.  See zip.h.
 *
 * The inflater is the plain one of RFC 1951 (after Mark Adler's puff.c):
 * canonical Huffman codes decoded a bit at a time.  It is not the fastest
 * there is, and does not need to be -- an entry is inflated once, when it is
 * opened, and the biggest thing anyone keeps in a zip for this machine is
 * smaller than its RAM. */
#include "zip.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define ZIP_MAX_ENTRY (256u << 20)             /* the machine's RAM: nothing bigger can be used */

typedef struct { char *name; uint32_t csize, usize, off, crc; uint16_t method, flags; int isdir; } zent_t;
struct zip_s { uint8_t *buf; uint32_t len; zent_t *e; int n; };

static uint32_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

/* ---- inflate ------------------------------------------------------------- */
typedef struct {
    const uint8_t *in; uint32_t inlen, inpos;
    uint32_t bitbuf; int bitcnt;
    uint8_t *out; uint32_t outlen, outpos;
    int err;
} inf_t;
typedef struct { short count[16], symbol[288]; } huff_t;

static int bits(inf_t *s, int n)
{
    uint32_t v;
    while (s->bitcnt < n) {
        if (s->inpos >= s->inlen) { s->err = 1; return 0; }
        s->bitbuf |= (uint32_t)s->in[s->inpos++] << s->bitcnt; s->bitcnt += 8;
    }
    v = s->bitbuf & ((1u << n) - 1); s->bitbuf >>= n; s->bitcnt -= n;
    return (int)v;
}
/* Returns what is left of the code space: 0 complete, > 0 incomplete (allowed:
 * a code no one uses is an error only when it turns up), < 0 over-subscribed. */
static int build(huff_t *h, const short *len, int n)
{
    short offs[16]; int left = 1;
    memset(h->count, 0, sizeof h->count);
    for (int s = 0; s < n; s++) h->count[len[s]]++;
    if (h->count[0] == n) return 0;
    for (int l = 1; l < 16; l++) { left <<= 1; left -= h->count[l]; if (left < 0) return left; }
    offs[1] = 0;
    for (int l = 1; l < 15; l++) offs[l + 1] = offs[l] + h->count[l];
    for (int s = 0; s < n; s++) if (len[s]) h->symbol[offs[len[s]]++] = (short)s;
    return left;
}
static int decode(inf_t *s, const huff_t *h)
{
    int code = 0, first = 0, index = 0;
    for (int l = 1; l < 16; l++) {
        code |= bits(s, 1);
        if (s->err) return -1;
        int count = h->count[l];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count; first += count; first <<= 1; code <<= 1;
    }
    return -1;                                  /* a code the table does not have */
}
static const short lbase[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
static const short lext[29]  = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
static const short dbase[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
static const short dext[30]  = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };

static int codes(inf_t *s, const huff_t *lc, const huff_t *dc)
{
    for (;;) {
        int sym = decode(s, lc);
        if (sym < 0) return 1;
        if (sym < 256) { if (s->outpos >= s->outlen) return 1; s->out[s->outpos++] = (uint8_t)sym; continue; }
        if (sym == 256) return 0;
        sym -= 257; if (sym >= 29) return 1;
        uint32_t len = (uint32_t)(lbase[sym] + bits(s, lext[sym]));
        int ds = decode(s, dc);
        if (ds < 0 || ds >= 30) return 1;
        uint32_t dist = (uint32_t)(dbase[ds] + bits(s, dext[ds]));
        if (s->err || dist > s->outpos || len > s->outlen - s->outpos) return 1;
        for (uint32_t i = 0; i < len; i++, s->outpos++) s->out[s->outpos] = s->out[s->outpos - dist];
    }
}
static int stored(inf_t *s)
{
    uint32_t len;
    s->bitbuf = 0; s->bitcnt = 0;               /* to the byte: never a whole byte left in the buffer (bits() reads only what it needs) */
    if (s->inlen - s->inpos < 4) return 1;
    len = rd16(s->in + s->inpos);
    if ((rd16(s->in + s->inpos + 2) ^ 0xFFFF) != len) return 1;
    s->inpos += 4;
    if (s->inlen - s->inpos < len || s->outlen - s->outpos < len) return 1;
    memcpy(s->out + s->outpos, s->in + s->inpos, len); s->inpos += len; s->outpos += len;
    return 0;
}
static int fixed(inf_t *s)
{
    static huff_t lc, dc; static int made;
    if (!made) {
        short l[288]; int i;
        for (i = 0; i < 144; i++) l[i] = 8;
        for (; i < 256; i++) l[i] = 9;
        for (; i < 280; i++) l[i] = 7;
        for (; i < 288; i++) l[i] = 8;
        build(&lc, l, 288);
        for (i = 0; i < 30; i++) l[i] = 5;
        build(&dc, l, 30);
        made = 1;
    }
    return codes(s, &lc, &dc);
}
static int dynamic(inf_t *s)
{
    static const short order[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
    short len[320]; huff_t lc, dc; int nlen, ndist, ncode, i = 0;
    nlen = bits(s, 5) + 257; ndist = bits(s, 5) + 1; ncode = bits(s, 4) + 4;
    if (s->err || nlen > 286 || ndist > 30) return 1;
    memset(len, 0, sizeof len);
    for (int k = 0; k < ncode; k++) len[order[k]] = (short)bits(s, 3);
    if (s->err || build(&lc, len, 19) != 0) return 1;      /* the code-length code must be complete */
    while (i < nlen + ndist) {
        int sym = decode(s, &lc), rep, v = 0;
        if (sym < 0) return 1;
        if (sym < 16) { len[i++] = (short)sym; continue; }
        if (sym == 16) { if (!i) return 1; v = len[i - 1]; rep = 3 + bits(s, 2); }
        else if (sym == 17) rep = 3 + bits(s, 3);
        else rep = 11 + bits(s, 7);
        if (s->err || i + rep > nlen + ndist) return 1;
        while (rep--) len[i++] = (short)v;
    }
    if (!len[256]) return 1;                    /* no end-of-block code: it could never stop */
    if (build(&lc, len, nlen) < 0 || build(&dc, len + nlen, ndist) < 0) return 1;
    return codes(s, &lc, &dc);
}
/* 0 when IN inflates to exactly OUTLEN bytes. */
static int inflate_all(const uint8_t *in, uint32_t inlen, uint8_t *out, uint32_t outlen)
{
    inf_t s = { in, inlen, 0, 0, 0, out, outlen, 0, 0 };
    int last, r;
    do {
        last = bits(&s, 1);
        int type = bits(&s, 2);
        if (s.err) return 1;
        r = type == 0 ? stored(&s) : type == 1 ? fixed(&s) : type == 2 ? dynamic(&s) : 1;
        if (r || s.err) return 1;
    } while (!last);
    return s.outpos != outlen;
}

static uint32_t crc32_of(const uint8_t *p, uint32_t n)
{
    static uint32_t t[256]; static int made;
    uint32_t c = 0xFFFFFFFFu;
    if (!made) { for (uint32_t i = 0; i < 256; i++) { uint32_t k = i; for (int j = 0; j < 8; j++) k = k & 1 ? 0xEDB88320u ^ (k >> 1) : k >> 1; t[i] = k; } made = 1; }
    while (n--) c = t[(c ^ *p++) & 255] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

/* ---- the zip -------------------------------------------------------------- */
/* A name is safe when every part of it is an ordinary name: nothing empty (but
 * the slash that ends a folder's), no "." or "..", no backslash or colon (a
 * Windows path), nothing below a space. */
static int safe_name(const char *s, size_t n)
{
    size_t i = 0;
    if (!n || s[0] == '/') return 0;
    while (i < n) {
        size_t j = i;
        while (j < n && s[j] != '/') { unsigned char c = (unsigned char)s[j]; if (c < 32 || c == '\\' || c == ':') return 0; j++; }
        if (j == i) return 0;
        if ((j - i == 1 && s[i] == '.') || (j - i == 2 && s[i] == '.' && s[i + 1] == '.')) return 0;
        if (j == n - 1) break;                  /* "DIR/": the folder's own entry */
        i = j + 1;
    }
    return 1;
}
void zip_close(zip_t *z)
{
    if (!z) return;
    for (int i = 0; i < z->n; i++) free(z->e[i].name);
    free(z->e); free(z->buf); free(z);
}
int zip_open_mem(uint8_t *buf, uint32_t len, zip_t **out)
{
    zip_t *z; uint32_t eocd = 0, cd, cdsize, pos; int n, found = 0;
    *out = NULL;
    if (!buf || len < 22) { free(buf); return 2; }
    for (uint32_t back = 22; back <= len && back <= 22 + 65535; back++) {   /* the end record, behind at most a 64 KB comment */
        uint32_t p = len - back;
        if (rd32(buf + p) == 0x06054b50 && p + 22 + rd16(buf + p + 20) == len) { eocd = p; found = 1; break; }
    }
    if (!found) { free(buf); return 2; }
    n = (int)rd16(buf + eocd + 10); cdsize = rd32(buf + eocd + 12); cd = rd32(buf + eocd + 16);
    if (rd16(buf + eocd + 4) || rd16(buf + eocd + 6) || rd16(buf + eocd + 8) != (uint32_t)n) { free(buf); return 3; }   /* split over disks */
    if (n == 0xFFFF || cd == 0xFFFFFFFFu || cdsize == 0xFFFFFFFFu) { free(buf); return 3; }                          /* zip64 */
    if (cd > eocd || cdsize > eocd - cd) { free(buf); return 2; }
    if (!(z = calloc(1, sizeof *z)) || !(z->e = calloc((size_t)(n ? n : 1), sizeof *z->e))) { free(z); free(buf); return 2; }
    z->buf = buf; z->len = len;
    pos = cd;
    for (int i = 0; i < n; i++) {
        zent_t *e = &z->e[i]; uint32_t nl, xl, cl;
        if (pos > cd + cdsize || cd + cdsize - pos < 46 || rd32(buf + pos) != 0x02014b50) { zip_close(z); return 2; }
        e->flags = (uint16_t)rd16(buf + pos + 8); e->method = (uint16_t)rd16(buf + pos + 10);
        e->crc = rd32(buf + pos + 16); e->csize = rd32(buf + pos + 20); e->usize = rd32(buf + pos + 24);
        nl = rd16(buf + pos + 28); xl = rd16(buf + pos + 30); cl = rd16(buf + pos + 32);
        e->off = rd32(buf + pos + 42);
        if (cd + cdsize - pos - 46 < nl + xl + cl) { zip_close(z); return 2; }
        if (e->csize == 0xFFFFFFFFu || e->usize == 0xFFFFFFFFu || e->off == 0xFFFFFFFFu) { zip_close(z); return 3; }
        if (!safe_name((const char *)buf + pos + 46, nl)) { zip_close(z); return 3; }
        if (!(e->name = malloc(nl + 1))) { zip_close(z); return 2; }
        memcpy(e->name, buf + pos + 46, nl); e->name[nl] = 0;
        if (nl && e->name[nl - 1] == '/') { e->isdir = 1; e->name[nl - 1] = 0; }
        z->n = i + 1;
        pos += 46 + nl + xl + cl;
    }
    *out = z;
    return 0;
}
static zent_t *find(zip_t *z, const char *path)
{
    while (*path == '/') path++;
    for (int i = 0; i < z->n; i++) if (!strcasecmp(z->e[i].name, path)) return &z->e[i];
    return NULL;
}
/* Is anything in the zip below PATH ("" is the top)? */
static int has_under(zip_t *z, const char *path)
{
    size_t pl;
    while (*path == '/') path++;
    if (!*path) return 1;
    pl = strlen(path);
    for (int i = 0; i < z->n; i++) if (!strncasecmp(z->e[i].name, path, pl) && z->e[i].name[pl] == '/') return 1;
    return 0;
}
int zip_isdir(zip_t *z, const char *path)
{
    zent_t *e = find(z, path);
    if (e && e->isdir) return 1;
    if (has_under(z, path)) return 1;
    return e ? 0 : -1;
}
int zip_fetch(zip_t *z, const char *path, uint8_t **buf, uint32_t *len)
{
    zent_t *e = find(z, path); uint32_t lo, data; uint8_t *out;
    *buf = NULL; *len = 0;
    if (!e || e->isdir) return 1;
    if (e->flags & 1) return 3;                                     /* encrypted */
    if (e->method != 0 && e->method != 8) return 3;
    if (e->usize > ZIP_MAX_ENTRY) return 2;
    lo = e->off;
    if (lo > z->len || z->len - lo < 30 || rd32(z->buf + lo) != 0x04034b50) return 2;
    data = lo + 30 + rd16(z->buf + lo + 26) + rd16(z->buf + lo + 28);
    if (data > z->len || z->len - data < e->csize) return 2;
    if (!(out = malloc(e->usize ? e->usize : 1))) return 2;
    if (e->method == 0) { if (e->csize != e->usize) { free(out); return 2; } memcpy(out, z->buf + data, e->usize); }
    else if (inflate_all(z->buf + data, e->csize, out, e->usize)) { free(out); return 2; }
    if (crc32_of(out, e->usize) != e->crc) { free(out); return 2; }
    *buf = out; *len = e->usize;
    return 0;
}
static int dirent_cmp(const void *a, const void *b) { return strcasecmp(((const net_dirent *)a)->name, ((const net_dirent *)b)->name); }
int zip_listdir(zip_t *z, const char *path, net_dirent **ents, int *n)
{
    net_dirent *d; int k = 0; size_t pl;
    *ents = NULL; *n = 0;
    while (*path == '/') path++;
    if (zip_isdir(z, path) != 1) return 1;
    pl = strlen(path);
    if (!(d = calloc((size_t)(z->n ? z->n : 1), sizeof *d))) return 1;
    for (int i = 0; i < z->n; i++) {
        const char *nm = z->e[i].name, *rest, *sl; size_t cl; int isdir, dup = 0;
        if (pl) { if (strncasecmp(nm, path, pl) || nm[pl] != '/') continue; rest = nm + pl + 1; }
        else rest = nm;
        if (!*rest) continue;
        sl = strchr(rest, '/');
        cl = sl ? (size_t)(sl - rest) : strlen(rest);
        isdir = sl || z->e[i].isdir;
        if (cl > 63) cl = 63;
        for (int j = 0; j < k; j++) if (strlen(d[j].name) == cl && !strncasecmp(d[j].name, rest, cl)) { d[j].isdir |= isdir; dup = 1; break; }
        if (dup) continue;
        memcpy(d[k].name, rest, cl); d[k].name[cl] = 0;
        d[k].isdir = isdir; d[k].size = isdir ? 0 : z->e[i].usize;
        k++;
    }
    if (k > 1) qsort(d, (size_t)k, sizeof *d, dirent_cmp);
    *ents = d; *n = k;
    return 0;
}
