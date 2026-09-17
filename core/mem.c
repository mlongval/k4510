/* K4510 memory system: 256 MB, 28-bit MAP, the eleven CPU callbacks.
 *
 * MAP semantics follow the 4510 as extended by the 45GS10, read from
 * Xemu's MEGA65 target (memory_mapper.c) rather than from a datasheet:
 *
 *   MAP with A,X,Y,Z:
 *     X == $0F : A is the megabyte for the lower 32 KB   (45GS10)
 *     else     : offset_low  = A<<8 | (X&15)<<16 ; mask[3:0] = X>>4
 *     Z == $0F : Y is the megabyte for the upper 32 KB   (45GS10)
 *     else     : offset_high = Y<<8 | (Z&15)<<16 ; mask[7:4] = Z>>4
 *   A mapped 8 KB block n (CPU $n000*2) resolves to
 *     phys = megabyte + ((offset + cpu_addr) & $FFFFF)
 *   An unmapped block resolves to phys = cpu_addr.
 *   MAP inhibits interrupts until the next EOM (NOP).
 */
#include "xemu/emutools_basicdefs.h"
#include "xemu/cpu65.h"
#include "mem.h"
#include "host.h"
#include "io.h"
#include <string.h>

uint8_t *k4510_ram;
int cpu_mega65_opcodes = 1;          /* MEGA65-build global: enable Q / 32-bit forms */

static k4510_map_t map;
static uint32_t block_base[8];       /* per 8 KB block: phys address of CPU offset 0 within it, or
                                        UNMAPPED */
#define UNMAPPED 0xFFFFFFFFu
static uint32_t bank_reg[8];         /* K-01: per block, 28-bit base or BANK_OFF */
static uint8_t  bank_on[8];          /* 1: the bank register owns this block (overrides MAP) */

/* ---- physical RAM ----------------------------------------------------- */

int mem_init(void)
{
    if (!k4510_ram) {
        k4510_ram = host_alloc_zeroed(K4510_PHYS_SIZE);
        if (!k4510_ram) return -1;
    } else {
        host_zero(k4510_ram, K4510_PHYS_SIZE);
    }
    mem_reset();
    return 0;
}

static void map_apply(void)
{
    for (int b = 0; b < 8; b++) {
        if (bank_on[b]) {
            block_base[b] = bank_reg[b];
        } else if (map.mask & (1u << b)) {
            uint32_t off = (b < 4) ? map.offset_low : map.offset_high;
            uint32_t mb  = (b < 4) ? map.mb_low     : map.mb_high;
            /* phys(cpu) = mb + ((off + cpu) & 0xFFFFF); cpu = b*8K + i, so precompute for i = 0 */
            block_base[b] = mb + ((off + (uint32_t)(b << 13)) & 0xFFFFFu);
        } else {
            block_base[b] = UNMAPPED;
        }
    }
}

void mem_reset(void)
{
    memset(&map, 0, sizeof map);
    for (int b = 0; b < 8; b++) { bank_reg[b] = BANK_OFF; bank_on[b] = 0; }
    far_table = 0; far_depth = 0; far_err = 0;
    map_apply();
    io_reset();
}

void mem_load(uint32_t phys, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++)
        k4510_ram[(phys + i) & K4510_PHYS_MASK] = data[i];
}

uint8_t mem_peek(uint32_t phys)            { return k4510_ram[phys & K4510_PHYS_MASK]; }
void    mem_poke(uint32_t phys, uint8_t v) { k4510_ram[phys & K4510_PHYS_MASK] = v; }

uint32_t mem_rom_base = 0xE000;

int mem_load_rom(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    static uint8_t buf[K4510_ROM_MAX];
    size_t n = fread(buf, 1, sizeof buf, f), base_n = n > 0x6000 ? 0x6000 : n;
    fclose(f);
    memcpy(&k4510_ram[K4510_ROM_PHYS + 0x10000 - base_n], buf, base_n);
    mem_rom_base = 0x10000 - base_n;
    if (n > base_n)                       /* sideways banks, appended to the base image */
        memcpy(&k4510_ram[K4510_SW_PHYS], buf + base_n, n - base_n);
    return (int)n;
}

const k4510_map_t *mem_map_state(void) { return &map; }

/* ---- CPU 16-bit view -> physical -------------------------------------- */

