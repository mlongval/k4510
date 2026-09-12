# K4510

**This is release alpha-0.5, titled 'Timbre'.** The release carries the
Raspberry Pi SD-card image and the handbook. It is named for what it
adds: the same notes, and a choice of what they sound like — a second
SID engine beside reSID, and nine voices of FM beside both.

A fantasy 8/16-bit computer, built from scratch in August 2026. Project
orchestrator: Michael Longval. It is not an emulation of any real
machine: its CPU, video chip, sound and operating system are its own, and
the parts it borrows — a 6502-family instruction set, the AdLib's FM
chip — it borrows openly and then outgrows.

**One machine, two ways to run it.** The computer is the **K4510** — the
45GS10, VICKY, SHEILA, an OPL2, and K/OS. Boot it from a USB stick on a
spare laptop and it is a whole computer: a minimal Debian
that exists only to hold the machine up, with the cross-compilers, git
and an editor beside it (`docs/LINUX.md`, built by
`linux/build-live.sh`). Or run the same program in a window — or in a
sandboxed container (`linux/podman.sh`) — on a Linux desktop, which is
how it is developed and tested. Same ROM bytes, same software.

A third way, bare metal on a Raspberry Pi 3B+ with no operating system
underneath (the **BMC-K4510**, after Randy Rossi's BMC64), was built and
retired on 2026-09-07: every feature was costing twice for a boot three
seconds faster. `docs/decision-2026-09-07-one-shape.md` has the
reasoning; the last tree with the port is tag `alpha-0.5`. A Pi still
runs the machine — under Linux, the same way the laptop does.

**Read the handbook first**: `doc/guide/k4510-guide.pdf`, the User's and
Programmer's Guide, 103 pages, every screenshot captured from the running
machine at build time. This README is the short version. Both are alpha
documentation of an alpha machine: things change, and the handbook's
first page says so.

## Installing

**Desktop (Linux):**

    git clone https://github.com/mlongval/k4510
    cd k4510 && ./setup.sh
    ./k4510

`setup.sh` installs the dependencies (gcc, SDL2, cc65, 64tass, nasm),
builds everything and runs the test battery. `./k4510` starts the
machine from the repo root; `./k4510 --no-startup.bat` skips
`/STARTUP.BAT` for that one run.

**From a USB stick:** `sudo ./linux/build-live.sh` makes the
image (Debian, debootstrap and a few compilers; about half an hour the
first time), write it to a stick with `dd`, boot the laptop from it.
It loads to RAM, keeps its settings and saved programs on the stick, and
never touches the internal drive. `docs/LINUX.md` has the details.

## The machine

- **CPU: the 45GS10** — the MEGA65's 45GS02 instruction set (Q register,
  32-bit flat addressing, 28-bit MAP) plus the K4510 MMU: bank registers,
  a far-call gate, RAM under the ROM. The machine is a fantasy and its
  timings are suggestions, so the clock is whatever the host can hold at
  60 fps with clean sound. A recent desktop holds 100+ (the sweep is in
  `docs/CPU-CLOCK-POLICY.md`); a Pi 3B+ holds 15. Today it
  is a setting (F7 → Machine, desktop starting at 40.5, Pi at 15) and
  the first boot on a host measures it and picks the highest step that
  fits with margin (`core/calib.c`); `BENCH` shows the whole ladder. The
  instruction core is Xemu's, byte for byte; everything around it is ours.
- **Memory: 256 MB**, flat, 28-bit. The CPU sees 64 KB at a time and
  everything else is one instruction away. Byte-pokeable **bank
  registers** ($D600) and a **far-call gate** ($DF00) make programs
  bigger than the window overlays rather than a puzzle; **sideways ROM**,
  Beeb-style, pages 8 KB banks of operating system through $A000-$BFFF;
  programs own $0800-$CFFF and $E000-$FEFF by default. EhBASIC boots with
  47103 bytes free.
- **VICKY**, the video chip: 640×480, 640×240 and 320×240 (and two
  smaller fields a program may ask for), 256 colours from 24-bit, four
  layers (bitmap / tile / text), 128 sprites with no per-line limit, a
  blitter with copy/fill/logic/line/triangle ops, and **SHEILA**, a
  display-list coprocessor in the Amiga copper's tradition. No video RAM:
  every pointer is a 28-bit address.
