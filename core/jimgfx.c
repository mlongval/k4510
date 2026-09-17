/* See jimgfx.h. */
#include "jimgfx.h"
#include "mem.h"
#include "vicky.h"
#include "zip.h"
#include "io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_IMAGES   32
#define MAX_PIXELS   (4096u * 4096u)               /* one image: 64 MB of RGBA is the most this will hold */
#define MAX_PAYLOAD  (48u << 20)
#define CUBE0        40                            /* palette entries 40..255: the 6x6x6 cube */

typedef struct { int id, w, h; uint8_t *rgba; unsigned used; } image_t;
static image_t images[MAX_IMAGES];
static unsigned tick;
static int plane_w, plane_h, active;

/* a transmission in pieces (m=1): the keys of the first chunk, the payload so far */
static struct { int on; int a, f, t, s, v, i, o, q, c, r, x, y, w, h, C; uint8_t *data; size_t n, cap; } tx;

/* ---- the plane ------------------------------------------------------------ */
static uint8_t *plane(void) { return k4510_ram + JIMGFX_PLANE; }
static void plane_on(void)
{
    int w = vicky_glass_w(), h = vicky_glass_h();
    if (active && w == plane_w && h == plane_h) return;
    plane_w = w; plane_h = h;
    memset(plane(), 0, (size_t) w * h);
    for (int r = 0; r < 6; r++) for (int g = 0; g < 6; g++) for (int b = 0; b < 6; b++) {       /* the cube */
        vicky_write(VR_PALIDX, (uint8_t)(CUBE0 + r * 36 + g * 6 + b));
        vicky_write(VR_PALR, (uint8_t)(r * 51)); vicky_write(VR_PALG, (uint8_t)(g * 51)); vicky_write(VR_PALB, (uint8_t)(b * 51));
    }
    { uint8_t L = VR_LAYER(JIMGFX_LAYER);
      vicky_write((uint8_t)(L + VL_PALOFS), 0);
      for (int i = 0; i < 4; i++) vicky_write((uint8_t)(L + VL_SCROLLX + i), 0);
      vicky_write((uint8_t)(L + VL_STRIDE), (uint8_t) w); vicky_write((uint8_t)(L + VL_STRIDE + 1), (uint8_t)(w >> 8));
      for (int i = 0; i < 4; i++) vicky_write((uint8_t)(L + VL_DATA + i), (uint8_t)(JIMGFX_PLANE >> (8 * i)));
      vicky_write((uint8_t)(L + VL_CTRL), 0x19); }                                            /* on, bitmap, 8 bpp */
    active = 1;
}
int jimgfx_active(void) { return active; }
void jimgfx_clear_rows(int y0, int y1)
{
    if (!active) return;
    if (y0 < 0) y0 = 0;
    if (y1 > plane_h) y1 = plane_h;
    if (y1 > y0) memset(plane() + (size_t) y0 * plane_w, 0, (size_t)(y1 - y0) * plane_w);
}
void jimgfx_clear(void) { jimgfx_clear_rows(0, plane_h); }
void jimgfx_scroll(int y0, int y1, int dy)
{
    if (!active || !dy) return;
    if (y0 < 0) y0 = 0;
    if (y1 > plane_h) y1 = plane_h;
    if (y1 <= y0) return;
    if (dy <= -(y1 - y0) || dy >= y1 - y0) { jimgfx_clear_rows(y0, y1); return; }
    if (dy < 0) { memmove(plane() + (size_t) y0 * plane_w, plane() + (size_t)(y0 - dy) * plane_w, (size_t)(y1 - y0 + dy) * plane_w); jimgfx_clear_rows(y1 + dy, y1); }
    else        { memmove(plane() + (size_t)(y0 + dy) * plane_w, plane() + (size_t) y0 * plane_w, (size_t)(y1 - y0 - dy) * plane_w); jimgfx_clear_rows(y0, y0 + dy); }
}
void jimgfx_reset(void)
{
    if (active) { vicky_write((uint8_t)(VR_LAYER(JIMGFX_LAYER) + VL_CTRL), 0); active = 0; }
    for (int i = 0; i < MAX_IMAGES; i++) { free(images[i].rgba); memset(&images[i], 0, sizeof images[i]); }
    free(tx.data); memset(&tx, 0, sizeof tx);
}

