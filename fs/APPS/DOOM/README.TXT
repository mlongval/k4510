DOOM, on the Tube co-processor
==============================

This folder is nearly empty on purpose: the game data is not shipped with
the machine.

id Software's DOOM engine is free software (GPL-2.0) and travels in this
repository as the Tube co-processor's source, but a game needs an IWAD --
the levels, the sprites, the sounds -- and the smallest respectable one is
28.8 MB.  The whole machine layer that an update carries is under 6 MB, so
shipping a WAD inside every image would make every update six times larger
for a game that is not why anyone turns this computer on.

So it is one command away instead, on the host:

    tools/get-freedoom.sh

That fetches Freedoom (https://freedoom.github.io/), a BSD-licensed set of
levels built to run on the DOOM engine without any of id's data, and puts
freedoom1.wad in /DISK/DOOM -- the part of the machine that stays on the
disk (see /DISK/README.TXT).  COPYING.TXT and CREDITS.TXT are its
licence and its long list of authors; they are kept here because the licence
asks that they travel with the data, and because the people who drew a
replacement for a 1993 shareware game over twenty years deserve the credit.

Or, on the machine itself, with no host to hand:

    /APPS/DOOM/WADCHOOSER

which lists the games that may be downloaded (WADS.CFG: Freedoom's two
phases, and id's own shareware episode), fetches the one you pick, and
remembers the choice in /DISK/DOOM/DOOM.CFG.  It lists only what its owners let anybody
pass on; the full DOOM, DOOM II and Final DOOM are still sold.

Any IWAD the engine accepts will do.  If you own DOOM, put its WAD in
/DISK/DOOM: WADCHOOSER lists whatever .WAD it finds there, downloaded or not.

Then, at the machine's prompt:

    DOOM

Arrows turn, W and S walk, A and D strafe, Ctrl fires, Space opens doors,
Shift runs, Tab is the map, [ and ] change weapons, Escape is the menu.

What this is, honestly
----------------------

Like every program on the Tube -- BBC BASIC, CP/M, NVIM -- DOOM runs on the
Linux beneath the machine, not on the 45GS02.  It is DOOM displayed on
VICKY: the co-processor draws a 320x200 paletted frame, the machine's video
chip shows it doubled on its own bitmap, and the keys you press travel back
the other way.  A 45GS10 at 60 MHz could not run DOOM, and this is not a
claim that it can.
