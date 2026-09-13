/* K4510: MONITOR -- the machine monitor and its memory tools, out of the ROM
 * (2026-09-13, Doc: "do the monitor program that consolidates stuff").
 *
 *   MON / WOZ            the * prompt, Wozmon's grammar; X, EXIT or Q leaves
 *   MON addr.addr        one line, no prompt:  addr  addr.addr  addr:b b b  addrR
 *   FILL from.to value   DMA fill, 28-bit addresses
 *   COPY from.to dest    DMA copy, 28-bit addresses
 *
 * The ROM's shell words MON, WOZ, FILL and COPY run MONITOR <word> <args>.
 *
 * It lives at $E000-$FEFF (demo/monitor.cfg), in the RAM under the ROM that
 * programs own, not at $6000 like the others: the memory it is there to look
 * at, $0800-$CFFF, is left alone.  Two things follow from where it sits:
 *  - Anything handed to the ROM must be below $A000.  During a system call the
 *    ROM is banked back in over $A000-$FFFF, so the line for SHELL is built in
 *    page 3 ($0300), which belongs to programs.
 *  - Addresses below $10000 read as the CPU sees them from a program:
 *    $A000-$FEFF is the RAM under the ROM (this monitor, a K4SG BASIC), not
 *    the ROM itself; $D000-$DFFF is I/O.  The ROM's own MON showed ROM there.
 * Anything else typed at the prompt goes to the shell through SWAP -k, which
 * saves and restores all 64 KB, so a program that loads into $E000 cannot
 * take the monitor with it.  (Under a SWAP already -- *MON from MS BASIC --
 * SWAP refuses to nest and says so; the monitor itself carries on.) */
#include "k4510.h"
#include "far.h"

void __fastcall__ rom_chrout(unsigned char c);
static unsigned char rom_args(void)  { return ((unsigned char (*)(void))0xFF95)(); }
static unsigned char rom_chrin(void) { return ((unsigned char (*)(void))0xFF83)(); }

#define SHBUF    ((char *)0x0300)                  /* page 3: the line for SHELL */
#define SHELL_RC (*(volatile uint8_t *)0x03FF)     /* the shell's result byte */

static char in[80];
static uint32_t xam;                               /* the last opened address */
static uint8_t mode;                               /* 0 examine, 1 store, 2 block */

static void say(const char *s) { while (*s) rom_chrout(*s++); }
static void nl(void) { rom_chrout('\n'); }
static void err(const char *s) { say(s); nl(); SHELL_RC = 1; }
static void hex2(uint8_t v) { static const char h[] = "0123456789ABCDEF"; rom_chrout(h[v >> 4]); rom_chrout(h[v & 15]); }
static void hex28(uint32_t v) { hex2((uint8_t)(v >> 24)); hex2((uint8_t)(v >> 16)); hex2((uint8_t)(v >> 8)); hex2((uint8_t)v); }
static void dec(uint32_t v) { char b[11]; uint8_t i = 10; b[i] = 0; do { b[--i] = (char)('0' + v % 10); v /= 10; } while (v); say(&b[i]); }