/* ---- images ----------------------------------------------------------------- */
static image_t *image_find(int id) { for (int i = 0; i < MAX_IMAGES; i++) if (images[i].rgba && images[i].id == id) return &images[i]; return NULL; }
static image_t *image_slot(int id)
{
    image_t *m = image_find(id), *old = &images[0];
    if (m) { free(m->rgba); m->rgba = NULL; return m; }
    for (int i = 0; i < MAX_IMAGES; i++) { if (!images[i].rgba) return &images[i]; if (images[i].used < old->used) old = &images[i]; }
    free(old->rgba); old->rgba = NULL;                                                         /* thirty-two held: the one shown longest ago goes */
    return old;
}

/* ---- PNG: the kinds a screenshot or a photograph come in; not interlaced ---- */
static uint32_t be32(const uint8_t *p) { return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3]; }
static int paeth(int a, int b, int c) { int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c); return pa <= pb && pa <= pc ? a : pb <= pc ? b : c; }
static uint8_t *png_decode(const uint8_t *d, size_t n, int *ow, int *oh)
{
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
    uint32_t w = 0, h = 0; int depth = 0, ct = 0, got = 0, ntrns = 0; uint8_t pal[256][3], trns[256]; uint8_t *z = NULL, *raw = NULL, *out = NULL; size_t zn = 0, p = 8;
    if (n < 8 || memcmp(d, sig, 8)) return NULL;
    memset(trns, 255, sizeof trns); memset(pal, 0, sizeof pal);
    while (p + 12 <= n) {
        uint32_t len = be32(d + p); const uint8_t *ty = d + p + 4, *body = d + p + 8;
        if (len > n - p - 12) break;
        if (!memcmp(ty, "IHDR", 4) && len >= 13) { w = be32(body); h = be32(body + 4); depth = body[8]; ct = body[9]; if (body[12]) goto fail; got = 1; }   /* interlaced: no */
        else if (!memcmp(ty, "PLTE", 4)) { for (uint32_t i = 0; i < len / 3 && i < 256; i++) memcpy(pal[i], body + i * 3, 3); }
        else if (!memcmp(ty, "tRNS", 4)) { ntrns = (int)(len > 256 ? 256 : len); memcpy(trns, body, (size_t) ntrns); }
        else if (!memcmp(ty, "IDAT", 4)) { uint8_t *nz = realloc(z, zn + len); if (!nz) goto fail; z = nz; memcpy(z + zn, body, len); zn += len; }
        else if (!memcmp(ty, "IEND", 4)) break;
        p += 12 + len;
    }
    if (!got || !w || !h || (uint64_t) w * h > MAX_PIXELS || !z) goto fail;
    { int ch = ct == 0 ? 1 : ct == 2 ? 3 : ct == 3 ? 1 : ct == 4 ? 2 : ct == 6 ? 4 : 0;
      int bpp, bypp; size_t rowb;
      if (!ch || (depth != 8 && depth != 16 && !((ct == 0 || ct == 3) && (depth == 1 || depth == 2 || depth == 4)))) goto fail;
      bpp = ch * depth; bypp = bpp >= 8 ? bpp / 8 : 1; rowb = ((size_t) w * bpp + 7) / 8;
      if (!(raw = malloc((rowb + 1) * h)) || !(out = malloc((size_t) w * h * 4))) goto fail;
      if (zip_zlib_inflate(z, (uint32_t) zn, raw, (uint32_t)((rowb + 1) * h))) goto fail;
      for (uint32_t y = 0; y < h; y++) {                                                       /* undo the row filters, in place */
          uint8_t *r = raw + y * (rowb + 1) + 1, *up = y ? r - (rowb + 1) : NULL; int f = r[-1];
          for (size_t x = 0; x < rowb; x++) {
              int a = x >= (size_t) bypp ? r[x - bypp] : 0, b = up ? up[x] : 0, c = up && x >= (size_t) bypp ? up[x - bypp] : 0;
              r[x] = (uint8_t)(r[x] + (f == 1 ? a : f == 2 ? b : f == 3 ? (a + b) / 2 : f == 4 ? paeth(a, b, c) : 0));
          }
      }
      for (uint32_t y = 0; y < h; y++) {
          const uint8_t *r = raw + y * (rowb + 1) + 1; uint8_t *o = out + (size_t) y * w * 4;
          for (uint32_t x = 0; x < w; x++, o += 4) {
              if (depth < 8) { int v = (r[(x * depth) >> 3] >> (8 - depth - ((x * depth) & 7))) & ((1 << depth) - 1);
                               if (ct == 3) { memcpy(o, pal[v], 3); o[3] = trns[v]; } else { o[0] = o[1] = o[2] = (uint8_t)(v * 255 / ((1 << depth) - 1)); o[3] = 255; } continue; }
              { const uint8_t *s = r + (size_t) x * ch * (depth / 8); int st = depth / 8;          /* 16 bits a sample: the high byte is the sample */
                switch (ct) {
                case 0: o[0] = o[1] = o[2] = s[0]; o[3] = 255; break;
                case 2: o[0] = s[0]; o[1] = s[st]; o[2] = s[2 * st]; o[3] = 255; break;
                case 3: memcpy(o, pal[s[0]], 3); o[3] = trns[s[0]]; break;
                case 4: o[0] = o[1] = o[2] = s[0]; o[3] = s[st]; break;
                default: o[0] = s[0]; o[1] = s[st]; o[2] = s[2 * st]; o[3] = s[3 * st]; break;
                } }
          }
      } }
    free(z); free(raw); *ow = (int) w; *oh = (int) h; (void) ntrns;
    return out;
fail:
    free(z); free(raw); free(out); return NULL;
}

