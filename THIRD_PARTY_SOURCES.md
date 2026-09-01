# Third-party sources

Where every piece of code in this repository that someone else wrote came
from, precisely enough to fetch it again and check that what is here is
what they published.

`LICENSES.md` is the legal record and `CREDITS.md` is the thanks. This
file is the provenance: upstream URL, version, whether it was altered,
and how to verify it.

**Last checked: 2026-08-26** against the tree at that date. The digests
for `core/xemu`, `core/resid` and `tube` were recomputed on that re-check:
the first two had changed since they were recorded, and `tube` had not
changed at all yet still did not reproduce, so that one was wrong when
written. The other three reproduce exactly. Every
component has a `VENDORED-FROM.txt` beside it; this file is the summary.

Digests written `dir:` are of the directory's source files, sorted by
path and concatenated -- reproduce them with the command at the bottom,
which pins `LC_ALL=C` because the sort order, and so the digest, depends
on it.
A component marked *not recorded* is a gap in this record, not a claim
that the code is unknown; those are listed again at the end.

---

## 45GS02 / 65CE02 CPU core (Xemu)

| | |
|---|---|
| Role | The CPU the machine runs on. The one component that is the real thing rather than a fantasy. |
| Local path | `core/xemu/` -- `cpu65.c`, `cpu65.h`, `cpu65_mega65_timings.h`, `cpu65ce02_disasm_tables.c`, `emutools_basicdefs.h` |
| Upstream | https://github.com/lgblgblgb/xemu (Gábor Lénárt, "LGB"). The cycle table `cpu65_mega65_timings.h` comes from a **second** repository of his, https://github.com/lgblgblgb/megacyc, which generated it. Xemu is LGB's own project, *not* a MEGA65-project repository — though it is the emulator that community uses, and what the core emulates is the 4510/45GS02 as extended in the MEGA65. |
| Version | Commit **not determined**; `cpu65.c` carries "Copyright (C)2016-2025 LGB" (`cpu65.h` says 2024), vendored 2026-08-21. `core/xemu/VENDORED-FROM.txt` says how to settle it. |
| Licence | GPL-2.0-or-later (`core/xemu/LICENSE.xemu`) |
| Altered | No. Used unchanged. |
| Verify | `dir: 8b7cdc2fcb509484` |

## reSID

| | |
|---|---|
| Role | All four SID chips. |
| Local path | `core/resid/` |
| Upstream | Dag Lem's reSID, as shipped in VICE 3.3 (https://sourceforge.net/projects/vice-emu/) |
| Version | As shipped in VICE 3.3, vendored 2026-08-22 — for a library distributed inside another project that is firmer than a commit. The headers' `version 2` is the SID model constant, not a release. `core/resid/VENDORED-FROM.txt` |
| Licence | GPL-2.0-or-later |
| Altered | No. |
| Verify | `dir: 69adec626fd3713d` |

## FastSID — REMOVED 2026-09-01

VICE's wavetable SID engine was vendored here on 2026-08-30 and removed on
2026-09-01, in the consolidation that made the machine an OPL2 machine.  It
had been the cheap answer to a Pi that could not afford four cycle-accurate
SIDs; once the Pi stopped sounding SIDs at all, it was a second engine for a
chip neither host plays by default, and it was the one that sounded worst.
No file of it remains in the tree (`core/fastsid/`, `core/fsid.[ch]`); the
git history has all of it, unaltered from upstream, if it is ever wanted
back.  Nothing else here depended on it.

## fmopl (the OPL2)

