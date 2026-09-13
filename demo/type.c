/* K4510: TYPE -- print a file a screen at a time.
 *
 *   TYPE name          a file here, or anywhere the device reaches:
 *   TYPE http://...    a URL, TYPE sftp://... -- the $D300 device fetches it
 *
 * It was a ROM command until 2026-09-13, when Doc asked for commands to move
 * out of the ROM ("loading from DISK is just loading from RAM").  Same
 * behaviour: "-- more --" after a screenful, Esc or Q stops, any other key
 * goes on -- except while a script runs (K_SCRIPT, $022E, the ROM's EXEC
 * flag): there is nobody to press the key, and the wait would hang
 * STARTUP.BAT.  HELP is TYPE /SYSTEM/ETC/HELP.  A failure sets the shell's
 * result byte, as the ROM's error() did, so a script can see it. */
#include "k4510.h"

void __fastcall__ rom_chrout(unsigned char c);
unsigned char rom_getin(void);
static unsigned char rom_args(void) { return ((unsigned char (*)(void))0xFF95)(); }

#define FS        0xD300u
#define SHELL_RC  (*(volatile uint8_t *)0x03FF)   /* the SHELL call's result byte (rom/kernal.c) */

static char name[80];
static char buf[256];

static void fs_w32(uint8_t r, uint32_t v)
{
    REG(FS + r) = (uint8_t)v; REG(FS + r + 1) = (uint8_t)(v >> 8);
    REG(FS + r + 2) = (uint8_t)(v >> 16); REG(FS + r + 3) = (uint8_t)(v >> 24);
}
static uint8_t fs_cmd(uint8_t c) { REG(FS) = c; return REG(FS + 1); }
static void say(const char *s) { while (*s) rom_chrout(*s++); }
static void fail(const char *s) { say(s); rom_chrout('\n'); SHELL_RC = 1; }

void main(void)
{
    const char *p;
    uint8_t i = 0, q = 0, rows, lines = 0, last = '\n', k, e;
    uint16_t got, j;

    rom_args();
    p = *(const char **)0xF0;
    while (*p == ' ') p++;
    if (*p == '"') { q = 1; p++; }                   /* TYPE "A FILE.TXT" */
    while (*p && i < sizeof name - 1 && (q ? *p != '"' : *p != ' ')) name[i++] = *p++;
    name[i] = 0;
    if (!i) { fail("type: name?"); return; }

    fs_w32(4, (uint16_t)name);
    if (fs_cmd(1)) { fail("type: not found"); return; }

    rows = REG(0xDA06);                              /* the console's rows (JIM, core/term.c) */
    if (rows < 4) rows = 25;
    for (;;) {
        fs_w32(8, (uint16_t)buf); fs_w32(12, sizeof buf);
        if (fs_cmd(3)) break;
        got = (uint16_t)REG(FS + 12) | ((uint16_t)REG(FS + 13) << 8);
        if (!got) break;
        for (j = 0; j < got; j++) {
            last = (uint8_t)buf[j];
            rom_chrout(last);
            if (last == '\n' && !K_SCRIPT && ++lines >= (uint8_t)(rows - 1)) {
                lines = 0;
                say("-- more --");
                do { k = rom_getin(); } while (!k);
                /* the prompt back off the line: backspace, blank, backspace --
                 * not "\r", which the console makes a whole new line */
                for (e = 0; e < 10; e++) rom_chrout(8);
                for (e = 0; e < 10; e++) rom_chrout(' ');
                for (e = 0; e < 10; e++) rom_chrout(8);
                if (k == 27 || k == 'q' || k == 'Q') { fs_cmd(5); return; }
            }
        }
    }
    fs_cmd(5);
    if (last != '\n') rom_chrout('\n');
}
