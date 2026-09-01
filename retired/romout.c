/* K4510: RAM under the ROM (K-05). The ROM occupies $A000-$FFFF of the
 * CPU view, but rom_out() banks blocks 5 and 7 onto the physical RAM that
 * sits under it: $A000-$BFFF and $E000-$FEFF become the program's. The ROM
 * keeps $C000-$CFFF (4 KB) next to the I/O page and the stub page $FF00,
 * and every system call still works -- the stub banks the ROM in and out
 * around it. This program fills the revealed RAM, prints through CHROUT
 * while the ROM is out, verifies, and counts what a program can use. */
#include "k4510.h"
#include "far.h"

void __fastcall__ rom_chrout(unsigned char c);
unsigned char rom_getin(void);
static void print(const char *s) { while (*s) rom_chrout(*s++); }
static void nl(void) { rom_chrout('\n'); }
static void dec(uint32_t v) { char b[11]; uint8_t i = 10; b[i] = 0; do { b[--i] = '0' + v % 10; v /= 10; } while (v); print(b + i); }
static void hex16(uint16_t v) { static const char h[] = "0123456789ABCDEF"; rom_chrout(h[v >> 12]); rom_chrout(h[(v >> 8) & 15]); rom_chrout(h[(v >> 4) & 15]); rom_chrout(h[v & 15]); }

static uint16_t fill_check(uint16_t from, uint16_t to, uint8_t seed)
{
    uint8_t *p = (uint8_t *)from; uint16_t n = to - from, i, bad = 0;
    for (i = 0; i < n; i++) p[i] = (uint8_t)(seed + i);
    for (i = 0; i < n; i++) if (p[i] != (uint8_t)(seed + i)) bad++;
    return bad;
}

void main(void)
{
    uint16_t bad5, bad7; uint8_t under;
    rom_chrout(12);
    print("RAM under the ROM (K-05)"); nl(); nl();
    print("ROM in:  $A000 reads $"); hex16(*(uint16_t *)0xA000); print(", $E000 reads $"); hex16(*(uint16_t *)0xE000); print(" (ROM code)"); nl();
    rom_out();
    print("ROM out: this line is printed by CHROUT with the ROM banked away"); nl();
    bad5 = fill_check(0xA000, 0xD000, 0x11);
    bad7 = fill_check(0xE000, 0xFF00, 0x22);
    under = *(uint8_t *)0xFF80;                    /* the stub page is still the ROM: a JMP */
    print("  filled $A000-$CFFF and $E000-$FEFF: "); dec(bad5 + bad7); print(" bad bytes"); nl();
    print("  $FF80 still reads $"); hex16(under); print(" (the jump table: the stub page never banks)"); nl();
    print("  bank 5 = $"); hex16((uint16_t)bank_get(5)); print(", bank 7 = $"); hex16((uint16_t)bank_get(7)); nl();
    rom_in();
    print("ROM in again: $A000 reads $"); hex16(*(uint16_t *)0xA000); print(" (the pattern is under it, untouched: $"); rom_out(); hex16(*(uint16_t *)0xA000); rom_in(); print(")"); nl(); nl();
    print("a program can use $0800-$CFFF + $E000-$FEFF = "); dec((0xD000UL - 0x0800) / 1024 + (0xFF00UL - 0xE000) / 1024); print(" KB of the 64 KB view;"); nl();
    print("I/O keeps $D000-$DFFF and the stub keeps $FF00-$FFFF, whatever is banked."); nl();
    nl(); print("DONE -- press a key"); nl();
    while (rom_getin()) ; while (!rom_getin()) ;
}
