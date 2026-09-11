#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "../core/xemu/emutools_basicdefs.h"
#include "../core/xemu/cpu65.h"
#include "../core/mem.h"
#include "../core/io.h"
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)
static void W32(uint16_t r, uint32_t v) { for (int i = 0; i < 4; i++) io_write(r + i, (v >> (8 * i)) & 0xFF); }
static uint32_t R32(uint16_t r) { uint32_t v = 0; for (int i = 0; i < 4; i++) v |= (uint32_t)io_read(r + i) << (8 * i); return v; }
/* The tests own their fixture: hello.txt used to be shipped in fs/, so
 * deleting it broke them. Created here, removed at the end. */
static void fixture(int make)
{
    if (make) { FILE *f = fopen("fs/hello.txt", "wb"); if (f) { fputs("hello from the host filesystem\n", f); fclose(f); } }
    else remove("fs/hello.txt");
}
int main(void)
{
    fixture(1);
    /* /STARTUP.BAT is the user's file and is gitignored, so it differs from
     * machine to machine -- one that ends in CLS wipes the banner this test
     * looks for.  Tell the ROM not to run it: the test owns its boot. */
    io_set_opts(SYSOPT_NOBOOT);
    mem_init(); fs_set_root("fs");
    mem_load(0x0300, (const uint8_t *)"hello.txt", 10);
    W32(IO_FS_NAMEPTR, 0x300); W32(IO_FS_ADDR, 0x100000); io_write(IO_FS_CMD, FS_LOAD);
    uint32_t n = R32(IO_FS_LEN);
    printf("1. LOAD hello.txt -> status %d, %u bytes: '%.*s'\n", io_read(IO_FS_STATUS), n, (int)(n > 20 ? 20 : n), (char *)&k4510_ram[0x100000]);
    CHECK(io_read(IO_FS_STATUS) == 0 && n == 31 && memcmp(&k4510_ram[0x100000], "hello from", 10) == 0, "load");
    mem_load(0x0300, (const uint8_t *)"nope.bin", 9); io_write(IO_FS_CMD, FS_STAT);
    CHECK(io_read(IO_FS_STATUS) == 1, "stat missing -> 1");
    mem_load(0x0300, (const uint8_t *)"out.bin", 8); mem_load(0x200000, (const uint8_t *)"K4510!", 6);
    W32(IO_FS_ADDR, 0x200000); W32(IO_FS_LEN, 6); io_write(IO_FS_CMD, FS_SAVE);
    CHECK(io_read(IO_FS_STATUS) == 0, "save");
    io_write(IO_FS_CMD, FS_STAT); CHECK(R32(IO_FS_SIZE) == 6, "saved size");
    io_write(IO_FS_CMD, FS_DIR_FIRST); int count = 0, seen = 0;
    for (;;) { W32(IO_FS_ADDR, 0x400); io_write(IO_FS_CMD, FS_DIR_NEXT); if (io_read(IO_FS_STATUS)) break; count++; if (!strcmp((char *)&k4510_ram[0x400], "out.bin")) seen = 1; }
    printf("2. DIR: %d entries, out.bin seen=%d\n", count, seen);

    /* 3. the date and time of a file, packed at $D314/$D316 for DIR -l (2026-09-11) */
    { struct stat sb; struct tm *m; uint16_t d, t;
      strcpy((char *)&k4510_ram[0x300], "hello.txt"); W32(IO_FS_NAMEPTR, 0x300); io_write(IO_FS_CMD, FS_STAT);
      d = (uint16_t)(io_read(IO_FS_WHEN) | (io_read(IO_FS_WHEN + 1) << 8)); t = (uint16_t)(io_read(IO_FS_WHEN + 2) | (io_read(IO_FS_WHEN + 3) << 8));
      CHECK(io_read(IO_FS_STATUS) == 0, "STAT hello.txt");
      if (stat("fs/hello.txt", &sb) == 0 && (m = localtime(&sb.st_mtime))) {
          CHECK((d & 31) == m->tm_mday && ((d >> 5) & 15) == m->tm_mon + 1 && 1980 + (d >> 9) == m->tm_year + 1900, "STAT date %04x is not %d-%02d-%02d", d, m->tm_year + 1900, m->tm_mon + 1, m->tm_mday);
          CHECK((t >> 8) == m->tm_hour && (t & 255) == m->tm_min, "STAT time %04x is not %02d:%02d", t, m->tm_hour, m->tm_min);
      } else CHECK(0, "cannot stat fs/hello.txt on the host");
      printf("3. STAT date/time of hello.txt: %02d.%02d.%d %02d:%02d\n", d & 31, (d >> 5) & 15, 1980 + (d >> 9), t >> 8, t & 255); }
    CHECK(count >= 2 && seen, "dir");
    mem_load(0x0300, (const uint8_t *)"../etc/passwd", 14); io_write(IO_FS_CMD, FS_STAT);
    CHECK(io_read(IO_FS_STATUS) == 1, "sandbox");
    remove("fs/out.bin");
    fixture(0);
    printf(fails ? "\n%d FAILED\n" : "\nALL OK\n", fails); return fails != 0;
}
