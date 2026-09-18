An Apple IIe, on the Tube co-processor
======================================

    APPLE                 boot DOS 3.3's master disk
    APPLE GAME.DSK        boot a disk image: a name here, or a path
    APPLE /DISK/APPLE/GAME.WOZ

An Enhanced Apple IIe -- 128 KB, 80 columns, a Disk II in slot 6, a
Mockingboard in slot 4, a joystick -- drawn on VICKY's bitmap and heard
through the DigiMAX, the way DOOM is.  The emulation is LinApple's, which
is AppleWin's core on Linux (GPL-2; tube/apple/ALTERED-K4510.md says what
was changed, which is little).  The ROMs are the Enhanced IIe's.

Disk and hard-disk images -- .dsk .do .po .nib .woz .2mg, and .hdv or any
image bigger than a floppy (a ProDOS hard disk, like Total Replay) -- go in
/DISK/APPLE.  A hard-disk image is put in slot 7 and booted from there, on the
disk side of the machine (never in the image; see /DISK/README.TXT).  A
name without a path is looked for where the shell is.

At the Apple's keyboard:
    the keys are the keys; Delete is its DELETE, the arrows its arrows
    Left Alt / Right Alt      the Open-Apple and Closed-Apple keys, which
                              are also the joystick's two buttons
    keypad 8 2 4 6, 5         the joystick; 5 centres it
    Ctrl+Alt+R                Ctrl-Reset
    Ctrl-C (from the shell)   is the Apple's; to leave, the F12 menu's
                              Tube > stop, or *QUIT from the prompt

Nothing of the Apple's is on the machine's side of the Tube: it is a
program on the Linux beneath, borrowing the glass and the speaker.

The colours are NTSC "TV emulation" (the softened artifact colour a real
Apple II showed on a colour TV), not the sharp per-column artifact striping.
K4510_APPLE_VIDEO in the emulator's environment picks another rendering:
1 standard, 2 text-optimised, 3 TV (the default), 5-7 amber/green/white.
