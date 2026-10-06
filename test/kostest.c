/* K/OS's own base page and stacks (2026-10-06), and the stack fence.
 *
 * The ROM runs on base page $06 and 6502 stack page 7, with its C stack in
 * the workspace at $DB00-$DEFF (rom/crt0.s, core/mem.h); a program has the
 * zero page and page 1 to itself.  This checks that it is so -- a program's
 * zero page survives the ROM, a program run from the prompt finds page 1
 * empty, the CPU is back on K/OS's page and stack at the prompt -- and that
 * the fence measures, shows in INFO -m, and trips. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../core/xemu/emutools_basicdefs.h"
#include "../core/xemu/cpu65.h"
#include "../core/mem.h"
#include "../core/io.h"
#include "../core/vicky.h"
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)
static uint8_t fb[640 * 480];
static void frames(int n) { for (; n; n--) { vicky_begin_frame(fb, 640); for (int y = 0; y < 480; y++) { cpu65.irqLevel = vicky_irq() ? 1 : 0; cpu65_step(40500000 / 60 / 480); vicky_line(y); } vicky_end_frame(); } }
static void type(const char *s) { for (; *s; s++) { kbd_push(*s == '\n' ? 0x0D : (uint8_t)*s); frames(1); } frames(3); }
static void row(int r, char *out) { for (int c = 0; c < 80; c++) { uint8_t ch = mem_peek(vicky_text_cell(c, r)); out[c] = (ch >= 0x20 && ch < 0x7F) ? ch : '.'; } out[80] = 0; for (int i = 79; i >= 0 && out[i] == ' '; i--) out[i] = 0; }
static int findsub(const char *sub) { char r[81]; for (int i = 0; i < 60; i++) { row(i, r); if (strstr(r, sub)) return i; } return -1; }
int main(void)
{
    char root[4096], tmp[] = "/tmp/kostest-XXXXXX";
    uint8_t font[2048]; FILE *f = fopen("data/fonts/unscii/font8-unscii.bin", "rb");
    if (!f || fread(font, 1, 2048, f) != 2048) { printf("kostest: no font\n"); return 1; }
    fclose(f);
    if (!realpath("fs", root) || !mkdtemp(tmp)) { printf("kostest: no fs/ or no temp dir\n"); return 1; }
    io_set_opts(SYSOPT_NOBOOT);
    mem_init(); fs_set_root(root); mem_load(K4510_FONT8_PHYS, font, 2048);
    CHECK(mem_load_rom(getenv("K4510_ROM") ? getenv("K4510_ROM") : "rom/kernal.bin") >= 24576, "rom");   /* K4510_ROM: try another (a before-and-after) */
    if (chdir(tmp)) { printf("kostest: chdir\n"); return 1; }   /* the fence's dump lands here, not in the user's dumps/ */
    cpu65_reset(); frames(40);
    CHECK(findsub("/]") >= 0, "prompt");

    /* the fence is armed, on K/OS's base page, over the workspace's stack */
    CHECK(mem_fence.on && mem_fence.page == 0x06 && mem_fence.floor == 0xDB00 && mem_fence.top == 0xDF00,
          "the fence is armed over $DB00-$DF00 on page $06 (on %d page $%02X floor $%04X top $%04X)", mem_fence.on, mem_fence.page, mem_fence.floor, mem_fence.top);
    /* at the prompt the CPU is K/OS's: base page $06, stack page 7 */
    CHECK(cpu65.bphi == 0x0600 && cpu65.sphi == 0x0700, "at the prompt B = $%04X and SPH = $%04X, not $0600 and $0700", cpu65.bphi, cpu65.sphi);

    /* A program: LDA #$5A STA $10 / LDA #'X' JSR CHROUT / LDA $10 STA $5F01 /
     * TSX STX $5F02 / TBA STA $5F03 / TSY STY $5F04 / RTS -- at $3000, run
     * from the prompt.  MONITOR puts it there and is gone before it runs. */
    type("mon 3000:A9 5A 85 10 A9 58 20 80 FF A5 10 8D 01 5F\n");   /* two lines: one was longer than the shell's */
    type("mon 300E:BA 8E 02 5F 7B 8D 03 5F 0B 8C 04 5F 60\n");
    mem_poke(0x5F01, 0); mem_poke(0x5F02, 0); mem_poke(0x5F03, 0xEE); mem_poke(0x5F04, 0xEE);
    type("run 3000\n");
    CHECK(mem_peek(0x5F01) == 0x5A, "the program's zero page did not survive CHROUT ($10 = $%02X)", mem_peek(0x5F01));
    CHECK(mem_peek(0x5F03) == 0x00 && mem_peek(0x5F04) == 0x01, "the program did not run on base page $00 and stack page 1 (B $%02X, SPH $%02X)", mem_peek(0x5F03), mem_peek(0x5F04));
    CHECK(mem_peek(0x5F02) >= 0xF8, "a program run from the prompt found page 1 used down to $01%02X (K/OS's frames there?)", mem_peek(0x5F02));
    type("cd /\n"); type("time\n"); frames(30);
    CHECK(mem_peek(0x10) == 0x5A, "K/OS wrote the zero page: $10 = $%02X after CD and TIME", mem_peek(0x10));
    CHECK(cpu65.bphi == 0x0600 && cpu65.sphi == 0x0700, "back at the prompt B = $%04X and SPH = $%04X", cpu65.bphi, cpu65.sphi);

    /* the measure: INFO -m says it, and nothing so far came near */
    type("cls\n"); type("info -m\n"); frames(30);
    CHECK(findsub("K/OS C stack:") >= 0 && findsub("of 1024 bytes") >= 0, "INFO -m does not show the C stack's measure");
    CHECK(findsub("6502 stack: K/OS down to $07") >= 0, "INFO -m does not show K/OS's own 6502 stack on page 7");
    CHECK(mem_fence.trips == 0 && mem_fence.low > mem_fence.floor + 256, "ordinary use came near the floor (deepest $%04X, %u trips)", mem_fence.low, mem_fence.trips);
    printf("kostest: deepest C stack so far $%04X (%u of %u bytes); K/OS 6502 stack down to $%04X, programs' to $%04X\n",
           mem_fence.low, (unsigned)(mem_fence.top - mem_fence.low), (unsigned)(mem_fence.top - mem_fence.floor), mem_fence.hw_low, mem_fence.prog_low);

    /* the trip: a floor just under the top, and any command crosses it */
    { uint16_t fl = (uint16_t)(mem_fence.top - 8), old = mem_fence.floor;
      io_write(IO_SYS + 0x52, (uint8_t)fl); io_write(IO_SYS + 0x53, (uint8_t)(fl >> 8));
      type("cd /\n"); type("time\n"); frames(30);
      CHECK(mem_fence.trips >= 1, "a stack below the floor did not trip the fence");
      CHECK(access("dumps/dump-001.txt", F_OK) == 0, "the trip wrote no dump");
      { FILE *d = fopen("dumps/dump-001.txt", "r"); char l[256] = ""; if (d) { if (!fgets(l, sizeof l, d)) l[0] = 0; fclose(d); }
        CHECK(strstr(l, "stack fence") != NULL, "the dump does not say why: %s", l); }
      io_write(IO_SYS + 0x52, (uint8_t)old); io_write(IO_SYS + 0x53, (uint8_t)(old >> 8));
      io_write(IO_SYS + 0x54, 0);                               /* the measure starts again */
      CHECK(mem_fence.trips == 0 && mem_fence.low == mem_fence.top, "the reset did not clear the measure");
      type("cd /\n"); type("time\n"); frames(30);
      CHECK(mem_fence.trips == 0, "the old floor still tripped"); }
    remove("dumps/dump-001.txt"); rmdir("dumps"); if (chdir("/")) { } rmdir(tmp);
    printf(fails ? "kostest: %d FAILED\n" : "kostest: OK (K/OS on base page $06 and stack page 7; a program's zero page and page 1 are its own; the fence measures, shows in INFO -m, trips and dumps)\n", fails);
    return fails ? 1 : 0;
}
