/* The I/O page: the keyboard and mouse, WAIT, DMA, and the dispatch of
 * $D000-$DFFF to the devices behind it (core/io_int.h lists them), with the
 * reset and save state that walk all of them. */
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include "io.h"
#include "io_int.h"
#include "mem.h"
#include "xemu/emutools_basicdefs.h"
#include "xemu/cpu65.h"
#include "vicky.h"
#include "opl2.h"
#include "audio.h"
#include "net.h"
#include "term.h"
#include "ui/menu.h"
#include "state.h"

/* ---- keyboard: a FIFO behind two registers (Wozmon polls them) --------
 * Each entry is a byte and one bit saying what KIND of byte: a character
 * (what a key or a dead-key sequence typed, in the font's code page 437) or
 * a key code (the arrows, Home, the function keys: KEY_* in io.h, $80-$9F).
 * The two ranges overlap -- KEY_LEFT is $82 and so is é -- and for as long
 * as the queue held bare bytes the shell could not tell them apart: on a
 * French keyboard the arrows printed Ç ü é â (Doc, hdieu, 2026-09-08).  The
 * kind rides in bit 8 here and is reported through KBDST bits 5 and 6. */
static uint16_t kbd_fifo[64];
static int      kbd_head, kbd_tail;
static uint8_t  kbd_last, kbd_last_key;   /* kbd_mods: io_int.h */
uint8_t kbd_mods;   /* kbd_last_key: the byte last read was a key code */
#define KBD_LATCH 0x1000     /* the key pipe's: this key carries its own modifiers, in bits 9-11 */
static uint8_t kbd_latched;   /* bit 7 set: KBDST reports these modifiers (bits 0-2), the ones the key last read was sent with */

/* Every key passes here, from the frontend's SDL loop. The F12 menu (core/ui) takes them first: its own key
 * opens it (unshifted only -- Shift with the menu key is the frontend's pause) and,
 * while it is open, every key is the menu's. */
static void kbd_enqueue(uint16_t ent)
{
    int next = (kbd_tail + 1) & 63;
    if (next == kbd_head) return;
    kbd_fifo[kbd_tail] = ent;
    kbd_tail = next;
}
static void kbd_in(uint16_t ent)
{
    uint8_t ascii = (uint8_t)ent;
    if (menu_is_open()) { menu_key(ascii); return; }
    if ((ent & KBD_KEY) && ascii == menu_key_code() && !(kbd_mods & 1)) { menu_open(); return; }
    if (term_screen() == 1) { s2_key(ent); return; }             /* the second screen is up: its session has the keyboard */
    dbg_key(ascii);
    kbd_enqueue(ent);
}
void kbd_push(uint8_t ascii)   { kbd_in(ascii); }                    /* a character */
void kbd_push_machine(uint8_t ascii) { kbd_enqueue(ascii); }            /* past the menu and the second screen (2026-10-07) */
/* WAIT ($D545, 2026-10-06): a write puts the CPU to sleep until the next
 * interrupt or a key in the queue -- the 45GS02 has no WAI, so it is a
 * register.  The loops that wait for a key or a frame (k_chrin, EhBASIC's
 * line input, wait_vblank, the programs' key loops) say so with it, and an
 * idle machine stops costing the host a core's fifth.  A sleep longer than
 * a frame ends anyway: a program that masked the interrupt is not stuck. */
int cpu65_waiting; static unsigned wait_slept;
int cpu65_wake(int cycles)
{
    if (cpu65.irqLevel || kbd_head != kbd_tail || (wait_slept += (unsigned) cycles) > 700000u) { cpu65_waiting = 0; return 1; }
    return 0;
}
void io_wait_start(void) { cpu65_waiting = 1; wait_slept = 0; cpu65.multi_step_stop_trigger = 1; }
void kbd_push_key(uint8_t code) { kbd_in((uint16_t)code | KBD_KEY); }  /* a KEY_* code */
void kbd_modifiers(uint8_t sh, uint8_t ct, uint8_t al) { kbd_mods = (sh ? 1 : 0) | (ct ? 2 : 0) | (al ? 4 : 0); kbd_latched = 0; }
/* A key with its modifiers bound to it, for the key pipe: Shift held live
 * would be gone by the time a busy program reads a queued key, so the
 * modifiers ride in the queue and KBDST reports them once the key is read
 * (2026-09-14, for testing PROG's selection from outside). */
