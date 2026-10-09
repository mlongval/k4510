# K4510

**Alpha 0.9 ('Bezel'), October 2026.** The machine boots from a USB
stick (or a second partition) as the K4510x appliance, or runs in a
window on a Linux desktop; the handbook is `doc/guide/k4510-guide.pdf`,
and on the web at Read the Docs. The last release with the bare-metal
Raspberry Pi image was alpha-0.5 ('Timbre'); that port is retired
(below).

A fantasy 8/16-bit computer, built from scratch in August 2026. Project
orchestrator: Michael Longval. It is not an emulation of any real
machine: its CPU, video chip, sound and operating system are its own, and
the parts it borrows — a 6502-family instruction set, the AdLib's FM
chip — it borrows openly and then outgrows.

**One machine, two ways to run it.** The computer is the **K4510** — the
45GS10, VICKY, SHEILA, MELODY (an OPL2), FRED (the MATH unit), JIM and K/OS. Boot it from a USB stick on a
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
Programmer's Guide, 160 pages, every screenshot captured from the running
machine at build time. This README is the short version. Both are alpha
documentation of an alpha machine: things change, and the handbook's
first page says so.

## There is no web browser

Not missing: left out. The K4510 reaches the network the way computers
did before the web ate everything — `TELNET` to a BBS, a URL that is
simply a file (`TYPE https://...`, `LOAD`, `CP`), a TNFS server as a
directory, sockets for programs that want them. Nothing on this machine
renders a page, runs someone else's JavaScript, plays an advertisement or
asks you to accept cookies. When you sit down at it, it is yours, and it
is quiet. Think of it as an antidote.

## New in 0.9 ('Bezel')

A release about the screen and what frames it.

- **The panel's own screens.** At start the machine finds the panel and
  offers every *integer display resolution* it divides into — on 1920×1080,
  1440×1080 down to 360×270, each pixel a whole block of the panel's — on
  a 4:3 canvas or the whole panel (F12 → Video → Canvas). K/OS and its text
  fit each one: 8×8, 8×16 and HD 16×16 / 16×32 cells. `MODE -l` lists them,
  `MODE -s 2` or `MODE 720x540` picks one; `tools/k4510-vidcap` measures a
  host and caps the pixels it can draw.
- **The frame.** The border and both status bands are one colour (F12 →
  Video → Frame colour, dark grey), following the palette or kept whatever
  it is, with the bands' text chosen for contrast.
- **Palettes that read.** The machine's own programs keep the contrast
  they were designed with under AMBER, GREEN or GREY; the Terminal's text
  is made readable when the palette changes; the mouse pointer takes the
  palette's colours; and the palette last loaded comes back at every
  power-on and reset.
- **POSIX options everywhere:** `-l`, `-al`, `--long`, `-s 2`,
  `--scale=2`, `--` — one parser for MODE, DIR, INFO, SWAP, EDIT, PROG,
  WORD, DELETE and FONTED.
- **Programs are told when the screen changes** (`$D546`, RESIZE): EDIT,
  PROG, WORD and VI lay themselves out again when F12 changes the canvas.
- **Save and power off** (F12 → Machine): the whole machine to a file, the
  computer off, and back where you were at the next power-on.
- **A laptop's companion:** the battery's time left in the bottom band
  (`77%↓ (7:16)`), *Charge to 100% once* for a trip, and a file you save
  is on the disk when SAVE returns.
- **The Terminal screen over mosh** (`k4510-connect`), falling back to ssh:
  what you type shows at once over a phone's hotspot. A K4510 power cycle
  no longer leaves holes in it.
- **Personalities.** Hold SPACE at power-on for a menu of other machines
  (Commodore 64, 128 and PET, Amiga 500 and 1200, Commander X16), run
  from RAM by the same K4510 Linux; quitting one brings up the K4510
  (`k4510-boot-menu`, `k4510-personality`; `docs/STORAGE.md`). Their
  picture sits on the left and the K4510's sidebar scenes fill the right,
  drawn by `sdl/libk4510side.so`; the C128 shows its 80-column screen
  there. The machines themselves are images built apart from this repo.
- **RANGER opens files by extension** (`/SYSTEM/ETC/RANGER.RC`: `.PAS` in
  PROG ...); EDIT's menus no longer break after About.
- **The jettison:** DOOM, the Apple IIe, Microsoft BASIC, TINY and the
  other trial balloons are gone, and EhBASIC after them (2026-10-09):
  K4510 BASIC, compiled, is the machine's BASIC and RX its interpreter; CP/M, the Tek40xx and the Navidrome radio
  are sidelined; `core/io.c` is one file a device.

`docs/BUILD-LOG.md` has the measurements and the reasoning.

## Installing

**Desktop (Linux):**

    git clone https://github.com/mlongval/k4510
    cd k4510 && ./setup.sh
    ./k4510

