# Stretch goal: a Tektronix 4010 for the PiDP-11

Doc, 2026-09-08: "how complicated would it be to write a Tektronix
terminal emulator for the K4510 so that I could access my PiDP-11 (nice
to be able to alt-tab between VT100 and Tektronix)". And then: "perhaps
it would be easier to host the tek4010 on Linux since it exists there
already, perhaps the only change would be the target graphics library
would now be SDL2 since that's all we have". He was right, and there
is less to do than that. A note, not a plan.

## What a 4010 is, in bytes

Small. GS (`$1D`) enters graph mode; then points arrive as four bytes
(high Y, low Y, high X, low X, tag bits `01`/`11`/`10` say which is
which, and unchanged bytes may be omitted); the first point after GS is
a move, the rest are draws; ESC FF clears the page; US (`$1F`) returns
to alpha mode, where bytes are text drawn on the same phosphor. The
4014 adds line styles, point-plot and incremental modes; nobody's RSX
plotting package needs them. A storage tube never erases: lines stay
until the page is cleared. The whole decoder is a few hundred lines of
C; xterm's `Tekproc.c` and rricharz's `tek4010` (written for exactly
this, the PiDP-11 on a Pi) are the references.

## Checked 2026-09-08: the SDL2 port already exists

Doc: "have you checked that no preexisting tek4010 SDL port already
exists?" I had not; now I have:

- **Tek40xx** (Ian Schofield, github.com/Isysxp/Tek40xx, GPL-3, C):
  "The emulator is based upon SDL2 for graphics services" and "Tek40xx
  is a simple telnet client: `tek40xx <hostname or IP> [<port>
  default:23]`". Linux build is `apt-get install libsdl2-dev` and the
  included makefile; ~10 source files (tek_main, tek_telnet,
  tek_display, tek_drawline, four bitmap fonts). HOME erases the screen,
  END hangs up. Last commit December 2025. Known gaps by its own
  README: no graphic input (crosshair moves, nothing is sent), one
  font size, no variable luminance. The window is fixed at 1.5x the
  4010's 1024x780 (1536x1170) for antialiasing.
- **Tek4010** (Rene Richarz, github.com/rricharz/Tek4010, GPL-3): the
  PiDP-11 one, the better emulation (real drawing speed, the fading
  spot, 4014/4015, APL), but GTK3 (`pkg-config gtk+-3.0`), not SDL2.

So the stretch goal is smaller than the note first said: not a port
but a **packaging**. Build Tek40xx into the K4510 Linux (build-live.sh
and the Containerfile already carry libsdl2-dev and a compiler), start
it on tty2 with `SDL_VIDEODRIVER=kmsdrm`, point it at the PiDP-11's DZ
port. The one likely patch: its fixed 1536x1170 window wants
`SDL_RenderSetLogicalSize` so it fits a 1366x768 or 1920x1080 panel
full-screen under KMSDRM. If the 4010's look matters more than the
size of the job, port Richarz's instead — the drawing-speed and fade
model is what makes it feel like a storage tube.

## The in-machine alternative, kept for the record

A TEK mode in TELNET.PRG: the same byte stream from the N: device goes
to a 4010 decoder instead of JIM, which draws lines into VICKY's bitmap
layer (1024x780 Tek space onto 640x480) and never clears until ESC FF.
JIM's text layer sits above it, so switching is one register write on an
F-key. The decoder is a few hundred lines; xterm's `Tekproc.c` and the
two projects above are the references. The prettier feature, and the
larger job.

## There is no serial line (Doc, corrected 2026-09-08)

The PiDP-11 is simh on a Pi: the console and the DZ11 terminal lines are
telnet ports, and every connection to the DZ is its own terminal line
(TT1:, TT2:, ...), the way a real DZ11 had eight of them. So the machine's
TELNET dials one port and is the VT100 session, tek4010 dials the same
DZ and is a second terminal — log in there and run the plotting program
on that line, edit on the other. Two terminals on one PDP-11 was the
normal state of a DZ11; nothing has to be split or proxied. This is what
makes B the plain answer.

## Which

Tek40xx, packaged into the Linux, on tty2: no new code beyond a
logical-size patch; the tty switch is the alt-tab on the stick and the
desktop's is on hdieu; the DZ11 gives the second line. The in-machine
Tek can come later if wanted. Neither is on the list; this note is so
the thought is not lost.
