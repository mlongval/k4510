/* Save states. See state.h. */
#include "state.h"
#include "mem.h"
#include "host.h"
#include "term.h"
#include "xemu/emutools_basicdefs.h"
#include "xemu/cpu65.h"
#include <string.h>
#include <stdlib.h>

static const char magic[8] = "K4510ST2";   /* 2: the SID chunks went (2026-09-05) */

int state_put(FILE *f, const char *tag, const void *p, size_t n)
{
    uint32_t len = (uint32_t) n;
    if (fwrite(tag, 1, 4, f) != 4 || fwrite(&len, 4, 1, f) != 1) return -1;
    return n && fwrite(p, 1, n, f) != n ? -1 : 0;
}
int state_get(FILE *f, const char *tag, void *p, size_t n)
{
    char t[4]; uint32_t len;
    if (fread(t, 1, 4, f) != 4 || fread(&len, 4, 1, f) != 1) return -2;
    if (memcmp(t, tag, 4) || len != n) return -2;
    return n && fread(p, 1, n, f) != n ? -2 : 0;
}

#define PAGE 4096u
static void ram_save(FILE *f)
{
    static const uint8_t zero[PAGE];
    uint32_t pages = K4510_PHYS_SIZE / PAGE, used = 0;
    for (uint32_t i = 0; i < pages; i++) if (memcmp(k4510_ram + (size_t) i * PAGE, zero, PAGE)) used++;
    state_put(f, "RAM ", &used, 4);
    for (uint32_t i = 0; i < pages; i++)
        if (memcmp(k4510_ram + (size_t) i * PAGE, zero, PAGE)) { fwrite(&i, 4, 1, f); fwrite(k4510_ram + (size_t) i * PAGE, 1, PAGE, f); }
}
static int ram_load(FILE *f)
{
    uint32_t used, i, n;
    if (state_get(f, "RAM ", &used, 4)) return -2;
    host_zero(k4510_ram, K4510_PHYS_SIZE);
    for (n = 0; n < used; n++) {
        if (fread(&i, 4, 1, f) != 1 || i >= K4510_PHYS_SIZE / PAGE) return -2;
        if (fread(k4510_ram + (size_t) i * PAGE, 1, PAGE, f) != PAGE) return -2;
    }
    return 0;
}

int state_save(const char *path)
{
    FILE *f = fopen(path, "wb"); int cur;
    if (!f) return -1;
    cur = term_cursor_park();              /* the RAM goes out without the cursor XORed into it */
    fwrite(magic, 1, 8, f);
    state_put(f, "CPU ", &cpu65, sizeof cpu65);
    ram_save(f);
    mem_state_save(f);
    vicky_state_save(f);
    io_state_save(f);
    term_state_save(f);
    state_put(f, "END ", 0, 0);
    term_cursor_unpark(cur);
    return fclose(f) ? -1 : 0;
}
int state_load(const char *path)
{
    char m[8]; int rc = -2;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fread(m, 1, 8, f) == 8 && !memcmp(m, magic, 8)
        && !state_get(f, "CPU ", &cpu65, sizeof cpu65)
        && !ram_load(f) && !mem_state_load(f) && !vicky_state_load(f) && !io_state_load(f) && !term_state_load(f)
        && !state_get(f, "END ", 0, 0)) rc = 0;
    fclose(f);
    return rc;
}