void kbd_push_mods(uint8_t ascii, uint8_t mods)    { kbd_in((uint16_t)ascii | KBD_LATCH | ((uint16_t)(mods & 7) << 9)); }
void kbd_push_key_mods(uint8_t code, uint8_t mods) { kbd_in((uint16_t)code | KBD_KEY | KBD_LATCH | ((uint16_t)(mods & 7) << 9)); }
static uint8_t kbd_held_mask;
void kbd_held(uint8_t mask) { kbd_held_mask = mask; }
static int mouse_x, mouse_y; static uint8_t mouse_btn; static int8_t mouse_wheel, mouse_dx, mouse_dy;
static int8_t clamp8(int v) { return (int8_t)(v > 127 ? 127 : v < -128 ? -128 : v); }
static uint8_t mouse_hostptr, mouse_wanthost;
void mouse_host_release(void) { mouse_wanthost = 0; }
void mouse_host_pointer(int shown) { mouse_hostptr = shown ? 1 : 0; }
int mouse_host_wanted(void) { return mouse_wanthost; }
void mouse_set(int x, int y, uint8_t buttons, int wheel, int dx, int dy)
{
    int gw = vicky_glass_w(), gh = vicky_glass_h();                     /* the glass: 640x480, or an HD mode's own size */
    mouse_x = x < 0 ? 0 : x > gw - 1 ? gw - 1 : x; mouse_y = y < 0 ? 0 : y > gh - 1 ? gh - 1 : y;
    mouse_btn = buttons; mouse_wheel = clamp8(wheel); mouse_dx = clamp8(dx); mouse_dy = clamp8(dy);
}
static int kbd_ready(void) { return kbd_head != kbd_tail; }
static uint8_t kbd_read(void)
{
    if (!kbd_ready()) return 0;
    kbd_last = (uint8_t)kbd_fifo[kbd_head]; kbd_last_key = (kbd_fifo[kbd_head] & KBD_KEY) ? 1 : 0;
    kbd_latched = (kbd_fifo[kbd_head] & KBD_LATCH) ? (uint8_t)(0x80 | ((kbd_fifo[kbd_head] >> 9) & 7)) : 0;
    kbd_head = (kbd_head + 1) & 63;
    return kbd_last;
}
/* ---- DMA ---------------------------------------------------------------- */
static uint8_t dma_reg[16];     /* SRC[4] DST[4] LEN[4] CMD STATUS .. */
const uint8_t *io_dma_regs(void) { return dma_reg; }

static void dma_run(uint8_t cmd)
{
    uint32_t src = rd32(&dma_reg[0]) & K4510_PHYS_MASK;
    uint32_t dst = rd32(&dma_reg[4]) & K4510_PHYS_MASK;
    uint32_t len = rd32(&dma_reg[8]) & K4510_PHYS_MASK;
    dma_reg[13] = cmd;
    switch (cmd) {
    case 1:   /* copy, memmove semantics (overlap-safe), wraps at 256 MB */
        if (src + len <= K4510_PHYS_SIZE && dst + len <= K4510_PHYS_SIZE) {
            memmove(k4510_ram + dst, k4510_ram + src, len);
        } else {
            for (uint32_t i = 0; i < len; i++)       /* rare: wrap */
                k4510_ram[(dst + i) & K4510_PHYS_MASK] = k4510_ram[(src + i) & K4510_PHYS_MASK];
        }
        break;
    case 2:   /* fill with SRC byte 0 */
        if (dst + len <= K4510_PHYS_SIZE) memset(k4510_ram + dst, dma_reg[0], len);
        else for (uint32_t i = 0; i < len; i++) k4510_ram[(dst + i) & K4510_PHYS_MASK] = dma_reg[0];
        break;
    case 3:   /* swap */
        for (uint32_t i = 0; i < len; i++) {
            uint8_t *a = &k4510_ram[(src + i) & K4510_PHYS_MASK], *b = &k4510_ram[(dst + i) & K4510_PHYS_MASK];
            uint8_t t = *a; *a = *b; *b = t;
        }
        break;
    default:
        dma_reg[13] = 0xFF;
    }
    dma_reg[12] = 0;   /* instant: idle again before the CPU sees the next instruction */
}
/* Every device back to power-on, in this order: the Tube is stopped before
 * JIM is reset, since its end hands JIM back from a `!` session. */
