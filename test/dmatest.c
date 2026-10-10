/* C-18 block DMA at $D200, driven from CPU code the way software will.
 * Programs assembled with ACME at test time would be nicer; for now, bytes.
 */
#include <stdio.h>
#include <string.h>
#include "../core/xemu/emutools_basicdefs.h"
#include "../core/xemu/cpu65.h"
#include "../core/mem.h"
#include "../core/io.h"

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static void boot(const uint8_t *prog, size_t len, int cycles)
{
    mem_reset();
    mem_load(0xC000, prog, len);
    mem_poke(K4510_ROM_PHYS + 0xFFFC, 0x00); mem_poke(K4510_ROM_PHYS + 0xFFFD, 0xC0);
    cpu65_reset();
    cpu65_step(cycles);
}

/* STA abs helper bytes */
#define LDA_IMM(v)   0xA9, (v)
#define STA_ABS(a)   0x8D, (a) & 0xFF, ((a) >> 8) & 0xFF
#define SET32(reg, v) LDA_IMM((v)&0xFF), STA_ABS(reg), LDA_IMM(((v)>>8)&0xFF), STA_ABS(reg+1), \
                      LDA_IMM(((v)>>16)&0xFF), STA_ABS(reg+2), LDA_IMM(((v)>>24)&0xFF), STA_ABS(reg+3)

