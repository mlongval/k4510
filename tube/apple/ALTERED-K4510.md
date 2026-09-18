# tube/apple -- LinApple, on the Tube

Doc, 2026-09-17: an Apple IIe as a Tube co-processor, "games should work", and
on the question of writing one: "tell me why we just dont port an existing
apple iie emulator". No reason. This is a port.

## Provenance

`linapple/` is **LinApple** (https://github.com/linappleii/linapple), commit
`bbe6aa64a8e758850e082d250d7d437b6bc87d1b` (2026-09-17), GPL-2.0-only
(`linapple/COPYING.txt`). It is AppleWin's emulation core brought to Linux and
since rebuilt around a frontend-neutral core (`src/core/LinAppleCore.h`) with
a headless frontend in the tree -- which is why it was chosen over
`mauiaaron/apple2` (2020, GL-bound) and Clock Signal (a much larger framework):
the Tube needs a machine that runs a frame at a time and hands over pixels,
samples and takes keys, and that is exactly the interface this core has.

Vendored: `src/`, `res/roms/`, `res/Master.dsk`, `res/font.xpm`, `COPYING.txt`,
`README.md`, `cmake/ConvertXPM.cmake`. Not vendored: the SDL frontends' assets
(fonts, icons), tests, docs, the `.github` machinery.

`generated/` holds what CMake would generate at build time, checked in so the
K4510 Linux -- g++ and make, no CMake, no ImageMagick -- can build it:
`gen/*.xpm` (the charsets, from `res/*.png` by ImageMagick) and
`roms/EmbeddedRoms.{h,cpp}` (from `res/roms/`). To regenerate: a plain upstream
checkout, `cmake -B build -DFRONTEND=headless -DBUILD_TESTING=OFF`, and copy
`build/gen` and `build/generated/roms`.

## Edits to the vendored tree, each marked `[K4510]`

| File | Change |
|---|---|
| `src/apple2/peripherals/disk/formats/DiskContainer.cpp` | `LINAPPLE_NO_COMPRESSED_IMAGES`: the `.gz`/`.zip` unpackers (zlib, libzip -- neither on the K4510 Linux) compiled out; a compressed image is refused with a message. |
| `src/core/LinAppleCore.cpp` | `LINAPPLE_NO_CURL`: the `<curl/curl.h>` include skipped; a headless build never uses `g_curl`, and the header already declares `CURL` as `void`. |

Nothing else. `src/frontends/headless/Main.cpp` is not built; `apple_k4510.cpp`
stands where it would.

## The ROMs

`res/roms/Apple2e_Enhanced.rom` and `Apple2e_Enhanced_Video.rom` are embedded
by LinApple, as AppleWin has always embedded them. Doc read the same two chips
out of his own Enhanced IIe (342-0349-B and 342-0265-A) on 2026-09-17, and the
SHA-256s are identical: `aab38a03ca8deabba...` and `52c3b87900ac939ff...`. His
copies are in `/DISK/APPLE` on the Dell, outside the repository.

## What apple_k4510.cpp does

The machine at 60 frames a second by the wall clock (17030 cycles each); the
560x384 RGBA frame turned into palette indices -- each new colour gets the
next VICKY entry, so the colours are exact -- and put in the shared segment
DOOM uses; the speaker and Mockingboard mixed to one 8-bit stream at 44100 Hz
for the DigiMAX; key events, the Apple keys and a joystick from the emulator.

Two things that cost time, for whoever comes next: `peripheral_command` only
QUEUES a command, worked in `peripheral_manager_think`, so a disk inserted
after a reset needs `think(0)` before the CPU's first instruction or the boot
ROM finds an empty drive; and LinApple's keyboard wants every press released
before the next is taken, so the emulator taps (down, then up) for every key
from the machine's queue.