/* ---- the protocol ----------------------------------------------------------- */
static int b64v(int c) { return c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : c == '/' ? 63 : -1; }
static int payload_add(const uint8_t *s, size_t n)
{
    size_t need = tx.n + n / 4 * 3 + 4; uint32_t acc = 0; int k = 0;
    if (need > MAX_PAYLOAD) return 0;
    if (need > tx.cap) { size_t nc = need * 2; uint8_t *nd = realloc(tx.data, nc); if (!nd) return 0; tx.data = nd; tx.cap = nc; }
    for (size_t i = 0; i < n; i++) { int v = b64v(s[i]); if (v < 0) continue; acc = acc << 6 | (uint32_t) v; if (++k == 4) { tx.data[tx.n++] = (uint8_t)(acc >> 16); tx.data[tx.n++] = (uint8_t)(acc >> 8); tx.data[tx.n++] = (uint8_t) acc; k = 0; acc = 0; } }
    if (k == 3) { tx.data[tx.n++] = (uint8_t)(acc >> 10); tx.data[tx.n++] = (uint8_t)(acc >> 2); }
    else if (k == 2) tx.data[tx.n++] = (uint8_t)(acc >> 4);
    return 1;
}
static void say(jimgfx_todo_t *todo, int id, int quiet, int ok, const char *msg)
{
    if (quiet >= 2 || (ok && quiet >= 1)) return;
    if (id) snprintf(todo->reply, sizeof todo->reply, "\033_Gi=%d;%s\033\\", id, msg); else if (!ok) snprintf(todo->reply, sizeof todo->reply, "\033_G;%s\033\\", msg);
}
static uint8_t *file_read(const char *path, int host, size_t *n, int temp)
{
    char real[1024]; FILE *f; long sz; uint8_t *b;
    if (host) snprintf(real, sizeof real, "%s", path); else if (!io_fs_hostpath(path, real, sizeof real)) return NULL;
    if (!(f = fopen(real, "rb"))) return NULL;
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0 || (size_t) sz > MAX_PAYLOAD || !(b = malloc((size_t) sz))) { fclose(f); return NULL; }
    if (fread(b, 1, (size_t) sz, f) != (size_t) sz) { free(b); fclose(f); return NULL; }
    fclose(f);
    if (temp && host && strstr(real, "tty-graphics-protocol")) unlink(real);                  /* the protocol's own rule for t=t: only a file that says it is one */
    *n = (size_t) sz; return b;
}

