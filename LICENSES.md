# Licences

The K4510 as a whole is distributed under the **GNU General Public
License, version 2 or (at your option) any later version** — see
`LICENSE` (the full GPL-2.0 text). It comes with **no warranty
whatsoever**: see sections 11 and 12 of that file, and the disclaimer
in the guide. Everything written for this project (the machine, VICKY,
SHEILA, the MATH unit, the ROM, the demos, the Pi port) is
Copyright (C) 2026 Michael Longval and is released under those terms.

Components with their own origins (`THIRD_PARTY_SOURCES.md` records
where each one came from and how to verify it):

| Path | What | Licence |
|---|---|---|
| `core/xemu/cpu65.c`, `cpu65.h`, `cpu65_mega65_timings.h`, `cpu65ce02_disasm_tables.c` | 65xx / 45GS02 CPU core from Xemu, Gábor Lénárt | GPL-2.0-or-later |
| `core/opl2/` (`fmopl.c`, `fmopl.h`) | fmopl 0.72 — the OPL/OPL2 FM sound generator, Jarek Burczynski and Tatsuyuki Satoh (MAME), adapted for VICE by Marco van den Heuvel. The other headers there are K4510 shims. | GPL-2.0-or-later |
| `tube/` | BBCSDL console edition ("BBCTTY"), Richard T. Russell (vendored, altered -- see `tube/ALTERED.md`; the name "BBC BASIC" is Richard Russell's interpreter's, and this project holds no licence to it) | zlib |
| `data/font8.bin`, built by `data/mkfont.py` | the Linux kernel's 8x8 console font (`lib/fonts/font_8x8.c`) | GPL-2.0 |
| `data/fonts/openroms/` | clean-room C64/C65 chargen + PXLfont 2.3 (via open-roms' recorded permission), MEGA65 open-roms project -- `8x8font.png` is the editable source, shipped for LGPL compliance | LGPL-3.0-or-later |
| `data/fonts/unscii/` | unscii-8, Viznut; `font8-unscii.bin` is a generated drop-in alternative to `data/font8.bin` (not yet wired) | public domain |
| `data/fonts/bescii/` | BESCII v3 (Mono + source), Damian Vila | CC0-1.0 |
| `data/tinydungeon/` | Tiny Dungeon tile sheet + Tiled sample map, Kenney (`demo/tiny.c` renders them; see `data/tinydungeon/VENDORED-FROM.txt`) | CC0-1.0 |
| `data/bombparty/` | Bomb Party sprite sheet, devurandom/richtaur/cemkalyoncu (`demo/bomber.c` renders it; see `data/bombparty/VENDORED-FROM.txt`) | CC-BY-3.0 |
| `basic/basic.asm` | EhBASIC 2.22, Lee Davison (ca65 form via jefftranter/6502) | **free for non-commercial use**; derivatives must carry "Derived from EhBASIC" — see `basic/README-EhBASIC.txt`. It is a separate program (`fs/ehbasic.prg`), not linked with the GPL code. |
| `cpm/src/` | RunCPM (CP/M 2.2 environment with internal CCP), Marcelo Dantas "Mockba the Borg" (vendored unmodified; see `cpm/VENDORED-FROM.txt`) | MIT |
| `forth/tali/` | Tali Forth 2, Scot W. Stevenson / Sam Colwell / Patrick Surry (vendored unmodified; see `forth/tali/VENDORED-FROM.txt`) | public domain |
| ROM and `.prg` binaries | linked against the cc65 runtime (`none.lib`) | cc65's zlib-style licence |
| `rom/wozmon.*` | Wozmon, reimplemented for the 45GS10 from Steve Wozniak's published 1976 listing | project licence (GPL-2.0-or-later) |
| `basic/msbasic/` | Microsoft BASIC for 6502, Microsoft's 2025 source release by way of Michael Steil's ca65 reconstruction `mist64/msbasic` (vendored unmodified, pure-MS files only; see `basic/msbasic/VENDORED-FROM.txt`). Built as a separate program (`fs/MSBASIC/msbasic.prg`); the K4510 port beside it, `basic/k4510msbasic.asm`, is the project's own and carries the project licence. | MIT |

Not in this repository, and no longer needed since the bare-metal Pi
build was retired (2026-09-07; the last tree with it is tag `alpha-0.5`,
whose `pi/Makefile` expected them beside the checkout): [Circle](https://github.com/rsta2/circle)
(GPL-3.0) and [circle-libsdl2](https://github.com/Xalior/circle-libsdl2)
(zlib; its `sdl-app.ld` is GPL-3.0). A `kernel8.img` built from them is
therefore GPL-3.0 as a whole, which GPL-2.0-or-later code permits. The
Raspberry Pi boot firmware (`bootcode.bin`, `start.elf`, `fixup.dat`)
in a distributed card image is Broadcom's, redistributable with
Raspberry Pi hardware under its own licence.

## Build tools (not distributed, needed to build)

GCC and GNU Make (GPL), cc65 (zlib-style), 64tass (GPL-2.0), NASM
(BSD-2-Clause), SDL2 (zlib), Python 3 (PSF). The guide additionally
needs XeLaTeX (LPPL) and is set in **Clear Sans** (Intel, Apache-2.0)
and **Iosevka** (Belleve Invis, SIL OFL 1.1) — neither font is part of
the machine. Mad Pascal and MADS (Tomasz Biela, MIT) are needed only to
compile Pascal; the K4510 target in `pascal/` installs into your own
Mad Pascal checkout.

## Deliberately not shipped

- **The CP/M system disk.** RunCPM's `DISK/A0.zip` — Digital Research's
  `ASM`, `MAC`, `DDT`, `STAT`, `SUBMIT` and third-party tools —
  installs into `fs/CPM/A/0/` for your use but is not committed: its 81
  files have mixed provenance and this repository is public. Same for
  WordStar, Turbo Pascal 3 and MBASIC, which the `K-*.SUB` launchers
  start but which you must supply yourself.
- **A Commodore character ROM.** Drop your own `chargen.bin` into
  `/SYSTEM/ETC` and the machine will use it; `.gitignore` keeps it out.
- **Your `STARTUP.BAT`** — yours, not the repository's (copy
  `/SYSTEM/ETC/STARTUP.SAMPLE`).

## If you redistribute

Ship the sources you built from, keep this file with them, keep
`8x8font.png` beside the open-roms font, and remember the two easy
traps: EhBASIC is non-commercial-only, and the "BBC BASIC" name is
licensed to this project and not to yours — call your fork's BASIC
something else.

History note: until 2026-08-23 the text font was derived from a
Commodore 64 character ROM. That file and every derivative were removed
from the repository and its history before it was made public.
