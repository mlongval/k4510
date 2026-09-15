/* K4510: CODEPAGE [437 | K4510] -- which code page the machine speaks.
 *
 *   CODEPAGE          say which
 *   CODEPAGE 437      IBM's code page 437: the machine's default, and every PC
 *                     program's and BBS's
 *   CODEPAGE K4510    CP437 with 26 of its Greek and maths places given to
 *                     Western Europe's letters: the accented capitals, oe/OE,
 *                     the euro, section and pilcrow, German quotes, o-slash
 *
 * It writes JIM's CODEPAGE register ($DA17): JIM's table and the screen's
 * fonts follow at once, and the frontend remembers it, as F12 -> Terminal ->
 * Code page does.  The box drawing, shades and blocks are the same in both.
 * Appendix D of the handbook has the page (Doc, 2026-09-15: "keep plain as
 * default but keep modified as option").
 */
#include "k4510.h"

#define TERM     0xDA00u
#define SHELL_RC (*(volatile uint8_t *)0x03FF)

void __fastcall__ rom_chrout(unsigned char c);
static unsigned char rom_args(void) { return ((unsigned char (*)(void))0xFF95)(); }
static void say(const char *s) { while (*s) rom_chrout((uint8_t) *s++); }

void main(void)
{
    const char *p;
    char c;
    rom_args();
    p = *(const char **) 0xF0;
    while (*p == ' ') p++;
    c = *p;
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    if (c == '4') REG(TERM + 0x17) = 0;
    else if (c == 'K') REG(TERM + 0x17) = 1;
    else if (c) { say("usage: CODEPAGE [437 | K4510]\n"); SHELL_RC = 1; return; }
    say(REG(TERM + 0x17) ? "code page: K4510 (CP437 with Western Europe's capitals, oe, euro)\n"
                         : "code page: 437 (IBM's)\n");
}
