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

**The video path: nothing.** It is reached by compile flags alone, which is
why there are no `[K4510]` markers in those files:

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

**The music needed five edits**, each marked `[K4510]` where it happens:

| file | what, and why |
|---|---|
| `opl/opl.c` | SDL's mutex and condition variable in `OPL_Delay` become pthreads. The co-processor is a plain program with no SDL of its own, and that block was the only place in the file that used it. |
| `opl/opl.c` | the driver table lists `opl_k4510_driver` in place of the SDL one, which is not vendored. |
| `opl/opl_internal.h` | declares `opl_k4510_driver`. `opl_sdl_driver`'s now-dangling declaration is left with a note, to keep the file diffable. |
| `midifile.c` | `SDL_SwapBE16/32` spelt out with `__builtin_bswap`. Six calls were the whole dependency. |
| `i_sound.c` | `InitMusicModule` chooses `music_opl_module`. doomgeneric replaced Chocolate Doom's module search with a hard-wired assignment, so the OPL module -- which `i_sound.h` still declares -- could never be selected however `snd_musicdevice` was set. |

Added rather than edited: **`opl_k4510.c`**, the driver, and
**`k4510_compat.h`**, which supplies what the newer music files use and this
older fork lacks (`PACKED_STRUCT`, `I_Realloc`, `M_fopen`, `M_remove`,
`opl_driver_ver_t`).
The header is force-included into those two files from the makefile, so they
stay byte-identical to chocolate-doom `895f581`.

The driver synthesises nothing: Chocolate Doom's music code decides which OPL
registers to write and when, and this hands them to the emulator through the
same shared segment the frames use, to be performed on MELODY by
`opl2_write_reg()` -- the door the machine's own sound sequencer already uses.

One honesty note. `OPL_Detect()` writes the AdLib timer registers and expects
the status byte to answer as a real chip's would, and `I_OPL_InitMusic`
refuses to start the music otherwise. Register writes cross an asynchronous
ring here, so a read can never be the answer to a write: `read_port_func`
answers from a small state machine of its own. It is not lying about whether
an OPL2 is fitted -- one is, and the writes reach it. Only the handshake is
theatre, and the source says so where it happens.

**No sound effects.** DOOM's are 11 kHz PCM and the machine has no DAC:
`$D480` answers three registers, and DigiMAX is a comment in the design map
with nothing behind it. Music only, by Doc's choice, 2026-09-17.

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

Two threads write OPL registers -- the driver's timer thread from its
callbacks, and DOOM's own from `I_OPL_SetMusicVolume` and `I_OPL_PauseSong`,
which upstream calls without `OPL_Lock`. Upstream's SDL driver has the same
unguarded latch; here it would also have meant two producers on a
single-producer ring. So the driver keeps its register latch per thread and
`k4510_opl_write()` takes a mutex, and the vendored music code is untouched.
ThreadSanitizer is clean over the title music (2026-09-17). Proved end to end
the same day: the emulator's audio output is silent without DOOM and carries
the music with it, about 6 dB under OPLPLAY at DOOM's default volume of 8/15.

## Sound effects (2026-09-17)

Added, not edited: **`snd_k4510.c`**, a `sound_module_t` that mixes DOOM's
eight channels of DMX lumps to one unsigned 8-bit stream at 11025 Hz and
pushes it into a second ring in the shared segment; the emulator clocks it
into DAC 0 of the machine's DigiMAX (`core/digimax.c`). `i_sdlsound.c` is in
the tree from doomgeneric and is not built -- it wants SDL_mixer.

One `[K4510]` edit: `i_sound.c` lists `sound_k4510_module` in
`sound_modules[]`, which doomgeneric left empty without `FEATURE_SOUND`.

Mono, so stereo separation is ignored. Sound lumps are cached `PU_STATIC` and
never released, because the mixer thread reads them and the zone must not
move them; the whole set is about 1.3 MB of a 6 MB zone.

