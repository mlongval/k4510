# Licences

The Thank You chapter ([Thank You](z2-thanks.md)) is the thanks; this is the record. The authoritative copy is `LICENSES.md` in the repository — if the two ever disagree, that file wins.

## The project itself

The K4510 as a whole is distributed under the **GNU General Public License, version 2 or (at your option) any later version**. Everything written for this project — the machine, VICKY, SHEILA, the DMA engine, the MATH unit, the system ROM and K/OS, JIM, the Tube ULA, the network layer, the editors, the demos, RX and the Pascal target — is Copyright © 2026 Michael Longval and is released under those terms. The choice is not really a choice: the CPU core and the OPL2 emulation are GPL-2.0-or-later, and a work containing them must be too.

## Code inside the machine

- **Xemu 65xx/45GS02 CPU core** — Gábor Lénárt. `core/xemu/`: `cpu65.c`, `cpu65.h`, `cpu65_mega65_timings.h`, `cpu65ce02_disasm_tables.c`, used unchanged. *GPL-2.0-or-later.*

- **fmopl (YM3812/OPL2)** — Jarek Burczyński and Tatsuyuki Satoh, from MAME by way of VICE. `core/opl2/`, vendored unaltered. *GPL-2.0-or-later.*

- **reSID** — Dag Lem, as shipped in VICE 3.3, was `core/resid/` until 2026-09-05 and is no longer in the tree ([Appendix C, The Sound, and What It Took](a3-sound.md)). *GPL-2.0-or-later*; recorded here because a vendored component that quietly disappears is worse than one recorded as gone.

- **BBC BASIC for SDL 2.0, console edition (BBCTTY)** — Richard T. Russell. `tube/`, vendored and altered (see `tube/ALTERED.md`). *zlib licence.* “BBC BASIC” is the name of Mr Russell’s interpreter, which he publishes under his own arrangement with the BBC. This project has not sought, and does not hold, any licence to the name: it is used here only to identify the interpreter, which is why this book says “Richard Russell’s BBC BASIC” and not “the K4510’s”.

- **EhBASIC 2.22** — Lee Davison, in the ca65 form from jefftranter/6502. `basic/basic.asm`. *Free for non-commercial use only*; derivatives must carry the words “Derived from EhBASIC” — see `basic/README-EhBASIC.txt`. It ships as a separate program (`fs/LANG/EHBASIC/ehbasic.prg`) and is not linked with the GPL code.

- **RunCPM** — Marcelo Dantas. `cpm/src/`, vendored unmodified (`cpm/VENDORED-FROM.txt`). *MIT.*

- **Tali Forth 2** — Scot W. Stevenson, Sam Colwell, Patrick Surry. `forth/tali/`, vendored unmodified. *Public domain.*

- **Wozmon** — Steve Wozniak’s 1976 monitor, reimplemented here for the 45GS10. Apple published the original listing; this machine’s version is the project’s own code under the project licence.

- **cc65 runtime** — the ROM and every `.prg` are linked against `none.lib`. *cc65’s zlib-style licence.*

- **Microsoft 6502 BASIC** — *MIT*, released by Microsoft in 2025, via [mist64/msbasic](https://github.com/mist64/msbasic) at a pure-Microsoft configuration. `basic/msbasic/`, vendored unmodified — and only the files a pure build assembles: none of the per-manufacturer material is here, which is the line that keeps the build resting on the MIT release alone.

- **Supermon+64 1.2** — Jim Butterfield, restored and commented by J. B. Langston ([jblang/supermon64](https://github.com/jblang/supermon64)), ported to the 45GS10. Butterfield published it for anyone to use; Langston asks for attribution.

## Fonts

- **unscii-8 and unscii-16** — Ville-Matias Heikkilä. `data/fonts/unscii/`: the upstream `.hex` files and the `font8-unscii.bin` and `font16-unscii.bin` made from them. The machine’s one screen font. *Public domain.*

- **Clear Sans** (Intel, *Apache-2.0*) and **Iosevka** (Belleve Invis, *SIL OFL 1.1*) — the two faces this book is set in. They are not part of the machine.

!!! note ""
    **No Commodore ROMs here.** Until 2026-08-23 the text font was derived from a Commodore 64 character ROM. That file and every derivative were removed from the repository *and from its history* before it was made public. Since 2026-09-14 the machine has one font, unscii, and reads no other: a `chargen.bin` in `/SYSTEM/ETC` is ignored.

## The Linux the machine boots on

The live image is a Debian, assembled by `linux/build-live.sh` from Debian’s own archive, so every package in it carries its own terms and Debian’s copyright files travel with it. One thing is built from source into it:

- **Tek40xx** — Ian Schofield’s Tektronix 4010/4014 terminal, built from upstream with one patch of ours. *GPL-3.0.* It is a separate program on the Linux side, not part of the emulator.

The Tektronix plot files in `linux/tek40xx/plt/` are Tek40xx’s own gnuplot examples plus a page generated here. The vintage cassette plots that circulate with *other* Tektronix emulators are marked personal-use-only by their authors and are deliberately *not* vendored; the README says how to fetch them for yourself.

!!! note ""
    **The Raspberry Pi build** was here until 2026-09-07 and needed two components that never lived in this repository: **Circle** (Rene Stange, *GPL-3.0*) and **circle-libsdl2** (Xalior, *zlib*, with a *GPL-3.0* linker script). A `kernel8.img` built from those was GPL-3.0 as a whole, which GPL-2.0-*or-later* code permits. The tag `alpha-0.5` is the last release that shipped one.

## Build tools

Not distributed with the machine, but required to build it: GCC and GNU Make (*GPL*), cc65 (*zlib-style*), 64tass (*GPL-2.0*), NASM (*BSD-2-Clause*), SDL2 (*zlib*), Python 3 (*PSF*), and for this book XeLaTeX (*LPPL*). Mad Pascal and MADS (Tomasz Biela, *MIT*) are needed only if you compile Pascal; the K4510 target in `pascal/` is installed into your own Mad Pascal checkout.

## What is deliberately not shipped

- **The CP/M system disk.** RunCPM’s `DISK/A0.zip` — Digital Research’s `ASM`, `MAC`, `DDT`, `STAT`, `SUBMIT` and third-party tools — installs into `fs/CPM/A/0/` for your use but is not committed: its files have mixed provenance and this repository is public. The same goes for WordStar, Turbo Pascal 3 and MBASIC, which the `K-*.SUB` launchers know how to start but which you must supply yourself.

- **Your `STARTUP.BAT`** — yours, not the repository’s (copy `/SYSTEM/ETC/STARTUP.SAMPLE` to start one).

## If you redistribute the machine

Ship the sources you built from, keep `LICENSES.md` and this chapter with them, and remember two things that are easy to forget: EhBASIC is non-commercial-only, and “BBC BASIC” is the name of Richard Russell’s interpreter rather than of anything made here — describe it as his, as this book does, and give whatever you build on it a name of its own.