- **Sound: an OPL2.** The machine sounds through a **YM3812** at $D480
  — nine FM voices wired the AdLib's way, an address port, a data port, a
  status register, so any AdLib register list or instrument patch means
  what it says. `OPLPLAY` plays `.OPL` streams; `tools/vgm2opl.py` makes
  them from VGM/VGZ logs. The BBC-style sound sequencer at $D5E0 (four
  queued channels: BBC BASIC's `SOUND`, Mad Pascal's `Sound`) plays
  through it too. The machine had four SIDs at $D400 until 2026-09-05;
  they were muted from 2026-09-01 and then removed outright, reSID and
  all. `git log` has them.
- **MATH unit:** eight IEEE-single registers with in-place ops and the
  transcendentals, a MEGA65-compatible multiplier/divider, and **math
  lists** — programs the unit runs by itself.
- **JIM**, the terminal: a VT100/ANSI in hardware at $DA00, drawing on the
  console's screen. CP/M programs, BBC BASIC, the editors and `TELNET`
  all write to it.
- **The network:** a URL is a file name (the Meatloaf rule) — `TYPE`,
  `LOAD`, `CP`, `RUN` and both BASICs' `LOAD` take `http://` and
  `https://`; `CD tnfs://host/dir` puts the current directory on a TNFS
  server (FujiNet, Meatloaf); the **N: device** at $D900 gives programs
  four channels (`tcp://`, `http://`), and `TELNET host port` is the
  demonstration — ANSI BBSes with their art and colours. No FTP. `https`
  is desktop-only (no TLS on the Pi); the Pi's Ethernet port is untested.

## The software

- **K/OS** (pronounced 'chaos'), the operating system, in the ROM: a
  shell with directories, `HELP` for the whole command set (the text is
  `/.HELP`), `MON`/`WOZ` the Wozmon-style monitor, `INFO`, `MODE 0-2`,
  `ALIAS`, `SWAP` (run a program on a clean machine and get this one
  back), `EXEC` scripts and `/STARTUP.BAT` at power-on (skip it from the
  F7 menu, Shell → Run STARTUP.BAT, or with `--no-startup.bat`). An
  unknown word runs `name.prg` from disk with its arguments — the REXX
  rule; `SAY` is the demo. Files live in `fs/`, one directory per
  language; bare names are searched across them.
- **Four tongues:** **EhBASIC 2.22** in ROM with graphics, sprites, the
  MATH unit and `*command` for any shell command (`*VI` edits the program
  in memory); **BBC BASIC** — Richard Russell's interpreter (BBCTTY, zlib)
  on **the Tube**, a co-processor port of Acorn heritage, with its own flat
  256 MB; **Forth** — Tali Forth 2, native 45GS10 code; and **CP/M 2.2**
  on the Tube's Z80 (RunCPM, MIT) — drives `A:`-`P:` are folders under
  `fs/CPM/`, `K:` is the machine's own filesystem, `CPM command` runs a
  program or a `.SUB` at boot, and the arrow keys arrive as the WordStar
  diamond so 1984's software can use them. The Tube runs on the desktop
  and on the Pi's core 3.