void jimgfx_apc(const uint8_t *buf, size_t len, const jimgfx_geom_t *g, jimgfx_todo_t *todo)
{
    size_t semi = 0; int more = 0, first = !tx.on;
    memset(todo, 0, sizeof *todo);
    if (!len || buf[0] != 'G') return;                                                         /* some other APC: not ours */
    while (semi < len && buf[semi] != ';') semi++;
    if (first) { uint8_t *keep = tx.data; size_t cap = tx.cap; memset(&tx, 0, sizeof tx); tx.data = keep; tx.cap = cap; tx.a = 't'; tx.f = 32; tx.t = 'd'; }
    for (size_t p = 1; p < semi; ) {                                                           /* key=value, ... */
        int key = buf[p], val = 0, neg = 0; size_t q = p + 2;
        if (p + 1 >= semi || buf[p + 1] != '=') break;
        if (q < semi && (buf[q] < '0' || buf[q] > '9') && buf[q] != '-') val = buf[q++];       /* a letter: a=T, t=f, o=z */
        else { if (q < semi && buf[q] == '-') { neg = 1; q++; } while (q < semi && buf[q] >= '0' && buf[q] <= '9') val = val * 10 + (buf[q++] - '0'); if (neg) val = -val; }
        if (key == 'm') more = val;
        else if (first || key == 'q') switch (key) {
            case 'a': tx.a = val; break; case 'f': tx.f = val; break; case 't': tx.t = val; break; case 's': tx.s = val; break; case 'v': tx.v = val; break;
            case 'i': tx.i = val; break; case 'o': tx.o = val; break; case 'q': tx.q = val; break; case 'c': tx.c = val; break; case 'r': tx.r = val; break;
            case 'x': tx.x = val; break; case 'y': tx.y = val; break; case 'w': tx.w = val; break; case 'h': tx.h = val; break; case 'C': tx.C = val; break;
        }
        while (q < semi && buf[q] != ',') q++;
        p = q + 1;
    }
    tx.on = 1;
    if (semi < len && !payload_add(buf + semi + 1, len - semi - 1)) { say(todo, tx.i, tx.q, 0, "ENOMEM:too much"); tx.on = 0; tx.n = 0; return; }
    if (more) return;                                                                           /* the rest is coming */
    tx.on = 0;

    if (tx.a == 'd') { jimgfx_clear(); tx.n = 0; return; }                                     /* delete: whichever was meant, the glass is cleared; ids stay loaded */
    if (tx.a == 'p') {
        image_t *m = image_find(tx.i);
        tx.n = 0;
        if (!m) { say(todo, tx.i, tx.q, 0, "ENOENT:no such image"); return; }
        goto place_it;
    }
    if (tx.a != 'q' && tx.a != 't' && tx.a != 'T') { say(todo, tx.i, tx.q, 0, "EINVAL:not an action JIM has"); tx.n = 0; return; }
    { uint8_t *data = tx.data; size_t n = tx.n; uint8_t *owned = NULL, *rgba = NULL; int w = tx.s, h = tx.v;
      if (tx.t == 'f' || tx.t == 't') {
          char path[1024]; size_t l = n < sizeof path - 1 ? n : sizeof path - 1;
          memcpy(path, tx.data, l); path[l] = 0;
          if (!(owned = file_read(path, g->host, &n, tx.t == 't'))) { say(todo, tx.i, tx.q, 0, "EBADF:cannot read that file"); tx.n = 0; return; }
          data = owned;
      } else if (tx.t != 'd') { say(todo, tx.i, tx.q, 0, "EINVAL:JIM takes t=d, f and t"); tx.n = 0; return; }
      if (tx.f == 100) rgba = png_decode(data, n, &w, &h);
      else if ((tx.f == 24 || tx.f == 32) && w > 0 && h > 0 && (uint64_t) w * h <= MAX_PIXELS) {
          size_t bp = tx.f == 32 ? 4 : 3, want = (size_t) w * h * bp; uint8_t *src = data, *unz = NULL;
          if (tx.o == 'z') { if ((unz = malloc(want)) && !zip_zlib_inflate(data, (uint32_t) n, unz, (uint32_t) want)) { src = unz; n = want; } else { free(unz); unz = NULL; src = NULL; } }
          if (src && n >= want && (rgba = malloc((size_t) w * h * 4)))
              for (size_t i = 0; i < (size_t) w * h; i++) { memcpy(rgba + i * 4, src + i * bp, 3); rgba[i * 4 + 3] = bp == 4 ? src[i * 4 + 3] : 255; }
          free(unz);
      }
      free(owned); tx.n = 0;
      if (!rgba) { say(todo, tx.i, tx.q, 0, "EBADPNG:JIM could not make a picture of that"); return; }
      if (tx.a == 'q') { free(rgba); say(todo, tx.i, tx.q, 1, "OK"); return; }
      { image_t *m = image_slot(tx.i); m->id = tx.i; m->w = w; m->h = h; m->rgba = rgba; m->used = ++tick; }
      say(todo, tx.i, tx.q, 1, "OK");
      if (tx.a != 'T') return; }
place_it:
    { image_t *m = image_find(tx.i); int sw, sh, cols = tx.c, rows = tx.r;
      if (!m) return;
      sw = tx.w > 0 ? tx.w : m->w - tx.x; sh = tx.h > 0 ? tx.h : m->h - tx.y;
      if (tx.x < 0 || tx.y < 0 || sw <= 0 || sh <= 0 || tx.x + sw > m->w || tx.y + sh > m->h) { say(todo, tx.i, tx.q, 0, "EINVAL:that part is not in the image"); return; }
      /* Kitty's rule: with neither c nor r the picture is its own size in
       * pixels; with both it fills exactly that box, whatever that does to
       * its shape; with one, the other follows so the shape is kept. */
      { int pw = sw, ph = sh;
        if (cols && rows) { pw = cols * g->cell_w; ph = rows * g->cell_h; }
        else if (cols) { pw = cols * g->cell_w; ph = (int)((int64_t) pw * sh / sw); }
        else if (rows) { ph = rows * g->cell_h; pw = (int)((int64_t) ph * sw / sh); }
        if (pw < 1) pw = 1;
        if (ph < 1) ph = 1;
        cols = (pw + g->cell_w - 1) / g->cell_w; rows = (ph + g->cell_h - 1) / g->cell_h;
        todo->pw = pw; todo->ph = ph; }
      todo->place = 1; todo->img = tx.i; todo->cols = cols; todo->rows = rows; todo->keep_cursor = tx.C == 1;
      todo->sx = tx.x; todo->sy = tx.y; todo->sw = sw; todo->sh = sh; }
}

