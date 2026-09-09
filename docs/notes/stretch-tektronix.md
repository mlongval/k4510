# Stretch goal: a Tektronix 4010 for the PiDP-11

Doc, 2026-09-08: "how complicated would it be to write a Tektronix
terminal emulator for the K4510 so that I could access my PiDP-11 (nice
to be able to alt-tab between VT100 and Tektronix)". And then: "perhaps
it would be easier to host the tek4010 on Linux since it exists there
already, perhaps the only change would be the target graphics library
would now be SDL2 since that's all we have". A note, not a plan.

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

## Two ways to build it

**A. Inside the machine.** A TEK mode in TELNET.PRG (or a TEK.PRG that
shares its network code): the same byte stream from the N: device goes
to a 4010 decoder instead of JIM, which draws lines into VICKY's bitmap
layer (1024x780 Tek space scaled onto 640x480, mode 0) and never clears
it until ESC FF. JIM's text layer sits above the bitmap, so "alt-tab" is
one register write on an F-key: hide the bitmap and you have the VT100,
hide the text and you have the Tek. Alpha-mode text needs a small font
drawn onto the bitmap. Everything it needs exists; it is the decoder,
the line routine (PGRAPH's, or EhBASIC's) and a key. A week of evenings.

**B. On the Linux, `tek4010` ported to SDL2.** rricharz's tek4010 is
GTK3 + cairo, talks to a child process (a telnet, here), and draws with
the phosphor glow. Swapping cairo for SDL2's renderer (lines, a texture
for the persistent page, a text atlas for alpha mode) is a contained
port; the decoder, the timing and the glow stay. On a desktop it is a
second window beside the machine and alt-tab is the desktop's. On the
stick it is a second KMSDRM program: two of those cannot share one
display, so it lives on its own tty — the machine on tty1, tek4010 on
tty2 — and **Ctrl+Alt+F2 is the alt-tab.** No emulator code changes at
all.

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

B first, if this is ever built: the decoder already exists and is
already the PiDP-11's; the SDL2 port is the only new code; the tty
switch gives the alt-tab for free on the stick and the desktop gives it
on hdieu; the DZ11 gives the second line. A is the prettier machine
feature — a Tek in the K4510's own video chip on its own DZ line — and
it can come later. Neither is on the list; this note is so the thought
is not lost.
