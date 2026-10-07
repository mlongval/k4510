/* The machine's debugging aids: WATCH, the PC/key/log recorder and DUMP,
 * and IDEA's brainshots -- each writes what the machine was doing to a file. */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include "io.h"
#include "io_int.h"
#include "mem.h"
#include "vicky.h"
#include "xemu/emutools_basicdefs.h"
#include "xemu/cpu65.h"
static int dbg_num;
static int dbg_auto; static uint32_t dbg_auto_next;
/* The text map as the console has it, for the dumps and the brainshots: a row
 * is JIM's stride of cells (up to 180 in MODE 5), and the physical rows follow
 * the mode -- an 80x60 read garbled every line at 90 columns (the Dell's
 * brainshot, 2026-09-14). */
void screen_geom(int *cols, int *rows)
{
    uint8_t ctrl = vicky_read(0), l0 = vicky_read(0x10); int st = io_read(0xDA0D), ch = vicky_cell_h(0);
    *cols = st > 0 && st <= 180 ? st : 80;
    *rows = (ctrl & 0x20) ? vicky_glass_h() / ch : (ctrl & 8) ? 25 : ((ctrl & 6) || (l0 & 0x60)) ? 30 : 60;
}
/* ---- WATCH: a write watchpoint on physical RAM (core/io.h) -------------- */
uint32_t dbg_watch_addr;
uint8_t  dbg_watch_ctl, dbg_watch_hits;
void dbg_watch_hit(void)
{
    if (dbg_watch_hits != 255) dbg_watch_hits++;
    dbg_watch_ctl = 0;                       /* one bug, one dump */
    dbg_dump("watch");
}

