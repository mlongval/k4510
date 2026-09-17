/* K4510: STATUS -- the state of the whole machine, at a glance.
 *
 * Doc, 2026-09-17: "I would like a command that gives me a birds eye view of
 * the state of the K4510: memory usage (total, reserved, used, free etc
 * whatever), same for disk, current resolution, current network state,
 * mounted filesystems, any other info you feel important to show."
 *
 * Almost none of that is the 45GS10's to know -- how much RAM the Linux
 * beneath has, which partition the system was copied from, what the Wi-Fi is
 * called -- so the storage device makes the report (FS_SYSINFO, 24; core/io.c
 * says what each line means) and this prints it: headings in bold, a pause at
 * a screenful.  On a desktop the same command describes the desktop.
 */
#include "k4510.h"

#define FS       0xD300u
#define FS_CMD   (FS + 0x00)
#define FS_ST    (FS + 0x01)
#define FS_ADDR  (FS + 0x08)
#define FS_LEN   (FS + 0x0C)
#define FS_CAP   (FS + 0x18)
#define C_SYSINFO 24

void __fastcall__ rom_chrout(unsigned char c);

static char row[100];
static void say(const char *s) { while (*s) rom_chrout(*s++); }

void main(void)
{
    uint8_t i, shown = 0;
    for (i = 0; ; i++) {
        w32(FS_ADDR, (uint32_t)(uint16_t) row); w32(FS_LEN, (uint32_t) i); REG(FS_CAP) = sizeof row;
        REG(FS_CMD) = C_SYSINFO;
        if (REG(FS_ST)) break;
        if (row[0] != ' ') {                      /* a heading: its first word in bold, a blank line before all but the first */
            char *p = row;
            if (i) { rom_chrout('\n'); shown++; }
            say("\033[1m"); while (*p && *p != ' ') rom_chrout(*p++);
            if (p[0] == ' ' && p[1] != ' ') { rom_chrout(*p++); while (*p && *p != ' ') rom_chrout(*p++); }   /* "THE MACHINE", "THE HOST" */
            say("\033[0m"); say(p);
        } else say(row);
        rom_chrout('\n');
        if (++shown >= 26) { say("\033[7m more \033[0m"); while (!(REG(KBDST) & 0x80)) ; (void) REG(KBD); say("\r      \r"); shown = 0; }
    }
    if (!i) say("STATUS: this machine's storage device cannot say (an older emulator)\n");
}
