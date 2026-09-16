# A platformer for the K4510 -- a plan

Status: **a plan**, 2026-09-15. Doc asked for open-source options after the
Super Mario Bros C64 port turned out to be a copyright fight rather than a
licence question, and then: "write the plan but also assess porting supertux".

Two things here: what an original platformer on this machine would look like,
and an honest assessment of SuperTux at the end.

## What the machine already gives us

This is the part that decides the design, and it is better than I assumed
before reading `core/vicky.h`:

- **A hardware tile layer.** A VICKY layer in mode 1 has its own tileset
  (`VL_DATA`), its own map (`VL_MAP`), a stride, and **its own scroll
  registers**. Cells are 8, 16, 32 or 64 pixels square at 1, 2, 4 or 8 bits
  a pixel. So the world scrolls by writing two registers a frame -- no
  redrawing, no blitter work, no cost that grows with the screen.
- **Four layers.** Far parallax, near parallax, the play field, and a text32
  layer for the score and lives. Each has its own scroll, so parallax is two
  more register writes.
- **128 sprites**, 8/16/32/64 square, 4 or 8 bpp, horizontal and vertical
  flip, and a Z per sprite that puts it in front of or behind any layer. The
  hero, the enemies, the pickups and the shots all fit with room to spare.
- **SHEILA**, the display-list coprocessor: at any scanline it can write any
  VICKY register. A status bar that does not scroll, a horizon whose layer
  scrolls at half speed, a colour that changes down the screen -- all of it
  is a list in RAM, not CPU time.
- **The blitter** for what is not tiles: fills, keyed copies, lines and
  filled triangles.
- **MELODY** (the OPL2) with the four-channel sequencer behind it, which is
  what BBC BASIC's `SOUND` already drives, so music and effects have a road.
- **256 MB and DMA.** Every level, every tileset and every tune can sit in
  far memory at once; the CPU view is 64 KB at a time and the banks do the
  rest.
- **The toolchain we already use**: cc65 for the body of it, assembly where a
  frame is tight, built exactly like the machine's other programs.

At 60 MHz a frame is about a million cycles. With scrolling and drawing free,
that budget goes on physics, enemies and the sprite table -- which is the
right place for it.

## The game

An original one: our own character, our own levels, our own name. A
side-scrolling platformer of the classic shape -- run, jump, collect, avoid,
reach the goal -- because that is the shape this hardware was born for, and
because it gives every part of the machine something to do.

The character should be the machine's own; Doc names it.

## The formats