int main(void)
{
    printf("K4510 C-18: block DMA\n");
    CHECK(mem_init() == 0, "mem_init");

    /* 1. copy 64 KB from phys $100000 (1 MB) to $0FF00000 (near the top) */
    {
        static const uint8_t p[] = {
            SET32(IO_DMA_SRC, 0x00100000),
            SET32(IO_DMA_DST, 0x0FF00000),
            SET32(IO_DMA_LEN, 0x00010000),
            LDA_IMM(1), STA_ABS(IO_DMA_CMD),
            0xAD, IO_DMA_CMD & 0xFF, IO_DMA_CMD >> 8,    /* LDA cmd -> 0 when idle */
            STA_ABS(0x0400),
            0x4C, 0x4B, 0xC0,
        };
        mem_reset();
        for (uint32_t i = 0; i < 0x10000; i++) mem_poke(0x100000 + i, (uint8_t)(i * 7));
        boot(p, sizeof p, 2000);
        int ok = 1;
        for (uint32_t i = 0; i < 0x10000; i += 997) if (mem_peek(0x0FF00000 + i) != (uint8_t)(i * 7)) ok = 0;
        printf("1. copy 64K 1MB->255MB: sample ok=%d  idle=$%02X  status=$%02X\n", ok, mem_peek(0x400), io_read(IO_DMA_STATUS));
        CHECK(ok, "copied data mismatch");
        CHECK(mem_peek(0x400) == 0, "DMA not idle after instant transfer");
        CHECK(io_read(IO_DMA_STATUS) == 1, "status should be last cmd");
    }

    /* 2. fill 4800 bytes of screen RAM with $2A from CPU code */
    {
        static const uint8_t p[] = {
            SET32(IO_DMA_SRC, 0x2A),              /* fill value in SRC byte 0 */
            SET32(IO_DMA_DST, 0x0800),
            SET32(IO_DMA_LEN, 4800),
            LDA_IMM(2), STA_ABS(IO_DMA_CMD),
            0x4C, 0x44, 0xC0,
        };
        boot(p, sizeof p, 2000);
        int ok = mem_peek(0x800) == 0x2A && mem_peek(0x800 + 4799) == 0x2A && mem_peek(0x800 + 4800) == 0x00;
        printf("2. fill screen $2A:     first=$%02X last=$%02X after=$%02X\n", mem_peek(0x800), mem_peek(0x800+4799), mem_peek(0x800+4800));
        CHECK(ok, "fill bounds");
    }

    /* 3. overlapping copy (scroll the screen up one row) is memmove-safe */
    {
        mem_reset();
        for (int i = 0; i < 4800; i++) mem_poke(0x800 + i, (uint8_t)(i / 80));   /* row number in each cell */
        static const uint8_t p[] = {
            SET32(IO_DMA_SRC, 0x0800 + 80),
            SET32(IO_DMA_DST, 0x0800),
            SET32(IO_DMA_LEN, 4800 - 80),
            LDA_IMM(1), STA_ABS(IO_DMA_CMD),
            0x4C, 0x44, 0xC0,
        };
        mem_load(0xC000, p, sizeof p); mem_poke(K4510_ROM_PHYS + 0xFFFC, 0x00); mem_poke(K4510_ROM_PHYS + 0xFFFD, 0xC0); cpu65_reset(); cpu65_step(2000);
        printf("3. overlapping scroll:  row0 now=%d row58 now=%d row59 untouched=%d\n", mem_peek(0x800), mem_peek(0x800+58*80), mem_peek(0x800+59*80));
        CHECK(mem_peek(0x800) == 1 && mem_peek(0x800 + 58 * 80) == 59 && mem_peek(0x800 + 59 * 80) == 59, "scroll");
    }

    /* 5. the data ports: a row through port 0 (step 4: every cell's char),
     * port 1 beside it (step 4 from +2: every cell's colour), a column read
     * back with a stride, and a step of -1 */
    {
        mem_reset(); io_reset();
        CHECK(io_read(IO_PORT0 + IO_PORT_STEP) == 1 && io_read(IO_PORT1 + IO_PORT_STEP) == 1, "ports reset with STEP 1");
        static const uint8_t p[] = {
            SET32(IO_PORT0 + IO_PORT_ADDR, 0x00220000),
            LDA_IMM(4), STA_ABS(IO_PORT0 + IO_PORT_STEP), LDA_IMM(0), STA_ABS(IO_PORT0 + IO_PORT_STEP + 1),
            SET32(IO_PORT1 + IO_PORT_ADDR, 0x00220002),
            LDA_IMM(4), STA_ABS(IO_PORT1 + IO_PORT_STEP), LDA_IMM(0), STA_ABS(IO_PORT1 + IO_PORT_STEP + 1),
            0xA2, 0x00,                                         /* LDX #0 */
            0x8A, STA_ABS(IO_PORT0 + IO_PORT_DATA),             /* loop: TXA; P0DATA = x */
            LDA_IMM(7), STA_ABS(IO_PORT1 + IO_PORT_DATA),       /* P1DATA = 7 */
            0xE8, 0xE0, 40, 0xD0, 0xF2,                         /* INX; CPX #40; BNE loop */
            0x4C, 0x4C, 0xC0,                                   /* JMP * (at $C04C) */
        };
        boot(p, sizeof p, 4000);
        int ok = 1;
        for (int i = 0; i < 40; i++) if (mem_peek(0x220000 + i * 4) != i || mem_peek(0x220002 + i * 4) != 7 || mem_peek(0x220001 + i * 4) != 0) ok = 0;
        uint32_t a0 = io_read(IO_PORT0) | io_read(IO_PORT0 + 1) << 8 | (uint32_t) io_read(IO_PORT0 + 2) << 16;
        printf("5. data ports: 40 cells, char and colour ok=%d  P0ADDR now $%06X\n", ok, (unsigned) a0);
        CHECK(ok, "the ports' row");
        CHECK(a0 == 0x220000 + 160, "ADDR moved on by STEP each write");
        /* a column: STEP 320 reads every row's first byte */
        for (int y = 0; y < 5; y++) mem_poke(0x300000 + y * 320, (uint8_t)(0x10 + y));
        io_write(IO_PORT1, 0x00); io_write(IO_PORT1 + 1, 0x00); io_write(IO_PORT1 + 2, 0x30); io_write(IO_PORT1 + 3, 0);
        io_write(IO_PORT1 + IO_PORT_STEP, 320 & 0xFF); io_write(IO_PORT1 + IO_PORT_STEP + 1, 320 >> 8);
        int col = 1; for (int y = 0; y < 5; y++) if (io_read(IO_PORT1 + IO_PORT_DATA) != 0x10 + y) col = 0;
        CHECK(col, "a column read with STEP 320");
        /* backwards: STEP -1 */
        io_write(IO_PORT0, 0x05); io_write(IO_PORT0 + 1, 0); io_write(IO_PORT0 + 2, 0x30); io_write(IO_PORT0 + 3, 0);
        io_write(IO_PORT0 + IO_PORT_STEP, 0xFF); io_write(IO_PORT0 + IO_PORT_STEP + 1, 0xFF);
        for (int i = 0; i < 3; i++) io_write(IO_PORT0 + IO_PORT_DATA, (uint8_t)(0xA0 + i));
        CHECK(mem_peek(0x300005) == 0xA0 && mem_peek(0x300004) == 0xA1 && mem_peek(0x300003) == 0xA2, "STEP -1 walks down");
        CHECK(io_read(IO_PORT0) == 0x02, "ADDR after three steps down");
    }

    /* 4. bad command -> status $FF, nothing moves */
    io_write(IO_DMA_CMD, 9);
    CHECK(io_read(IO_DMA_STATUS) == 0xFF, "bad cmd status");

    printf(fails ? "\n%d FAILED\n" : "\nALL OK\n", fails);
    return fails != 0;
}
