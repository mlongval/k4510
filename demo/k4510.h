/* Shared hardware access for the K4510 demo programs (cc65, .prg run from
 * the system ROM). Mirrors core/io.h and core/vicky.h. */
#ifndef K4510_DEMO_H
#define K4510_DEMO_H
#include <stdint.h>
#include <string.h>

#define REG(a) (*(volatile uint8_t *)(a))
#define VICKY  0xD000u
#define KBD    0xD100u
#define KBDST  0xD101u
#define DMA    0xD200u
#define SYS    0xD500u

#define V_CTRL   0xD000u
#define V_BGCOL  0xD001u
#define V_IRQST  0xD004u
#define V_PALIDX 0xD006u
#define V_PALR   0xD007u
#define V_PALG   0xD008u
#define V_PALB   0xD009u
#define V_SPRTAB 0xD00Au
#define V_SPRCTL 0xD00Eu
#define V_LAYER(n) (0xD010u + (n) * 0x10)
#define V_SHEILA 0xD060u
#define V_SHEILACTL 0xD064u

#define FONT8    0x00010000UL        /* 256 ASCII glyphs, placed by the frontend */
/* MATH unit ops (core/io.h) */
#define MATH_MOV 0
#define MATH_ADD 1
#define MATH_SUB 2
#define MATH_MUL 3
#define MATH_DIV 4
#define MATH_SQRT 5
#define MATH_CMP 18
#define MATH_ITOF 19
#define MATH_FTOI 20
#define ML_END 0x80
#define ML_STOPNEG 0x81
#define ML_STOPPOS 0x82
#define ML_JUMP 0x85
#define ML_DJNZ 0x86
#define ML_STOPFIGE 0x87
#define ML_LDF 0x88
#define ML_LDI 0x89
#define WINDOW   ((uint8_t *)0x2000) /* the MAP window, 16 KB, see prg0.s */

void __fastcall__ map_window(unsigned long phys);
void __fastcall__ far_poke(unsigned long a, unsigned char v);
void __fastcall__ far_poke16(unsigned long a, unsigned int v);
unsigned char __fastcall__ far_peek(unsigned long a);

static void w32(uint16_t r, uint32_t v) { REG(r) = v; REG(r + 1) = v >> 8; REG(r + 2) = v >> 16; REG(r + 3) = v >> 24; }
static void w16(uint16_t r, uint16_t v) { REG(r) = v; REG(r + 1) = v >> 8; }

static void pal(uint8_t i, uint8_t r, uint8_t g, uint8_t b) { REG(V_PALIDX) = i; REG(V_PALR) = r; REG(V_PALG) = g; REG(V_PALB) = b; }

/* vblank from the system frame counter ($D50D), since the ROM owns the IRQ */
static void wait_vblank(void) { uint8_t f = REG(SYS + 0x0D); while (REG(SYS + 0x0D) == f) ; }
/* any key pressed? consume it and return nonzero */
static uint8_t key_hit(void) { if (REG(KBDST) & 0x80) { (void)REG(KBD); return 1; } return 0; }
/* key pressed? return it (0 if none) */
static uint8_t key_get(void) { return (REG(KBDST) & 0x80) ? REG(KBD) : 0; }
/* The keys held right now (core/io.h $D104): a joystick for games.  The
 * queue above says a key was PRESSED; this says it is still DOWN, which is
 * what "stop when I let go" needs. */
#define KBDHELD 0xD104u
#define HELD_UP 1
#define HELD_DOWN 2
#define HELD_LEFT 4
#define HELD_RIGHT 8
#define HELD_FIRE 16
#define HELD_A 32
#define HELD_B 64
static uint8_t keys_held(void) { return REG(KBDHELD); }

/* frames-per-second meter: call fps_tick() once per drawn frame; fps_value
 * is refreshed every 60 vblanks (from the $D50D frame counter) */
static uint8_t fps_value, fps_count, fps_last;
static uint16_t fps_vbl;
static void fps_tick(void)
{
    uint8_t f = REG(SYS + 0x0D);
    fps_count++;
    fps_vbl += (uint8_t)(f - fps_last); fps_last = f;
    if (fps_vbl >= 60) { fps_value = fps_count; fps_count = 0; fps_vbl -= 60; }
}
static void put_num(uint32_t map, uint8_t cols, uint8_t x, uint8_t y, uint8_t v)
{
    uint32_t p = map + (uint32_t)y * cols + x;
    far_poke(p, '0' + v / 100); far_poke(p + 1, '0' + (v / 10) % 10); far_poke(p + 2, '0' + v % 10);
}

/* DMA: copy len bytes phys src -> phys dst */
static void dma_copy(uint32_t src, uint32_t dst, uint32_t len) { w32(DMA, src); w32(DMA + 4, dst); w32(DMA + 8, len); REG(DMA + 12) = 1; }
/* DMA: fill len bytes at phys dst with value (taken from SRC register byte 0) */
static void dma_fill(uint8_t value, uint32_t dst, uint32_t len) { REG(DMA) = value; w32(DMA + 4, dst); w32(DMA + 8, len); REG(DMA + 12) = 2; }

/* A text8 layer (1 byte per cell, 1-bit glyphs, colour = LPALOFS<<1|1); the map is in far memory */
static void text8_layer(uint8_t n, uint32_t map, uint8_t cols, uint8_t palofs)
{
    uint16_t L = V_LAYER(n);
    REG(L + 1) = palofs; w16(L + 2, 0); w16(L + 4, 0); w16(L + 6, cols);
    w32(L + 8, FONT8); w32(L + 12, map);
    REG(L) = 1 | (2 << 1);          /* enable, text8, 8x8 */
}
static void text8_print(uint32_t map, uint8_t cols, uint8_t x, uint8_t y, const char *s)
{
    uint32_t p = map + (uint32_t)y * cols + x;
    while (*s) far_poke(p++, *s++);
}
#endif

unsigned char __fastcall__ rom_shell(const char *line);   /* run a K/OS command ($FF8F) */
