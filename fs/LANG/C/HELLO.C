/* K4510: the first C program.  CC HELLO makes hello.prg from this file,
 * and HELLO runs it.  rom_chrout is the ROM's console call; k4510.h has
 * the registers and the other calls. */
#include "k4510.h"

void __fastcall__ rom_chrout(unsigned char c);

static void print(const char *s) { while (*s) rom_chrout(*s++); }

void main(void)
{
    print("Hello from C on the K4510.\n");
}