`setup.sh` installs the dependencies (gcc, SDL2, cc65, 64tass, nasm),
builds everything and runs the test battery. `./k4510` starts the
machine from the repo root; `./k4510 --no-startup.bat` skips
`/STARTUP.BAT` for that one run.

**No camera.** The K4510x image ships with no webcam driver at all, on
purpose: a machine meant for children, which anyone may copy, should not
carry the means to watch them. An institution that needs one builds its own
image with `K4510_CAMERA=1`; `docs/CAMERA.md` has how and why.

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
  60 fps with clean sound. **The MHz is how soon a long calculation
  finishes, not how fast the machine feels**: the console, files,
  network, DMA and the MATH unit run at host speed whatever the clock,
  so typing and `DIR` are the same at 15 as at 40.5, while a
  Mandelbrot takes 2.7 times as long. It is not comparable with another
  computer's MHz either; `MARK` gives the figure that is (at 40.5, a
  65C02 at 43-51). A recent desktop holds 100+ (the sweep is in
  `docs/CPU-CLOCK-POLICY.md`). It is a setting (F12 → Machine), capped
  at 60 MHz for now, and the first boot on a host measures it and picks
  the highest step that fits with margin (`core/calib.c`); `BENCH`
  shows the whole ladder. The
  instruction core is Xemu's, unchanged but for two hooks (the stack
  fence and WAIT, below); everything around it is ours. **WAIT**
  (`$D545`) puts the CPU to sleep until the next frame or a key, and the
  shell, the BASICs and the editors use it while they wait, so an idle
  machine costs the computer beneath it next to nothing; a window whose
  picture has not changed is not drawn again either.
- **Memory: 256 MB**, flat, 28-bit. The CPU sees 64 KB at a time and
  everything else is one instruction away. Byte-pokeable **bank
  registers** ($D600) and a **far-call gate** ($DF00) make programs
  bigger than the window overlays rather than a puzzle; **sideways ROM**,
  Beeb-style, pages 8 KB banks of operating system through $A000-$BFFF;
  programs own $0800-$CFFF and $E000-$FEFF by default (C and Pascal
  programs load at $0800), and the zero page and the stack page are the
  program's own: K/OS runs on a base page and a 6502 stack of its own
  (pages 6 and 7), with its C stack in a kilobyte of RAM in the I/O page,
  watched by a **stack fence** (`INFO -m` shows the margin). The handbook's Memory chapter maps every
  byte the system uses, in the 64 KB and in the 256 MB.
- **VICKY**, the video chip: 640×480, 640×240, 320×240, 320×200 and
  160×200, and the panel's own integer display resolutions (on 1920×1080:
  1440×1080, 720×540, 480×360, 360×270 — whatever this panel divides
  into, 4:3 or the whole panel); 256 colours
  from 24-bit, four
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
- **FRED, the MATH unit:** eight IEEE-single registers with in-place ops and the
  transcendentals, a MEGA65-compatible multiplier/divider, and **math
  lists** — programs the unit runs by itself.
- **JIM**, the terminal: a VT100/ANSI in hardware at $DA00, drawing on the
  console's screen. Everything that writes text writes it to JIM — the
  shell, both BASICs, Pascal's CRT, CP/M, the editors, `TELNET` — so a
  screen is a stream that can go anywhere. JIM owns the **status bands**
  (what is running, the clock, the network, the battery) and has a
  **second screen**: a terminal session on the Linux beneath (`TERMINAL`,
  or Alt+1 / Alt+2; tmux at the far end gives as many as you like).
- **The network:** a URL is a file name (the Meatloaf rule) — `TYPE`,
  `LOAD`, `CP`, `RUN` and both BASICs' `LOAD` take `http://` and
  `https://`; `CD tnfs://host/dir` puts the current directory on a TNFS
  server (FujiNet, Meatloaf); the **N: device** at $D900 gives programs
  four channels (`tcp://`, `http://`), and `TELNET host port` is the
  demonstration — ANSI BBSes with their art and colours. No FTP.

## The software

- **K/OS** (pronounced 'chaos'), the operating system, in the ROM: a
  shell with directories, `HELP` for the whole command set (the text is
  `/SYSTEM/ETC/HELP`), `MON` the monitor, `INFO`, `MODE`, `TERMINAL`,
  `ALIAS`, `SWAP` (run a program on a clean machine and get this one
  back), `EXEC` scripts and `/STARTUP.BAT` at power-on (skip it from the
  F12 menu, Shell → Run STARTUP.BAT, or with `--no-startup.bat`). An
  unknown word runs `name.prg` from disk with its arguments — the REXX
  rule; `SAY` is the demo. Files live in `fs/`, one directory per
  language; bare names are searched across them.