void io_reset(void)
{
    sys_reset(); cpu65_waiting = 0;              /* the frame count and the title; WAIT: a reset wakes it */
    tube_reset();                                /* a power cycle left BBC BASIC running and $D800 saying so (review 2026-09-12) */
    net_reset(); hostfs_reset(); term_reset();
    dbg_reset();
    fred_reset();
    seq_reset();
    kbd_head = kbd_tail = 0; kbd_last = 0;
    memset(dma_reg, 0, sizeof dma_reg);
    vicky_reset();
    audio_reset();
}

/* ---- I/O profile: how often the CPU touches the I/O page, and where.  Read
 * by the frontend for SYSTEM/PERF.TXT.  (Its cost in time was measured only
 * on the Pi, with the ARM timer; that went with the port.) */
uint32_t io_prof_reads, io_prof_writes, io_prof_hist[256];   /* hist: (addr >> 4) & 255, 16-byte groups */
int io_prof_on;                                  /* set by the frontend only while its PERF window is open:
                                                  * the counters cost every single I/O access, so they run
                                                  * only when someone is looking */
void io_prof_reset(void) { io_prof_reads = io_prof_writes = 0; memset(io_prof_hist, 0, sizeof io_prof_hist); }
static uint8_t io_read_inner(uint16_t addr);
uint8_t io_read(uint16_t addr)
{
    if (io_prof_on) {
        uint8_t v = io_read_inner(addr);
        io_prof_reads++; io_prof_hist[(addr >> 4) & 255]++;
        return v;
    }
    return io_read_inner(addr);
}
static uint8_t io_read_inner(uint16_t addr)
{
    if ((addr & 0xFF00) == IO_STORAGE || (addr & 0xFF00) == IO_NET || (addr & 0xFF00) == IO_TUBE) vicky_dirty = 1;   /* their reads move data into memory */
    if ((uint16_t)(addr - K4510_WS_LO) < K4510_WS_SIZE) return k4510_ram[K4510_WS_PHYS(addr)];   /* K/OS's workspace (core/mem.h): first, it is the busiest
                                                                                                   * (tried in mem.c's callbacks instead, it cost more, 2026-10-06) */
    switch (addr & 0xFF00) {
    case IO_VICKY:
        return vicky_read(addr & 0xFF);
    case IO_SOUND:
        if (addr >= IO_FM && (addr - IO_FM) < 3) return opl2_read((uint8_t)(addr - IO_FM));   /* the OPL2: STATUS, data readback, ID */
        return 0xFF;                                                       /* $D400-$D47F: nothing there (the SIDs, until 2026-09-05) */
    case IO_SYS:
        return sys_read(addr & 0xFF);
    case IO_MATH:
        return fred_read(addr & 0xFF);
    case IO_BANK: {
        uint8_t r = addr & 0xFF;
        if (r < 0x20) { uint32_t v = mem_bank_get(r >> 2) == BANK_OFF ? (mem_bank_base(r >> 2) | 0xFF000000u) : mem_bank_base(r >> 2); return (uint8_t)(v >> (8 * (r & 3))); }   /* off: base with byte 3 = $FF */
        if (r == 0x20) return mem_bank_mask();
        if (r == 0x21) return mem_map_state()->mask;
        return 0xFF;
    }
    case IO_TUBE:
        return tube_io_read(addr & 0xFF);
    case IO_FAR: {
        uint8_t r = addr & 0xFF;
        if (r >= 0x80 && r < 0x84) return (uint8_t)(far_table >> (8 * (r - 0x80)));
        if (r == 0x84) return far_depth;
        if (r == 0x85) return far_err;
        return 0xFF;
    }
    case IO_INPUT:
        if (addr == IO_KBD)   return kbd_read();
        if (addr == IO_KBDST) return (kbd_ready() ? 0x80 : 0x00) | (kbd_last_key ? 0x40 : 0x00)
                                   | (kbd_ready() && (kbd_fifo[kbd_head] & KBD_KEY) ? 0x20 : 0x00) | (kbd_latched ? (kbd_latched & 7) : kbd_mods);
        if (addr == IO_KBDST + 1) return kbd_ready() ? (uint8_t)kbd_fifo[kbd_head] : 0;   /* peek: next key, not popped */
        if (addr == IO_KBDHELD) return menu_is_open() ? 0 : kbd_held_mask;     /* the keys down now; none while the menu has them */
        if (addr == IO_MOUSEPTR) return mouse_hostptr | (mouse_wanthost ? 2 : 0);
        if (addr >= IO_MOUSEX && addr <= IO_MOUSEDY) {                        /* the mouse; the menu keeps its clicks */
            /* The host reports the glass (640x480); the program wants the pixels
             * of the mode VICKY is in (core/vicky.h CTRL): halve or quarter the
             * columns, halve the lines, take the 200-line field's top off. */
            uint8_t ctrl = vicky_read(0); int xs = (ctrl & 16) ? 2 : (ctrl & 2) ? 1 : 0, ys = (ctrl & 4) ? 1 : 0;
            int x = mouse_x >> xs, y = mouse_y - ((ctrl & 8) ? 40 : 0), ymax = ((ctrl & 8) ? 400 : 480) - 1;
            y = (y < 0 ? 0 : y > ymax ? ymax : y) >> ys;
            if (ctrl & 0x20) { xs = ys = 0; x = mouse_x; y = mouse_y; }        /* the HD family: the glass is the mode's own pixels */
            switch (addr - IO_MOUSEX) {
            case 0: return (uint8_t) x;  case 1: return (uint8_t)(x >> 8);
            case 2: return (uint8_t) y;  case 3: return (uint8_t)(y >> 8);
            case 4: return menu_is_open() ? 0 : mouse_btn;
            case 5: return menu_is_open() ? 0 : (uint8_t) mouse_wheel;
            case 6: return menu_is_open() ? 0 : (uint8_t)(mouse_dx >> xs);
            default: return menu_is_open() ? 0 : (uint8_t)(mouse_dy >> ys);
            }
        }
        if (addr == IO_KBDST + 2) {                                              /* break pending: an ESC or Ctrl-C anywhere in the queue is removed and returned */
            for (int i = kbd_head; i != kbd_tail; i = (i + 1) & 63) {
                uint8_t k = kbd_fifo[i];
                if (k == 0x03 || k == 0x1B) { for (int j = i; j != kbd_tail; j = (j + 1) & 63) kbd_fifo[j] = kbd_fifo[(j + 1) & 63]; kbd_tail = (kbd_tail + 63) & 63; return k; }
            }
            return 0;
        }
        return 0xFF;
    case IO_STORAGE:
        return hostfs_read(addr & 0xFF);
    case IO_NET:
        return net_read(addr & 0xFF);
    case IO_TERM:
        return term_read(addr & 0xFF);
    case IO_DMA:
        if ((addr & 0xFF) < 16) return dma_reg[addr & 0xFF];
        return 0xFF;
    default:
        return 0xFF;
    }
}

