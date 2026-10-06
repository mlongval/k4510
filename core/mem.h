/* K4510 memory system.
 *
 * 256 MB physical (28-bit), reached from the CPU's 16-bit bus through the
 * 4510/45GS10 MAP mechanism, and from the 45GS10's 32-bit flat forms
 * directly. The CPU never sees any of this: it only talks through the
 * callbacks in mem.c.
 *
 * Physical RAM is a single mmap'd region with lazy commit: the address
 * space is reserved, pages cost host memory only when first touched.
 */
#ifndef K4510_MEM_H
#define K4510_MEM_H
#include <stdint.h>
#include <stddef.h>

#define K4510_PHYS_BITS  28
#define K4510_PHYS_SIZE  (1u << K4510_PHYS_BITS)        /* 256 MB */
#define K4510_PHYS_MASK  (K4510_PHYS_SIZE - 1)

extern uint8_t *k4510_ram;          /* K4510_PHYS_SIZE bytes, lazily committed */

/* ---- spike I/O and ROM, in the CPU's unmapped 64 KB view ------------- */
#define K4510_ROM_MAX    0x26000u    /* base image (24 KB, top-aligned at $10000) + up to 16 sideways banks */
#define K4510_SW_PHYS    0x0FF00000u  /* sideways ROM: bank 1 at +0, 8 KB per bank (the $A000-$BFFF window) */
#define K4510_SW_SIZE    0x2000u
#define K4510_SW_MAX     16
/* The ROM image lives in the top 64 KB of physical memory and is seen in the
 * unmapped CPU view from mem_rom_base up; the physical RAM at $A000-$FFFF is
 * "RAM under the ROM", revealed by banking blocks 5/7 onto $A000/$E000
 * (K-05). The page $FF00-$FFFF always reads the ROM, whatever is banked:
 * the system-call stub and the vectors live there. */
#define K4510_ROM_PHYS   0x0FFF0000u
extern uint32_t mem_rom_base;        /* first ROM address in the CPU view; set by mem_load_rom */
#define K4510_IO_PAGE    0xD000u     /* $D000-$DFFF: I/O, see io.h */
/* K/OS's workspace -- 1 KB of RAM in the I/O page, $DB00-$DEFF (2026-10-06),
 * where no device is.  Visible wherever the I/O is -- so whenever the ROM
 * runs, whatever a program has banked.  The ROM keeps its base page and its
 * C stack there.  The bytes are the RAM under the I/O page at the same
 * addresses, physical $00DB00-$00DEFF: a buffer on the ROM's C stack has the
 * same address for the CPU as for DMA and the devices, which take physical
 * ones (the first try put the workspace elsewhere, and every file name the
 * shell built on its stack was read by the file device as empty), and SWAP's
 * 64 KB image includes it, as it included the old stack at $0600. */
#define K4510_WS_LO      0xDB00u
#define K4510_WS_SIZE    0x0400u
#define K4510_WS_PHYS(a) (a)

int      mem_init(void);                                /* 0 on success */
void     mem_reset(void);                               /* MAP off, etc. */
void     mem_load(uint32_t phys, const uint8_t *data, size_t len);
uint8_t  mem_peek(uint32_t phys);                       /* physical, no side effects */
void     mem_poke(uint32_t phys, uint8_t v);
int      mem_load_rom(const char *path);                /* <= 32 KB, top-aligned at $10000; returns size */

/* CPU-view translation, for tests and the monitor. */
uint32_t mem_cpu_to_phys(uint16_t cpu_addr);

/* MAP state, for tests/monitor/snapshots. */
typedef struct {
    uint32_t offset_low, offset_high;     /* 20-bit, bits 8-19 used */
    uint32_t mb_low, mb_high;             /* megabyte, already << 20 */
    uint8_t  mask;                        /* bit n: 8 KB block n is mapped */
} k4510_map_t;
const k4510_map_t *mem_map_state(void);

/* ---- bank registers (K-01) and the far-call gate (K-02) -------------- */
/* A bank register puts one 8 KB block of the CPU view onto any 28-bit
 * physical base (byte granularity): phys = base + (cpu & $1FFF). A block
 * is owned by whichever wrote it last, MAP or a bank register; MAP always
 * rewrites all eight blocks, so "MAP everything off" also clears the banks. */
#define BANK_OFF 0xFFFFFFFFu
void     mem_bank_set(uint8_t block, uint32_t phys);    /* block 0-7 */
void     mem_bank_off(uint8_t block);
uint32_t mem_bank_get(uint8_t block);                   /* BANK_OFF if not banked */
uint32_t mem_bank_base(uint8_t block);                  /* the stored base even when off */
void     mem_bank_setbase(uint8_t block, uint32_t phys);/* change the base; on/off unchanged */
uint8_t  mem_bank_mask(void);                           /* bit n: block n banked */
/* Far-call gate: JSR $DF00+4n banks descriptor n in and jumps to it; the
 * callee's RTS lands on the return gate, which restores the bank. */
#define FAR_GATE     0xDF00u
#define FAR_SLOTS    32
#define FAR_RET      0xDFF0u
#define FAR_DEPTH_MAX 64
extern uint32_t far_table;          /* 28-bit phys of the descriptor table ($DF80-$DF83) */
extern uint8_t  far_depth, far_err; /* nesting depth; last error: 1 overflow, 2 underflow, 3 bad slot */

/* ---- the stack fence (2026-10-06) -------------------------------------
 * The ROM is C, and C keeps a stack of its own: memory and a pointer in the
 * base page.  It once overflowed into the ROM's own variables and nothing
 * said so (*PROG from EhBASIC, 2026-10-05).  The fence is a register the ROM
 * arms at reset ($D550-$D55A, core/io.h): it names its stack pointer and the
 * lowest address the stack may reach.  Every instruction the ROM executes
 * that reaches memory through that pointer -- (sp),Y and (sp),Z -- is
 * checked (a hook in the CPU's address modes, so it costs next to nothing):
 * the deepest address is kept (INFO -m shows the margin), and one below the
 * floor is a trip, said on stderr and written as a dump.  The ROM's 6502
 * stack is noted as it goes; K4510_FENCE_DEEP=1 notes it, and the programs'
 * page 1, at every instruction instead -- exact, and half the speed. */
typedef struct {
    uint8_t  on, deep, zp, page, trips;   /* deep: K4510_FENCE_DEEP, the 6502 stacks at every instruction (a measuring mode) */   /* armed; the pointer's base-page address; the base page it lives in ($00 or the B the ROM runs with) */
    uint16_t floor, top, low;       /* the lowest allowed; the pointer when armed; the deepest access seen */
    uint16_t hw_low;                /* the lowest S (SPH:SPL) seen while ROM code ran on its own stack page */
    uint16_t hw_page;               /* that page: the one the ROM's stack was on when it armed the fence */
    uint16_t prog_low;              /* the lowest S on page 1 seen while a program's own code ran */
} mem_fence_t;
extern mem_fence_t mem_fence;
extern uint32_t cpu65_fence_zp;                  /* the watched pointer's CPU address, $10000 off (cpu65.c's hook) */
void    mem_fence_write(uint8_t r, uint8_t v);   /* r: $00-$0F of the fence's registers */
uint8_t mem_fence_read(uint8_t r);

#endif
