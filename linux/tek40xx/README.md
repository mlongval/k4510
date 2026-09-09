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
