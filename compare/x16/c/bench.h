/* bench.h -- the few things the comparison programs need from
 * each machine, so sieve.c, loop.c and memcpy.c are one source
 * built twice:
 *     cl65 -t cx16 -O ...                  the Commander X16
 *     cc65 -t none --cpu 65c02 -O ...      the K4510 (tools/k4510-cc's recipe)
 * Time is each machine's own 60 Hz tick: the X16's jiffy clock
 * (cc65's clock() reads RDTIM; CLOCKS_PER_SEC is 60) and the
 * K4510's frame counter at SYS+$0D.  Never the host's clock.
 * Strings are lowercase: cc65 maps them to PETSCII for the X16,
 * where they come out as capitals. */
#ifndef BENCH_H
#define BENCH_H
#include <stdint.h>

#ifdef __CX16__
#include <stdio.h>
#include <time.h>
static uint16_t ticks(void) { return (uint16_t) clock(); }
static void outc(char c) { putchar(c); }
static void out(const char *s) { fputs(s, stdout); }
static void finish(void) { }                 /* back to BASIC's READY. */
#define MACHINE "x16 (cc65 -t cx16)"
#else
#include "k4510.h"
void __fastcall__ rom_chrout(unsigned char c);
unsigned char rom_getin(void);
static uint16_t ticks(void) { return REG(SYS + 0x0D) | ((uint16_t) REG(SYS + 0x0E) << 8); }
static void outc(char c) { rom_chrout(c); }
static void out(const char *s) { while (*s) rom_chrout(*s++); }
static void finish(void) { while (rom_getin()) ; while (!rom_getin()) ; }   /* the ROM clears the screen on return */
#define MACHINE "k4510 (cc65 -t none --cpu 65c02)"
#endif

static void dec(uint32_t v)
{
    char b[11]; uint8_t i = 10; b[i] = 0;
    do { b[--i] = (char) ('0' + v % 10); v /= 10; } while (v);
    out(b + i);
}

/* RESULT NAME s.ss S -- ticks are 60ths of a second on both machines */
static void report(const char *name, uint16_t t)
{
    uint16_t h = (uint16_t) (((uint32_t) (t % 60) * 100 + 30) / 60);
    out("result "); out(name); outc(' ');
    dec(t / 60); outc('.'); outc((char) ('0' + h / 10)); outc((char) ('0' + h % 10));
    out(" s\n");
}
#endif
