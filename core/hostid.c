/* The host fingerprint.  See hostid.h. */
#include <stdio.h>
#include <stdint.h>
#include "hostid.h"
#include "host.h"

/* FNV-1a over what the host says it is; the low 31 bits, never zero */
int host_id_hash(void)
{
    char id[256]; host_fingerprint(id, sizeof id);
    uint32_t h = 2166136261u;
    for (const char *p = id; *p; p++) { h ^= (uint8_t)*p; h *= 16777619u; }
    h &= 0x7FFFFFFFu;
    return h ? (int)h : 1;
}

/* A host that has not said what it is.  The Pi's Circle glue can override
 * this with the board revision; until it does, the cache is trusted on any
 * Pi, which is right for a card that stays in one machine. */
__attribute__((weak)) void host_fingerprint(char *out, unsigned n)
{
    snprintf(out, n, "unknown host");
}
