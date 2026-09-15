# Thank You

The K4510 is a machine that never existed. Almost everything in it that *does* exist was written by somebody else first, and given away. This chapter is the thanks; the Licences chapter ([Licences](z3-licences.md)) is the paperwork.

## The heart of the machine

**Gábor Lénárt** (LGB) wrote the 45GS02 CPU core in [Xemu](https://github.com/lgblgblgb/xemu), the MEGA65 emulator. It runs here unchanged — not a line altered, not a bug fixed — and every instruction this book describes is his code executing. There would be no K4510 without it.

**Jarek Burczyński** and **Tatsuyuki Satoh** wrote `fmopl`, the OPL2 emulation in MAME that this machine’s YM3812 is — by way of VICE, vendored unaltered. Nine voices of FM that any AdLib register list from 1987 can drive is their work, not ours.

**The VICE team**, whose decades of emulation scholarship — documentation, test programs, arguments settled in code — this project consulted at nearly every turn.

## The tongues

**Lee Davison** (1966–2013) wrote EhBASIC, the machine’s first BASIC and still the one that comes up in ROM. He is not here to be asked about the graphics and sound words bolted onto it; the machine carries his name in its `README` and its startup banner instead.

**Richard T. Russell** wrote BBC BASIC, and has kept writing it for forty years. The console edition of his BBC BASIC for SDL 2.0 (BBCTTY) is what runs on the Tube co-processor in [Chapter 6, The Tube: BBC BASIC](06-tube.md), under the zlib licence he publishes it with. “BBC BASIC” is the name of his interpreter; this book uses it only to say what is running, and claims nothing in it.

**Marcelo Dantas** (“Mockba the Borg”) wrote [RunCPM](https://github.com/MockbaTheBorg/RunCPM), which is the whole of [Chapter 9, CP/M: the Z80 Second Processor](09-cpm.md): a complete CP/M 2.2 with its own CCP, vendored here unmodified and simply handed a Z80’s worth of address space.

**Scot W. Stevenson**, **Sam Colwell** and **Patrick Surry** wrote [Tali Forth 2](https://github.com/SamCoVT/TaliForth2) and put it in the public domain — a Forth written to be read, which is why [Chapter 7, Forth](07-forth.md) can be honest about how it works.

**Tomasz Biela** (“tebe”) wrote [Mad Pascal](https://github.com/tebe6502/Mad-Pascal) and [MADS](https://github.com/tebe6502/Mad-Assembler); the K4510 target in `pascal/` is a guest in his compiler. **Wojciech Bociański** (“bocianu”), whose Neo6502 target showed how such a guest should behave.

**Steve Wozniak** wrote Wozmon in 1976 in 256 bytes, and the monitor in this machine is still recognisably it.

**Jim Butterfield** (1936–2007) wrote Supermon, and gave it away as he gave away everything: the machine language monitor a generation of Commodore programmers learned on, published in *Compute!* for anyone to type in. Supermon+64 V1.2 is `SUPERMON` here, ported and not rewritten. **J. B. Langston** restored and commented the source ([jblang/supermon64](https://github.com/jblang/supermon64)), which is what made porting it a day’s work rather than a month’s; he asks only for the attribution, and it is given gladly.

**Michael Steil** maintains [msbasic](https://github.com/mist64/msbasic), and **Microsoft** released 6502 BASIC 1.1 under the MIT licence in 2025 — between them, the machine’s next native BASIC.

## Letters on the screen

Fonts are the part of a computer you look at longest, and clean ones with a clear pedigree are hard to come by.

**Ville-Matias Heikkilä** (“Viznut”) — [unscii](https://viznut.fi/unscii/), placed in the public domain. It is the machine’s one screen font: unscii-8 in the 240-line modes, unscii-16 at 640×480, and both in the F7 menu and the side panel.

**The Linux kernel** contributors — the 8x8 console font (`font_8x8.c`) that got the text mode on its feet, and the 8x16 VGA font the K4510x consoles wear.

**Kenney** makes game art and puts it in the public domain, at a scale and a standard that has quietly furnished a decade of small games. The dungeon in the `TINY` demo — every tile, every little person, and the map they stand on — is his [Tiny Dungeon](https://kenney.nl/assets/tiny-dungeon), CC0. The demo exists because the art did. Two more games stand on the same ground: `SKYFIRE` flies Kenney’s [Pixel Shmup](https://kenney.nl/assets/pixel-shmup) ships, and `FLUFFY` is drawn with **Chloe Wolfe**’s Game Boy platformer set, also CC0. In both cases the art came first and the game was written around it, which is the reverse of the usual order and a much better way to spend an evening.

**Ian Schofield** wrote [Tek40xx](https://github.com/ijschofield/Tek40xx), a Tektronix 4010/4014 storage tube on SDL2 that is also a telnet client. It rides along on the machine’s Linux as the second terminal ([Chapter 13, The Linux Underneath](13-linux.md)), built from upstream with one patch of ours.

## The bare metal, which the machine no longer stands on

This section is kept because the debt is not cancelled by the code coming out. From August to September 2026 the K4510 ran on a Raspberry Pi 3B+ with no operating system underneath it, and that edition was retired on 2026-09-07 ([One machine, three ways to run it](01-machine.md#one-machine-three-ways-to-run-it)). Everything below is why it was possible at all.

**Randy Rossi** wrote [BMC64](https://github.com/randyrossi/bmc64): VICE on a Raspberry Pi with no operating system under it, 50 Hz smooth scrolling and input latency measured in single frames. His initials were in that edition’s name — the **BMC-K4510** was the K4510 on the bare-metal platform he proved out — and it is the direct reason the port existed at all: the route to the Pi was always meant to be BMC64’s `emux_api` seam, and the bare-metal case it proved is what made it thinkable. He also built the **VIC-II Kawari**, a modern drop-in VIC-II with video modes the original never had: the demonstration that you may extend an 8-bit machine’s video chip and still be working inside the tradition rather than outside it. VICKY is a more reckless cousin of that idea. **minch** (aminch) has taken BMC64 over from him and carries it onto the newer Pis; the project is in good hands.

**Rene Stange** wrote [Circle](https://github.com/rsta2/circle), the bare-metal C++ environment that was the entire reason a Raspberry Pi 3B+ could boot straight into this machine with no operating system under it. **Xalior** wrote [circle-libsdl2](https://github.com/Xalior/circle-libsdl2), which let the desktop emulator move to the Pi essentially as written.

## The workshop

The machine is built with **cc65** (compiler, assembler, linker and runtime for the whole system ROM), Marco Baye’s **ACME** (Wozmon and the demos), Zsolt Soós’s **64tass** (Tali Forth and Supermon), **NASM**, **GCC** and **GNU Make**; it shows itself to you through **SDL2** — Sam Lantinga’s library and its contributors’, which is the window, the keyboard and the sound on the desktop, and through Xalior’s port, on the Pi as well. This book is set in **XeLaTeX** with **Clear Sans** and **Iosevka**, and every screenshot in it was captured from the machine actually running — a picture that cannot be produced fails the build.

## Heritage

Some debts are not code.

**Acorn Computers** — the Tube, the sideways-ROM model, the `*` command prefix, and the idea that a second processor should be a normal thing to own. The Beeb’s ghost is all over this machine.

**Commodore** — the other half of its soul: PETSCII, the SID, and the line from the PET to the C65 that stopped one machine short of this one.

**The MEGA65 project** — for keeping the 45GS02 and the C65 dream alive in real silicon, and for open-roms.

**Kim Lemon** and the Lemoners — [Lemon64](https://www.lemon64.com) has been the C64’s front door since 1998: the games database, the reviews and scans, the music, and a forum that actually answers. Twenty-eight years of one person’s dedication to a machine’s community, and a good half of the small facts this project needed came from there.

**The Meatloaf project** (idolpx) — the C64 cartridge that made a URL a file name, which is the whole of this machine’s rule for the network: name it, and it is fetched. **The FujiNet project** — the network device the Atari got first, whose N: device this machine imitates and whose TNFS servers it speaks to; `CD tnfs://fujinet.online/` is a directory on theirs.

**Paul Scott Robson** wrote the Neo6502’s firmware — the operating system and BASIC that make an Olimex Neo6502 a computer rather than a board — and the handbook that documents it. That book is the one this one is modelled on, down to the page size, and the rule that every example must be run and photographed before it may be printed is his. The other model is the **MEGA65 User’s Guide**.

**The High Voltage SID Collection** and the composers in it, who kept thirty years of a machine’s music alive and free to listen to. The K4510 played it for one summer, through a `SIDPLAY` that retired with the chips. None of that music was ever distributed with this machine; it is theirs.

!!! note ""
    **The project’s orchestrator** is Michael Longval — Doc — who decided what the machine is, what it borrows and what it refuses, what gets built next and what gets thrown away, and read every page of this book against the machine.
    
    **And the machine’s other author.** Most of the K4510’s own code — VICKY, SHEILA, the DMA engine, the ROM, K/OS, the Tube ULA, the editors, RX — was written in conversation with Claude (Anthropic’s Claude Code), over several months of long sessions with a build log to prove it. The design decisions are the author’s; a great deal of the typing was not.

## And whoever is missing

This list was assembled by hand and is certainly incomplete. If your work is in this machine and your name is not on this page, that is an error and not a judgement — open an issue at <https://github.com/mlongval/k4510> and it will be fixed in the next printing.
