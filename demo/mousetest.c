/* K4510: MOUSETEST -- the mouse registers $D108-$D10F live, and a pointer
 * drawn the way a program is meant to draw one: a 16x16 sprite that
 * follows $D108/$D10A.  The line at the bottom is the raw registers.
 * Esc leaves (the machine has no pointer of its own to click with). */
#include "k4510.h"
#define MOUSEX 0xD108u
#define MOUSEY 0xD10Au
#define MOUSEB 0xD10Cu
#define MOUSEW 0xD10Du
#define MOUSEDX 0xD10Eu
#define MOUSEDY 0xD10Fu
#define SPRTAB  0x123000UL
#define SPRDATA 0x123100UL
void __fastcall__ rom_chrout(unsigned char c);
static void print(const char *s) { while (*s) rom_chrout(*s++); }
static void num(int v) { char b[8]; uint8_t i = 0, n; if (v < 0) { rom_chrout('-'); v = -v; } do { b[i++] = '0' + v % 10; v /= 10; } while (v); for (n = i; n < 5; n++) rom_chrout(' '); while (i) rom_chrout(b[--i]); }
static const uint8_t arrow[12] = { 0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xF0, 0xD8, 0x98, 0x0C, 0x0C };
static uint8_t at(int8_t x, int8_t y) { return (x >= 0 && x < 8 && y >= 0 && y < 12 && ((arrow[y] << x) & 0x80)) ? 1 : 0; }
static void make_pointer(void)
{
    uint32_t d = SPRDATA; int8_t x, y, dx, dy;
    for (y = 0; y < 16; y++) for (x = 0; x < 16; x += 2) {
        uint8_t v[2], k;
        for (k = 0; k < 2; k++) {
            int8_t px = x + k; uint8_t edge = 0;
            if (at(px, y)) { v[k] = 1; continue; }
            for (dy = -1; dy <= 1; dy++) for (dx = -1; dx <= 1; dx++) if (at(px + dx, y + dy)) edge = 1;
            v[k] = edge ? 2 : 0;
        }
        far_poke(d++, (v[0] << 4) | v[1]);
    }
    pal(17, 255, 255, 255); pal(18, 0, 0, 0);            /* PALOFS 1: 16+pixel */
    far_poke(SPRTAB + 4, (uint8_t) SPRDATA); far_poke(SPRTAB + 5, (uint8_t)(SPRDATA >> 8)); far_poke(SPRTAB + 6, (uint8_t)(SPRDATA >> 16)); far_poke(SPRTAB + 7, 0);
    far_poke(SPRTAB + 8, 0x31);                           /* enable, 4 bpp, after layer 3 */
    far_poke(SPRTAB + 9, 0x05);                           /* 16 x 16 */
    far_poke(SPRTAB + 10, 1);
    w32(V_SPRTAB, SPRTAB); REG(V_SPRCTL) = 1;
}
void main(void)
{
    int x, y; uint8_t b, w, dx, dy, row;
    print("MOUSETEST: $D108-$D10F, live.  The arrow is a sprite at the\n");
    print("mouse's position; the numbers are the registers.  Esc leaves.\n\n");
    make_pointer();
    row = REG(0xDA0Au);                                   /* JIM's cursor row: the readout line is rewritten in place */
    for (;;) {
        if (key_get() == 0x1B) break;
        x = REG(MOUSEX) | (REG(MOUSEX + 1) << 8); y = REG(MOUSEY) | (REG(MOUSEY + 1) << 8);
        b = REG(MOUSEB); w = REG(MOUSEW); dx = REG(MOUSEDX); dy = REG(MOUSEDY);
        far_poke(SPRTAB + 0, (uint8_t) x); far_poke(SPRTAB + 1, (uint8_t)(x >> 8));
        far_poke(SPRTAB + 2, (uint8_t) y); far_poke(SPRTAB + 3, (uint8_t)(y >> 8));
        REG(0xDA09u) = 0; REG(0xDA0Au) = row;
        print("  X"); num(x); print("  Y"); num(y);
        print("  buttons "); rom_chrout(b & 1 ? 'L' : '.'); rom_chrout(b & 4 ? 'M' : '.'); rom_chrout(b & 2 ? 'R' : '.');
        print("  wheel"); num((int8_t) w); print("  dx"); num((int8_t) dx); print("  dy"); num((int8_t) dy); print("   ");
        wait_vblank();
    }
    REG(V_SPRCTL) = 0;
    print("\n");
}
