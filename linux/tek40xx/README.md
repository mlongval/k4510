# Tek40xx on the K4510 Linux

[Tek40xx](https://github.com/Isysxp/Tek40xx) (Ian Schofield, GPL-3) is a
Tektronix 4010/4014 storage-tube terminal on SDL2 that is itself a
telnet client. It is built into the K4510 Linux -- the stick and the
container -- as `/usr/local/bin/tek40xx`, with one patch
(`fit-the-panel.patch`): `TEK40XX_FULLSCREEN=1` takes the whole
display, and a logical size scales its 1536x1170 page to the panel.

The `tek` wrapper (`config/includes.chroot/usr/local/bin/tek`) sets
that up: on a console with no desktop it picks KMSDRM and goes
full-screen; on a desktop it opens a window.

    tek pidp11 2000        # the PiDP-11's DZ11 telnet port, a second terminal line
    tek host               # port 23

HOME erases the page, END hangs up and quits.  On the stick: log in on
tty2 (Ctrl+Alt+F2) and run it there; Ctrl+Alt+F1 is the K4510,
Ctrl+Alt+F2 the Tektronix.  `docs/notes/stretch-tektronix.md` has the
story.

## Trying it without a PiDP-11

    tekplay                     # every shipped plot in turn; HOME clears, END quits
    tekplay k4510 gnu1          # or the shipped plots by name
    tekplay ~/snoopy.plt        # or any file
    tekmenu                     # a numbered chooser over every plot it finds
    TEKPLAY_BAUD=115200 tekplay ~/tekplots/mona.plt   # the big grey-scale pictures, faster than 9600

`tools/tekplay`, so it is on PATH wherever the machine's tools are: at
the K4510 prompt on a desktop, `!tekplay` opens the Tektronix in
a window beside the machine (the prompt waits until END).  On the
stick the emulator owns the screen, so run it from tty2 instead.  A
plain desktop checkout needs the terminal built once, no root needed:
`sh linux/tek40xx/build.sh` puts it in `~/.local/bin` (needs gcc, git,
patch and the SDL2 headers).

Two patches go on at build time: `fit-the-panel.patch` (full screen,
the picture fitted) and `special-point-plot.patch` (the 4014's `ESC FS`
mode with an intensity byte per dot -- the grey-scale cassette pictures
printed as text without it).

`plt/` holds Tek40xx's four gnuplot examples (GPL-3, with the program)
as the scripts they are, `gnu1.plt` to `gnu4.plt`, each with its
rendered Tektronix stream beside it, `gnuN.tek` (made once with
`gnuplot`; a `.plt` is a gnuplot SCRIPT and drawn on the terminal
raw it is only text), and `k4510.tek`, a test page made by `mkplt.py`
(axes, a sine, a Lissajous, text).  `tekplay` tells the two apart by
their bytes, so a script of your own is rendered live where gnuplot is
installed and refused where it is not. The classic cassette-tape plots (snoopy, the US map,
the wizard...) live in rricharz/Tek4010's `pltfiles/`, marked "for your
personal use only, not part of the GNU public license", so they are not
here; for your own use:

    curl -LO https://raw.githubusercontent.com/rricharz/Tek4010/master/pltfiles/05_snoopy.plt
    tekplay 05_snoopy.plt

## 4010 or 4014

Tek40xx emulates both: the 4014's 12-bit addressing (4096x3120, the
extra byte in a coordinate) and its line styles are what gnuplot's
`tek40xx` terminal and the PDP-11 plotting packages send. The 4010's
1024x780 is the page size either way. Nothing more to add.
