# Decision, 2026-09-07: one shape, and the OS language

*Doc's ruling on two questions put in `suggestions.txt`, after an
assessment in the coding session. Both agreed the same evening. This
note records what was decided and why; `BUILD-LOG.md` records what was
then done.*

## 1. The machine is delivered ONE way: Linux underneath, the emulator on top

**Decided.** Every K4510 is the emulator running on a Linux that is
there to hold it up. There is no longer a bare-metal build, and "the
desktop emulator" is not a product of its own.

- **The bare-metal Raspberry Pi port (BMC-K4510) is dropped.** Every
  feature since 2026-08-25 cost twice: a Pi build that stayed
  "UNVERIFIED" for weeks at a time, one build host (p15) with a
  toolchain that must never be mixed, no `<stdatomic.h>`, no TLS, a
  network port never run, a Tube rewritten for GPIO, PSU under-voltage
  chased as a software fault, a frame budget fought over vsync. What it
  bought was a boot about three seconds faster and the romance of no
  operating system. The T480 stick boots to the prompt in about ten
  seconds and loads to RAM. Not a trade worth a second port of
  everything. The last tree carrying `pi/` is tag `alpha-0.5`.
- **"Emulator only" is not dropped, it is reframed.** The program that
  runs on the T480 and the program that runs in a window on hdieu are
  the same binary — `build-live.sh` builds `sdl/k4510` inside its
  chroot. What stops is *advertising* the desktop build as a product.
  It remains how the machine is developed and tested (`make test`, the
  headless harness, the container flavour, the WASM build), and none of
  that is to be lost.
- **A Raspberry Pi still runs the machine — under Linux.** A Pi 4 is
  the target if one is wanted: `build-live.sh` with an arm64 root and
  Debian's `raspi-firmware`, KMS on vc4. A Pi 3 will probably work the
  same way and is no longer a requirement; nobody spends a day proving
  it.
- **The names collapse.** *K4510* is the machine. *K4510x* is the
  appliance you boot. "BMC-" is a fossil: the GitHub repository keeps
  its name until the next push, after which it should be renamed
  (GitHub redirects the old one). `docs/NAMING.md`'s three-way split is
  superseded by this note.

**The line to hold, now that Linux is always there.** Stockfish on the
Tube, telnet, `!`, passwordless sudo — all fine, *as long as the machine
stays a machine*: the ROM self-contained, Linux reached only through the
Tube and the `!` escape, nothing the ROM needs at boot living on the
Linux side. The moment K/OS needs bash to start, it is a terminal, not a
computer.

## 2. The OS language: BAT boots, REXX automates, BASIC builds programs

**Decided.** The layering is the Amiga's: a tiny boot script language,
a real scripting language that can talk to programs, and application
languages that are not the shell.

| Layer | Language | Rule |
|---|---|---|
| Boot glue | `.BAT` (K/OS's own, resident in ROM) | A list of commands. **Do not grow it** — no IF, no GOTO, no variables. Every DOS that did ended with two half-languages. |
| Scripting and automation | **REXX** (`RX`, `fs/RX/`) | The system scripting language. `ADDRESS COMMAND` / `ADDRESS TUBE`, and the file port (`NAME @file`) for driving a program — the ARexx pattern, which nothing else on the list can do. Runs on the 45GS02, so the machine stays the machine. A `.RX` beside a command name runs as that name (`try_rx`), so `STARTUP.BAT` can call one. |
| Programs | EhBASIC, MS BASIC, Mad Pascal, C, Forth | What people write software in. **Not the shell.** BASIC-as-shell is the C64 model and K4510 is deliberately not that. |

Rejected, and why:

- **EhBASIC as the OS language** — a fundamental redesign the ROM has no
  room for (the banks are full; MS BASIC lives at `$7000` as a program
  for the same reason), and the wrong model for a machine with a DOS-like
  shell, a filer and a trash.
- **Growing BAT into a language** — see the table.
- **Forth** — on the shelf (`forth/`); Doc does not write Forth.
- **bash through `!`** — the tempting one now that Linux is underneath,
  and the one that turns the computer into a terminal. No.

What REXX must earn over the next weeks, by being used (a feature is not
done until something is written in it): ports on more programs (RANGER,
VI, the filer), a stem of the machine's registers so scripts stop
PEEKing magic numbers, error text with line numbers, and a handbook
chapter. None of that is ROM work — the other reason it is the right
choice: it can keep growing without the ROM budget.

## What this changed in the tree, the same evening

- `pi/` and `install-sd.sh` removed; the `K4510_PI` branches stripped
  from `sdl/main.c`, `core/io.c`, `core/term.c`, `core/tube_cp.c`,
  `core/ui/settings.c`, `core/ui/menu.c`; the Pi-only "Sound on core 3"
  setting gone with them.
- `$D522` now answers what is beneath the machine — 0 a desktop,
  1 K4510x — and `INFO` says which; the F7 Info row does the same.
- `README.md`, `CREDITS.md`, `LICENSES.md`, `THIRD_PARTY_SOURCES.md`
  say the port is retired and where its last tree is.
- Left for the handbook session (its files): `docs/NAMING.md`,
  `docs/CAPABILITIES.md` §12, `docs/TODO.md`'s Pi items,
  `docs/PORTABILITY.md`, the handbook's Pi chapter, and the issue
  template it generates.

## Addendum, 2026-09-08 — one name

Doc: "drop all naming differences to go by ONLY K4510". The "K4510x"
name is withdrawn. The machine is the K4510; that it boots on its own
minimal Linux is implicit. The desktop-window emulator is the case that
gets named when it matters ("on a desktop"). And "drop restrictions on
host access in plain emulator": the `!` host shell is fitted on every
build. The podman container keeps its own wall.
