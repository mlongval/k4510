/* K4510: keyboard test, for the PC keyboard the machine is driven from.
 * Asks for every key the machine has a code for, checks what $D100 delivers
 * and -- for the arrows, Home/End, Delete and the F-keys -- that $D101 bit 6
 * says it was a KEY code and not the accented letter that shares its byte
 * (Left and é are both $82).  Then the three modifiers through the status
 * register, shifted combinations, a dead-key letter if the keyboard can make
 * one, a summary, and fs/keytest.txt.  A key that never arrives times out
 * after 8 s.  F7 is not asked for: it is the menu's, and never reaches the
 * machine unshifted -- Shift+F7 is in the shifted list instead.
 * (Until 2026-09-08 this asked for the C64 matrix of the bare-metal Pi's
 * GPIO keyboard, which is gone with that port.) */
#include "k4510.h"

void __fastcall__ rom_chrout(unsigned char c);
unsigned char rom_getin(void);

static void print(const char *s) { while (*s) rom_chrout(*s++); }
static void nl(void) { rom_chrout('\n'); }
static void hex2(uint8_t v) { static const char h[] = "0123456789ABCDEF"; rom_chrout(h[v >> 4]); rom_chrout(h[v & 15]); }

static char report[6000]; static uint16_t rlen;
static void rep(const char *s) { while (*s && rlen < sizeof report - 2) report[rlen++] = *s++; }
static void repc(char c) { if (rlen < sizeof report - 2) report[rlen++] = c; }
static void rephex(uint8_t v) { static const char h[] = "0123456789ABCDEF"; repc(h[v >> 4]); repc(h[v & 15]); }
static void both(const char *s) { print(s); rep(s); }

static uint8_t kind;                                      /* $D101 bit 6 after the read: 1 = a KEY code */
static uint8_t wait_key(uint8_t seconds)
{
    uint8_t f0 = REG(SYS + 0x0D), k; uint16_t frames = 0;
    for (;;) {
        k = rom_getin(); if (k) { kind = (REG(KBDST) & 0x40) ? 1 : 0; return k; }
        if (REG(SYS + 0x0D) != f0) { f0 = REG(SYS + 0x0D); if (++frames >= (uint16_t)seconds * 60) return 0; }
    }
}
static void flush_keys(void) { while (rom_getin()) ; }

typedef struct { const char *label; uint8_t code; } key_t;   /* a code of $80 or more must arrive as a KEY code ($D101 bit 6) */
static const key_t keys[] = {
    {"Enter", 0x0D}, {"Backspace", 0x08}, {"Tab", 0x09}, {"Esc", 0x1B}, {"Space", ' '},
    {"Up", 0x80}, {"Down", 0x81}, {"Left", 0x82}, {"Right", 0x83},
    {"Home", 0x84}, {"End", 0x85}, {"Page Up", 0x86}, {"Page Down", 0x87}, {"Insert", 0x88}, {"Delete", 0x89},
    {"F1", 0x90}, {"F2", 0x91}, {"F3", 0x92}, {"F4", 0x93}, {"F5", 0x94}, {"F6", 0x95},
    {"F8", 0x97}, {"F9", 0x98}, {"F10", 0x99}, {"F11", 0x9A}, {"F12", 0x9B},
    {"A", 'a'}, {"Z", 'z'}, {"Q", 'q'}, {"M", 'm'}, {"1", '1'}, {"0", '0'},
    {"-", '-'}, {"=", '='}, {"[", '['}, {"]", ']'}, {";", ';'}, {"'", '\''}, {",", ','}, {".", '.'}, {"/", '/'}, {"\\", '\\'}, {"`", '`'},
};
static const key_t shifted[] = {
    {"Shift+A", 'A'}, {"Shift+1 (!)", '!'}, {"Shift+2 (@ or \")", 0}, {"Shift+F7 (reaches the machine; F7 alone is the menu's)", 0x96},
    {"Ctrl+A (code 01)", 0x01}, {"Ctrl+C (code 03)", 0x03},
};

