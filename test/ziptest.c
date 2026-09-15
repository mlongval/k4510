/* test/ziptest.c -- MOUNT a zip, through the machine's own file registers.
 * Run by test/ziptest.sh, which makes the zips (DIR/fs/HOME) and the files
 * they hold (DIR/src) and passes DIR. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/xemu/emutools_basicdefs.h"
#include "../core/xemu/cpu65.h"
#include "../core/mem.h"
#include "../core/io.h"
#include "../core/zip.h"
static int fails = 0;
static const char *T;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)
static void W32(uint16_t r, uint32_t v) { for (int i = 0; i < 4; i++) io_write(r + i, (v >> (8 * i)) & 0xFF); }
static uint32_t R32(uint16_t r) { uint32_t v = 0; for (int i = 0; i < 4; i++) v |= (uint32_t)io_read(r + i) << (8 * i); return v; }
/* one command: NAME at $300, a second name (MOUNT's path, COPY's target) at $380 */
static int fs(uint8_t cmd, const char *name, const char *second)
{
    strcpy((char *)&k4510_ram[0x300], name); W32(IO_FS_NAMEPTR, 0x300);
    if (second) { strcpy((char *)&k4510_ram[0x380], second); W32(IO_FS_ADDR, 0x380); }
    else W32(IO_FS_ADDR, 0x100000);
    W32(IO_FS_LEN, 0);
    io_write(IO_FS_CMD, cmd);
    return io_read(IO_FS_STATUS);
}
static uint8_t *slurp(const char *path, long *n)
{
    FILE *f = fopen(path, "rb"); uint8_t *b; if (!f) { *n = -1; return NULL; }
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    b = malloc(*n + 1); if (fread(b, 1, *n, f) != (size_t)*n) *n = -1; fclose(f); return b;
}
/* LOAD guest NAME; it must be the host's src/WANT, byte for byte */
static void same(const char *name, const char *want)
{
    char p[1024]; long n; uint8_t *b; int st;
    snprintf(p, sizeof p, "%s/src/%s", T, want); b = slurp(p, &n);
    st = fs(FS_LOAD, name, NULL);
    CHECK(st == 0 && n >= 0 && R32(IO_FS_LEN) == (uint32_t)n && !memcmp(&k4510_ram[0x100000], b, n), "LOAD %s (status %d, %u bytes, want %ld)", name, st, R32(IO_FS_LEN), n);
    free(b);
}
int main(int argc, char **argv)
{
    char root[1024], p[1024]; int st;
    if (argc < 2) { fprintf(stderr, "usage: ziptest DIR (run by test/ziptest.sh)\n"); return 2; }
    T = argv[1];
    io_set_opts(SYSOPT_NOBOOT); mem_init();
    snprintf(root, sizeof root, "%s/fs", T); fs_set_root(root);

    printf("1. MOUNT GOOD.ZIP and read every file in it\n");
    CHECK(fs(FS_MOUNT, "/HOME/GOOD.ZIP", "/MNT/GOOD") == 0, "MOUNT GOOD.ZIP");
    same("/MNT/GOOD/README.TXT", "README.TXT");
    same("/MNT/GOOD/BIN/HELLO.PRG", "BIN/HELLO.PRG");
    same("/MNT/GOOD/DATA/BIG.TXT", "DATA/BIG.TXT");
    same("/MNT/GOOD/DATA/RAND.BIN", "DATA/RAND.BIN");
    same("/MNT/GOOD/EMPTY.TXT", "EMPTY.TXT");
    same("/MNT/GOOD/mixed.CASE.TXT", "Mixed.Case.txt");                        /* any case */

    printf("2. STAT: files, folders listed and not, nothing\n");
    CHECK(fs(FS_STAT, "/MNT/GOOD/README.TXT", NULL) == 0 && R32(IO_FS_SIZE) == 25, "STAT README.TXT size %u", R32(IO_FS_SIZE));
    CHECK(fs(FS_STAT, "/MNT/GOOD/DATA", NULL) == 0 && R32(IO_FS_SIZE) == 0xFFFFFFFFu, "STAT DATA (a folder by its files)");
    CHECK(fs(FS_STAT, "/MNT/GOOD/DOCS", NULL) == 0 && R32(IO_FS_SIZE) == 0xFFFFFFFFu, "STAT DOCS (a folder by its own entry)");
    CHECK(fs(FS_STAT, "/MNT/GOOD/NOPE.TXT", NULL) == 1, "STAT a name not there -> 1");

    printf("3. CD into it, DIR, a bare name, CD ..\n");
    CHECK(fs(FS_CHDIR, "/MNT/GOOD", NULL) == 0, "CD /MNT/GOOD");
    { static const char *want[] = { "BIN", "DATA", "DOCS", "EMPTY.TXT", "Mixed.Case.txt", "README.TXT" }; int k = 0, ok = 1;
      io_write(IO_FS_CMD, FS_DIR_FIRST); CHECK(io_read(IO_FS_STATUS) == 0, "DIR_FIRST");
      for (;;) { W32(IO_FS_ADDR, 0x400); io_write(IO_FS_CMD, FS_DIR_NEXT); if (io_read(IO_FS_STATUS)) break;
          const char *nm = (char *)&k4510_ram[0x400]; uint32_t sz = R32(IO_FS_SIZE);
          if (k >= 6 || strcmp(nm, want[k])) ok = 0;
          if (k < 3 && sz != 0xFFFFFFFFu) ok = 0;
          if (!strcmp(nm, "README.TXT") && sz != 25) ok = 0;
          printf("     %-16s %d\n", nm, (int)sz); k++; }
      CHECK(ok && k == 6, "DIR of the zip: %d entries", k); }
    CHECK(fs(FS_CHDIR, "DATA", NULL) == 0, "CD DATA");
    same("BIG.TXT", "DATA/BIG.TXT");
    CHECK(fs(FS_CHDIR, "..", NULL) == 0, "CD ..");
    same("README.TXT", "README.TXT");
    CHECK(fs(FS_CHDIR, "NOPE", NULL) != 0, "CD a folder not there");
    CHECK(fs(FS_CHDIR, "README.TXT", NULL) != 0, "CD a file");
    CHECK(fs(FS_CHDIR, "/HOME", NULL) == 0, "CD /HOME");

    printf("4. read-only: nothing written, made or removed in it\n");
    strcpy((char *)&k4510_ram[0x100000], "new");
    strcpy((char *)&k4510_ram[0x300], "/MNT/GOOD/NEW.TXT"); W32(IO_FS_NAMEPTR, 0x300); W32(IO_FS_ADDR, 0x100000); W32(IO_FS_LEN, 3);
    io_write(IO_FS_CMD, FS_SAVE); CHECK(io_read(IO_FS_STATUS) != 0, "SAVE into the zip refused");
    CHECK(fs(FS_OPEN_WRITE, "/MNT/GOOD/NEW.TXT", NULL) != 0, "OPEN for writing refused");
    CHECK(fs(FS_MKDIR, "/MNT/GOOD/NEWDIR", NULL) != 0, "MKDIR refused");
    CHECK(fs(FS_RM, "/MNT/GOOD/README.TXT", NULL) != 0, "RM refused");
    CHECK(fs(FS_COPYFILE, "/HOME/GOOD.ZIP", "/MNT/GOOD/COPY.ZIP") != 0, "COPY into it refused");
    same("/MNT/GOOD/README.TXT", "README.TXT");

    printf("5. COPY out of it\n");
    CHECK(fs(FS_COPYFILE, "/MNT/GOOD/DATA/BIG.TXT", "/HOME/OUT.TXT") == 0, "COPY out");
    { long a, b; uint8_t *x, *y; snprintf(p, sizeof p, "%s/fs/HOME/OUT.TXT", T); x = slurp(p, &a); snprintf(p, sizeof p, "%s/src/DATA/BIG.TXT", T); y = slurp(p, &b);
      CHECK(a == b && a > 0 && !memcmp(x, y, a), "the copy is the file"); free(x); free(y); }

    printf("6. MOUNT lists it\n");
    W32(IO_FS_ADDR, 0x500); W32(IO_FS_LEN, 0); io_write(IO_FS_CMD, FS_MOUNTS);
    CHECK(io_read(IO_FS_STATUS) == 0 && !strcmp((char *)&k4510_ram[0x500], "/MNT/GOOD  /HOME/GOOD.ZIP"), "MOUNT list: '%s'", (char *)&k4510_ram[0x500]);

    printf("7. refused: climbing names, damaged, not a zip, not there\n");
    CHECK(fs(FS_MOUNT, "/HOME/EVIL1.ZIP", "/MNT/E") != 0, "../ in a name");
    CHECK(fs(FS_MOUNT, "/HOME/EVIL2.ZIP", "/MNT/E") != 0, "a name from the root");
    CHECK(fs(FS_MOUNT, "/HOME/EVIL3.ZIP", "/MNT/E") != 0, "a backslash");
    CHECK(fs(FS_MOUNT, "/HOME/EVIL4.ZIP", "/MNT/E") != 0, ".. inside a name");
    CHECK(fs(FS_MOUNT, "/HOME/TRUNC.ZIP", "/MNT/E") != 0, "cut short");
    CHECK(fs(FS_MOUNT, "/HOME/NOTZIP.ZIP", "/MNT/E") != 0, "not a zip");
    CHECK(fs(FS_MOUNT, "/HOME/NOPE.ZIP", "/MNT/E") != 0, "not there");
    CHECK(fs(FS_MOUNT, "/HOME/GOOD.ZIP", "/") != 0, "the root as a mount");
    W32(IO_FS_ADDR, 0x500); W32(IO_FS_LEN, 1); io_write(IO_FS_CMD, FS_MOUNTS);
    CHECK(io_read(IO_FS_STATUS) == 4, "still one mount");

    printf("8. mounts, but its entry is refused: encrypted, damaged\n");
    CHECK(fs(FS_MOUNT, "/HOME/ENC.ZIP", "/MNT/ENC") == 0, "MOUNT ENC.ZIP");
    CHECK(fs(FS_LOAD, "/MNT/ENC/SECRET.TXT", NULL) != 0, "an encrypted entry refused");
    CHECK(fs(FS_MOUNT, "/HOME/BADCRC.ZIP", "/MNT/BAD") == 0, "MOUNT BADCRC.ZIP");
    CHECK(fs(FS_LOAD, "/MNT/BAD/DATA.TXT", NULL) != 0, "a damaged entry refused");

    printf("9. Info-ZIP's zip, an empty zip, a zip inside a zip\n");
    CHECK(fs(FS_MOUNT, "/HOME/INFO.ZIP", "/MNT/INFO") == 0, "MOUNT INFO.ZIP");
    same("/MNT/INFO/DATA/BIG.TXT", "DATA/BIG.TXT");
    same("/MNT/INFO/BIN/HELLO.PRG", "BIN/HELLO.PRG");
    same("/MNT/INFO/EMPTY.TXT", "EMPTY.TXT");
    CHECK(fs(FS_MOUNT, "/HOME/EMPTY.ZIP", "/MNT/NONE") == 0, "MOUNT EMPTY.ZIP");
    CHECK(fs(FS_CHDIR, "/MNT/NONE", NULL) == 0, "CD into it");
    io_write(IO_FS_CMD, FS_DIR_FIRST); W32(IO_FS_ADDR, 0x400); io_write(IO_FS_CMD, FS_DIR_NEXT);
    CHECK(io_read(IO_FS_STATUS) == 4, "and nothing in it");
    CHECK(fs(FS_CHDIR, "/HOME", NULL) == 0, "CD /HOME");
    CHECK(fs(FS_MOUNT, "/HOME/NEST.ZIP", "/MNT/NEST") == 0, "MOUNT NEST.ZIP");
    CHECK(fs(FS_MOUNT, "/MNT/NEST/INNER.ZIP", "/MNT/IN") == 0, "MOUNT the zip inside it");
    same("/MNT/IN/DATA/RAND.BIN", "DATA/RAND.BIN");

    printf("10. a streamed zip (data descriptor), straight through zip.c\n");
    { long n; uint8_t *b, *out; uint32_t ol; zip_t *z;
      snprintf(p, sizeof p, "%s/fs/HOME/STREAM.ZIP", T); b = slurp(p, &n);
      CHECK(n > 0 && zip_open_mem(b, (uint32_t)n, &z) == 0, "open STREAM.ZIP");
      if (n > 0 && z) { st = zip_fetch(z, "-", &out, &ol);
          CHECK(st == 0 && ol == 24 && !memcmp(out, "streamed through a pipe\n", 24), "its entry (status %d, %u bytes)", st, ol);
          if (!st) free(out);
          zip_close(z); } }

    printf("11. UMOUNT, and it is gone\n");
    CHECK(fs(FS_UMOUNT, "/MNT/GOOD", NULL) == 0, "UMOUNT /MNT/GOOD");
    CHECK(fs(FS_LOAD, "/MNT/GOOD/README.TXT", NULL) != 0, "nothing there after");
    same("/MNT/IN/README.TXT", "README.TXT");                                  /* the others moved in the table, and still work */
    CHECK(fs(FS_UMOUNT, "/MNT/GOOD", NULL) != 0, "UMOUNT again -> not mounted");

    printf(fails ? "\n%d FAILED\n" : "\nALL OK\n", fails); return fails != 0;
}
