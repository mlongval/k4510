/* K4510: BUG -- interview the user and write a finished issue to disk.
 *
 * DUMP writes what the machine knows about itself.  This writes what the
 * machine cannot know: what you typed, what you expected, and why.  The two
 * are meant to travel together -- the report names the dump.
 *
 * The result goes to /SYSTEM/LOG/BUGREPORTS/ as plain text, ready to paste into
 * a GitHub issue.  Nothing here is a form: an issue on this project is handed
 * to a coding session as written, so the questions ask for the things that
 * save it a day, in the order a person remembers them.
 *
 * The parts the machine already knows are filled in without asking: which
 * machine (SYS $D522), the emulator's version, the video mode, the working
 * directory, and the last DUMP.
 */
#include "k4510.h"

#define TERM   0xDA00u
#define SYS    0xD500u
#define BUF    ((char *)0x0800)          /* the report as it is built: below us, ours */
#define BUFMAX 0x4000u
#define ANS    120                       /* longest answer we take */

void __fastcall__ rom_chrout(unsigned char c);
unsigned char rom_getin(void);
unsigned char __fastcall__ rom_shell(const char *line);
static unsigned char rom_save(void) { return ((unsigned char (*)(void))0xFF8C)(); }
static void zp16(uint8_t a, uint16_t v) { REG(a) = v; REG(a + 1) = v >> 8; }
static void zp32(uint8_t a, uint32_t v) { REG(a)=v; REG(a+1)=v>>8; REG(a+2)=v>>16; REG(a+3)=v>>24; }

/* BUG-LABELS: the handbook checks these against
   doc/guide/chapters/88-issues.tex.  Change one here and the guide build
   fails until the page agrees.  (Machine, Version, Screen, Dump and When
   the machine answers itself; the rest are the questions it asks.) */
static const char *const bug_labels[] = {
    "Machine", "Version", "Screen", "Dump", "When",
    "Where were you?",
    "What did you type, exactly?",
    "What happened?",
    "What did you expect instead?",
    "Why did you expect that?",
    "Still wrong with  k4510 --no-startup.bat ?",
    "Anything else?" };

static unsigned len;
static char name[72], ans[ANS + 2];

