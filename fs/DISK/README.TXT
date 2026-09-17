/DISK -- the part of the machine that stays on the disk
=======================================================

The K4510 loads itself into RAM when it starts and runs from there; that is
why nothing on it ever waits for a drive.  Everything that SHIPS with the
machine -- /SYSTEM, /APPS, /LANG, this note -- is part of that, so everything
that ships costs memory at every start, used or not.

/DISK is the other kind of place.  Nothing in it ships, nothing in it is
loaded when the machine starts, and what is here is read from the disk when
somebody asks for it (and is fast the second time: the Linux beneath keeps
what it has read).

What belongs here is anything large that the machine can start without:

    /DISK/DOOM     DOOM's game data (WADCHOOSER fetches into it)
    downloads, archives, big projects, anything measured in megabytes

On a K4510 Linux with a persistence partition, all of /HOME and whatever else
you save is on the disk too and survives a restart; /DISK is simply the
place with the promise written on it.  On a machine started from a USB stick
with no persistence, /DISK is as temporary as everything else.

Doc, 2026-09-17: "loading a bunch of doom wads into ram for a slim to none
chance of being played is nonsensical".