/* ---- debug recorder and DUMP ------------------------------------------- */
#define DBG_PCS 4096
#define DBG_KEYS 256
#define DBG_LOG 8192
static uint16_t dbg_pcs[DBG_PCS]; static uint32_t dbg_pci;
static uint8_t dbg_keys[DBG_KEYS]; static uint32_t dbg_keyi;
static char dbg_log[DBG_LOG]; static uint32_t dbg_logi;
void dbg_pc(uint16_t pc) { dbg_pcs[dbg_pci++ & (DBG_PCS - 1)] = pc; if (XEMU_UNLIKELY(mem_fence.deep)) mem_fence_fetch(pc); }   /* ...and the fence's deep mode (core/mem.h) */
extern int dbg_rec;
void dbg_key(uint8_t k) { dbg_keys[dbg_keyi++ & (DBG_KEYS - 1)] = k; }
static void dbg_logc(uint8_t c) { dbg_log[dbg_logi++ & (DBG_LOG - 1)] = (char)c; }
extern uint8_t vicky_read(uint8_t r);
int dbg_dump(const char *why)
{
    char name[64]; FILE *f; time_t t = time(NULL); struct tm *m = localtime(&t);
    mkdir("dumps", 0777);
    if (++dbg_num > 100) dbg_num = 1;                  /* rotate: at most 100 files */
    snprintf(name, sizeof name, "dumps/dump-%03d.txt", dbg_num);
    if (!(f = fopen(name, "w"))) { dbg_num--; return -1; }
    fprintf(f, "K4510 dump %d  %04d-%02d-%02d %02d:%02d:%02d  (%s)\n", dbg_num, m->tm_year + 1900, m->tm_mon + 1, m->tm_mday, m->tm_hour, m->tm_min, m->tm_sec, why);
    fprintf(f, "frame %u  cwd /%s\n\n", (unsigned)sys_frames, fs_cwd);
    { uint8_t pf = cpu65_get_pf(); char fl[8]; const char *names = "NVEBDIZC";
      for (int b = 0; b < 8; b++) fl[b] = (pf & (0x80 >> b)) ? names[b] : '-';
      fprintf(f, "CPU  PC=%04X A=%02X X=%02X Y=%02X Z=%02X SP=%04X  P=%02X (%.8s)  B=%04X  inhibit=%d\n",
            cpu65.pc, cpu65.a, cpu65.x, cpu65.y, cpu65.z, cpu65.s | cpu65.sphi, pf, fl, cpu65.bphi, cpu65.cpu_inhibit_interrupts); }
    { const k4510_map_t *mp = mem_map_state();
      fprintf(f, "MAP  mask=%02X off_lo=%05X off_hi=%05X mb_lo=%X mb_hi=%X   BANKS mask=%02X", mp->mask, mp->offset_low, mp->offset_high, mp->mb_low >> 20, mp->mb_high >> 20, mem_bank_mask());
      for (int b = 0; b < 8; b++) if (mem_bank_get(b) != BANK_OFF) fprintf(f, " %d=%07X", b, mem_bank_get(b));
      fprintf(f, "   FAR table=%07X depth=%d err=%d\n", far_table, far_depth, far_err); }
    fprintf(f, "VICKY ctrl=%02X bg=%02X irqst=%02X irqmask=%02X\n", vicky_read(0), vicky_read(1), vicky_read(4), vicky_read(5));
    for (int n = 0; n < 4; n++) { fprintf(f, "  layer %d:", n); for (int i = 0; i < 16; i++) fprintf(f, " %02X", vicky_read(0x10 + n * 16 + i)); fprintf(f, "\n"); }
    fprintf(f, "  sprites=%02X sheila=%02X list=", vicky_read(0x0E), vicky_read(0x64)); for (int i = 3; i >= 0; i--) fprintf(f, "%02X", vicky_read(0x60 + i)); fprintf(f, "\n");
    { size_t nfs; const uint8_t *fs_reg = hostfs_regs(&nfs), *dma_reg = io_dma_regs();
      fprintf(f, "FS   reg:"); for (int i = 0; i < (int)nfs; i++) fprintf(f, " %02X", fs_reg[i]); fprintf(f, "   DMA:"); for (int i = 0; i < 14; i++) fprintf(f, " %02X", dma_reg[i]); fprintf(f, "\n"); }
    fred_dump(f);
    fprintf(f, "\nSCREEN (text layer at $030000, 80 columns):\n");
    { int sc, sr; screen_geom(&sc, &sr);
      for (int y = 0; y < sr; y++) { char r[181]; int last = -1; for (int x = 0; x < sc; x++) { uint8_t ch = k4510_ram[vicky_text_cell(x, y)]; r[x] = (ch >= 0x20 && ch < 0x7F) ? ch : (ch ? '.' : ' '); if (r[x] != ' ') last = x; } r[last + 1] = 0; if (last >= 0) fprintf(f, "%2d|%s\n", y, r); } }
    fprintf(f, "\nSHELL LOG (command lines and DUMP notes, oldest first):\n");
    { uint32_t n = dbg_logi < DBG_LOG ? dbg_logi : DBG_LOG, start = dbg_logi - n; for (uint32_t i = 0; i < n; i++) fputc(dbg_log[(start + i) & (DBG_LOG - 1)], f); fprintf(f, "\n"); }
    fprintf(f, "\nKEYS (last %u, oldest first, hex):", dbg_keyi < DBG_KEYS ? dbg_keyi : DBG_KEYS);
    { uint32_t n = dbg_keyi < DBG_KEYS ? dbg_keyi : DBG_KEYS, start = dbg_keyi - n; for (uint32_t i = 0; i < n; i++) { uint8_t k = dbg_keys[(start + i) & (DBG_KEYS - 1)]; if (k >= 0x20 && k < 0x7F) fprintf(f, " %c", k); else fprintf(f, " %02X", k); } fprintf(f, "\n"); }
    fprintf(f, "\nPC HISTORY (last %u opcode fetches, oldest first; runs of consecutive PCs collapsed as a-b):\n", dbg_pci < DBG_PCS ? dbg_pci : DBG_PCS);
    { uint32_t n = dbg_pci < DBG_PCS ? dbg_pci : DBG_PCS, start = dbg_pci - n; int col = 0; uint16_t run0 = 0, prev = 0; int inrun = 0;
      for (uint32_t i = 0; i <= n; i++) {
          uint16_t pc = i < n ? dbg_pcs[(start + i) & (DBG_PCS - 1)] : 0; int seq = i < n && inrun && pc > prev && pc - prev <= 3;
          if (i == 0) { run0 = pc; prev = pc; inrun = 1; continue; }
          if (seq) { prev = pc; continue; }
          if (run0 == prev) col += fprintf(f, "%04X ", run0); else col += fprintf(f, "%04X-%04X ", run0, prev);
          if (col > 90) { fprintf(f, "\n"); col = 0; }
          run0 = pc; prev = pc;
      }
      fprintf(f, "\n"); }
    fprintf(f, "\nZERO PAGE:\n"); for (int i = 0; i < 256; i += 32) { fprintf(f, "%02X:", i); for (int j = 0; j < 32; j++) fprintf(f, " %02X", k4510_ram[i + j]); fprintf(f, "\n"); }
    fprintf(f, "K/OS BASE PAGE $0600 (its zero page, the B register's; ARGS at $0630):\n"); for (int i = 0; i < 64; i += 32) { fprintf(f, "%04X:", 0x0600 + i); for (int j = 0; j < 32; j++) fprintf(f, " %02X", k4510_ram[0x0600 + i + j]); fprintf(f, "\n"); }
    fprintf(f, "K/OS STACK $0700-$07FF:\n"); for (int i = 0x700; i < 0x800; i += 32) { fprintf(f, "%04X:", i); for (int j = 0; j < 32; j++) fprintf(f, " %02X", k4510_ram[i + j]); fprintf(f, "\n"); }
    fprintf(f, "STACK $0100-$01FF:\n"); for (int i = 0x100; i < 0x200; i += 32) { fprintf(f, "%04X:", i); for (int j = 0; j < 32; j++) fprintf(f, " %02X", k4510_ram[i + j]); fprintf(f, "\n"); }
    fprintf(f, "$0300-$04FF (EhBASIC vectors, input buffer, K4510 glue state):\n"); for (int i = 0x300; i < 0x500; i += 32) { fprintf(f, "%04X:", i); for (int j = 0; j < 32; j++) fprintf(f, " %02X", k4510_ram[i + j]); fprintf(f, "\n"); }
    fclose(f);
    fprintf(stderr, "K4510: %s written (%s)\n", name, why);
    return dbg_num;
}