Ours, simple, and readable at the source end -- the house pattern, as with
the sidebar zips and the code page.

    /APPS/<GAME>/TILES.SET     the tileset: N cells of 16x16, 4 bpp, and one
                               attribute byte each (solid, platform, slope,
                               hazard, water, goal, ...)
    /APPS/<GAME>/L01.K4L       a level: a small header (width, height, the
                               tileset, the tune, where you start, the time),
                               the map as one byte a cell, then the objects
                               (type, x, y, a parameter or two)
    /APPS/<GAME>/MUSIC/*.OPL   the tunes, as OPLPLAY already knows them

A level is written as **text** -- rows of characters, one per tile, with a
key at the top -- and `tools/mklevel.py` turns it into `L01.K4L`. So a level
can be written in VI on the machine or in NVIM on the Linux, read in a diff,
and kept in git as text. The binary is a build product, like the .prg files.

16x16 cells give 40 by 30 of them on a 640x480 screen, which is the right
density for this kind of game; the tile layer will do 32 if we ever want it.

## The engine

- **Fixed point, 8.8.** Position, velocity, gravity. No floats: the MATH unit
  is for LOGO and the BASICs, and 8.8 is what a platformer wants anyway.
- **Collision against the map**, by the tile's attribute byte: solid, one-way
  platform, slope, hazard, water. Four probes for the body, two for the feet.
- **A camera with a dead zone**, written to the layer's scroll registers; the
  parallax layers take a fraction of the same number.
- **Enemies as small state machines** in a fixed table -- walk, turn at an
  edge, fall, die -- with their sprites written once a frame.
- **Sound**: the effects on the OPL2 directly (jump, coin, hurt), the music
  through the sequencer so it keeps playing while the game runs.
- **The loop**: read the pad and keys, step the hero, step the enemies, test
  collisions, move the camera, write the sprite table, wait for the frame.

## The art

CC0 -- Kenney's packs and OpenGameArt's CC0 collections are public-domain
dedications: no attribution required, no share-alike, nothing that reaches
into this tree (which is GPL-2.0-or-later and keeps a careful record of
everything borrowed in `LICENSES.md` and `THIRD_PARTY_SOURCES.md`). We would
credit the artists in `CREDITS.md` regardless, because that is the habit
here.

`tools/mktiles.py` takes the source art and emits `TILES.SET` in the
machine's palette at the machine's cell size -- generated from a recorded
source, like the fonts, the code page and the sidebar zips. PAINT and FONTED
can draw tiles on the machine too.

## The order of work

Each step is a commit that leaves the machine working, and each brings its
own test.

| # | What | You would see |
|---|---|---|
| 1 | the formats, `mklevel.py`, `mktiles.py`, and a round-trip test | a level and a tileset built from text |
| 2 | the tile layer up, with parallax and the status split (SHEILA) | a world that scrolls when you hold a key |
| 3 | the hero: physics and tile collision, nothing else | you can run and jump around a level |
| 4 | enemies, pickups, hazards, the goal | a level you can finish or lose |
| 5 | sound and music | it sounds like a game |
| 6 | a title screen, lives, the level order | a game you can sit down to |
| 7 | the handbook chapter, and the game in `/APPS` | it ships with the machine |

**Testing.** The physics and the collision go in a header the way VI's
renumbering does (`demo/renum.h`, tested by `test/renumtest` compiled
natively), so the rules can be tested on the host in milliseconds: a jump
clears exactly this gap, a slope does not eat the hero, a one-way platform
is solid from above only. The whole game gets a headless run -- keys in,
screen out -- like `test/logotest.sh`, and a contact sheet of frames for the
eye, as the sidebars have.

## Could we port SuperTux?

Doc asked for an assessment. Short answer: **its code, no; its art and its
levels, yes -- and that is a different project from "porting it".**

**What SuperTux is.** C++ on SDL3 with an OpenGL renderer, a Squirrel
scripting VM, some fifteen thousand commits, built for desktops, phones and
WebAssembly. Its tiles are 32x32 PNGs described in an S-expression file
(`data/images/tiles.strf`); its levels are `.stl` files, also S-expressions,
holding integer lists of tile numbers plus the objects.

**What the K4510 is.** A 45GS02 at 40 to 60 MHz, C89-ish through cc65, no
C++, no OpenGL, no scripting VM, and a 64 KB CPU window onto 256 MB. The
distance is not "hard work": the language, the renderer, the VM and the
memory model are each out of reach on their own.

**So a port means a rewrite.** The engine above, plus two converters:

- `tools/stl2k4l.py` -- read a `.stl`, map its tile numbers onto ours, emit
  `L01.K4L`. The level data is integer lists, so this is honest work rather
  than clever work.
- `tools/strf2set.py` -- read `tiles.strf` and the PNGs, downsample 32x32 to
  our 16x16, quantise to the machine's sixteen colours, carry the solid and
  slope attributes across.

**What it would cost legally.** SuperTux's data is mostly **CC-BY-SA**: we
would owe attribution for every asset used (their `data/credits.stxt` is the
record to carry over) and our derived tilesets and sprite sheets would have
to carry the same share-alike terms -- a standing obligation, recorded in
`LICENSES.md` beside the others. That is perfectly workable; it is simply
heavier than CC0, which asks nothing.

**What it would buy.** Content: a mascot people know, hundreds of finished
levels, and art that was drawn by people who can draw. What it would not buy
is any of the engine, which is the part that takes the time.

**My recommendation.** Build the engine with CC0 art and our own character
(steps 1-7). If it plays well and you want more levels, add the two
converters afterwards and run Tux's world on it -- the same engine, a second
set of data, and an attribution file. The decision costs nothing if it is
taken in that order, and everything if we start by trying to port C++ to a
6502.
