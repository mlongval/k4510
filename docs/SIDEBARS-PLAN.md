# The sidebars -- a plan for managing them

Status: **a plan**, 2026-09-15, rewritten the same day around Doc's packaging
idea. Doc: "I would like a plan for managing the sidebars, I have a feeling it
will get complicated quickly if there is not a structured way of managing
them."  Then: "anyone who wishes to distribute a sidebar would just have to
pack it into a .zip file and put it in /SYSTEM/SIDEBARS", "zip mounting
first", and F12 limited to "which one" and "Edit options".

## Where it stands

Ten sidebars: border, gradient and knot (drawn in `sdl/main.c`), and
halloween, christmas, space, river, dreamfall, tetris and antfarm (in
`sdl/savers.c`, 1000 lines and growing). Adding one today touches five places
that must agree: `SIDEBAR_*` (`core/ui/settings.h`), `sidebar_names[]`
(`core/ui/settings.c`), `SAVER_*` (`sdl/savers.h`), the `switch` in
`saver_draw()`, and the dispatch in `main.c`. Options are constants someone
edits or an environment variable (`K4510_SAVER_DAY`); every reboot starts a
new ant colony; nothing in `make test` draws a sidebar.

## The shape it takes

    /SYSTEM/SIDEBARS/
        SIDEBARS.CFG            which one is shown, and the choices for all of them
        ANTFARM.ZIP             the sidebar: read-only, never written
        RIVER.ZIP
        ...
        ANTFARM/                made on first use, never shipped
            OPTIONS.CFG         yours: its speed and its own choices
            STATE.DAT           the machine's: the colony, saved as it grows

Three kinds of thing, kept apart because each is handled differently:

| | written by | when | if deleted |
|---|---|---|---|
| `NAME.ZIP` | its author | installing, updating | the sidebar is gone |
| `NAME/OPTIONS.CFG` | you | when you choose | back to its defaults |
| `NAME/STATE.DAT` | the machine | every few minutes, and at shutdown | it starts new |

Installing is copying a zip in. Updating is replacing the zip, and your
options and colony survive it. Deleting the folder resets one sidebar;
deleting the zip too removes it without a trace.

**The zip is never written.** A zip cannot be changed in place, only
rewritten whole, and a sidebar saving its state every few minutes would be
rewriting its archive all day -- one power cut in the middle and the sidebar
is gone. So packages are read-only, and everything that changes lives beside
them as ordinary files.

**The directory is the list.** The Sidebars choice in F12 is a listing of
`/SYSTEM/SIDEBARS/*.ZIP`, read from each one's `SIDEBAR.INF`. There is no
table to keep in step with anything: this is what stops it getting
complicated.

## Step 1: zip files mount, read-only

The machine already mounts: `MOUNT tnfs://... /MNT/FUJI` maps a local-looking
path onto a server through a table in `core/io.c` (`fs_mnt`, `fs_mount_url`),
and writes under a mount are already refused (`fs_name_mounted`). A zip
becomes the second kind of mount:

    MOUNT GAMES.ZIP /MNT/GAMES
    CD /MNT/GAMES
    DIR
    UMOUNT /MNT/GAMES

- **Reading only.** `DIR`, `CD`, `TYPE`, `LOAD`, running a program from it,
  `COPY` out of it. Writing, deleting and renaming are refused, as for the
  network mounts.
- **Stored and deflated entries**, which is what every zip tool makes. No
  zlib to link -- the machine already writes its PNGs without one
  (`sdl/main.c`) -- so a small inflater of our own, `core/zip.c`: read the
  central directory once at `MOUNT`, inflate an entry into memory when it is
  opened. The disk is RAM, so this costs nothing anyone will see.
- **Names** as the rest of the file system: case-insensitive, folders inside
  the zip are folders under the mount.
- **Refuses the unsafe**: an entry named `../` or `/`, an encrypted entry, a
  zip64 or a method it does not know -- an error, not a guess.

Tests: `test/ziptest` (a zip made by `zip`, one by Python, one made wrong on
purpose), and a remote script that mounts a zip, lists it, runs a program
from it and fails to write into it.

It is useful well past sidebars: a game with its data as one file, a
documentation bundle, someone's whole disk of work looked at without
unpacking, a test's fixed set of files that nothing can change by accident.

## Step 2: what a sidebar zip holds

    ANTFARM.ZIP
        SIDEBAR.INF     name, one line about it, author, version, season, and how it is drawn
        PREVIEW.PIC     its picture, for the menu and the handbook
        OPTIONS.CFG     the defaults, commented: copied into ANTFARM/ on first use
        ...             anything else it needs: pictures, sounds

`SIDEBAR.INF`:

    name     = Ant farm
    about    = A colony digs through a day and a night.
    author   = K4510
    version  = 1
    season   =                     # empty: any time; or 10 for October, 12 for December
    draw     = builtin antfarm

`draw` says what makes the picture, and there are three kinds, of which only
the first is needed now:

1. **`builtin NAME`** -- C inside the emulator, as today. Safe and fast, but
   only we can write them. Every sidebar that exists now becomes one of these:
   its zip holds only the INF, the preview and the default options.
2. **`scene`** -- pictures and a small description in the zip: layers that
   scroll, drift, twinkle, cycle colours. Anyone can make one with PAINT and
   a text editor; it can do nothing but draw. It would do river, space,
   halloween and christmas; it will never do an ant farm.