void io_write(uint16_t addr, uint8_t v)
{
    if ((uint16_t)(addr - K4510_WS_LO) < K4510_WS_SIZE) {                       /* K/OS's workspace: RAM (core/mem.h) */
        if (dbg_watch_ctl && K4510_WS_PHYS(addr) == dbg_watch_addr) dbg_watch_hit();
        k4510_ram[K4510_WS_PHYS(addr)] = v; return;
    }
    if (addr != IO_VICKY + 4 && addr != IO_WAIT && (addr & 0xFF00) != IO_BANK) vicky_dirty = 1;
        /* VICKY's idle frames: all but the IRQ acknowledge, WAIT and the bank registers (the CPU's view; the
         * stub writes them on every interrupt) may change the picture */
    switch (addr & 0xFF00) {
    case IO_VICKY:
        vicky_write(addr & 0xFF, v); return;
    case IO_SOUND:
        if (addr >= IO_FM && (addr - IO_FM) < 2) opl2_write((uint8_t)(addr - IO_FM), v);   /* the OPL2: ADDR, DATA */
        return;
    case IO_MATH:
        fred_write(addr & 0xFF, v); return;
    case IO_SYS:
        sys_write(addr & 0xFF, v); return;
    case IO_BANK: {
        uint8_t r = addr & 0xFF, b = r >> 2, i = r & 3;
        if (r >= 0x20) return;
        { uint32_t cur = (mem_bank_base(b) & ~(0xFFu << (8 * i))) | ((uint32_t)v << (8 * i));
          if (i == 3) { if (v & 0x80) { mem_bank_setbase(b, cur); mem_bank_off(b); } else mem_bank_set(b, cur); }   /* byte 3 decides on/off */
          else mem_bank_setbase(b, cur); }                                                                           /* bytes 0-2: the base only */
        return;
    }
    case IO_TUBE:
        tube_io_write(addr & 0xFF, v); return;
    case IO_FAR: {
        uint8_t r = addr & 0xFF;
        if (r >= 0x80 && r < 0x84) { far_table = (far_table & ~(0xFFu << (8 * (r - 0x80)))) | ((uint32_t)v << (8 * (r - 0x80))); far_table &= K4510_PHYS_MASK; }
        if (r == 0x85) far_err = 0;
        return;
    }
    case IO_DMA:
        if ((addr & 0xFF) < 12) { dma_reg[addr & 0xFF] = v; return; }
        if (addr == IO_DMA_CMD) { dma_run(v); return; }
        return;
    case IO_STORAGE:
        hostfs_write(addr & 0xFF, v); return;
    case IO_NET:
        net_write(addr & 0xFF, v);
        return;
    case IO_TERM:
        term_write(addr & 0xFF, v);
        return;
    case IO_INPUT:
        /* Type-ahead: a write to KBD pushes a key into the queue, so a program
         * can type at the shell -- the C64's keyboard buffer, which is how a
         * program there handed a command back to BASIC.  It goes straight into
         * the FIFO, not through kbd_push: a program is not allowed to open the
         * menu, and the debugger's key log is for keys a person pressed. */
        if ((addr & 0xFF) == 0) kbd_enqueue(v);
        if (addr == IO_MOUSEPTR) mouse_wanthost = (uint8_t)((v >> 1) & 1);
        return;
    default:
        return;
    }
}