| | |
|---|---|
| Role | The Yamaha YM3812 at `$D480` — nine FM voices, the AdLib's chip, wired the AdLib's way. Mutually exclusive with the SIDs. |
| Local path | `core/opl2/` — `fmopl.c`, `fmopl.h`. The device and wrapper `core/opl2.c` are ours. |
| Upstream | MAME's FM sound generator (Jarek Burczynski, Tatsuyuki Satoh), version 0.72, adapted for VICE by Marco van den Heuvel. Taken from **BMC64's** vendored copy — `third_party/vice-3.3/src/core/` in https://github.com/randyrossi/bmc64. |
| Version | VICE 3.3's copy, vendored 2026-08-30. `core/opl2/VENDORED-FROM.txt` |
| Licence | GPL-2.0+ (stated in the file's own header) |
| Altered | No. The other headers in that directory are K4510 shims; `alarm.h` is two timer slots and a poll where VICE has a general alarm queue. |
| Verify | `dir: 3ff3515ef70fe50f` (`fmopl.c` and `fmopl.h` only) |

## BBC BASIC (BBCSDL console edition, "BBCTTY")

| | |
|---|---|
| Role | The Tube co-processor's BASIC. |
| Local path | `tube/` (sources in `tube/src`, headers in `tube/include`) |
| Upstream | https://github.com/rtrussell/BBCSDL (Richard T. Russell) |
| Version | 1.34b (`tube/include/BBC.h`) |
| Licence | zlib. "BBC BASIC" is the name of Richard Russell's interpreter; this project holds no licence to that name and asserts no rights in it, using it only to identify what is vendored. See `tube/ALTERED.md`. |
| Altered | **Yes** -- every change marked `[K4510]`; the notice required by condition 2 of the licence is `tube/ALTERED.md`. |
| Verify | `dir: 8988e29475436852` |

## RunCPM

| | |
|---|---|
| Role | The Z80 second processor's CP/M 2.2, with the internal CCP. |
| Local path | `cpm/src/` |
| Upstream | https://github.com/MockbaTheBorg/RunCPM (Marcelo Dantas, "Mockba the Borg") |
| Version | commit `e698e8ab59c2de915b23be7f5b146a5c621f5c76`, vendored 2026-07-21 (`cpm/VENDORED-FROM.txt`) |
| Licence | MIT |
| Altered | No. Built `CCP_INTERNAL`, so no DRI binaries are distributed. |
| Verify | `dir: 607fde766f32bb56` |

## Tali Forth 2

| | |
|---|---|
| Role | The machine's Forth. |
| Local path | `forth/tali/` (the port is `forth/platform.asm`, which is ours) |
| Upstream | https://github.com/SamCoVT/TaliForth2 (Scot W. Stevenson, Sam Colwell, Patrick Surry) |
| Version | commit `cb887532b9fdc2d8c96d891dafb613ddf9640bb8`, vendored 2026-08-13 (`forth/tali/VENDORED-FROM.txt`) |
| Licence | public domain |
| Altered | No. |
| Verify | `dir: ebeb4a68bb2d232f` |

## EhBASIC 2.22

| | |
|---|---|
| Role | The machine's first BASIC. |
| Local path | `basic/basic.asm` (the K4510 glue in `basic/k4510*.asm` is ours) |
| Upstream | Lee Davison (1966--2013); the ca65 form came via https://github.com/jefftranter/6502 |
| Version | 2.22 |
| Licence | Free for non-commercial use. Derivatives must carry the string **"Derived from EhBASIC"** in any binary image, and `basic/README-EhBASIC.txt` in any human-readable distribution. Shipped as a separate program (`fs/EHBASIC/ehbasic.prg`), not linked with the GPL code. |
| Altered | **Yes** -- extended with the machine's graphics, sound, file and `*` statements. |
| Verify | `dir: df25f7f9fc1cb7ba` (whole `basic/`, ours and theirs together) |

## Linux console font, 8x8

| | |
|---|---|
| Role | The machine's default chargen. |
| Local path | `data/font8.bin`, generated by `data/mkfont.py` |
| Upstream | the Linux kernel, `lib/fonts/font_8x8.c` |
| Version | Kernel tree **not determined** — `mkfont.py` takes the source file as an argument, so it was never recorded, and the output cannot say. That font has been unchanged in Linux for decades. `data/VENDORED-FROM-font8.txt` |
| Licence | GPL-2.0 |
| Altered | Reformatted to a flat 2048-byte chargen; glyphs unchanged. |
| Verify | `sha256 b8c58a575869ca4f1e8faf94cdd55781f89bdc1db64575d6ef38c4575bb04f0f` |

## open-roms chargen and PXLfont 2.3

| | |
|---|---|
| Role | Selectable screen fonts (F7 menu -> Video -> Screen font). |
| Local path | `data/fonts/openroms/` |
| Upstream | https://github.com/MEGA65/open-roms -- `master`, fetched 2026-08-24 |
| Version | Fetched from `master` 2026-08-24; commit **not determined** (a `.rom` carries no version). PXLfont is pinned by its own name: 88665b RF2.3, by Retrofan, with open-roms' recorded permission. `data/fonts/openroms/VENDORED-FROM.txt` |
| Licence | LGPL-3.0-or-later. `8x8font.png` is the editable source, shipped for LGPL compliance. |
| Altered | No. |
| Verify | `sha256 5e3451466841b93df7e01e4b635b07b8d8633351bae483b1961d96b3131186e7  chargen_openroms.rom`<br>`sha256 bc5ed24e8e694543f0229800d050acff86d9674aec6dbd95055a26e824d8a395  chargen_pxlfont_2.3.rom` |

## unscii 8

| | |
|---|---|
| Role | Selectable screen font; also the F7 menu's own font, which must draw when the guest has wrecked everything. |
| Local path | `data/fonts/unscii/` -- `unscii-8.hex` (source), `font8-unscii.bin` (generated by `data/fonts/tools/hex2chargen.py`) |
| Upstream | https://viznut.fi/unscii/ (Viznut) |
| Version | Release **not determined** — the `.hex` is bare `codepoint:bitmap` rows with no header. Upstream was at 2.1. `data/fonts/unscii/VENDORED-FROM.txt` |
| Licence | public domain |
| Altered | No; converted to chargen form. |
| Verify | `sha256 5130fc27c18e32309d3a35f2b9e5f2d96650dc33e4f77a8b731ab74e5e41201e  font8-unscii.bin` |

## BESCII

| | |
|---|---|
| Role | Selectable screen font. |
| Local path | `data/fonts/bescii/` |
| Upstream | Damian Vila -- https://damianvila.com/blog/designing-the-bescii-font/ |
| Version | v3 (`data/fonts/bescii/fonts/v3`) |
| Licence | CC0-1.0 (`data/fonts/bescii/LICENCE`) |
| Altered | No. |

## Bomb Party

| | |
|---|---|
| Role | Art for the BOMBER game: walls, crates, bombs, blasts and the four little people become VICKY tiles and sprites. |
| Local path | `data/bombparty/` -- `bomb_party_v4.png` |
| Upstream | https://opengameart.org/content/bomb-party-the-complete-set (devurandom, with richtaur and cemkalyoncu; fetched 2026-08-28) |
| Version | v4, the "complete set" sheet; 15x19 cells of 16x16. |
| Licence | CC-BY 3.0 -- attribution required, given in CREDITS.md. |
| Altered | No. `tools/mkbomber.py` derives `demo/bomber.h` at build time; the derived file is not committed. |
| Verify | `sha256 1635752a826c6d4b0d3a793e635d6675244d1590dfc88560da85a682036b9f56  bomb_party_v4.png` |

## Tiny Dungeon

| | |
|---|---|
| Role | Art for the TINY demo: the tile sheet becomes VICKY 8 bpp tiles and sprites, the Tiled sample map becomes the world. |
| Local path | `data/tinydungeon/` -- `tilemap_packed.png`, `sampleMap.tmx`, `Tilesheet.txt`, `License.txt` |
| Upstream | Kenney -- https://kenney.nl/assets/tiny-dungeon ; fetched from the OpenGameArt mirror https://opengameart.org/content/tiny-dungeon (`kenney_tinydungeon.zip`, 2026-08-28) |
| Version | The zip carries no version; 132 tiles of 16x16. `data/tinydungeon/VENDORED-FROM.txt` |
| Licence | CC0-1.0 (`data/tinydungeon/License.txt`) |
| Altered | No. `tools/mktiny.py` derives `demo/tiny.bin` and `demo/tiny.h` at build time; neither is committed. |
| Verify | `sha256 d24e60a41e4ac7a745c0304dfde121143688557f40215f23221c29cfe683825f  tilemap_packed.png` |

## cc65

| | |
|---|---|
| Role | Build tool. The system ROM, the demos and the editors are C compiled with it; `.prg` binaries link its runtime (`none.lib`). |
| Local path | Not vendored. Built from source into `~/opt/cc65` on each build host. |
| Upstream | https://github.com/cc65/cc65 |
| Version | tag **V2.19**, commit `5552824`. Note that the binaries report `V2.18 - Git 5552824`: cc65 does not bump the version string until release, so `--version` understates it. `git describe --tags` is the reliable check. |
| Licence | zlib-style (applies to the linked runtime) |
| Altered | No. **Do not build from `master`** -- it deprecates the `sp` symbol the ROM's crt0 uses, and the resulting ROM hangs at boot with no diagnostic. |

## Circle and circle-libsdl2 (Raspberry Pi only)

| | |
|---|---|
| Role | The bare-metal runtime the Pi 3B+ port links against. |
| Local path | Not vendored. Expected at `$(SHIM)` — default `~/Projects/k4510-pi/circle-libsdl2` (`pi/Makefile`). |
| Upstream | https://github.com/Xalior/circle-libsdl2 — **the shim, not `rsta2/circle` directly.** Circle, newlib, mbedtls and the rest arrive through it as submodules. |
| Version | `30cbcbd` (`vPoC3-155-g30cbcbd`), checked on the build host 2026-08-26. Circle itself is `6177984e` (tag `Step51`), circle-stdlib `a4fbed9` (`v8.0-620`). The full submodule list is in `pi/VENDORED-FROM.txt`. |
| Licence | GPL-2.0-or-later (Circle); the submodules carry their own. |
| Altered | No. |

## Wozmon

Not third-party code in the vendoring sense: `rom/wozmon.a` is a
reimplementation for the 45GS10 from Steve Wozniak's published 1976
listing, and carries this project's licence. Listed here so that a
reader looking for it does not conclude it was overlooked.

## MS 6502 BASIC 1.1

`basic/msbasic/` -- Microsoft's 2025 source release (MIT) by way of
`mist64/msbasic` (https://github.com/mist64/msbasic), Michael Steil's
ca65 reconstruction. Vendored unmodified at commit
`2a0bc2fe0db13f8cf1b5c40b1d5617263cdb9cb4`, 2026-08-31.

**Only the files a pure-Microsoft configuration assembles are here.**
Left upstream on purpose: `orig/*.bin` (the original Commodore, Apple,
AIM-65 and MicroTAN ROM images -- the machine vendors' binaries, not
Microsoft's MIT release), the per-OEM `defines_*.s`, and the OEM
ISCNTC/LOAD/SAVE/EXTRA reconstructions. That is the split the licence
research called for on 2026-08-24: a pure-MS build rests on the MIT
release alone, while a byte-exact OEM ROM rebuild would not.

The K4510 configuration and port -- `basic/k4510msbasic.asm`, which is
the machine's whole side of it (config, console glue, `.prg` header) --
sits *outside* the vendored directory so everything inside stays
byte-identical to upstream. Built to `fs/MSBASIC/msbasic.prg`.

---

## Gaps in this record

Every component now has a `VENDORED-FROM.txt` beside it. Four of them
still cannot name an upstream commit, and say so rather than guessing:

| Component | What is missing | Why |
|---|---|---|
| `core/xemu/` | the commit | no version in the sources |
| `core/resid/` | the upstream commit | shipped inside VICE 3.3, which is the firmer statement anyway |
| `data/font8.bin` | which kernel tree | `mkfont.py` takes the file as an argument; the output cannot say |
| `data/fonts/openroms/` | the commit | fetch date recorded, a `.rom` carries no version |
| `data/fonts/unscii/` | the release | the `.hex` has no header |
| `data/tinydungeon/` | the release | Kenney's zip has no version; the mirror's file name is the only mark |

Each record carries the command that would settle it from a machine with
network access. A guessed hash in a provenance file is worse than an
honest gap, so none were guessed.

## Re-check commands

    # a directory digest, as used above.  The same command for every
    # component -- .cc for reSID, .asm/.s/.inc for Tali Forth and EhBASIC.
    # LC_ALL=C matters: without it the sort order and the digest change.
    dir_digest() {
      find "$1" -type f \( -name '*.c' -o -name '*.h' -o -name '*.cc' \
           -o -name '*.asm' -o -name '*.s' -o -name '*.inc' \) -print0 \
        | LC_ALL=C sort -z | xargs -0 cat | sha256sum | cut -c1-16
    }
    dir_digest core/xemu

    # a single artefact
    sha256sum data/font8.bin

    # the vendoring records already in the tree
    cat cpm/VENDORED-FROM.txt forth/tali/VENDORED-FROM.txt
    cat tube/ALTERED.md data/fonts/README.md

    # the toolchain actually in use (the version string understates it)
    cc65 --version; git -C ~/opt/cc65 describe --tags