/* ---- IDEA: brainshots ----------------------------------------------------
 * Doc, 2026-09-14: "the text equivalent of a screenshot ... when I use
 * things I often get a small brain fart to improve it, but you are often
 * busy ... and then I forget it."  The ROM's IDEA sends the text a byte at a
 * time to SYS+$42 and writes SYS+$43 (1: that text is the idea, 2: an empty
 * one, for VI); this writes /SYSTEM/BRAINSHOTS/IDEA-date-time.TXT -- the idea, then
 * the machine as it was, which the ROM could not know: the host's time, the
 * directory, what was running (the top band's title), the build, the
 * screen.  Reading SYS+$43 hands the file's name back, a byte at a time, so
 * IDEA alone can open VI on it. */
static char idea_txt[256], idea_path[64];
static unsigned idea_n, idea_rd;
void idea_add(uint8_t c) { if (c >= 0x20 && idea_n < sizeof idea_txt - 1) idea_txt[idea_n++] = (char) c; }
uint8_t idea_next(void) { return idea_path[idea_rd] ? (uint8_t) idea_path[idea_rd++] : 0; }
void idea_write(uint8_t how)
{
    char host[800]; time_t t = time(NULL); struct tm *m = localtime(&t); FILE *f;
    idea_txt[idea_n] = 0; idea_n = 0; idea_rd = 0; idea_path[0] = 0;
    /* /SYSTEM/BRAINSHOTS since 2026-09-15 (Doc); /BRAINSHOTS, where they were, moves
     * there whole the first time, so none is left behind */
    { char old[800]; snprintf(old, sizeof old, "%s/BRAINSHOTS", fs_root);
      snprintf(host, sizeof host, "%s/SYSTEM/BRAINSHOTS", fs_root);
      if (access(host, F_OK) != 0 && access(old, F_OK) == 0) rename(old, host); }
    mkdir(host, 0777);
    for (int k = 1; k < 100; k++) {                   /* two in one second: -2, -3 ... */
        if (k == 1) snprintf(idea_path, sizeof idea_path, "/SYSTEM/BRAINSHOTS/IDEA-%04d%02d%02d-%02d%02d%02d.TXT",
                             m->tm_year + 1900, m->tm_mon + 1, m->tm_mday, m->tm_hour, m->tm_min, m->tm_sec);
        else snprintf(idea_path, sizeof idea_path, "/SYSTEM/BRAINSHOTS/IDEA-%04d%02d%02d-%02d%02d%02d-%d.TXT",
                      m->tm_year + 1900, m->tm_mon + 1, m->tm_mday, m->tm_hour, m->tm_min, m->tm_sec, k);
        snprintf(host, sizeof host, "%s%s", fs_root, idea_path);
        if (access(host, F_OK) != 0) break;
    }
    if (!(f = fopen(host, "w"))) { idea_path[0] = 0; return; }
    fprintf(f, "%s\n\n", how == 1 ? idea_txt : "");
    fprintf(f, "-- the machine, as it was --\n");
    fprintf(f, "when     %04d-%02d-%02d %02d:%02d:%02d\n", m->tm_year + 1900, m->tm_mon + 1, m->tm_mday, m->tm_hour, m->tm_min, m->tm_sec);
    fprintf(f, "where    /%s\n", fs_cwd);
    fprintf(f, "running  %s\n", io_title());
    fprintf(f, "build    %.16s\n", sys_version);
    fprintf(f, "screen\n");
    int sc, sr; screen_geom(&sc, &sr);
    for (int y = 0; y < sr; y++) {
        char r[181]; int last = -1;
        for (int x = 0; x < sc; x++) { uint8_t ch = k4510_ram[vicky_text_cell(x, y)]; r[x] = (ch >= 0x20 && ch < 0x7F) ? (char) ch : (ch ? '.' : ' '); if (r[x] != ' ') last = x; }
        r[last + 1] = 0;
        if (last >= 0) fprintf(f, "  |%s\n", r);
    }
    fclose(f);
    fprintf(stderr, "K4510: brainshot %s\n", idea_path);
}