/* ---- save states (core/state.h) ------------------------------------------
 * The chunks are read back in the order they were written, so the order
 * here is the file format: each device writes its own, in the sequence the
 * single io.c wrote them in before the split (2026-10-07). */
void io_state_save(FILE *f)
{
    state_put(f, "SYSF", &sys_frames, sizeof sys_frames);
    state_put(f, "KBDQ", kbd_fifo, sizeof kbd_fifo);
    state_put(f, "KBDH", &kbd_head, sizeof kbd_head);
    state_put(f, "KBDT", &kbd_tail, sizeof kbd_tail);
    hostfs_state_save(f);
    state_put(f, "DMA ", dma_reg, sizeof dma_reg);
    fred_state_save(f);
    sys_state_save(f);
    seq_state_save(f);
    tube_state_save(f);
}
int io_state_load(FILE *f)
{
    if (state_get(f, "SYSF", &sys_frames, sizeof sys_frames) || state_get(f, "KBDQ", kbd_fifo, sizeof kbd_fifo)
        || state_get(f, "KBDH", &kbd_head, sizeof kbd_head) || state_get(f, "KBDT", &kbd_tail, sizeof kbd_tail)
        || hostfs_state_load(f)
        || state_get(f, "DMA ", dma_reg, sizeof dma_reg) || fred_state_load(f)
        || sys_state_load(f) || seq_state_load(f) || tube_state_load(f)) return -2;
    /* the file closed, the network dropped, the Tube stopped.  The OPL2's
     * registers are not carried: a state loads with the chip reset, and the
     * sequencer's playing notes re-sound as their queues advance. */
    kbd_head = (int)((unsigned) kbd_head % 64); kbd_tail = (int)((unsigned) kbd_tail % 64);   /* unsigned: -5 % 64 is still -5 */
    hostfs_after_load(); net_reset(); tube_stop();
    return 0;
}
