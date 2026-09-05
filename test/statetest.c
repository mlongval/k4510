/* Save states: the ROM runs a while, the machine is saved, wrecked, loaded, and must carry on exactly. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../core/xemu/emutools_basicdefs.h"
#include "../core/xemu/cpu65.h"
#include "../core/mem.h"
#include "../core/io.h"
#include "../core/vicky.h"
#include "../core/audio.h"
#include "../core/state.h"
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)
#define CYCLES_PER_LINE (40500000 / 60 / VICKY_HEIGHT)
static uint8_t fb[VICKY_WIDTH * VICKY_HEIGHT];
static void frames(int n)
{
    while (n--) {
        vicky_begin_frame(fb, VICKY_WIDTH);
        for (int y = 0; y < VICKY_HEIGHT; y++) { cpu65.irqLevel = vicky_irq() ? 1 : 0; cpu65_step(CYCLES_PER_LINE); vicky_line(y); int16_t t[256]; audio_render(CYCLES_PER_LINE, t, 256); }
        vicky_end_frame();
    }
}
static void type(const char *s) { while (*s) { kbd_push(*s == '\n' ? 0x0D : (uint8_t) *s); s++; frames(1); } }
static uint32_t screen_sum(void) { uint32_t h = 0; for (int i = 0; i < 80 * 60 * 4; i++) h = h * 31 + k4510_ram[0x30000 + i]; return h; }
int main(void)
{
    const char *path = "test/statetest.k4s";
    uint8_t font[2048]; FILE *f = fopen("data/font8.bin", "rb"); if (!f || fread(font, 1, 2048, f) != 2048) { printf("need data/font8.bin\n"); return 1; } fclose(f);
    /* /STARTUP.BAT is the user's own file and gitignored, so it differs from
     * machine to machine.  One that ends in CAPSLOCK makes the machine echo
     * typed capitals as lower case -- correctly -- and every assertion here
     * about what is on screen then fails.  The test owns its boot. */
    io_set_opts(SYSOPT_NOBOOT);
    mem_init(); io_reset(); fs_set_root("fs"); mem_load(K4510_FONT8_PHYS, font, 2048); mem_load_rom("rom/kernal.bin"); audio_init(40500000.0, 48000); cpu65_reset();
    frames(120); type("ECHO SAVED HERE\n"); frames(30);
    uint32_t sum1 = screen_sum(); uint16_t pc1 = cpu65.pc; uint8_t cx = io_read(0xDA09), cy = io_read(0xDA0A);
    io_write(0xD702, 0x55);                                   /* a MATH register, a DMA register: device state, not RAM */
    io_write(0xD208, 0x77);
    CHECK(state_save(path) == 0, "save");
    printf("1. saved after 'ECHO SAVED HERE' (PC $%04X, cursor %d,%d)\n", pc1, cx, cy);
    type("CLS\n"); frames(30); cpu65_reset(); frames(60);
    CHECK(screen_sum() != sum1, "the screen changed meanwhile");
    io_write(0xD702, 0); io_write(0xD208, 0);
    CHECK(state_load(path) == 0, "load");
    CHECK(screen_sum() == sum1, "screen back");
    CHECK(cpu65.pc == pc1, "PC back ($%04X vs $%04X)", cpu65.pc, pc1);
    CHECK(io_read(0xDA09) == cx && io_read(0xDA0A) == cy, "JIM's cursor back");
    CHECK(io_read(0xD702) == 0x55 && io_read(0xD208) == 0x77, "MATH and DMA registers back (%02X %02X)", io_read(0xD702), io_read(0xD208));
    printf("2. wrecked, loaded: screen, PC, JIM, device registers back\n");
    type("ECHO STILL ALIVE\n"); frames(30);
    { int found = 0; for (int i = 0; i < 80 * 60 * 4 - 40; i += 4) if (k4510_ram[0x30000 + i] == 'S' && k4510_ram[0x30000 + i + 4] == 'T' && k4510_ram[0x30000 + i + 8] == 'I' && k4510_ram[0x30000 + i + 12] == 'L') found = 1;
      CHECK(found, "the machine runs on after the load"); }
    printf("3. runs on: 'STILL ALIVE' printed after the load\n");
    { struct { char m[8]; } bad = { "K4510ST9" }; f = fopen(path, "wb"); fwrite(&bad, 1, 8, f); fclose(f); CHECK(state_load(path) == -2, "another version refused"); }
    CHECK(state_load("test/none.k4s") == -1, "no file -> -1");
    remove(path);
    printf(fails ? "\n%d FAILED\n" : "\nALL OK\n", fails); return fails != 0;
}