- **Two Pascals**, kept apart: Turbo Pascal 3 on CP/M (yours to supply,
  it is Borland's; drive `P:` is where it goes), and **Mad Pascal**, a
  cross-compiler — `pascal/` holds the K4510 target, `make pascal` turns
  `fs/LANG/PASCAL/*.PAS` into `.prg` files beside them; Write/CRT go through JIM, `uses
  k4510` gives every chip as a typed variable, `single` runs on the MATH
  unit, `uses graph` draws with the blitter.
- **Two editors:** `EDIT`, the nano of this machine, and `VI`, modal,
  with counts, operators, unlimited undo, `:s`, `:map`/`:imap`, a
  `/SYSTEM/ETC/VI.RC` startup file, and the whole file in far memory — 32000
  lines. `*SWAP EDIT name` edits from inside a BASIC.
- **Two file managers,** because they are two different ideas about what
  one is for: `KOMMANDER`, two panels and function keys, and `RANGER`,
  three miller columns and vi's fingers. Enter on a directory descends;
  Enter on a `.prg` or a CP/M `.com` leaves the browser and runs the
  program, which is where the output belongs. `DD` and the shell's `RM`
  move things to `/.TRASH` rather than destroying them; `DELETE` lists it
  and puts them back.
- **F7** opens the settings menu (C64u-style: video, audio — including
  which sound chip has the machine — keys, save and load state, the Tube,
  the shell's switches; saved to `k4510.cfg`).
  Super+PageUp resets. Esc is RUN/STOP, Shift+Esc quits the emulator.
- **When something goes wrong:** `DUMP ON`, make it go wrong again, then
  `BUG` — it interviews you and writes a finished report to
  `/SYSTEM/LOG/BUGREPORTS/`, with the build, the machine and the last dump
  filled in. Attach that and the dump to an issue. Appendix B of the
  handbook is the whole of it.

## Build and run (desktop)

    make            # gcc, SDL2, cc65, 64tass, nasm
    make test       # the test battery; also checks that no tracked binary is stale
    ./k4510         # the machine, from the repo root

## Documentation

- **The handbook** — `doc/guide/k4510-guide.pdf`, built and tracked in
  the repo. Source in `doc/guide/`, built with `doc/guide/make-guide.sh`:
  screenshots first, from the machine itself; Appendix A generated from
  the register comments in `core/*.h`; the GitHub issue template
  generated from Appendix B; and the build fails on a missing figure, a
  line off the page, or a `BUG` question the book does not know about.
- **The diary** — `docs/BUILD-LOG.md`, every session with its reasoning.
- **The design records** — `docs/`, mapped in `docs/README.md`.
- **Credits and terms** — `CREDITS.md` (thanks), `LICENSES.md` (the legal
  record), `THIRD_PARTY_SOURCES.md` (where each vendored component came
  from, and how to check it), `LICENSE`.
- **Filing an issue** — the handbook's Appendix B, or the template the
  issues page offers, which asks the same questions `BUG` does.

## Layout

    core/xemu/   the CPU core from Xemu (GPL-2.0-or-later), unchanged
    core/        memory, I/O devices, VICKY, the OPL2 and the audio seam, MATH unit, JIM, the network, host seam
    core/opl2/   fmopl, MAME's OPL2 by way of VICE (GPL-2.0-or-later)
    sdl/         the frontend + POSIX host glue
    linux/       the Linux the machine boots on: the live-stick build, the container flavour, Tek40xx
    rom/         system ROM (cc65) and Wozmon
    demo/        programs in C -> fs/SYSTEM/BIN and fs/APPS/NAME/*.prg  (the editors, TELNET, BUG, the games)
    retired/     programs that were part of the machine and are not any more (see its README)
    basic/       EhBASIC 2.22 + K4510 glue
    forth/       Tali Forth 2 (vendored) + the platform file
    tube/        Richard Russell's BBC BASIC, console edition (vendored, altered as marked)
    cpm/         RunCPM (vendored, unmodified)
    pascal/      the Mad Pascal target
    fs/          the machine's filesystem: /SYSTEM /LANG /APPS /HOME /CPM /MNT (fs/HOME/README.TXT)
    test/        tests, headless capture and benchmark tools
    tools/       romfree.py, which measures what is left in each ROM bank
    data/        fonts (the kernel 8x8, open-roms, unscii, BESCII)
    doc/guide/   the handbook: source, style, generators, and the built PDF
    docs/        design records and the build diary (docs/README.md maps them)

## Licence

GPL-2.0-or-later for the project (full text in `LICENSE`); Copyright
(C) 2026 Michael Longval. Components and their terms are listed in
`LICENSES.md`, the thanks in `CREDITS.md`. EhBASIC is free for
non-commercial use only — see `basic/README-EhBASIC.txt`. "BBC BASIC"
is the name of Richard Russell's interpreter; it appears here only to
identify what is vendored, and this project claims nothing in it.

This is a hobby machine offered as a gift, and it comes with **no
warranty of any kind** — see sections 11 and 12 of `LICENSE`. It is a
toy, and it is allowed to be wrong. Keep backups.

Constructive comments can be left at the repository's issues page. All
complaints, criticisms and negativity can be addressed to `/dev/null`.