/* A part of an image into cols x rows cells at the cursor: each glass pixel is
 * the mean of the image pixels it covers, put on the cube with an ordered
 * dither so that 216 colours carry a photograph. */
void jimgfx_draw(const jimgfx_todo_t *todo, const jimgfx_geom_t *g)
{
    static const uint8_t bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
    image_t *m = image_find(todo->img); int dw = todo->pw, dh = todo->ph, ox, oy;
    if (!m || !todo->place || dw < 1 || dh < 1) return;
    plane_on(); m->used = ++tick;
    ox = g->px0 + g->cx * g->cell_w; oy = g->py0 + g->cy * g->cell_h;
    for (int y = 0; y < dh; y++) {
        int gy = oy + y, y0 = todo->sy + (int)((int64_t) y * todo->sh / dh), y1 = todo->sy + (int)((int64_t)(y + 1) * todo->sh / dh);
        if (gy < 0 || gy >= plane_h) continue;
        if (y1 <= y0) y1 = y0 + 1;
        for (int x = 0; x < dw; x++) {
            int gx = ox + x, x0 = todo->sx + (int)((int64_t) x * todo->sw / dw), x1 = todo->sx + (int)((int64_t)(x + 1) * todo->sw / dw);
            unsigned r = 0, gg = 0, b = 0, a = 0, cnt = 0;
            if (gx < 0 || gx >= plane_w) continue;
            if (x1 <= x0) x1 = x0 + 1;
            for (int yy = y0; yy < y1; yy++) { const uint8_t *s = m->rgba + ((size_t) yy * m->w + x0) * 4; for (int xx = x0; xx < x1; xx++, s += 4) { r += s[0]; gg += s[1]; b += s[2]; a += s[3]; cnt++; } }
            if (a / cnt < 128) { plane()[(size_t) gy * plane_w + gx] = 0; continue; }          /* see-through: the text shows */
            { int d = bayer[y & 3][x & 3] * 51 / 16, q[3]; unsigned v[3] = { r / cnt, gg / cnt, b / cnt };
              for (int k = 0; k < 3; k++) { q[k] = (int)(v[k] + (unsigned) d) / 51; if (q[k] > 5) q[k] = 5; }
              plane()[(size_t) gy * plane_w + gx] = (uint8_t)(CUBE0 + q[0] * 36 + q[1] * 6 + q[2]); }
        }
    }
}
