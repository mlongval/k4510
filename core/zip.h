/* core/zip.h -- a zip file, read-only: what MOUNT GAMES.ZIP /MNT/GAMES shows.
 *
 * The whole zip is held in memory (the disk is RAM anyway), its central
 * directory read once when it is opened, and an entry inflated when it is
 * fetched.  Stored and deflated entries, which is what every zip tool makes;
 * no zlib -- the inflater is in zip.c.
 *
 * Refused, not guessed at: a name that could climb out of the mount ("../",
 * "/", "\"), a zip64, an encrypted entry, a method other than stored or
 * deflate, an entry whose CRC does not match.  Names are matched without
 * regard to case, like the rest of the file system; folders are the entries'
 * paths, whether or not the zip lists them on their own. */
#ifndef K4510_ZIP_H
#define K4510_ZIP_H
#include <stdint.h>
#include "net.h"                                /* net_dirent: a listing looks like a server's */

typedef struct zip_s zip_t;

/* BUF (malloc'd, LEN bytes) becomes the zip's: freed by zip_close, or here on
 * failure.  0 ok, 2 not a zip (or damaged), 3 a zip this cannot read safely. */
int  zip_open_mem(uint8_t *buf, uint32_t len, zip_t **out);
void zip_close(zip_t *z);
int  zip_isdir(zip_t *z, const char *path);    /* 1 a folder ("" is the top), 0 a file, -1 nothing there */
/* An entry inflated into a new buffer (the caller frees): 0 ok, 1 not found
 * (or a folder), 2 damaged, 3 encrypted or a method this does not know. */
int  zip_fetch(zip_t *z, const char *path, uint8_t **buf, uint32_t *len);
/* A folder's entries, sorted, each once (the caller frees): 0 ok, 1 not a folder. */
int  zip_listdir(zip_t *z, const char *path, net_dirent **ents, int *n);
/* a zlib stream into exactly outlen bytes (0 ok): PNG and Kitty's o=z, for core/jimgfx.c */
int zip_zlib_inflate(const uint8_t *in, uint32_t inlen, uint8_t *out, uint32_t outlen);
#endif