static XEMU_INLINE uint32_t cpu_to_phys(uint16_t a)
{
    uint32_t base = block_base[a >> 13];
    if (base == UNMAPPED) return a >= mem_rom_base ? K4510_ROM_PHYS + a : a;
    if (a >= 0xFF00) return K4510_ROM_PHYS + a;                      /* the stub page is always the ROM */
    if (bank_on[a >> 13]) return (base + (a & 0x1FFF)) & K4510_PHYS_MASK;
    /* within the mapped block the 20-bit add can wrap at the megabyte: redo it exactly */
    uint32_t off = (a < 0x8000) ? map.offset_low : map.offset_high;
    uint32_t mb  = (a < 0x8000) ? map.mb_low     : map.mb_high;
    (void)base;
    return (mb + ((off + a) & 0xFFFFFu)) & K4510_PHYS_MASK;
}

uint32_t mem_cpu_to_phys(uint16_t a) { return cpu_to_phys(a); }

/* ---- bank registers (K-01) -------------------------------------------- */
void mem_bank_set(uint8_t b, uint32_t phys) { b &= 7; bank_reg[b] = phys & K4510_PHYS_MASK; bank_on[b] = 1; map_apply(); }
void mem_bank_off(uint8_t b)                { b &= 7; bank_on[b] = 0; map_apply(); }   /* the base stays, for a byte-wise restore */
uint32_t mem_bank_get(uint8_t b)            { b &= 7; return bank_on[b] ? bank_reg[b] : BANK_OFF; }
uint32_t mem_bank_base(uint8_t b)           { b &= 7; return bank_reg[b] == BANK_OFF ? 0 : bank_reg[b]; }
void mem_bank_setbase(uint8_t b, uint32_t p){ b &= 7; bank_reg[b] = p & K4510_PHYS_MASK; map_apply(); }
uint8_t mem_bank_mask(void)                 { uint8_t m = 0; for (int b = 0; b < 8; b++) if (bank_on[b]) m |= 1u << b; return m; }

/* ---- far-call gate (K-02) --------------------------------------------- */
/* Descriptor n, 8 bytes at far_table + 8n:
 *   0-3 phys base of the code (28-bit)   4 block (0-7) to bank it into
 *   5 flags: bit0 leave banked on return, bit1 do not bank (long jump only)
 *   6-7 entry, a CPU address inside that block
 * An opcode fetch from $DF00+4n (i.e. JSR/JMP there) runs the call; the
 * core sees a NOP and continues at the entry. The callee's RTS returns to
 * $DFF0, which restores the block and returns to the original caller. */
uint32_t far_table; uint8_t far_depth, far_err;
static struct { uint8_t block, on, flags; uint32_t base; } far_stack[FAR_DEPTH_MAX];
static void far_push(uint8_t v) { cpu65_write_callback(cpu65.s | cpu65.sphi, v); if (cpu65.s-- == 0 && !cpu65.pf_e) cpu65.sphi -= 0x100; }
static uint8_t far_pop(void) { if (++cpu65.s == 0 && !cpu65.pf_e) cpu65.sphi += 0x100; return cpu65_read_callback(cpu65.s | cpu65.sphi); }
static uint8_t far_gate(uint16_t addr)
{
    if (addr == FAR_RET) {
        uint16_t ret;
        if (far_depth == 0) { far_err = 2; return 0x60; }              /* nothing to restore: behave as RTS */
        far_depth--;
        if (!(far_stack[far_depth].flags & 1) && !(far_stack[far_depth].flags & 2)) {
            uint8_t b = far_stack[far_depth].block;
            bank_on[b] = far_stack[far_depth].on; bank_reg[b] = far_stack[far_depth].base; map_apply();
        }
        ret = far_pop(); ret |= (uint16_t)far_pop() << 8;
        cpu65.pc = ret;                 /* the core increments after the fetch: lands on ret+1 */
        return 0xEA;
    }
    if (addr >= FAR_GATE + 4 * FAR_SLOTS || (addr & 3)) { far_err = 3; return 0x60; }
    {
        uint32_t d = (far_table + 8u * ((addr - FAR_GATE) >> 2)) & K4510_PHYS_MASK;
        uint32_t base = k4510_ram[d] | (k4510_ram[(d + 1) & K4510_PHYS_MASK] << 8) | (k4510_ram[(d + 2) & K4510_PHYS_MASK] << 16) | ((uint32_t)k4510_ram[(d + 3) & K4510_PHYS_MASK] << 24);
        uint8_t block = k4510_ram[(d + 4) & K4510_PHYS_MASK] & 7, flags = k4510_ram[(d + 5) & K4510_PHYS_MASK];
        uint16_t entry = k4510_ram[(d + 6) & K4510_PHYS_MASK] | (k4510_ram[(d + 7) & K4510_PHYS_MASK] << 8);
        if (far_depth >= FAR_DEPTH_MAX) { far_err = 1; return 0x60; }
        far_stack[far_depth].block = block; far_stack[far_depth].on = bank_on[block]; far_stack[far_depth].base = bank_reg[block]; far_stack[far_depth].flags = flags;
        far_depth++;
        far_push((FAR_RET - 1) >> 8); far_push((FAR_RET - 1) & 0xFF);   /* so the callee's RTS lands on the return gate */
        if (!(flags & 2)) { bank_on[block] = 1; bank_reg[block] = base & K4510_PHYS_MASK; map_apply(); }
        cpu65.pc = entry - 1;           /* ditto: the NOP we return is "executed" at entry-1 */
        return 0xEA;
    }
}

