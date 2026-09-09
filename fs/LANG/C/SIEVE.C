/* K4510: the Byte Sieve (Gilbreath, BYTE Sept 1981) in C, 10 iterations,
 * as in rprouse/8bit-benchmarks ByteSieve.c. Timed with the $D50D frame
 * counter. The original compares compilers on a 1 MHz 6502 too. */
#include "k4510.h"

void __fastcall__ rom_chrout(unsigned char c);
unsigned char rom_getin(void);

#define true 1
#define false 0
#define size 8190
#define sizepl 8191
static char flags[sizepl];

static void print(const char *s) { while (*s) rom_chrout(*s++); }
static void nl(void) { rom_chrout('\n'); }
static void dec(uint32_t v) { char b[11]; uint8_t i = 10; b[i] = 0; do { b[--i] = '0' + v % 10; v /= 10; } while (v); print(b + i); }
static uint16_t frames_now(void) { return REG(SYS + 0x0D) | ((uint16_t)REG(SYS + 0x0E) << 8); }

void main(void)
{
    int i, prime, k, count = 0, iter; uint16_t t0, t;
    rom_chrout(12);
    print("Byte Sieve in C (cc65 -O, 65C02 output), 10 iterations"); nl();
    t0 = frames_now();
    for (iter = 1; iter <= 10; iter++) {
        count = 0;
        for (i = 0; i <= size; i++) flags[i] = true;
        for (i = 0; i <= size; i++) {
            if (flags[i]) {
                prime = i + i + 3;
                k = i + prime;
                while (k <= size) { flags[k] = false; k += prime; }
                count = count + 1;
            }
        }
    }
    t = frames_now() - t0;
    dec(count); print(" primes"); nl();
    print("TIME: "); dec(t); print(" frames = "); dec(t / 60); rom_chrout('.'); dec((t % 60) * 100 / 60 / 10); dec(((t % 60) * 100 / 60) % 10); print(" seconds"); nl();
    print("DONE -- press a key"); nl();
    while (rom_getin()) ; while (!rom_getin()) ;   /* the ROM clears the screen when we return */
}
