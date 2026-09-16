/* K4510: HOST -- a login on the Linux beneath, in one word.
 *
 *   HOST        the Linux's own telnet, on the loopback (127.0.0.1:23)
 *
 * The same door as F12 -> Host -> "Telnet into the host", without the menu
 * (Doc, 2026-09-16: "a command to allow quick connection (HOST) instead of
 * going through the menu").  It is a full login session with its own tty --
 * what a second shell, or a curses program, wants -- where `!` gives a shell
 * on the Tube and SSH goes to another computer.  Since 2026-09-16 that login
 * asks for no password: the socket is loopback-only and `!` is already an
 * unauthenticated shell on the same Linux (linux/build-live.sh).
 *
 * `linux = locked` in the menu file turns all three away, this one included:
 * TELNET says so, because the ROM refuses the Tube then. */
#include "k4510.h"

void main(void)
{
    rom_shell("TELNET 127.0.0.1 23");
}
