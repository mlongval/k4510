# Altered source notice — doomgeneric on the K4510 Tube

This directory vendors **doomgeneric** (https://github.com/ozkl/doomgeneric),
upstream commit `dcb7a8d`, which is itself Chocolate Doom / id Software's
DOOM source with a four-function porting layer.

**Licence.** DOOM's source is GPL-2.0 (Copyright © 1993-1996 id Software,
© 2005-2014 Simon Howard and the Chocolate Doom contributors); doomgeneric
inherits it. This repository is GPL-2.0-or-later, so the two sit together
without difficulty. The game data is **not** here: see "The WAD" below.

**What runs where, stated plainly.** Like every Tube program, this runs on
the *Linux beneath the machine*, not on the 45GS02. It is DOOM displayed on
VICKY, not DOOM running on a K4510. The handbook says so in the same breath
that it says it of NVIM, and so does this file, because the distinction is
the honest part of the trick.

## What was removed

The ports for other hosts, which would never be built here and only invite
the question of whether they work: `doomgeneric_sdl.c`, `_win.c`, `_xlib.c`,
`_allegro.c`, `_emscripten.c`, `_sosox.c`, and their makefiles. `_soso.c`
and `_linuxvt.c` are kept as reference: the K4510 port was written against
their shape.

## What was added

- `doomgeneric_k4510.c` — the port. Frames and input travel through a shared
  memory segment the emulator creates (`K4510_DOOM_SHM`), not the pty.
- `Makefile.k4510` — builds `doomk4510` with the flags below.

## What was changed in the vendored source

**Nothing.** The paletted path is reached by compile flags alone, which is
why there are no `[K4510]` markers in these files:

    -DCMAP256                 pixel_t becomes uint8_t and the framebuffer
                              8 bpp, so I_FinishUpdate memcpy's 320 bytes a
                              row straight out of I_VideoBuffer, and the
                              palette arrives through colors[256] with a
                              palette_changed flag
    -DDOOMGENERIC_RESX=320    the defaults are 640x400; VICKY's bitmap is
    -DDOOMGENERIC_RESY=200    8 bpp and DOOM's own frame is 320x200, so the
                              doubling is done on our side, once, in the
                              emulator

`CMAP256` is present upstream but commented out (`i_video.c`, near the top).
Defining it rather than editing the file is deliberate: a vendored tree that
is byte-identical to upstream can be re-vendored by copying, and the next
person can diff it against `dcb7a8d` and find nothing to explain.

## The WAD

No game data is vendored. `tools/get-freedoom.sh` fetches **Freedoom**
(BSD-3-Clause, https://freedoom.github.io/) into `fs/APPS/DOOM/`, which is
gitignored: `freedoom1.wad` is 28.8 MB against a machine layer of 5.9 MB, so
shipping it inside every image would make a deploy six times larger for a
game nobody has asked to be there by default.

Freedoom rather than the shareware `DOOM1.WAD` because its redistribution
terms are plain, which matters for a machine that is given away as an image.
Any IWAD the engine accepts will do: put it in `fs/APPS/DOOM/` and name it
with `DOOM <file>`.