3. **`program NAME.PRG`** -- 45GS02 code on a second processor (the Tube
   idea): a real K4510 program, written on the machine, drawing into the
   sidebar's own screen, able to reach only its own memory. For when the Tube
   project happens.

A zip from a stranger can never carry host code. That is why there is no
fourth kind.

The built-in zips are made by `make` from `sdl/sidebars/NAME/`, like the
programs in `fs/SYSTEM/BIN`, so the repository holds their sources and not
their zips.

## Step 3: the emulator's side

- **One file per sidebar**, `sdl/sidebars/NAME.c`, and the drawing helpers
  (`rect`, `disc`, `glow`, `vgrad`, `mix`, `hh`, `isin` ...) shared in
  `sdl/sidebars/canvas.h`. The gradient and knot move out of `main.c` into the
  same shape.
- **One interface**: the draw function is given its canvas, a clock that only
  goes forward, which side, its state, and its options -- no globals.
- **The active one is mounted** at `/MNT/SIDEBAR` (so `MOUNT` lists it and you
  can look inside); the others stay closed zips.
- **The built-in table** in the emulator only maps `builtin antfarm` to its
  function. What exists, what it is called, and whether it is shown come from
  the zips.
- **Rules every sidebar keeps**: one frame in under 2 ms at the largest size
  the Dell draws, its simulation on its own clock (the ant farm's 90 ms);
  the same picture for the same state and clock; any width, down to a few
  pixels; it reads nothing of the guest's. Debug traces under one switch,
  `K4510_SIDEBAR_DEBUG=antfarm` (the ant farm's `AF_DEBUG` becomes this).

This step changes **nothing on the screen**, and is checked that way: the
test below draws every sidebar before and after, pixel for pixel.

## Step 4: the test

`test/sidebartest`, in `make test`: every sidebar in `/SYSTEM/SIDEBARS`, both
sides, at three sizes, over minutes of simulated clock. It fails on a crash,
a frame over budget, a blank or frozen picture, a different picture for the
same state, a zip whose INF is wrong, or a sidebar whose `OPTIONS.CFG`
defaults it cannot read. It writes the previews (`PREVIEW.PIC`) and one
contact sheet of them all, for eyes and for the handbook.

## Step 5: options and state

**`OPTIONS.CFG`**, one `name = value` a line, `#` for comments, the shipped
copy explaining each:

    # Ant farm
    speed = 1          # 0.25 to 4: how fast everything happens
    day   = real       # real: the host's clock.  Or a length: 1h, 10m
    ants  = 24         # how many start

- **Speed is here**, not in F12: every sidebar has it, from the framework.
- **Read again when it changes**: save it in VI and the sidebar changes on
  the next second; no restart.
- **A line it does not understand** is ignored and noted in
  `/SYSTEM/LOG`, never fatal; a missing file is the defaults.
- `K4510_SAVER_DAY` becomes a test override of `day`.

**`SIDEBARS.CFG`**, the same format, for what is not one sidebar's:

    show    = antfarm
    right   = same         # or another sidebar's name
    change  = never        # or 10m, 1h, 1d: the next one, in turn
    seasons = on           # a sidebar with a season only in its months

**`STATE.DAT`**: only for a sidebar that grows. It begins with the sidebar's
name and version; one that does not match is set aside (`STATE.OLD`) and the
sidebar starts new, so an update can never be broken by old state. Written to
a new file and renamed over the old, so a power cut leaves the last good one.
The ant farm saves its tunnels, ants, food and queen every five minutes and at
shutdown, and a reboot finds the colony where it was.

## Step 6: F12

Two rows, no more:

    Sidebar          Ant farm         (← → through the zips, by their names)
    Edit options...                   (its OPTIONS.CFG in VI)

`Edit options...` opens VI when the machine is at the shell prompt. When a
program is running it shows the path to type instead, rather than stopping
the program. Everything else -- speed, left and right, changing, seasons --
is in the two files.

The old `video.sidebars` setting is read once, becomes `show =` in
`SIDEBARS.CFG`, and is dropped, as `term.bands` was.

## Step 7 and after

7. **Scene sidebars** (`draw = scene`): river, space, halloween and christmas
   move to it as the proof, and PAINT pictures become sidebars.
8. **The handbook**: a generated list, each sidebar's name, line, options and
   preview, from the zips.
9. **Program sidebars** (`draw = program`), with the Tube.

## The order of work

| # | what | you would see |
|---|---|---|
| 1 | zip mounting, read-only, with its tests | `MOUNT GAMES.ZIP /MNT/GAMES` |
| 2 | the sidebar zip format; built-ins packed by `make` | `/SYSTEM/SIDEBARS/*.ZIP` |
| 3 | the emulator reads the list from the zips; one file per sidebar | nothing -- the same pictures |
| 4 | `sidebartest` in `make test` | |
| 5 | `OPTIONS.CFG`, `SIDEBARS.CFG`, the ant farm's `STATE.DAT` | speed, day length; the colony survives a reboot |
| 6 | F12: Sidebar and Edit options | |
| 7 | scene sidebars | your own, from PAINT |

Each is its own commit and leaves the machine working; each deploy runs the
Dell's remote tests.

## Still open

- **The register panel** (Video → Side panel) uses the same space. Leave it
  in Video, or make it one more choice in the Sidebar row?
- **Left and right**: the plan puts it in `SIDEBARS.CFG` (`right = same`), not
  F12. Enough?