static uint8_t ishex(char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'); }
static uint8_t hexval(char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; }
static uint32_t parsehex(const char **p, uint8_t *digits)
{
    uint32_t v = 0; *digits = 0;
    while (ishex(**p)) { v = (v << 4) | hexval(**p); (*p)++; (*digits)++; }
    return v;
}
static void skipsp(const char **p) { while (**p == ' ') (*p)++; }
static uint8_t word(const char **p, const char *w)   /* a whole word, either case */
{
    const char *s = *p;
    while (*w) { if ((*s | 0x20) != (*w | 0x20)) return 0; s++; w++; }
    if (*s && *s != ' ') return 0;
    *p = s; skipsp(p);
    return 1;
}

static uint8_t peek(uint32_t a) { return a < 0x10000UL ? *(volatile uint8_t *)(uint16_t)a : far_peek(a); }
static void poke(uint32_t a, uint8_t v) { if (a < 0x10000UL) *(volatile uint8_t *)(uint16_t)a = v; else far_poke(a, v); }

static void dump(uint32_t from, uint32_t to)       /* the ROM's format, byte for byte */
{
    uint8_t n = 0;
    for (; from <= to; from++) {
        if (n == 0) { hex28(from); say(": "); }
        hex2(peek(from)); rom_chrout(' ');
        if (++n == 16) { n = 0; nl(); }
        if (from == 0x0FFFFFFFUL) break;
    }
    if (n) nl();
}

static void shell(const char *pre, const char *s)  /* SHELL, from page 3 */
{
    uint8_t i = 0;
    while (*pre) SHBUF[i++] = *pre++;
    while (*s && i < 95) SHBUF[i++] = *s++;
    SHBUF[i] = 0;
    rom_shell(SHBUF);
}

static void run(uint32_t a)                        /* addrR: RUN, as the ROM's own */
{
    static const char h[] = "0123456789ABCDEF";
    char r[5]; uint8_t i;
    if (a >= 0x10000UL) { err("run: 16-bit address"); return; }
    for (i = 0; i < 4; i++) r[i] = h[(uint8_t)(a >> (12 - 4 * i)) & 15];
    r[4] = 0;
    shell("RUN ", r);
}

static void mon_line(const char *p)
{
    uint8_t d; uint32_t v;
    mode = 0;
    for (;;) {
        skipsp(&p);
        if (!*p) return;
        if (*p == ':') { mode = 1; p++; continue; }
        if (*p == '.') { mode = 2; p++; continue; }
        if (*p == 'R' || *p == 'r') { run(xam); return; }
        v = parsehex(&p, &d);
        if (!d) { err("?"); return; }
        if (mode == 1) { poke(xam++, (uint8_t)v); continue; }
        if (mode == 2) { dump(xam, v); xam = v + 1; mode = 0; continue; }
        /* An address alone is examined -- unless a "." follows, when it is
         * the start of a range and the range prints it.  Examining it first
         * printed "FF80: 4C" and then the range again from FF80. */
        xam = v; skipsp(&p);
        if (*p != '.') dump(v, v);
    }
}

static void fill(const char *p)
{
    uint8_t d; uint32_t from, to, v;
    from = parsehex(&p, &d); if (!d || *p != '.') { err("fill: from.to value"); return; }
    p++; to = parsehex(&p, &d); if (!d || to < from) { err("fill: from.to value"); return; }
    skipsp(&p); v = parsehex(&p, &d); if (!d) { err("fill: from.to value"); return; }
    far_fill(from, to - from + 1, (uint8_t)v);
    dec(to - from + 1); say(" bytes filled"); nl();
}

static void copy(const char *p)
{
    uint8_t d; uint32_t from, to, dst;
    from = parsehex(&p, &d); if (!d || *p != '.') { err("copy: from.to dest"); return; }
    p++; to = parsehex(&p, &d); if (!d || to < from) { err("copy: from.to dest"); return; }
    skipsp(&p); dst = parsehex(&p, &d); if (!d) { err("copy: from.to dest"); return; }
    far_copy(dst, from, to - from + 1);
    dec(to - from + 1); say(" bytes copied to "); hex28(dst); nl();
}

static void getline(void)                          /* typing, Backspace, Enter */
{
    uint8_t i = 0, k;
    for (;;) {
        k = rom_chrin();
        if (k == 13) { nl(); break; }
        if (k == 8 || k == 0x7F) { if (i) { i--; rom_chrout(8); rom_chrout(' '); rom_chrout(8); } continue; }
        if (k >= 32 && k < 127 && i < sizeof in - 2) { in[i++] = (char)k; rom_chrout(k); }
    }
    in[i] = 0;
}

/* Is this line the monitor's?  By its whole first word: hex digits, '.' and
 * ':', and an R to end it (2000.2001  3000:A9  3000R).  The ROM's monitor
 * went by the first character, so ECHO, DIR, CD, ALIAS -- E, D, C, A are hex
 * -- never reached the shell from the * prompt ("ECHO X" examined $EC).  A
 * word that is all hex (ADD, FACE) is still an address, as in Wozmon. */
static uint8_t monline(const char *q)
{
    for (; *q && *q != ' '; q++) {
        if (ishex(*q) || *q == '.' || *q == ':') continue;
        if ((*q == 'R' || *q == 'r') && (!q[1] || q[1] == ' ')) continue;
        return 0;
    }
    return 1;
}

static void prompt(void)
{
    const char *q;
    say("monitor: addr  addr.addr  addr:b b b  addrR  (28-bit hex)   X leaves"); nl();
    for (;;) {
        rom_chrout('*');
        getline();
        q = in; skipsp(&q);
        if (!*q) continue;
        if (word(&q, "X") || word(&q, "EXIT") || word(&q, "Q")) return;
        if (monline(q)) mon_line(q); else shell("SWAP -k ", q);
    }
}

void main(void)
{
    const char *p;
    rom_args();
    p = *(const char **)0xF0;
    skipsp(&p);
    if (word(&p, "FILL")) { fill(p); return; }
    if (word(&p, "COPY")) { copy(p); return; }
    if (!word(&p, "MON")) word(&p, "WOZ");
    if (*p) mon_line(p); else prompt();
}