/* ---- the eleven callbacks --------------------------------------------- */
/* The ROM lives in the unmapped view only (a block MAPped or banked over it
 * shows RAM); the stub page $FF00-$FFFF is always ROM, whatever is mapped.
 * The I/O page $D000-$DFFF wins in the unmapped and K-01-banked views, but
 * a MAP of block 6 puts RAM under the I/O (K-06): MAP is an instruction, so
 * the program that hid the I/O can always bring it back -- no register
 * deadlock. The far gate is unreachable while block 6 is MAPped. */

int dbg_rec;                             /* the PC recorder costs a store per instruction: armed by DUMP */
/* the WATCH write hook (core/io.h): armed rarely, checked cheaply */
#define WATCH_WR(phys) do { if (XEMU_UNLIKELY(dbg_watch_ctl && ((phys) & K4510_PHYS_MASK) == dbg_watch_addr)) dbg_watch_hit(); } while (0)
Uint8 cpu65_read_callback(Uint16 addr)
{
    uint32_t base = block_base[addr >> 13];
    if (XEMU_UNLIKELY(dbg_rec && addr == cpu65.old_pc)) dbg_pc(addr);   /* opcode fetch: the debug recorder */
    if (XEMU_LIKELY(base == UNMAPPED)) {                          /* the fast path: one compare for I/O, one for ROM */
        if (XEMU_UNLIKELY((addr & 0xF000) == K4510_IO_PAGE)) {
            if (XEMU_UNLIKELY(addr >= FAR_GATE && addr == cpu65.old_pc)) return far_gate(addr);   /* opcode fetch in the gate page */
            return io_read(addr);
        }
        if (XEMU_UNLIKELY(addr >= mem_rom_base)) return k4510_ram[K4510_ROM_PHYS + addr];
        return k4510_ram[addr];
    }
    if (XEMU_UNLIKELY((addr & 0xF000) == K4510_IO_PAGE)) {
        if (XEMU_LIKELY(bank_on[6])) {                            /* banked (K-01) view: the I/O page still wins */
            if (XEMU_UNLIKELY(addr >= FAR_GATE && addr == cpu65.old_pc)) return far_gate(addr);
            return io_read(addr);
        }
        /* block 6 is MAPped (K-06): RAM under the I/O -- the CPU asked for it
           by instruction, so it can always ask back; no register needed */
    }
    return k4510_ram[cpu_to_phys(addr)];
}

void cpu65_write_callback(Uint16 addr, Uint8 data)
{
    uint32_t base = block_base[addr >> 13];
    if (XEMU_LIKELY(base == UNMAPPED)) {
        if (XEMU_UNLIKELY((addr & 0xF000) == K4510_IO_PAGE)) { io_write(addr, data); return; }
        if (XEMU_UNLIKELY(addr >= mem_rom_base)) return;               /* ROM (I/O page wins: the hole in a big ROM) */
        WATCH_WR(addr);
        k4510_ram[addr] = data;
        return;
    }
    if (XEMU_UNLIKELY((addr & 0xF000) == K4510_IO_PAGE) && bank_on[6]) { io_write(addr, data); return; }   /* K-01 banked: I/O wins; MAPped (K-06): RAM under the I/O */
    if (XEMU_UNLIKELY(addr >= 0xFF00)) return;                         /* the stub page: ROM even when banked */
    { uint32_t phys = cpu_to_phys(addr); WATCH_WR(phys); k4510_ram[phys] = data; }
}

void cpu65_write_rmw_callback(Uint16 addr, Uint8 old_data, Uint8 new_data)
{
    (void)old_data;
    cpu65_write_callback(addr, new_data);
}

/* 32-bit flat forms. The target fetches the operand byte itself (as Xemu's
 * MEGA65 target does); the base-page pointer is read through the CPU view
 * so a MAPped base page works. */
static Uint32 flat_address(Uint8 index)
{
    const Uint8  bp   = cpu65_read_callback(cpu65.pc++);
    const Uint16 base = cpu65.bphi;
    Uint32 p =  (Uint32)cpu65_read_callback(base |  bp              )
             | ((Uint32)cpu65_read_callback(base | ((bp + 1) & 0xFF)) << 8)
             | ((Uint32)cpu65_read_callback(base | ((bp + 2) & 0xFF)) << 16)
             | ((Uint32)cpu65_read_callback(base | ((bp + 3) & 0xFF)) << 24);
    return (p + index) & K4510_PHYS_MASK;
}

