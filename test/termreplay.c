/* termreplay -- feed a K4510_TERMLOG back through JIM, byte for byte and
 * register write for register write, in the order the machine did them.
 * Built with AddressSanitizer it stops on the line that goes wrong: the way
 * to chase a crash that only a real session provoked (the Dell, 2026-09-12:
 * the emulator died twice while the `!` shell was printing).
 *
 *     test/termreplay LOG [--host]
 *
 * --host switches JIM's Unix-session mode (UTF-8 on, LNM off) on after every
 * CTRL 1, as the emulator does when the ROM starts a `!` session. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/xemu/emutools_basicdefs.h"
#include "../core/xemu/cpu65.h"
#include "../core/mem.h"
#include "../core/io.h"
#include "../core/term.h"

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s LOG [--host]\n", argv[0]); return 2; }
    int host = argc > 2 && !strcmp(argv[2], "--host");
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *d = malloc((size_t) n + 1);
    if (!d || fread(d, 1, (size_t) n, f) != (size_t) n) { fprintf(stderr, "read failed\n"); return 2; }
    fclose(f);
    mem_init(); io_reset();
    long bytes = 0, regs = 0;
    for (long i = 0; i < n; ) {
        /* the log's own marks: "\n<t HH:MM:SS.mmm>" and "\n<rXX<-VV ...>" */
        if (d[i] == '\n' && i + 2 < n && d[i + 1] == '<' && (d[i + 2] == 't' || d[i + 2] == 'r')) {
            long j = i + 2; while (j < n && d[j] != '>') j++;
            if (d[i + 2] == 'r' && j - i > 8) {
                unsigned r = 0, v = 0;
                if (sscanf((const char *) d + i + 3, "%2x<-%2x", &r, &v) == 2) {
                    io_write((uint16_t)(IO_TERM + r), (uint8_t) v); regs++;
                    if (host && r == 4 && v == 1) term_host_session(1);
                }
            }
            i = j + 1; continue;
        }
        io_write(IO_TERM, d[i]); bytes++; i++;
    }
    printf("replayed %ld bytes and %ld register writes: no crash\n", bytes, regs);
    free(d);
    return 0;
}
