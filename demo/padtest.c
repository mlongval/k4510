/* K4510: PADTEST -- shows the $D104 held-keys register live, so a USB
 * gamepad (or the arrow keys, space, Z, X) can be seen arriving.  Each
 * of the seven bits is a lamp; Esc leaves.  This is the first thing to
 * run with a new pad: if nothing lights, the host never saw it. */
#include "k4510.h"
void __fastcall__ rom_chrout(unsigned char c);
static void print(const char *s) { while (*s) rom_chrout(*s++); }
static const char *names[7] = { "UP", "DOWN", "LEFT", "RIGHT", "FIRE", "A", "B" };
void main(void)
{
    uint8_t i, h, last = 0xFF; uint16_t n = 0;
    print("PADTEST: the $D104 held-keys register, live.\n");
    print("Move the pad's d-pad or left stick, press its buttons;\n");
    print("or hold the arrow keys, SPACE, Z, X.  Esc leaves.\n\n");
    for (;;) {
        uint8_t k = key_get();
        if (k == 0x1B) break;
        h = keys_held();
        if (h != last) {
            last = h; n++;
            print("\r  ");
            for (i = 0; i < 7; i++) { print((h >> i) & 1 ? "[" : " "); print(names[i]); print((h >> i) & 1 ? "]" : " "); print("  "); }
            print("   $"); rom_chrout("0123456789ABCDEF"[h >> 4]); rom_chrout("0123456789ABCDEF"[h & 15]); print("   ");
        }
        wait_vblank();
    }
    print("\n");
}