/* SYS's debug registers: WATCH's address ($30-$33), arm ($34) and hits ($35);
 * DUMP ($F0, a write dumps, a read is the last dump's number), the shell log
 * ($F1) and the 15-second auto-dump ($F2). */
int dbg_reg_read(uint8_t r, uint8_t *v)
{
    if (r >= 0x30 && r <= 0x33) { *v = (uint8_t)(dbg_watch_addr >> (8 * (r - 0x30))); return 1; }
    if (r == 0x34) { *v = dbg_watch_ctl; return 1; }
    if (r == 0x35) { *v = dbg_watch_hits; return 1; }
    if (r == 0xF0) { *v = (uint8_t)dbg_num; return 1; }
    if (r == 0xF2) { *v = (uint8_t)dbg_auto; return 1; }
    return 0;
}
void dbg_reg_write(uint8_t r, uint8_t v)
{
    if (r >= 0x30 && r <= 0x33) { uint8_t sh = 8 * (r - 0x30);
        dbg_watch_addr = (dbg_watch_addr & ~(0xFFu << sh)) | ((uint32_t)v << sh); dbg_watch_addr &= K4510_PHYS_MASK; }
    if (r == 0x34) { dbg_watch_ctl = v ? 1 : 0; dbg_watch_hits = 0; dbg_rec = dbg_watch_ctl ? 1 : dbg_rec; }
    if (r == 0xF0) { dbg_rec = 1; dbg_dump("DUMP register"); }
    if (r == 0xF1) dbg_logc(v);
    if (r == 0xF2) { dbg_auto = v ? 1 : 0; dbg_rec = dbg_auto ? 1 : dbg_rec; dbg_auto_next = sys_frames + 900; }
}
void dbg_frame(void) { if (dbg_auto && sys_frames >= dbg_auto_next) { dbg_auto_next = sys_frames + 900; dbg_dump("auto, 15 s"); } }
/* Off by default on BOTH now (Doc, 2026-08-27): auto-dump every 15 s was
   intrusive, and dbg_rec -- the PC recorder -- costs a store per emulated
   instruction whether or not a dump is ever written.  DUMP ON re-arms both
   when you actually want to debug; DUMP writes state on demand without it. */
void dbg_reset(void) { dbg_auto = 0; dbg_rec = 0; }
