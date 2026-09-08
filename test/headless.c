/* Headless runner: boot a ROM, type keys (one per frame from frame 5), run
 * until a marker string appears on the text screen or MAXFRAMES pass, then
 * print the text screen (non-blank rows).  Used by test/benchmarks.sh.
 *   headless ROM "keys" MAXFRAMES [marker]      marker may be "a|b" (either); a ~ in keys waits 30 frames
 * Exit status 0 if the marker was seen (or none given), 2 on timeout. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/xemu/emutools_basicdefs.h"
#include "../core/xemu/cpu65.h"
#include "../core/mem.h"
#include "../core/io.h"
#include "../core/vicky.h"
static uint8_t fb[640 * 480];
/* the wall clock for SYS+$36.  The harness runs flat out, so this is the only
 * place in it where real time and frame time genuinely differ. */
#include <time.h>
static uint32_t hl_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
                              return (uint32_t)(t.tv_sec * 1000u + t.tv_nsec / 1000000u); }
static void row(int r, char *out) { for (int c = 0; c < 80; c++) { uint8_t ch = mem_peek(0x30000 + (r * 80 + c) * 4); out[c] = (ch >= 0x20 && ch < 0x7F) ? ch : ' '; } out[80] = 0; for (int i = 79; i >= 0 && out[i] == ' '; i--) out[i] = 0; }
static int on_screen(const char *s) { char r[81]; for (int i = 0; i < 60; i++) { row(i, r); if (strstr(r, s)) return 1; } return 0; }
/* marker may be "a|b": either string */
static int marker_seen(const char *m) { char buf[256]; strncpy(buf, m, 255); buf[255] = 0; for (char *p = strtok(buf, "|"); p; p = strtok(NULL, "|")) if (on_screen(p)) return 1; return 0; }
int main(int argc, char **argv)
{
    const char *rom = argc > 1 ? argv[1] : "rom/kernal.bin"; const char *keys = argc > 2 ? argv[2] : "";
    int maxf = argc > 3 ? atoi(argv[3]) : 600; const char *marker = argc > 4 ? argv[4] : NULL;
    size_t ki = 0, kn = strlen(keys); int fr, seen = 0, wait_until = 0;
    uint8_t font[2048]; FILE *ff = fopen("data/font8.bin", "rb"); if (!ff || fread(font, 1, 2048, ff) != 2048) { fprintf(stderr, "font\n"); return 1; } fclose(ff);
    if (mem_init()) return 1; fs_set_root("fs"); mem_load(K4510_FONT8_PHYS, font, 2048); if (mem_load_rom(rom) <= 0) { fprintf(stderr, "rom\n"); return 1; }
    { extern void io_set_ms_source(uint32_t (*)(void)); io_set_ms_source(hl_ms); }
    if (getenv("K4510_HOST_SHELL")) io_host_shell = 1;      /* bangtest.sh: the `!` path, gated as the frontend gates it */
    io_reset(); cpu65_reset();
    for (fr = 0; fr < maxf; fr++) {
        /* as the frontend's K4510_KEYS: $80+ is a KEY_* code, $1F makes the next byte a character */
        if (fr >= 5 && ki < kn && fr >= wait_until) { uint8_t k = (uint8_t)keys[ki++]; if (k == '~') wait_until = fr + 30; else if (k == 0x1F && ki < kn) kbd_push((uint8_t)keys[ki++]); else if (k >= 0x80) kbd_push_key(k); else kbd_push(k == '\n' ? 0x0D : k); }
        vicky_begin_frame(fb, 640);
        for (int y = 0; y < 480; y++) { cpu65.irqLevel = vicky_irq() ? 1 : 0; cpu65_step(40500000 / 60 / 480); vicky_line(y); }
        vicky_end_frame();
        if (marker && ki >= kn && marker_seen(marker)) { seen = 1; break; }
    }
    for (int i = 0; i < 60; i++) { char r[81]; row(i, r); if (*r) printf("%s\n", r); }
    { const char *d = getenv("K4510_DUMP"); if (d) { unsigned long a, n; if (sscanf(d, "%lx,%lx", &a, &n) == 2) { printf("dump $%06lX:", a); for (unsigned long i = 0; i < n; i++) { uint8_t b = mem_peek(a + i); printf(i % 32 ? " %02X" : "\n%02X", b); } printf("\n"); } } }
    if (getenv("K4510_EXITDUMP")) dbg_dump("headless exit");
    fprintf(stderr, "[%d frames%s]\n", fr, marker ? (seen ? ", marker seen" : ", TIMEOUT") : "");
    return marker && !seen ? 2 : 0;
}
