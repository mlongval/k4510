/* k4510_compat.h -- [K4510] what Chocolate Doom's music code expects and
 * doomgeneric's fork does not have.
 *
 * i_oplmusic.c and midifile.c come from chocolate-doom 895f581; the rest of
 * this tree is doomgeneric dcb7a8d, forked from an older Chocolate Doom.  Two
 * small things have appeared upstream since that fork, and both vendored
 * music files use them:
 *
 *   PACKED_STRUCT(...)   a wrapper around a packed struct.  This tree has
 *                        PACKEDATTR (doomtype.h) but not the wrapper.
 *   I_Realloc, M_fopen   thin helpers over realloc and fopen.
 *
 * They are supplied HERE, force-included into only those two files (see
 * Makefile.k4510), rather than edited into the vendored sources: the files
 * then stay byte-identical to upstream and can be diffed against 895f581
 * with nothing to explain.  The definitions are upstream's own.
 */
#ifndef K4510_COMPAT_H
#define K4510_COMPAT_H

#include <stdio.h>
#include <stdlib.h>

#include "doomtype.h"          /* for PACKEDATTR */

/* Upstream: PACKEDPREFIX is empty on everything but Watcom and MSVC. */
#ifndef PACKEDPREFIX
#define PACKEDPREFIX
#endif
#ifndef PACKED_STRUCT
#define PACKED_STRUCT(...) PACKEDPREFIX struct __VA_ARGS__ PACKEDATTR
#endif

/* Upstream's I_Realloc aborts on failure the way Z_Malloc does; the music
 * code only grows a track buffer with it, so plain realloc plus a check is
 * the same contract.  Static, because only these two files include this. */
#ifndef K4510_HAVE_I_REALLOC
#define K4510_HAVE_I_REALLOC
static inline void *I_Realloc(void *ptr, size_t size)
{
    void *p = realloc(ptr, size);
    if (p == NULL && size != 0) {
        fprintf(stderr, "I_Realloc: out of memory (%zu bytes)\n", size);
        exit(1);
    }
    return p;
}
#endif

#ifndef K4510_HAVE_M_FOPEN
#define K4510_HAVE_M_FOPEN
static inline FILE *M_fopen(const char *filename, const char *mode)
{
    return fopen(filename, mode);   /* upstream's handles UTF-8 paths on Windows */
}
#endif

/* Upstream's OPL driver-version enum (chocolate-doom src/i_sound.h).  DOOM's
 * OPL code changed between 1.666 and 1.9 and i_oplmusic.c reproduces both;
 * this fork's i_sound.h predates the distinction. */
#ifndef K4510_HAVE_OPL_DRIVER_VER
#define K4510_HAVE_OPL_DRIVER_VER
typedef enum
{
    opl_doom1_1_666,    // Doom 1 v1.666
    opl_doom2_1_666,    // Doom 2 v1.666, Hexen, Heretic
    opl_doom_1_9        // Doom v1.9, Strife
} opl_driver_ver_t;
void I_SetOPLDriverVer(opl_driver_ver_t ver);
#endif

#ifndef K4510_HAVE_M_REMOVE
#define K4510_HAVE_M_REMOVE
static inline int M_remove(const char *path)
{
    return remove(path);            /* upstream's handles UTF-8 paths on Windows */
}
#endif

#endif /* K4510_COMPAT_H */