static void put(char c) { rom_chrout((unsigned char)c); }
static void say(const char *s) { while (*s) put(*s++); }
static void add(const char *s) { while (*s && len < BUFMAX - 1) BUF[len++] = *s++; }
static void addn(unsigned long v, uint8_t w)          /* w digits, zero padded */
{
    char b[8]; uint8_t i = 0;
    do { b[i++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (i < w) b[i++] = '0';
    while (i) { if (len < BUFMAX - 1) BUF[len++] = b[--i]; }
}
static void nl(void) { add("\n"); }

/* one question: print it, read a line, put "  answer" in the report */
static void ask(const char *q, const char *hint)
{
    uint8_t n = 0, k;
    say("\n"); say(q); say("\n");
    if (hint) { say("   ("); say(hint); say(")\n"); }
    say(" > ");
    for (;;) {
        do { k = rom_getin(); } while (!k);
        if (k == 0x0D) break;
        if (k == 0x08) { if (n) { n--; put(8); put(' '); put(8); } continue; }
        if (k == 0x1B) { n = 0; break; }
        if (k >= 0x20 && k != 0x7F && n < ANS) { ans[n++] = (char)k; put((char)k); }
    }
    ans[n] = 0;
    put('\n');
    add(q); nl();
    add("  "); add(n ? ans : "(not given)"); nl(); nl();
}

void main(void)
{
    uint8_t i, mode;

    { volatile uint8_t d = REG(SYS + 4); (void)d; }   /* latch the host clock: the time
                                                       * registers read 0 until you do */
    say("\nBUG -- this writes an issue you can paste into GitHub.\n");
    say("Answer what you can; Enter alone skips a question.\n");

    add("K4510 issue\n===============\n\n");

    add("Machine:  "); add(REG(SYS + 0x22) ? "Raspberry Pi 3B+" : "desktop"); nl();
    add("Version:  ");
    for (i = 0; i < 16 && REG(SYS + 0x10 + i); i++) { if (len < BUFMAX - 1) BUF[len++] = (char)REG(SYS + 0x10 + i); }
    nl();
    add("Screen:   "); addn(REG(TERM + 5), 0); add("x"); addn(REG(TERM + 6), 0);
    mode = (uint8_t)(REG(0xD000) & 0x1E);
    add("  (MODE "); addn(mode == 0 ? 0 : mode == 4 ? 1 : mode == 2 ? 2 : mode == 10 ? 3 : 4, 0); add(")"); nl();
    add("Dump:     ");
    if (!REG(SYS + 0xF0)) add("none -- type DUMP while it is wrong, then run BUG again");
    else { add("dumps/dump-"); addn(REG(SYS + 0xF0), 3); add(".txt"); }
    nl();
    add("When:     ");
    addn(REG(SYS + 0x0A) | ((unsigned long)REG(SYS + 0x0B) << 8), 4); add("-");
    addn(REG(SYS + 9), 2); add("-"); addn(REG(SYS + 8), 2); add(" ");
    addn(REG(SYS + 7), 2); add(":"); addn(REG(SYS + 6), 2); add(":"); addn(REG(SYS + 5), 2);
    nl(); nl();

    ask(bug_labels[5], "shell, EhBASIC, BBC BASIC, CP/M, Forth, Pascal, VI, EDIT, the F7 menu");
    ask(bug_labels[6], "the command or the keys, as typed");
    ask(bug_labels[7], 0);
    ask(bug_labels[8], 0);
    ask(bug_labels[9], "the handbook page, a real C64 or Beeb, or it worked before");
    ask(bug_labels[10], "yes, no, or did not try");
    ask(bug_labels[11], 0);

    add("(Written by the BUG command on the machine itself.)\n");

    /* the folder may not exist yet; the shell is the only thing that can make it */
    rom_shell("MKDIR /SYSTEM/LOG"); rom_shell("MKDIR /SYSTEM/LOG/BUGREPORTS");

    name[0] = 0;
    { const char *pfx = "/SYSTEM/LOG/BUGREPORTS/BUG-"; uint8_t n = 0;
      while (*pfx) name[n++] = *pfx++;
      /* YYYYMMDD-HHMMSS: sorts by itself, and says when without opening it */
      { unsigned long y = REG(SYS + 0x0A) | ((unsigned long)REG(SYS + 0x0B) << 8);
        name[n++] = (char)('0' + (y / 1000) % 10); name[n++] = (char)('0' + (y / 100) % 10);
        name[n++] = (char)('0' + (y / 10) % 10);   name[n++] = (char)('0' + y % 10); }
      name[n++] = (char)('0' + REG(SYS + 9) / 10); name[n++] = (char)('0' + REG(SYS + 9) % 10);
      name[n++] = (char)('0' + REG(SYS + 8) / 10); name[n++] = (char)('0' + REG(SYS + 8) % 10);
      name[n++] = '-';
      name[n++] = (char)('0' + REG(SYS + 7) / 10); name[n++] = (char)('0' + REG(SYS + 7) % 10);
      name[n++] = (char)('0' + REG(SYS + 6) / 10); name[n++] = (char)('0' + REG(SYS + 6) % 10);
      name[n++] = (char)('0' + REG(SYS + 5) / 10); name[n++] = (char)('0' + REG(SYS + 5) % 10);
      { const char *sfx = ".TXT"; while (*sfx) name[n++] = *sfx++; }
      name[n] = 0; }

    zp16(0xF0, (uint16_t)name); zp32(0xF2, (uint32_t)(uint16_t)BUF); zp32(0xF6, (uint32_t)len);
    if (rom_save()) { say("\nBUG: could not write "); say(name); say("\n"); return; }
    say("\nWritten: "); say(name); say("\n");
    say("Paste it into a GitHub issue, or TYPE it to read it back.\n");
}
