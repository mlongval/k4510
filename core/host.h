/* What the K4510 core needs from whoever hosts it.  One implementation today,
 * sdl/host_posix.c; the seam stays because it is where a macOS or Windows
 * port would begin (the bare-metal Pi's went with that port, 2026-09-07).
 * Everything else the core uses is plain C library: fopen/opendir/stat for
 * the storage device, time() for the clock. */
#ifndef K4510_HOST_H
#define K4510_HOST_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void *host_alloc_zeroed(size_t bytes);          /* NULL on failure; may be lazily committed */
void  host_zero(void *p, size_t bytes);         /* back to all-zero, releasing pages if it can */
void  host_poll_input(void);
/* for PERF.TXT: the host's own account of clock, temperature, throttling and
 * memory speed, as text; empty where the host has nothing to say */
void  host_perf_probe(char *out, unsigned n);
/* what this host is, as text -- CPU model and count, a board revision --
 * so a clock measured on it (core/calib.c) is trusted only on it.  A weak
 * default in calib.c answers "unknown host" where the host has not said. */
void  host_fingerprint(char *out, unsigned n);
#ifdef __cplusplus
}
#endif
#endif