static uint8_t test_key(const key_t *k)
{
    uint8_t got;
    print("  press "); print(k->label); print(" ");
    rep("  "); rep(k->label); rep(": ");
    flush_keys();
    got = wait_key(8);
    if (!got) { both("NO KEY (timeout)"); nl(); repc('\n'); return 0; }
    print("-> $"); hex2(got); rep("got $"); rephex(got);
    if (kind) both(" key");
    if (got >= 0x20 && got < 0x7F) { print(" '"); rom_chrout(got); print("'"); repc(' '); repc('\''); repc(got); repc('\''); }
    if (k->code == 0 && got >= 0x20 && got != 0x7F && !kind) { both("  OK"); nl(); repc('\n'); return 1; }
    if (got == k->code && (got < 0x80) == !kind) { both("  OK"); nl(); repc('\n'); return 1; }
    if (got == k->code) both("  FAIL: the right byte but the wrong KIND ($D101 bit 6)");
    else { print("  FAIL, expected $"); hex2(k->code); rep("  FAIL, expected $"); rephex(k->code); }
    nl(); repc('\n');
    return 0;
}

static uint8_t test_mod(const char *label, uint8_t bit)
{
    uint8_t f0 = REG(SYS + 0x0D); uint16_t frames = 0;
    print("  hold "); print(label); print(" ... "); rep("  "); rep(label); rep(": ");
    for (;;) {
        if (REG(KBDST) & bit) { both("seen, OK"); nl(); repc('\n'); while (REG(KBDST) & bit) ; return 1; }
        if (REG(SYS + 0x0D) != f0) { f0 = REG(SYS + 0x0D); if (++frames >= 8 * 60) break; }
    }
    both("NOT SEEN (timeout)"); nl(); repc('\n'); return 0;
}

static void save_report(void)
{
    static const char name[] = "keytest.txt";
    w32(0xD304u, (uint16_t)name); w32(0xD308u, (uint16_t)report); w32(0xD30Cu, rlen);
    REG(0xD300u) = 10;
    if (REG(0xD301u) == 0) print("report saved as fs/keytest.txt"); else print("could not save the report");
    nl();
}

void main(void)
{
    uint8_t i, ok = 0, n = 0, mods = 0;
    rom_chrout(12);
    print("K4510 keyboard test -- press each key as asked (8 s each)"); nl(); nl();
    rep("K4510 keyboard test\n\nkeys:\n");
    for (i = 0; i < sizeof keys / sizeof keys[0]; i++) { n++; ok += test_key(&keys[i]); }
    nl(); print("modifiers (status register $D101):"); nl(); rep("\nmodifiers:\n");
    mods += test_mod("SHIFT", 0x01);
    mods += test_mod("CTRL", 0x02);
    mods += test_mod("ALT", 0x04);
    nl(); print("shifted combinations:"); nl(); rep("\nshifted:\n");
    for (i = 0; i < sizeof shifted / sizeof shifted[0]; i++) { n++; ok += test_key(&shifted[i]); }
    nl(); print("an accented letter, if this keyboard makes one (dead key, AltGr): type it, or Enter to skip"); nl();
    rep("\naccented: ");
    flush_keys();
    { uint8_t k = wait_key(15);
      if (!k || k == 0x0D) both("skipped");
      else { n++; print("  -> $"); hex2(k); rep("got $"); rephex(k);
             if (k >= 0x80 && !kind) { ok++; both("  a character, OK"); }
             else if (k >= 0x80) both("  FAIL: arrived as a KEY code");
             else both("  (plain ASCII: no accent reached the machine)"); }
      nl(); repc('\n'); }
    nl();
    print("result: "); rep("\nresult: ");
    { char b[8]; uint8_t v = ok; b[0] = '0' + v / 10; b[1] = '0' + v % 10; b[2] = '/'; b[3] = '0' + n / 10; b[4] = '0' + n % 10; b[5] = 0; both(b); }
    both(" keys, "); repc('0' + mods); rom_chrout('0' + mods); both("/3 modifiers"); nl(); repc('\n');
    save_report();
    nl(); print("free typing -- codes echo in hex, K marks a KEY code, Esc ends:"); nl();
    flush_keys();
    for (;;) {
        uint8_t k = wait_key(60);
        if (!k || k == 0x1B) break;
        print("$"); hex2(k); if (kind) print("K"); if (k >= 0x20 && k < 0x7F) { print(" '"); rom_chrout(k); print("'"); } print("   ");
    }
    nl();
}
