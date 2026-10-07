/* PNG out, for the screenshots (sdl/main.c): RGB rows in stored (uncompressed)
 * deflate blocks -- no zlib to link, and 900 KB a frame is nothing. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "png.h"
static uint32_t png_crc(uint32_t c, const uint8_t *p, size_t n) {
    static uint32_t t[256];
    if (!t[1]) for (uint32_t i = 0; i < 256; i++) { uint32_t v = i; for (int k = 0; k < 8; k++) v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1; t[i] = v; }
    for (size_t i = 0; i < n; i++) c = t[(c ^ p[i]) & 255] ^ (c >> 8);
    return c;
}
static void png_be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t) v; }
static void png_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len) {
    uint8_t b[4];
    png_be32(b, len); fwrite(b, 1, 4, f); fwrite(type, 1, 4, f); if (len) fwrite(data, 1, len, f);
    uint32_t c = png_crc(0xFFFFFFFFu, (const uint8_t *) type, 4);
    if (len) c = png_crc(c, data, len);
    png_be32(b, c ^ 0xFFFFFFFFu); fwrite(b, 1, 4, f);
}
/* RAW is H rows of 1 + W*3 bytes, each led by its filter byte (none), as PNG
 * wants them; written to PATH (through PATH.tmp, renamed when whole). */
void png_write(const char *path, const uint8_t *raw, int W, int H) {
    enum { BLK = 65535 };
    const size_t rawlen = (size_t) H * (size_t)(1 + W * 3);
    size_t nblk = (rawlen + BLK - 1) / BLK;
    uint8_t *z = malloc(2 + rawlen + nblk * 5 + 4), *q = z;
    if (!z) return;
    *q++ = 0x78; *q++ = 0x01;                                      /* zlib: deflate, no dictionary */
    uint32_t a = 1, b = 0;
    for (size_t off = 0; off < rawlen; off += BLK) {
        size_t n = rawlen - off < BLK ? rawlen - off : BLK;
        *q++ = (uint8_t)(off + n == rawlen);                  /* a stored block; the last one says so */
        *q++ = (uint8_t) n; *q++ = (uint8_t)(n >> 8); *q++ = (uint8_t) ~n; *q++ = (uint8_t)(~n >> 8);
        memcpy(q, raw + off, n); q += n;
        for (size_t i = 0; i < n; i++) { a = (a + raw[off + i]) % 65521; b = (b + a) % 65521; }
    }
    png_be32(q, b << 16 | a); q += 4;                              /* Adler-32 of the rows */
    char tmp[96];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);                    /* renamed when whole: k4510-shot waits for the .png */
    FILE *f = fopen(tmp, "wb");
    if (f) {
        static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
        uint8_t ihdr[13] = { 0 };
        png_be32(ihdr, W); png_be32(ihdr + 4, H); ihdr[8] = 8; ihdr[9] = 2;   /* 8-bit RGB */
        fwrite(sig, 1, 8, f);
        png_chunk(f, "IHDR", ihdr, 13); png_chunk(f, "IDAT", z, (uint32_t)(q - z)); png_chunk(f, "IEND", NULL, 0);
        if (fclose(f) == 0 && rename(tmp, path) == 0) fprintf(stderr, "k4510: screenshot %s\n", path);
        else remove(tmp);
    }
    free(z);
}