Uint8  cpu65_read_linear_opcode_callback(void)             { return k4510_ram[flat_address(cpu65.z)]; }
void   cpu65_write_linear_opcode_callback(Uint8 data)      { Uint32 a = flat_address(cpu65.z); WATCH_WR(a); k4510_ram[a] = data; }

Uint32 cpu65_read_linear_long_opcode_callback(const Uint8 index)
{
    Uint32 a = flat_address(index);
    return  (Uint32)k4510_ram[(a    ) & K4510_PHYS_MASK]
         | ((Uint32)k4510_ram[(a + 1) & K4510_PHYS_MASK] << 8)
         | ((Uint32)k4510_ram[(a + 2) & K4510_PHYS_MASK] << 16)
         | ((Uint32)k4510_ram[(a + 3) & K4510_PHYS_MASK] << 24);
}

void cpu65_write_linear_long_opcode_callback(const Uint8 index, Uint32 data)
{
    Uint32 a = flat_address(index);
    WATCH_WR(a); WATCH_WR(a + 1); WATCH_WR(a + 2); WATCH_WR(a + 3);
    k4510_ram[(a    ) & K4510_PHYS_MASK] =  data        & 0xFF;
    k4510_ram[(a + 1) & K4510_PHYS_MASK] = (data >>  8) & 0xFF;
    k4510_ram[(a + 2) & K4510_PHYS_MASK] = (data >> 16) & 0xFF;
    k4510_ram[(a + 3) & K4510_PHYS_MASK] = (data >> 24) & 0xFF;
}

/* ---- MAP / EOM --------------------------------------------------------- */

void cpu65_do_aug_callback(void)
{
    cpu65.cpu_inhibit_interrupts = 1;       /* until EOM */
    if (cpu65.x == 0x0F) {
        map.mb_low = (uint32_t)cpu65.a << 20;
    } else {
        map.offset_low = ((uint32_t)cpu65.a << 8) | ((uint32_t)(cpu65.x & 15) << 16);
        map.mask = (map.mask & 0xF0) | (cpu65.x >> 4);
    }
    if (cpu65.z == 0x0F) {
        map.mb_high = (uint32_t)cpu65.y << 20;
    } else {
        map.offset_high = ((uint32_t)cpu65.y << 8) | ((uint32_t)(cpu65.z & 15) << 16);
        map.mask = (map.mask & 0x0F) | (cpu65.z & 0xF0);
    }
    for (int b = 0; b < 8; b++) { bank_on[b] = 0; bank_reg[b] = BANK_OFF; }   /* MAP owns all eight blocks now */
    map_apply();
}

void cpu65_do_nop_callback(void)
{
    cpu65.cpu_inhibit_interrupts = 0;       /* EOM */
}

void cpu65_illegal_opcode_callback(void)
{
    DEBUGPRINT("K4510: illegal opcode $%02X at PC=$%04X" NL, cpu65.op, cpu65.old_pc);
}

/* ---- save states (core/state.h) ------------------------------------------ */
#include "state.h"
void mem_state_save(FILE *f)
{
    state_put(f, "MAP ", &map, sizeof map);
    state_put(f, "BANK", bank_reg, sizeof bank_reg);
    state_put(f, "BKON", bank_on, sizeof bank_on);
    state_put(f, "FART", &far_table, sizeof far_table);
    state_put(f, "FARD", &far_depth, 1);
    state_put(f, "FARE", &far_err, 1);
    state_put(f, "FARS", far_stack, sizeof far_stack);
    state_put(f, "ROMB", &mem_rom_base, sizeof mem_rom_base);
}
int mem_state_load(FILE *f)
{
    if (state_get(f, "MAP ", &map, sizeof map) || state_get(f, "BANK", bank_reg, sizeof bank_reg) || state_get(f, "BKON", bank_on, sizeof bank_on)
        || state_get(f, "FART", &far_table, sizeof far_table) || state_get(f, "FARD", &far_depth, 1) || state_get(f, "FARE", &far_err, 1)
        || state_get(f, "FARS", far_stack, sizeof far_stack) || state_get(f, "ROMB", &mem_rom_base, sizeof mem_rom_base)) return -2;
    /* a hand-edited .k4s must not index out of bounds: far_gate pops
     * far_stack[far_depth - 1] and writes bank_reg[block] (review 2026-09-17) */
    if (far_depth > FAR_DEPTH_MAX) far_depth = FAR_DEPTH_MAX;
    for (int i = 0; i < FAR_DEPTH_MAX; i++) far_stack[i].block &= 7;
    map_apply();
    return 0;
}