- **The languages:** **K4510 BASIC**, compiled (`BAS NAME`, by way of Mad
  Pascal) with graphics, sprites, sound, files and `*command` for any
  shell command (`docs/KBASIC.md`); **LOGO**; **RX**, a REXX, the
  interpreter for a quick try; **C**, compiled
  on the machine's own Linux with cc65 (`fs/LANG/C`); **BBC BASIC** — Richard Russell's interpreter (BBCTTY, zlib)
  on **the Tube**, a co-processor port of Acorn heritage, with its own flat
  256 MB; and **Forth** — Tali Forth 2, native 45GS10 code.  (**CP/M 2.2**
  on the Tube's Z80, RunCPM, is sidelined since 2026-10-07: `cpm/` is kept
  and `make cpm/runcpm` builds it, but it is not in the images or the
  handbook.)
- **Pascal:** **Mad Pascal**, a
  cross-compiler — `pascal/` holds the K4510 target, `make pascal` turns
  `fs/LANG/PASCAL/*.PAS` into `.prg` files beside them; Write/CRT go through JIM, `uses
  k4510` gives every chip as a typed variable, `single` runs on the MATH
  unit, `uses graph` draws with the blitter.
- **The editors:** `EDIT`, MS-DOS EDIT's look with menus and the mouse;
  `PROG`, the same window as a programmer's editor that compiles and runs
  what it holds; `WORD`, for prose; and `VI`, modal, with counts,
  operators, unlimited undo, `:s`, `:map`/`:imap`, a `/SYSTEM/ETC/VI.RC`
  startup file, and the whole file in far memory — 32000 lines. `*EDIT`
  and `*VI` edit from inside a BASIC.
- **Two file managers,** because they are two different ideas about what
  one is for: `KOMMANDER`, two panels and function keys, and `RANGER`,
  three miller columns and vi's fingers. Enter on a directory descends;
  Enter on a `.prg` or a CP/M `.com` leaves the browser and runs the
  program, which is where the output belongs; RANGER opens anything else
  in the program `/SYSTEM/ETC/RANGER.RC` names for its extension. `DD` and the shell's `RM`
  move things to `/.TRASH` rather than destroying them; `DELETE` lists it
  and puts them back.
- **F12** opens the settings menu (C64u-style: the resolution, canvas,
  frame colour and palette, the bands, audio, keys, save and load state,
  Save and power off, the Tube, the shell's switches, and on the K4510
  Linux the host — Wi-Fi, the lid, the battery; saved to `k4510.cfg`).
  Shift+F12 pauses; F7 and F8 are ordinary keys (the menu was on F7 until
  2026-09-15). Super+PageUp resets. Esc is RUN/STOP, Shift+Esc quits the emulator.
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

    core/xemu/   the CPU core from Xemu (GPL-2.0-or-later), unchanged but for two marked hooks
    core/        memory, I/O devices, VICKY, the OPL2 and the audio seam, MATH unit, JIM, the network, host seam
    core/opl2/   fmopl, MAME's OPL2 by way of VICE (GPL-2.0-or-later)
    sdl/         the frontend + POSIX host glue
    linux/       the Linux the machine boots on: the live-stick build, the container flavour; Tek40xx (sidelined)
    rom/         system ROM (cc65) and Wozmon
    demo/        programs in C -> fs/SYSTEM/BIN and fs/APPS/NAME/*.prg  (the editors, TELNET, BUG, the games)
    forth/       Tali Forth 2 (vendored) + the platform file
    mon/         SUPERMON, the machine-code monitor (vendored, ported)
    tube/        Richard Russell's BBC BASIC, console edition (vendored, altered as marked)
    cpm/         RunCPM (vendored, unmodified; sidelined: built, not in the images)
    pascal/      the Mad Pascal target
    fs/          the machine's filesystem: /SYSTEM /LANG /APPS /HOME /CPM /MNT (fs/HOME/README.TXT)
    test/        tests, headless capture and benchmark tools
    tools/       the build's generators, romfree.py (what is left in each ROM bank), the k4510-* helpers
    data/        the screen font (unscii, 8x8 and 8x16), tiles, sprites
    doc/guide/   the handbook: source, style, generators, and the built PDF
    docs/        design records and the build diary (docs/README.md maps them)

## Licence

GPL-2.0-or-later for the project (full text in `LICENSE`); Copyright
(C) 2026 Michael Longval. Components and their terms are listed in
`LICENSES.md`, the thanks in `CREDITS.md`. "BBC BASIC"
is the name of Richard Russell's interpreter; it appears here only to
identify what is vendored, and this project claims nothing in it.

This is a hobby machine offered as a gift, and it comes with **no
warranty of any kind** — see sections 11 and 12 of `LICENSE`. It is a
toy, and it is allowed to be wrong. Keep backups.

Constructive comments can be left at the repository's issues page. All
complaints, criticisms and negativity can be addressed to `/dev/null`.
