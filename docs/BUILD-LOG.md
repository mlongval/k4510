# BMC-K4510 — Build Log

Dated record of what was actually done, as opposed to what was
planned. `K4510-Design.md` is the plan; this is the diary. Newest
entry last.

---

## Where things live

| What | Where |
|---|---|
| Design docs (this folder) | `k4510/docs/` in the main repo since 2026-08-24; old standalone docs repo archived at ubuntu-s1 `~/Projects/BMC-K4510/` |
| **Code** — own emulator, branch `master` | **t480i5** `~/Projects/BMC-K4510/k4510/` (mirror: ubuntu-s1 `~/LocalRepositories/k4510.git`) |
| Build | `./build-desktop.sh` in that repo |
| Run | `cd third_party/vice-3.3/data/K4510 && ../../src/xk4510 +sound` |
| Pi image builds (not started) | p15, when Phase 0's Pi half begins |
| Xemu (donor CPU core) | scratch clone; take `xemu/cpu65.{c,h}` fresh when Phase 2 starts |
| MEGA65 ROM for Xemu oracle | ubuntu-s1 `/media/doc/Internal_3TB/Emulation/Mega65/MEGA65.bin` |

**Workflow decision (2026-08-21):** develop on the desktop first, Pi
later. VICE 3.3 builds in ~2 min on the t480i5 and runs in an SDL
window on the machine Doc sits at, so there is nothing to copy. p15
is reserved for the Circle/ARM kernel-image builds, which are the
heavy ones. X forwarding over the tailnet was considered and rejected
for a 60 fps emulator.

---

## 2026-08-21 — Phase 0 (desktop half) and Phase 1 skeleton

### ASK-7: Xemu core audit — done, clean
See `ASK-7-xemu-audit.md`. Callbacks throughout; three hypervisor
sites; 6502-style reset. Phase 2 is a wrapper, not a port.

### Phase 0, desktop half — done
bmc64 `v5.0.2`'s VICE 3.3 builds on Fedora 43 (GCC 15) and `x64sc`
boots to `READY.`. What a 2018 tree needs on a 2026 compiler:

- **`./autogen.sh` first** — checked-in `aclocal.m4` is older than
  `configure.ac`, so a bare `make` tries to regenerate and fails.
- **`-fcommon`** plus `-Wno-error=incompatible-pointer-types`,
  `-Wno-error=implicit-function-declaration`,
  `-Wno-error=int-conversion`. All captured in `build-desktop.sh`.
- **One real source fix** (commit `e4e18301`):
  `arch/gtk3/archdep_unix.h` *defined* `const char *archdep_pref_path`
  instead of declaring it; the SDL build includes a gtk3 `.c` that
  pulls it in, and `const` defeats `-fcommon`. → `extern`.
- `vsid` and `c1541` fail on genuine tree bugs. Not built, not wanted.
- Build time: 1m46s, 8 threads.

### Phase 1 skeleton — done (commit `027fa0d6`)
`xk4510` exists and boots the C64 ROM.

- `src/k4510/` = the 60-file `libc64sc` set copied verbatim, names
  unchanged for now. `machine_name = "K4510"`, own log channel.
- **Deliberate crutch:** `machine_class` stays `VICE_MACHINE_C64SC`.
  Thirteen shared files switch on it (VIC-II resources, SID, CIAs,
  joystick, autostart…); keeping the value means they treat the new
  machine as a C64 and it boots with zero shared-code changes.
  `VICE_MACHINE_K4510 = 14` is reserved in `machine.h` for when the
  machine has hardware of its own.
- `data/K4510/`: kernal/basic/chargen + SDL keymaps/palettes.
  ROMs are found via `sysfile_init(machine_name)` → `data/<name>/`,
  so the directory name matters.
- Registered in `configure.proto`, `src/Makefile.am` (an `xk4510`
  target modelled line-for-line on `x64sc`), `data/Makefile.am`.

### VICE-isms learned the hard way
- **`configure.ac` is generated.** `autogen.sh` rebuilds it from
  **`configure.proto`**. Edits to `configure.ac` silently vanish.
- **`make -j` has a race** in the drive-library subdirs: two recursive
  makes compile the same `.o`, and `ar` bus-errored once. Rerun.
- ROMs are looked up relative to **CWD** and the data dir; the
  `-directory` option is the fsdevice path, not the ROM path.
- `-limitcycles N -exitscreenshot file.png` with
  `SDL_VIDEODRIVER=dummy` is the headless boot test. Exit code is 1
  on success (cycle limit reached), not 0.

### Launching onto the t480i5 desktop from ubuntu-s1 over SSH
```
cd .../data/K4510 && XDG_RUNTIME_DIR=/run/user/1000 \
  WAYLAND_DISPLAY=wayland-0 SDL_VIDEODRIVER=wayland \
  nohup ../../src/xk4510 -default +sound &
```
XWayland route needs `XAUTHORITY=/run/user/1000/.mutter-Xwaylandauth.*`;
native Wayland is simpler.

### Open bug: reSID segfault with sound enabled
`reSID::SID::clock_resample` at `sid.cc:1039`, 32768-sample buffer,
both under `SDL_AUDIODRIVER=dummy` and on the real desktop. Unrelated
to the machine work; `+sound` sidesteps it. Must be fixed before
anyone hears a SID. Suspect: the `-fcommon`-era merging of an
uninitialised global (a VIC-II log line is also tagged "Sampler
Filedrv", which is the same symptom — two `log_t`s merged).

### Not yet done
- **The strip.** Nothing has been removed from `src/k4510/` yet.
- Phase 0's Pi half (stock `kernel8-32.img` on the 3B+, benchmark).

---

## Next baby step

**Make `xk4510` boot with the cartridge system gone.** Not the whole
not-carried-in list — just cartridges, because it is the largest
inheritance, the one wired deepest into `c64memsc.c`, and removing it
forces the first real Phase 3 decision (what a read/write of `$xxxx`
does on this machine) while the C64 ROM is still there to say whether
it broke.

1. `c64memsc.c`: remove cart hooks (`cart_*`, roml/romh, Ultimax,
   I/O1–2 cart decode) and the RAM hacks (`plus60k`, `plus256k`,
   `c64_256k`, `c64-memory-hacks`). Delete those files from
   `libk4510`.
2. Drop `libc64cartsystem`/`libc64cart`/`libc64commoncart` from
   `xk4510_libs`. The linker's complaint list *is* the audit.
3. Stub or delete until it links. DigiMAX and the FM chip are
   implemented *as* carts — lift out, don't delete (E-01).
4. Boot. `READY.` with no cartridge code in the binary is the exit.

Tape, TDE, disk images, RS232, printer, MIDI come out in one sweep
afterward — they are shallow. Cartridges are the only deep one.

---

## 2026-08-21 (late) — Pivot: own core, VICE dropped

### The question
Half-way into the cartridge strip, Doc asked what of VICE we are
actually keeping and why we are going through it at all. Honest
accounting: as an *emulator*, two libraries (reSID, fmopl). Everything
VICE emulates — CPU, memory, video, carts, drives — we had already
decided to replace. What remained was VICE as a *framework*, and the
strip was showing what that costs: C64 assumptions in every file,
`viciisc` with cartridge hooks, SDL menus hard-linking cart code.

### The finding that settled it
The assumed reason for staying — "BMC64's Pi layer is a Circle port
of VICE, so we need VICE to get the Pi" — is **false**. BMC64 already
hosts a non-VICE emulator (plus4emu, `Makefile-Plus4Emu`) through a
defined interface:
- `third_party/common/emux_api.h` — 78 `emux_*` functions a core
  provides (attach, reset, joystick, frame buffer…)
- `third_party/common/circle.h` — 62 `circle_*` functions the Pi
  layer provides (framebuffer, audio, input, SD/FAT)
- `third_party/common/` ≈ 11k lines of Pi-hardened plumbing: menus,
  virtual keyboard, joystick config, CRT shaders, GPIO.
BMC64's Pi layer is emulator-agnostic. VICE is one tenant; plus4emu
is another; we will be the third.

### New shape
- `k4510core/` — Xemu `cpu65.c` (unchanged), our memory, VICKY,
  reSID + fmopl as libraries. Plain C, no VICE headers.
- Desktop frontend: SDL2 directly, a Makefile, no autotools.
- Pi frontend: implement `emux_api.h` against BMC64's common layer,
  as plus4emu does (its glue is 9 lines + the `emux_*` bodies).

### What is lost
VICE's monitor (23k lines; did not know the 45GS02 anyway — Xemu's
`emutools_umon.c` does), VICE's snapshot/resources framework (we
write small ones), and the `xk4510` skeleton from earlier tonight
(kept on the `k4510` branch as history; sunk cost).

### Decision gate: the spike
Nothing else until this runs:
**`cpu65.c` + 64 KB RAM + one VICKY text layer in an SDL window +
Wozmon in ROM + typing works. No VICE anywhere.**
If it is a few hundred lines and runs, the pivot is confirmed by a
program rather than an argument. If it is a swamp, we learn that
cheaply. The bmc64 `k4510` branch stays as the fallback.

---

## 2026-08-22 (overnight) — The spike ran. Pivot confirmed.

**Gate:** `cpu65.c` + 64 KB RAM + one VICKY text layer in an SDL window
+ Wozmon in ROM + typing works. No VICE anywhere.
**Result: passed, all four steps, in one night.** 961 lines of ours
around 3,000 of Xemu's. Screenshot: `images/spike-wozmon-2026-08-22.png`.

### Where the code is now
**t480i5 `~/Projects/BMC64k4502/k4510/`** — a new git repo, 4 commits.
The bmc64 fork (`k4510` branch) is now history only.

```
k4510/
  core/xemu/    cpu65.c cpu65.h timings, disasm tables  <- Xemu, byte-for-byte
  core/xemu/emutools_basicdefs.h   the shim: 60 lines, the core's whole environment
  core/hypervisor.h                in_hypervisor = false (constant)
  core/mem.c/.h                    64 KB RAM, the 11 callbacks, ROM protect, keyboard regs
  core/vicky.c/.h                  one 80x60 text layer, 8x8 glyphs, ASCII indices
  sdl/main.c                       SDL2 window, 40.5 MHz/60 Hz, keyboard -> FIFO
  rom/wozmon.a                     Wozmon retargeted; ACME --cpu m65; 4 KB at $F000
  test/cputest.c  test/woztest.c   both ALL OK
  data/chargen                     VICE's C64 font, reordered to ASCII at load
  Makefile
```
`make test` runs both tests. `./sdl/k4510` from the repo root runs it.
**Shift+Esc quits, F12 resets**, Backspace = Wozmon `_`, input is folded
to uppercase (1976 monitor).

### Step 1 — CPU (commit 187ecc1)
- The core compiles unchanged against the shim. Needed `Uint64` too.
- `flat_address()` for `[$nn],Z` / Q forms must **fetch its own operand
  byte** (`cpu65_read_callback(cpu65.pc++)`) — the core has not consumed
  it. I got this wrong first and caught it by reading Xemu's
  `memory_mapper.c:1014` before running. Mirrors upstream exactly now.
- `cpu_mega65_opcodes = 1` enables the extensions.
- Test: 6502 counter, 65CE02 `LDZ`/`INW`, 45GS02 `LDA [$20],Z` + `LDQ`/`STQ`.
  **The 32-bit flat pointer and Q ops passed on first run.** One test
  bug of mine (`$9B` is `STX $nnnn,Y`, 3 bytes, not a NOP).

### Step 2 — pixels (commit 38563c9)
- `vicky_render()` reads screen RAM at `$0800` every frame into an 8-bit
  framebuffer; the frontend owns palette and window. Two colours, C64
  blues, for now.
- Font: ASCII-ordered 8x8 built at load from `chargen` (uppercase set).
  PETSCII has no backslash, so `\` shows as `£`. Own font later.

### Steps 3+4 — Wozmon, typing (commit 0505f40)
- **ACME:** Fedora has none; the GitHub mirror is 0.96 (2019) and lacks
  `m65`. Built **0.97 from SourceForge svn** into `~/.local/bin/acme`
  (`svn export https://svn.code.sf.net/p/acme-crossass/code-0/trunk`).
  It emits exactly the bytes hand-assembled in step 1.
- Woz's logic is verbatim (source: jefftranter/6502 wozmon.s). Only
  `ECHO` (now a screen-RAM terminal with scroll) and the keyboard
  register are ours. `ECHO` **must preserve A, X, Y** — callers depend
  on it; PHA/PHX/PHY.
- Spike I/O map: ROM `$F000-$FFFF` write-protected; `$D010` key|`$80`,
  `$D011` bit7 ready (Apple-1 shaped because Wozmon polls it that way).
  **This is scaffolding, not the K4510 I/O map.**
- Authentic Wozmon quirks seen and kept: prints `\` on reset; a
  `0300:41 42 43` store line first prints `0300: 00` (the XAM before
  the `:` switches mode).
- `woztest`: boots, `FF00.FF0F`, store, read back, `400R` runs planted
  code (`$0700` = `$99` afterwards). ALL OK.

### What this settles
The VICE route is dead, by demonstration. Phase 0, Phase 1 (skeleton)
and the Phase 2 wrapper are done in the new shape. The Plan half of
`K4510-Design.md` needs rewriting around this repo; the capability
matrix stands.

### Still scaffolding (to be replaced, not extended)
- 64 KB flat RAM, MAP ignored → Phase 3: 256 MB + 28-bit MAP.
- Apple-1-shaped keyboard regs → the real K4510 I/O map.
- C64 chargen → own font, lowercase, backslash.
- Two-colour VICKY with fixed geometry → register block, colour RAM.
- Uppercase fold in the frontend → the ROM's problem, not SDL's.

### Phase 3 spine, same night (commit d2b59d9)
**256 MB and the 45GS02 MAP, in `core/mem.c`. `test/maptest` ALL OK.**
- Physical RAM is one `mmap(MAP_NORESERVE)` of 256 MB. Reserving it
  moved host RSS by 64 KB; after the tests touched pages across the
  whole range, by ~100 KB. Lazy commit is free from the kernel.
- MAP semantics taken from Xemu's `memory_mapper.c`, not a datasheet:
  `X==$0F`/`Z==$0F` select the megabyte per half; otherwise A/X, Y/Z
  give a 20-bit offset and a per-8KB-block mask;
  `phys = mb + ((offset + cpu_addr) & $FFFFF)`. I/O and ROM exist only
  in the unmapped view (as on the C65).
- Verified: block 1 → `$40000`; megabyte 5 into zero page; `STA/LDA
  [ptr],Z` at `$0FFFFFFF`; `STQ` at 8 MB.
- **Gotcha recorded:** the mask nibble is one bit per 8 KB block, and
  MAPping block 0 unmaps code at `$1000`. Test programs live at `$C000`.
- Wozmon still runs unchanged on top of it (`woztest` passes).

### After the spike, same night (commits 1c4ddac, e491dc7)
- **Own font.** `data/mkfont.py` builds `data/font8.bin` (ASCII-ordered,
  256×8) from the chargen's *second* set, which has lowercase; `\ ` { | } ~`
  drawn by hand. Wozmon's reset `\` is a backslash now.
- **`core/io.c`** owns the `$D000` page: dispatch by `$100` device page,
  keyboard FIFO at **`$D100`** (moved from the Apple-1 `$D010`; Wozmon ROM
  follows), and **block DMA at `$D200`** — SRC/DST/LEN 28-bit LE, CMD 1
  copy (memmove), 2 fill, 3 swap, instant. `test/dmatest`: 64 KB from
  1 MB to 255 MB, screen fill, overlap-safe scroll, bad-cmd status. ALL OK.
- **The I/O map in `io.h` is the PLAN-v2 proposal and is PROVISIONAL.**
  Every base is one constant; moving a device is a one-line change.
- `make test` = 4 tests, all green. Six commits on the new repo.
- Safety net: bare mirror at ubuntu-s1 `~/LocalRepositories/k4510.git`
  (remote `ubuntu-s1` on the laptop; `git push ubuntu-s1`). GitHub not
  pushed — waiting for Doc.

## Morning summary, 2026-08-22

What exists now: **a K4510 that boots its own ROM on a 45GS02 with 256 MB,
MAP, DMA and a text display, in 1,300 lines of ours + Xemu's core, with
four headless tests.** The window on the t480i5 is running it. Type hex
at Wozmon: `F000.F01F` dumps the ROM; `0300:41 42 43` stores; `300.302`
reads back; `400R` runs. Shift+Esc quits, F12 resets.

Decisions waiting for you, in order of how much they block:
1. **The `$D000` I/O map** (`PLAN-v2.md` §3, `core/io.h`). Agree, amend?
2. **`PLAN-v2.md`** itself — edit it, then it replaces the Plan half of
   the design doc.
3. Push `k4510` to GitHub (private)? Name: `mlongval/bmc-k4510`?

Next baby step, my pick: **VICKY palette + 8 bpp bitmap layer with a
register block at `$D000`** (VICKY-SPEC §13 step 1). That retires the
spike's fixed text layer as the only display and starts the real chip.

---

## 2026-08-22 — I/O map agreed, plan merged, GitHub, circle-libsdl2

Doc: I/O map stands as proposed; merge the plan; push.

- **I/O map agreed.** `core/io.h` is authoritative; "PROVISIONAL"
  notes there are now wrong and removed in the next code commit.
- **Plan merged** into `K4510-Design.md` as the Plan half; the VICE-route
  plan moved to `sources-superseded/PLAN-v1-vice-route.md`. `PLAN-v2.md`
  deleted. PDF/EPUB rebuilt.
- **circle-libsdl2** (github.com/Xalior/circle-libsdl2, found by Doc):
  from-scratch SDL2 for bare-metal Circle, Pi 3/4/5 AArch64, zlib,
  proven by a MAME port. Implements exactly what `sdl/main.c` uses
  (software renderer, streaming ARGB8888, scaled RenderCopy, HID,
  audio callback, RWops). **Phase 6 Route A is now "recompile our own
  frontend"**; BMC64 `emux_api` becomes Route B, the fallback. Risk is
  one author / no community, not the code. Toolchain: Arm GNU
  `aarch64-none-elf`, on p15 when the time comes.

## 2026-08-22 night 2 — VICKY, step by step

### VICKY step 1 (commit 0105db3) — palette + bitmap + text behind registers
`core/vicky.c` is a chip now: 256-byte register file at `$D000` (see
`vicky.h` header comment for the map), palette write port with
auto-increment, four 16-byte layer groups, per-scanline composition.
Bitmap 1/2/4/8 bpp; tile/text 8x8 1-bpp from a glyph set in RAM. No
video memory: font at phys `$010000` (frontend places it until the
system ROM carries it), ROM `VINIT` programs the chip at reset. The
C-side text path is gone — Wozmon is displayed through the registers.
`test/vickytest` ALL OK. 5 tests green. Pushed to GitHub.

### VICKY step 3 — tiles and text32 (pushed)
Tile mode: 8–64 px cells, 2-byte map entries (10-bit index, H/V flip,
4-bit palette offset), at the layer's bpp — 8 bpp tiles are the
full-colour tiles the spec wants. `text8` (1 byte/cell, Wozmon) and
`text32` (glyph16 + reverse + fg + bg bytes), both 8x8 or 8x16. Tested.

### VICKY step 5 — sprites (pushed)
128 sprites from a 16-byte-entry table in RAM (`$D00A` pointer): signed
X/Y, 28-bit data, 4/8 bpp, H/V flip, width and height 8–64 chosen
independently, Z-slot (drawn after layer 0–3). Sprite-sprite and
sprite-layer collision bitmaps at `$40`/`$50`, clear-on-read. Tested.

### VICKY step 6 — copper and interrupts (pushed)
Copper list in RAM (`$D060` pointer): END/WAIT/MOVE/SKIP/JUMP/IRQ, run
at each scanline start, restarts per frame. IRQSTAT/IRQMASK at `$04/05`
with vblank, raster-compare, copper, collision; ack by writing 1s. The
frontend now steps the CPU **per scanline** and drives `cpu65.irqLevel`
from the chip — raster IRQs work for real. Tested. Wozmon still runs.
- **The coprocessor is named SHEILA** (Doc). Renamed in code, spec and
  design doc; `VR_SHEILA`/`VR_SHEILACTL`/`VI_SHEILA`.

### VICKY step 7 — blitter (pushed)
`$D070-$D082`: SRC/DST 28-bit, W/H, strides, ops copy/keyed/fill/AND/
OR/XOR, H/V flip, instant, 8 bpp. Tested.

**VICKY status after night 2:** every item in VICKY-SPEC §13 steps 1–7
exists and is tested — palette, bitmap 1/2/4/8 bpp, tiles 8–64 px with
flips, text8/text32 at 8x8/8x16, 4 layers, 128 sprites with collisions,
SHEILA, blitter, IRQs, scanline-granular CPU. Not yet: active-area/
border registers, layer width/height clipping, per-layer Z register
(layers are in fixed order 0–3 with sprites interleaved by Z-slot),
sub-line SHEILA WAIT, 4-bpp blits, HAM/blend stretch items.
`make test` = 5 tests, all green. Repo: github.com/mlongval/bmc-k4510.

### Demo ROM and screenshots (pushed, d71d9e2)
`rom/demo.a`: SHEILA gradient, text, bouncing 8-bpp sprite — all from
45GS02 code. `./sdl/k4510 rom/demo.bin`. `test/capture ROM FRAMES OUT.png
[keys]` renders any ROM headless to PNG. Screenshots in `images/`:
`demo-2026-08-22.png`, `wozmon-2026-08-22.png`. Two bugs were in the
demo ROM, not the chip (Y-index wrap at 256 in the list builder; ASL
carry leaking into an address).
**Semantics change:** index 0 is transparent in every layer; BGCOL is
the ground (spec updated). The old rule hid SHEILA gradients under text.
The demo is running on the t480i5 desktop.

### Phase 4b — sound (pushed, dd66854, b8e1bf7)
`core/resid/`: reSID from VICE 3.3, vendored unchanged (C++, needs
`-DVERSION`). `core/sid.cc` wraps four chips at `$D400` (`$20` each);
`sid_render(cycles)` mixes them to 16-bit mono. SDL frontend: ring
buffer fed per scanline, drained by the audio callback at 48 kHz. The
demo plucks a triangle note on SID 0 at reset. `test/sidtest`: silence
is flat after the filter's reset transient; a sawtooth via the I/O page
has energy and oscillates. **6 tests green.** Not yet: OPL2 (fmopl),
PCM, stereo, model select per chip from software.

## Morning summary, 2026-08-22 (second night)

**What exists now:** a K4510 with every capability in VICKY-SPEC §13
steps 1–7, four SIDs, 256 MB + MAP, DMA, a Wozmon ROM and a demo ROM,
on GitHub with six headless tests. Lines of ours ≈ 2,600; vendored:
Xemu's core (3k) and reSID (3k). **The demo is running on the t480i5
desktop with sound**; `images/demo-sheila-2026-08-22.jpg` is what it
looks like.

Done tonight, in order: I/O map agreed → plan merged into the design
doc → GitHub → VICKY registers/palette/bitmap/text → tiles/text32 →
sprites → SHEILA (named by Doc) + IRQs + scanline CPU → blitter →
index-0-transparent fix → demo ROM + capture tool → reSID + audio.

Decisions/checks for Doc:
1. **Two spec deviations I made, flagged:** index 0 is always
   transparent (was "opaque in the bottom layer"); layers are in fixed
   Z order 0–3 with sprites interleaved by Z-slot (spec wanted a per-
   layer Z register — cheap to add, ask).
2. **Keyboard is still Wozmon-shaped** (`$D100` ASCII|$80 + ready bit).
   A real K4510 keyboard (scancodes, modifiers, key-up) is the next
   I/O item and wants a decision on layout (US-International policy).
3. OPL2 and PCM are the remaining sound items; straightforward.

Next baby step, my pick: **the system ROM, Stage 2** — the thing on
the critical path. A real terminal (text32 with colour, cursor,
lowercase), a keyboard driver, and a `LOAD` from the host filesystem
via `$D300` + DMA. That is the point at which the machine stops being
a demo and starts being a computer.

## 2026-08-22 — System ROM Stage 2 (pushed, f236923 + 767a124)

**The machine is a computer now.** `./sdl/k4510` boots `rom/kernal.bin`:
a colour terminal, a shell, and files from the host.

- **Toolchain:** Fedora's cc65 2.19 has no 45GS02 target, but its
  65C02 output (`BRA`, `STZ`, `PHX`, `(zp)`) is a strict subset, so the
  ROM is **C with cc65** (`--cpu 65c02 -t none`, own `k4510.cfg`,
  `crt0.s`). 5 KB of code in an 8 KB ROM at `$E000`. ACME stays for
  pure-asm ROMs (Wozmon, demo).
- **ROM:** `text32` terminal (colour, cursor blinks from the vblank IRQ,
  scroll by DMA), keyboard driver, shell with Wozmon's grammar
  (`addr`, `addr.addr`, `addr:b b`, `addrR`) + `LOAD name [addr]`,
  `SAVE name from.to`, `DIR`, `CLS`, `HELP`. Jump table at `$FF80`:
  CHROUT/CHRIN/GETIN/LOAD/SAVE.
- **Machine:** ROM up to 8 KB, top-aligned load. **Keyboard v2** at
  `$D100` (ASCII or `$80+` codes, modifiers in status; host layout does
  the dead keys on the desktop). **Host filesystem at `$D300`**:
  OPEN/READ/WRITE/CLOSE/DIR/STAT + LOAD/SAVE conveniences, sandboxed to
  `fs/` (second argv).
- **Lessons:** (1) cc65 C must never run inside an IRQ — it clobbers
  zero-page temporaries; the blink handler is assembly in `crt0.s`.
  (2) DMA copy is memmove-safe, so it does not replicate a seed row;
  `cls` copies row 0 to each row. (3) `$0800–$5300` is the text32
  screen; loading a file there paints its bytes as cell colours.
- **8 tests green**: cpu, woz, map, dma, vicky, sid, fs, rom.
- **Screenshots of all tests** in `images/`: `tests-2026-08-22.png`
  (console output), `test-*-2026-08-22.png` (one per VICKY stage and per
  ROM), `tests-visual-2026-08-22.jpg` (contact sheet),
  `rom-stage2-2026-08-22.jpg`.

Next: OPL2 + PCM; a per-layer Z register; the ROM's `LOAD` should set
the run address from a header; ASK-6/ASK-9 still open.

## 2026-08-22 (night) — Three demos, and the bug they found

Doc asked for three demo programs: 15 bouncing balls, a rotating
wireframe/solid cube (double buffered), and a Mandelbrot. All three are
C with cc65, in `k4510/demo/`, built as 8 KB ROMs (`make demos` →
`rom/balls.bin`, `rom/cube.bin`, `rom/mandel.bin`; run with
`./sdl/k4510 rom/balls.bin`). Screenshots: `images/demo-balls-*`,
`demo-cube-wire-*`, `demo-cube-solid-*`, `demo-mandel-*`,
`demo-mandel-zoom1-*` (PNG) and `demos-2026-08-22.jpg` (contact sheet).

- **balls** — 15 × 32×32 4-bpp sprites from **one** bitmap, 15 palette
  banks via per-sprite PALOFS; the sprite table is double buffered by
  flipping `SPRTAB`; SHEILA paints the sky gradient (16 MOVEs) and the
  floor. 640×480.
- **cube** — 320×240 in the **new lowres mode** (VICKY CTRL bit1: every
  pixel doubled; layers and sprites see 320×240, RASTER/SHEILA still
  count 480 lines). Two 4-bpp frame buffers at `$110000`/`$120000`,
  reached through the 45GS02 **MAP window** (`demo/crt0.s
  map_window()`: CPU `$2000-$BFFF` → any physical address; the
  megabyte MAP first, then offset+mask, then EOM). DMA fill clears, the
  flip is one write to the layer DATA pointer. 300 frames wireframe
  (all 12 edges), 300 frames solid with back-face culling, scanline
  fill and visible edges in white.
- **mandel** — 320×240 8 bpp at `$200000`, drawn row by row through a
  RAM buffer + DMA so you watch it. No multiplier on a 45GS02, so
  squares come from a 32 KB table of 4.12 squares built at start and
  MAPped in; `2xy = (x+y)² − x² − y²`. Cardioid and period-2 bulb tests
  skip the interior. ~16 s per image, then 3 s of palette cycling, then
  the next zoom level (5 levels into the seahorse valley).

**Bug found by the balls (real VICKY bug):** the collision registers
`COLSS $40-$4F` / `COLSL $50-$5F` sat exactly on **layer 3's register
block** (`$10 + 3×$10 = $40`). The first time two balls touched, the
collision bits "enabled" layer 3 with garbage settings and the screen
filled with junk. Moved to **`COLSS $90-$9F`, `COLSL $A0-$AF`**
(vicky.h, spec). Tests still 8/8.

Other lessons: (1) DMA fill takes its byte from the **SRC register's
low byte**, not from memory at SRC. (2) SHEILA instructions are 4 bytes
each, padded. (3) `test/capture` now prints the final PC/SP/IRQ state —
useful for "is it hung or is it rendering wrong". (4) cc65 demo RAM is
`$0200-$0FFF`; keep tables at `$1000+` or they eat the C stack.
(5) The sandbox image viewer I use mis-decodes capture's raw
stored-deflate PNGs; run them through `magick` first — I lost half an
hour chasing a "bug" that was the viewer, before finding the real one.

## 2026-08-22 (late) — ROM Stage 3: INFO, a bigger ROM, a system device

Doc: "an INFO command with unix-like flags, no flags dumps everything,
date/time too; add more functions as you see fit; 8 KB was only a
suggestion." So:

- **ROM is now 24 KB at `$A000-$FFFF`** (`rom/k4510.cfg`: ROM1
  `$A000-$CFFF`, the `$D000` I/O hole filled, ROM2 `$E000-$FFFF`).
  `mem_load_rom` sets a variable ROM base; I/O now wins over ROM in the
  write path so the hole works. ROM bss moved to `$5300-$5FFF`;
  **user programs get `$6000-$9FFF` (16 KB)** and `LOAD` defaults there.
  11 KB of the 24 used.
- **System device at `$D500`** (`core/io.c`): CPU clock kHz, RAM MB,
  host date/time latched on read of `$D504`, 24-bit frame counter,
  version string, ROM base page. SID registers `$00-$18` now read back
  their last written value (a shadow; real SIDs are write-only) so
  `INFO -s` can show volume/gates/filter.
- **`INFO [-v -c -m -g -s -f -t]`**: version, CPU (nominal + *measured*
  clock: `speed_loop` in crt0 counts an 18-cycle loop for one frame →
  40.42 MHz), memory map + last load, VICKY state (resolution, layers,
  sprites, SHEILA, raster, IRQ mask), the four SIDs, host files, date/
  time/uptime. No flags = everything. Screenshot
  `images/rom-stage3-info-2026-08-22.png`.
- **New commands:** `TYPE name`, `RUN [addr]` (defaults to the last
  LOAD), `FILL from.to value`, `COPY from.to dest`, `COLOR fg [bg]`,
  `ECHO`, `TIME`, `RESET`. Commands are case-insensitive.
- **Wozmon grammar is 28-bit now:** examine/store/block beyond 64 KB go
  through DMA (`peek`/`poke` copy one byte via `$D200`), so
  `1000000.100000F` and `1000004:55` work at 16 MB. Addresses print as
  8 hex digits.
- The banner prints the date and time at boot.
- romtest extended (FILL/TYPE/INFO/28-bit access); its user program
  moved from `$0700` (inside the C stack — it had been getting away
  with it) to `$5F00`. 8/8 green.

Next candidates: the `.prg` header + `RUN` for the demos as loadable
programs (the step I proposed); per-layer Z; OPL2.

## 2026-08-22 (morning) — Programs: LOAD, RUN, and back

The step I proposed: what a *program* is on this machine.

- **`.prg` format:** 4-byte header — load address, run address — then
  the image. `LOAD name.prg` honours it (`LOAD name.prg addr` overrides
  the load address); `RUN name.prg` loads and runs; `RUN` alone reruns
  the last one; `RUN addr` still works. The ROM re-initialises VICKY
  (including the first 16 palette entries) and clears the screen when
  the program returns.
- **Program environment** (`demo/prg.cfg`, `demo/prg0.s`): code at
  `$6000-$9FFF`, own zero page `$40-$63` and own C stack at the top of
  that range, so the ROM's state survives; the ROM's IRQ keeps running.
  vblank comes from the `$D50D` frame counter. Any key returns to the
  shell. `prg0.s` also provides `far_poke/far_poke16/far_peek` — the
  45GS02's NOP-prefixed `STA/LDA ($zp),Z` 32-bit flat forms, the first
  use of them in our own code — and a 16 KB MAP window at `$2000-$5FFF`
  (the text screen's RAM, idle while a program runs).
- **The three demos are now `fs/balls.prg`, `cube.prg`, `mandel.prg`**
  (3.5–6.7 KB). Their big tables moved to far memory: sprite tables and
  SHEILA list written with flat stores, the cube's 8-bpp frame buffers
  drawn with flat stores and DMA-fill spans, the Mandelbrot's square
  table halved by symmetry to fit the 16 KB window. Screenshots
  `images/prg-shell-2026-08-22.png`, `prg-balls-2026-08-22.png`.
- **Bug of the day:** `RUN balls.prg` jumped to `$00BA` — "b" and "a"
  are hex digits, so the name parsed as an address. `mandel.prg` had
  worked. A token is a name if it contains any non-hex character.
- ROM bss is back in block 0 (`$0200-$07FF`) so a program may MAP
  `$2000-$5FFF`; `$5300-$5FFF` is free. RODATA moved to the `$E000`
  half of the ROM — the code half was 20 bytes over 12 KB.
- romtest covers LOAD-with-header, RUN, and the return on a key. 8/8.

Next: OPL2 + PCM (`INFO -s` already says "not fitted yet"); per-layer
Z; ASK-6/ASK-9.

## 2026-08-22 — Portability review and the 21 ms surprise

Doc asked whether to optimise before the Pi. Answer: measure, don't
optimise — and the measurement paid off. `test/bench` (headless
frame-time split) showed **reSID taking 21.5 ms of a 16.7 ms frame** on
the i5: the four SIDs were clocked at the CPU's 40.5 MHz, 41× a real
SID. SDL had been quietly running behind real time since the sound went
in. SIDs now run at 1 MHz whatever the CPU clock (`SID_HZ` in
`core/sid.cc`); reSID fell to 0.63 ms, a frame is 3.4–4.1 ms total, and
the frequency registers mean what they mean on a C64.

The review itself is `PORTABILITY.md`: host dependencies live in two
files (`mem.c`: mmap + ROM loading; `io.c`: the storage device's POSIX
file calls and `time()`), everything else — cpu65, VICKY, reSID — is
clean; alignment/endianness/pointer-size checked; proposed `core/host.h`
seam (alloc, log, clock, eight file calls) as the first commit of the Pi
work. Pi 3B+ extrapolation: 17–33 ms single-core → CPU emulation and
VICKY+SID on separate cores is probably needed, and the per-line
interface already allows it.

p15 is reachable (16 threads, gcc/make/git, no cross toolchain yet) —
the Pi build goes there.

## 2026-08-22 — Pi: the SDL-layer probe (built, not yet booted)

Doc: before cross-compiling the emulator, a test kernel that only asks
"does the SDL2 layer on the Pi do what we need?" Done on p15:

- **Toolchain:** Arm GNU 15.2 `aarch64-none-elf` in p15 `~/opt/`
  (14.2 was too old: circle-stdlib's libc++ 22 needs GCC 15 builtins).
  Plus `cmake`, `ninja`, `texinfo`. The rpi3 "world" (Circle Step51 +
  newlib 4.5 + libc++ 22, multicore, 2 MB stacks) and `libSDL2-rpi3.a`
  are built: p15 `~/Projects/k4510-pi/circle-libsdl2/`.
  Gotcha: `make world` is idempotent on `Config.mk`; after changing the
  toolchain, `rm Config.mk` and `git checkout -- build install` (the
  placeholder dirs are tracked) before rebuilding.
- **`k4510probe`** (`pi/k4510probe/` here, sources + README): a Circle
  kernel that does exactly what `sdl/main.c` asks of SDL and measures
  it — 640×480 ARGB streaming texture refilled every frame from an 8-bit
  buffer through a palette (PRESENT ms on screen), a synthetic CPU load
  (2 M-iteration integer loop ≈ 1.9 ms on the i5, and a 256 KB memory
  walk) to calibrate the Pi/i5 ratio, USB keyboard scancodes, 48 kHz
  S16 mono audio callback (SPACE = 440 Hz tone), SD card read/write via
  `fopen`, a touched 256 MB `malloc`, CPU MHz and SoC temperature. Own
  3×5 font so the HDMI picture alone tells the story; everything also on
  serial. 980 KB `kernel8.img`.
- **`pi/k4510probe-sd-2026-08-22.tar.gz`**: the complete SD card —
  firmware, `config.txt` (arm_64bit), our kernel, `k4510probe.txt`, and
  `alt/` with the library's own gradient / keyecho / tone kernels as
  fallbacks. Untar onto a FAT32 card, boot the 3B+, read the numbers.

Not yet run on hardware. The numbers to bring back: FPS, PRESENT ms,
INT LOOP ms (÷1.9 = the slowdown factor), whether keys/tone/file work.

## 2026-08-22 — First light on the Pi 3B+ (and a 2 GB card lesson)

**Run 1 (probe without SD mount): screen, scaling, USB keyboard and HDMI
audio all work bare-metal** on Doc's BMC64 box (C64 keyboard on GPIO
attached and ignored; an 8BitDo USB keyboard receiver; a USB stick;
panel 1824×984). "Flabbergasted am I." No files, because the kernel
never mounted the card — the library leaves EMMC + `f_mount("SD:")` to
the host kernel, unlike everything else it brings up itself.

**Run 2 (with the mount): hung in boot messages.** Photo in
`screenshots/IMG_6002.jpeg`. The log shows why: `emmc: Capacity is 967
MBytes` for a 1.9 GB card, then `error sending CMD24 … Giving up`.
Circle's SD driver computes CSD-v1 capacity without `READ_BL_LEN`
(1024 on 2 GB SDSC cards) — half the size, and block writes fail. The
write-retry loop stalled the program before `SDL_Init`. **Use an SDHC
card (≥ 4 GB).** The probe now turns file logging off after the first
failed write and says so on screen, so a bad card still shows the
picture and the numbers.

## 2026-08-22 (evening) — The Pi numbers

Three more boots on the BMC64 box (Pi 3B+, Sharp 1080p TV, USB
keyboard). Photos `screenshots/IMG_6003.jpeg`, `IMG_6004.jpeg`.

**Run 3 (4 GB SDHC, TV at 1080p):** picture, sound, keys — and the
first real numbers. INT LOOP 2M = **8.74 ms** (i5: 1.9 → the Pi is
**4.6× slower**, better than the 5–8× guessed). MEM WALK 256K 0.97 ms.
But PRESENT = **22.8 ms**: the library scaling 640×480 onto 1824×984
(1080p minus the firmware's overscan margins) cost more than a frame;
29.8 fps. Files still failed — wrongly blamed on the card by my own
message.

**Run 4 (config.txt: `hdmi_group=1 hdmi_mode=1 disable_overscan=1`, the
TV driven at 640×480 and upscaling itself):** PRESENT **6.8 ms**, frame
**17.2 ms / 58 fps** *including* ~9.6 ms of synthetic load. Card: mount
0, open 0, create 0, 3781 MB, "valid version 3.0x SD card"; read and
write OK. So the 2 GB card really was the earlier problem, and the
reformatted 4 GB (ex-BMC64, backed up on p15) is fine. Per-second
logging stopped after the setup lines — one failed append and my code
gave up; now it retries and shows ok/failed counts on screen.

**What this means for the port:** the emulation core (3.4–4.1 ms on the
i5) should land around **16–19 ms on one Pi core** — marginal alone,
comfortable once CPU emulation and VICKY+SID run on separate cores
(the library's core split exists for exactly this). Presentation at
640×480 output is ~7 ms on core 0, or free to the application core
with the split's presentation worker. 256 MB allocates in 4 ms. The
display must be driven at a small mode (BMC64 used a custom 768×544
for the same reason). Keyboard: USB fine; a 2.4 GHz 8BitDo dongle
dropped SPACE/ESC (HID report quirk) — note for later, the real machine
uses the C64 keyboard on GPIO anyway.

Files: `pi/k4510log-run3.txt` (the setup block the card kept),
`pi/k4510probe/config.txt` (the working TV mode).

## 2026-08-22 (night) — THE K4510 BOOTS ON THE PI 3B+

Doc: "Pi frontend and C64 keyboard on GPIO it is." Same evening:

- **`core/host.h`** — the seam from PORTABILITY.md turned out to be two
  functions (`host_alloc_zeroed`, `host_zero`) plus a per-frame input
  hook. Everything else the core uses — `fopen`, `opendir`, `stat`,
  `time()` — newlib on Circle provides. `sdl/host_posix.c` keeps the
  mmap path; `pi/host_circle.cpp` uses the kernel heap.
- **`sdl/main.c` is the Pi frontend too**, unchanged but for its entry
  name (`k4510_frontend_main`) and the hook. circle-libsdl2 delivers
  `SDL_TEXTINPUT`, key repeat, the audio callback at our 48 kHz S16
  mono, and the 640×480 ARGB streaming texture exactly as desktop SDL
  does. Route A, as designed on 2026-08-21.
- **`pi/`**: Circle kernel (EMMC + `f_mount("SD:")`, `chdir SD:/k4510`,
  then the frontend with `rom/kernal.bin` and `fs/`), Makefile (Circle's
  Rules.mk compiles the C core; a `.cc` rule for reSID), `make-sd.sh`
  (firmware + kernel at the root, `/k4510/{rom,data,fs}`), `config.txt`.
- **`pi/c64kbd.cpp`**: the C64 keyboard on GPIO, wired the BMC64-PCB way
  (BMC64 "GPIO Config 2", which is what Doc's `settings.txt`
  `gpio_config=1` means). Rows GPIO 5,20,19,16,13,6,12,26 driven low in
  turn; columns 8,25,24,18,23,27,17,22 read with pull-ups; RESTORE on
  GPIO 4. Standard matrix → our `$D100` ASCII/KEY_* codes: shift gives
  the upper symbols, C= = Alt, CTRL = Ctrl, RUN/STOP and RESTORE =
  Escape, cursor keys shift into up/left, F-keys into the even ones;
  0.5 s then 30/s repeat. Read from the frame loop via the hook.
- **First boot stopped at `sdhost: unexpected command 13 error`** right
  after `SDL2Circle_ArmCoreRuntime`: that call raises the CPU clock, and
  the core clock the SD host's timing derives from moves with it. The
  library documents the cure — `SDL2Circle_HardwareInit()` in the kernel
  constructor, before the SD driver — and a retry on the first access.
- **Second boot: it works.** Blue shell, `DIR`, `HELP`, the C64
  keyboard typing (Doc's `di` → Wozmon examined `$D`, as it should),
  `RUN balls.prg`, `cube.prg`, `mandel.prg` on the Sharp TV. Photos
  `screenshots/IMG_6008..6011.jpeg`. Single core, no split yet.
- Linked first time, 1.26 MB `kernel8.img`. 8/8 desktop tests still
  green; desktop frame time unchanged.
- Card gotchas: a card pulled mid-write left a dirty FAT (`fsck.vfat`
  fixed it); a stale mount on p15 looked like a write-protected card.
  TV overscan crops 480p edges → `overscan_*` margins in `config.txt`.

Still to do on the Pi, in order: **measure** (`INFO -c` on the Pi is
the number), the **core split** (CPU on one core, VICKY+SID on another,
presentation on a third), a real-time clock (network or a saved
date), and the desktop's Shift+Esc quit becomes a reboot.

## 2026-08-22 (late) — Core split, full speed, the picture, the keyboard

- **Keyboard test** (`fs/keytest.prg`, through the ROM jump table — the
  first program to call CHROUT/GETIN): 64/71 with every miss a wrong
  key; all three modifiers and the shifted combinations correct. The
  GPIO matrix table is right. Report saved to `fs/keytest.txt` by the
  program itself. Two to recheck at leisure: ↑/← arrows, CTRL+A.
- **Core split** (`pi/kernel.cpp`): core 0 devices + servo, **core 1
  the emulator**, core 2 presentation, core 3 parked. `sdl/main.c`
  still unchanged; file calls from core 1 redirected to the library's
  I/O service by `circle-syscallwrap` (one include in the Makefile —
  which then became the default goal; `.DEFAULT_GOAL := kernel8.img`).
  **Result: `INFO -c` on the Pi = 40.42 MHz, 37,427 iterations — the
  desktop number exactly; 4655 frames in 1:17 = 60 fps.** The K4510
  runs at full speed on the Pi 3B+.
- **Picture:** Doc found the firmware overscan margins squash the image
  and 720p output resamples the font (640→960 is 1.5×, uneven). Final:
  **640×480 output, 1:1 present, no margins**; the TV scales. If a TV
  crops, its "Just Scan"/"Screen Fit" setting is the fix.
- **VICKY 640×240** (CTRL bit2, lines doubled) beside 320×240 (bit1).
  **ROM `MODE 0|1|2`**: 80×60 / 80×30 / 40×30 text; COLS/ROWS are
  variables now; `INFO -g` reports the mode. Photo of MODE 2 on the TV:
  `screenshots/IMG_6020.jpeg`.
- **Demo speed, measured** (new on-screen FPS meters; cube's D key
  toggles double buffering, mandel shows seconds per image): balls 60
  fps; **cube 15 fps on desktop and Pi alike** — it is bound by the
  emulated 45GS02 (a cc65 far-store per pixel, 32-bit edge math), not
  by the host, which is why the split moved balls and mandel but not
  the cube. Single-buffering cannot help it; it just tears.
  The honest fix is in the design, not the program: a **line-draw op in
  the blitter** (VICKY-SPEC §8 already allows "instant" blits) would
  make wireframes free, the way the span fill already is via DMA.
  Proposed, not done.

**Blitter LINE + TRIANGLE (same night).** Doc: "not a purist — if you
can make it faster, do." Ops 6 and 7, clipped; `vickytest` gained two
checks. Cube: **15 → 60 fps, wireframe and solid**. First try (lines
only) gave 60/22: the solid half was still bound by 450 DMA span
setups per frame, each with a 32-bit multiply in cc65 — the triangle
op removed the edge walker and the spans both. Lesson for every later
program: on this machine, geometry goes to VICKY, not to the CPU.

## 2026-08-22 (night) — The MATH unit

Doc asked whether the machine needs a "virtual FPU". Yes — the cheapest
device we will ever add. `$D700`, in `core/io.c`:

- **Float unit:** eight IEEE-single registers F0..F7 (`$D700-$D71F`),
  ops that work *in place* — write `FARG = dst<<4|src`, write `FOP`,
  done: MOV ADD SUB MUL DIV SQRT SIN COS TAN ATAN ATAN2 EXP LOG POW ABS
  NEG FLOOR ROUND FMOD, CMP (flags only), ITOF/FTOI through a 32-bit
  `FI` register. Two byte writes per operation is the whole point: on
  an 8-bit bus a naive "write 8 operand bytes, read 4" FPU loses to a
  table lookup.
- **Integer unit, MEGA65-compatible, same addresses:** `MULTINA/B` at
  `$D770/$D774`, 64-bit `MULTOUT` at `$D778`, 32.32 `DIVOUT` at
  `$D768` — so MEGA65 conventions (and a future BASIC) carry over.
- `test/mathtest`, 9/9 suites green.
- **Mandelbrot rewritten on it:** no table, no MAP window, no fixed
  point; eleven register ops per iteration. 64 iterations in 13 s vs
  40 in 16 s, and eight zoom levels where 4.12 fixed point gave out at
  four. Bug on the first run: loaded a constant into a register *after*
  multiplying by it — whole set "inside", all black, 3 s. Register
  discipline matters when the registers are in I/O space.
- On the Pi's A53 the float ops are hardware; on the desktop they are
  C `float`. The Pi kernel is rebuilt with the device and on the card.

Where this leads: BASIC with real floating point that is fast, and any
program's 3-D maths, at the cost of nothing.

**Math lists (same night).** Doc asked if a 16-bit FPU would be faster.
No: the arithmetic is free on the host; the cost is the 8-bit CPU
issuing ops, ~14 cycles each, and a narrower register saves nothing
per op. The fix is fewer issues: a **math list** — SHEILA for numbers.
`MLPTR` points at a program of the same 2-byte ops in RAM, `MLRUN`
runs it on the unit until `END` or a `STOP*`; `JUMP`/`DJNZ` (with a
16-bit `MLCNT`), `STOPFIGE` (stop when FI ≥ n), `LDF`/`LDI` immediates.
The Mandelbrot iteration is a 16-op list: per pixel the CPU writes
three bytes and reads two. **Under 1 s per image, from 16 s this
morning** — and the same mechanism is how a fast BASIC will evaluate
expressions. Bug: jump offsets are relative to the op *after* the
jump; my first lists were one short. `mathtest` covers a looping list.

## 2026-08-22/23 — EhBASIC stands up, with graphics

Doc: "Step 1: stand up EhBASIC. Step 2: add graphics commands."

**Groundwork, because BASIC owns the machine.** EhBASIC uses zero page
`$00-$E5` and `$EF-$FF`, page 3, and wants a contiguous program area.
So the ROM became a host: (1) its **text screen moved to far memory
(`$030000`)** — VICKY reads it there, the ROM writes it with flat
stores and DMA, the cursor blink in the IRQ borrows `$02-$05` and puts
them back — which frees **`$0800-$9FFF`, 38 KB, for programs**;
(2) every jump-table call **swaps the ROM's zero page (`$02-$21`) in
and out**, and `call_prog` snapshots it before a program runs and
restores it after; (3) the ROM's C stack sits at `$0500-$07FF` so a
program may use `$0300-$04FF`. Bug: `call_prog` patched its own JSR —
in ROM. Indirect jump through RAM instead. 9/9 still green.

**EhBASIC 2.22** (`basic/`: Lee Davison's `basic.asm` in ca65 form
from jefftranter/6502, `Ram_base $0800`, `Ram_top $7000`, code at
`$7000`; `k4510basic.asm` = the host glue: input/output through the
ROM jump table, CR-only newlines, a-z folded to upper case as a C64
does, RUN/STOP = reset to the shell; `README-EhBASIC.txt` carries the
"Derived from EhBASIC" notice). `fs/ehbasic.prg`, 10.9 KB, **26,623
bytes free**. `PRINT 2+2`, loops, `SQR`, `PI`, strings all correct.

**Graphics keywords** (`basic/k4510gfx.asm`, six new tokens spliced
into the command, keyword and LIST tables): `GRAPHICS n` (0 off,
1 = 320×240, 2 = 640×480; 8-bpp bitmap on VICKY layer 1 above the
text, index 0 transparent, cleared on entry), `GCLS`, `PLOT x,y,c`,
`LINE x1,y1,x2,y2,c`, `TRI x1,y1,x2,y2,x3,y3,c` — all blitter ops, so
instant — and `PALETTE i,r,g,b`. Arguments go through EhBASIC's own
expression evaluator. Note: `GRAPHICS 1` shows only 40 of the ROM's 80
text columns; `GRAPHICS 2` keeps the full text screen.

Not done yet: LOAD/SAVE of BASIC programs (vectors are stubs), the
MATH unit under EhBASIC's float package (that is the big win and the
reason the unit exists), `BYE`.

## 2026-08-23 (small hours) — LOAD/SAVE, a tune, BASIC demos, the package

- **`sids.prg`**: two SIDs — melody + fast arpeggio on SID 0, filtered
  bass + noise drums on SID 1, 16 bars in A minor at 120 bpm, level
  bars per voice. The first music on the machine.
- **EhBASIC LOAD/SAVE** (`basic/k4510file.asm`): `LOAD "NAME.BAS"` reads
  the text file into far memory with the ROM's LOAD and feeds it through
  the input vector as if typed (output muted, a leading CR for the
  Ctrl-C check to swallow); `SAVE "NAME.BAS"` captures LIST through the
  output vector and writes it with the ROM's SAVE. Plain text: a PC can
  write programs too. Input lines raised to 126 characters.
- **Three bugs, all memory layout, all mine:** (1) my page-3 variables
  sat on EhBASIC's input buffer (`$0321-$0368`) — `GRAPHICS 2` wrote 480
  into the line being parsed; (2) the ROM's BSS had grown (the 320-byte
  row buffer) from `$0227` to `$03E7`, over page 3 entirely — every
  printed line trampled EhBASIC; the ROM now has DATA `$0200-$02FF`,
  BSS `$0440-$05FF`, stack `$0600-$07FF`, and blanks rows from a far
  template; (3) EhBASIC's LIST formats numbers in `$EF-$FF` — the
  ROM's parameter block — so SAVE sets its parameters after LIST. Plus
  one non-bug: `RND(1)` in EhBASIC *reseeds*; `RND(0)` is "next".
  And a Makefile lesson: the `.prg` rule lacked the included files as
  prerequisites, so for an hour I was testing a stale binary.
- **Five BASIC demos** in `fs/`: README, LINES, TRIS, SINE, STARS.
- **`pi/bmc-k4510-pi3-2026-08-23.zip`** (2.3 MB): the complete card —
  firmware, kernel, ROM, demos, BASIC, a README — for Doc's friend.
  Unzip onto a FAT32 SDHC card, boot a Pi 3B+.

## 2026-08-23 — Public

Licence audit before opening the repo: Xemu core GPL-2.0-or-later,
reSID GPL-2.0-or-later, cc65 runtime zlib-style, Circle (GPL-3.0) and
circle-libsdl2 (zlib) outside the repo, EhBASIC under Lee Davison's
non-commercial terms as a separate program with his notice. **One
problem: `data/chargen` was a Commodore 64 character ROM**, in the tree
since the spike, with `font8.bin` derived from it. Replaced by the
Linux kernel's `font_8x8` (GPL-2.0, `data/mkfont.py` rebuilds it), the
file purged from the whole history (`git filter-branch`, gc, force-push
to GitHub and the mirror; zero references remain), the zip and the
card rebuilt with the new font. `LICENSE` (GPL-2.0-or-later),
`LICENSES.md`, README rewritten for strangers.
**github.com/mlongval/bmc-k4510 is public.**

**EhBASIC on the MATH unit (same night).** `basic/k4510math.asm`: the
Microsoft float in FAC1/FAC2 ↔ IEEE single is a byte shuffle (exponent
− 2, the 23 bits after the explicit leading 1, sign bit), so `LAB_ADD`,
`LAB_MULTIPLY`, `LAB_DIVIDE` and `SQR SIN COS TAN ATN EXP LOG` now
begin with a `JMP` into routines that convert, issue one op, convert
back; zero, negative and overflow keep EhBASIC's own errors. Every
check value matches; `BENCH.BAS` (5000 × `A=A+SQR(I)*1.5/3+SIN(I/100)`)
**4.03 s → 1.66 s**, identical result. What remains is the interpreter
walking text and variables — level 2 (expression → math list) is the
next multiplier, and a different kind of work.

## 2026-08-23 — Level 2: expressions become math lists

`basic/k4510expr.asm`, hooked at `LAB_EVEX`: in a program, a numeric
expression is compiled once — constants `LDF`, variables `LDMS` (a new
unit op that loads a Microsoft-format float straight from memory),
operators and SQR/SIN/COS/TAN/ATN/EXP/LOG/ABS/INT as register ops on a
seven-deep register stack, `^` left-associative as EhBASIC has it —
and cached by the expression's address (256-entry table + arena in far
memory at `$0D0000`). Later evaluations: one write to MLRUN, one
conversion back. Unknown things (strings, arrays, RND, PEEK,
comparisons, FN) bail to the interpreter, which still uses the unit
for arithmetic. Cache emptied at RUN/CLEAR/NEW and on line entry;
immediate mode never cached. Counters at PEEK 1051..1055.
**BENCH.BAS 4.03 → 1.66 → 0.53 s; BENCH2 (20000 pure-expression
loops) 6.46 → 2.11 s.** What is left is statement dispatch.
Bugs: PI pointed at the wrong constant; and for an hour every list
"failed" — I had rebuilt only `mathtest` after adding LDMS, so the
emulator under `capture` had no such op. Always `make all`.
EhBASIC moved to `$6C00` (12.7 KB now; 25,599 bytes free).
`GETIN` shows the cursor while waiting, so BASIC has one again.
Zip rebuilt; the Pi kernel rebuilt (new op) — card not in p15 tonight.

## 2026-08-23 — Benchmarks: CHROUT, and the classic 8-bit suite

Doc asked two things after the "programs bigger than 64 KB" discussion
(FEATURES.txt section K, K-01..K-04, written today): would an assembly
ROM be faster, and how does the machine compare on the period
benchmarks (github.com/rprouse/8bit-benchmarks: Rugg/Feldman 1977,
Byte Sieve 1981, Ahl's Creative Computing 1983). Measure first.

**New in `k4510/`** (on t480i5, uncommitted — Doc's call):
- `demo/chrout.c` → `fs/chrout.prg`: CHROUT throughput through `$FF80`,
  three passes (character stream / 79-char lines / bare newlines),
  timed by the `$D50D` frame counter so the host does not matter.
- `demo/sieve.c` → `fs/sieve.prg`: the Byte Sieve in C, 10 iterations.
- `fs/RF1.BAS`..`RF8.BAS`, `AHL.BAS`, `SIEVE.BAS`: the BASIC benchmarks
  verbatim plus a frame-counter wrapper (lines 1–2 and 750–770).
- `test/headless`: boot a ROM, type keys, run until a marker string is
  on the text screen (or `"a|b"`), dump the screen. General tool.
- `test/benchmarks.sh`: runs the lot headless (~1.5 min on the t480i5).
- Benchmark programs wait for a key after `DONE` — `run_at()` in the ROM
  does `cls()` when a program returns, which ate the first results.

**Results** (1 frame = 1/60 s, 45GS02 at 40.5 MHz, EhBASIC with the
MATH-unit floats and the expression compiler):

| Benchmark | K4510 | C64 / 1 MHz 6502, for scale |
|---|---|---|
| Rugg/Feldman 1 | 0.05 s | 1.2 s |
| RF 2 | 0.23 s | 9.3 s |
| RF 3 | 0.29 s | 17.6 s |
| RF 4 | 0.29 s | 19.5 s |
| RF 5 | 0.38 s | 21.0 s |
| RF 6 | 0.76 s | 29.5 s |
| RF 7 | 1.18 s | 47.5 s |
| RF 8 (100 × ^, LOG, SIN) | 0.03 s | 11.9 s |
| Ahl's (accuracy .0115, random 31.2) | 0.54 s | 1:53 |
| Byte Sieve, BASIC | **out of memory** | minutes (one iteration) |
| Byte Sieve, C (cc65), 10 iterations | 0.93 s | tens of seconds |
| CHROUT, character stream | 4,000 cycles/char, 10,100 ch/s | — |
| CHROUT, 79-char lines | 3,780 cycles/char, 10,700 ch/s | — |
| CHROUT, newline only (scroll by DMA) | 9,200 cycles | — |

(C64 figures from the Wikipedia Rugg/Feldman table and the Creative
Computing article; rough, stopwatch-era numbers.)

**Readings.**
- Rugg/Feldman scale ~40× over the C64 on the interpreter loop (RF2–7),
  which is the 40.5 MHz clock doing its job on EhBASIC's parser;
  RF8 is 400× because `^`, `LOG`, `SIN` go to the MATH unit. The
  interpreter, not arithmetic, is the cost now.
- Ahl's accuracy .0115356 is the MS-float answer (IBM PC: .01159668),
  so the MATH-unit shuffle loses nothing the 40-bit format did not
  already lose. **Gotcha:** EhBASIC's Galois RNG starts from an
  all-zero seed, so `RND(0)` returns 0 forever until one `RND(n≠0)`;
  AHL.BAS seeds on line 3 (Ahl's own note anticipates this).
- **Byte Sieve in BASIC does not fit**: `DIM FLAGS(8191)` is 48 KB of
  6-byte floats and EhBASIC at `$6C00` has 25.6 KB free. First concrete
  case for section K (or for moving EhBASIC's workspace out of the
  window); recorded, not fixed.
- **CHROUT: ~4,000 cycles per character, i.e. ~100 µs, 10 K chars/s.**
  A full 80×60 screen in under half a second. Scroll costs ~9,000
  cycles — the DMA is instant, the rest is the cc65 row-template and
  blank-row bookkeeping. So: the C ROM is not the bottleneck of
  anything a human can see; a `TYPE` of a 100 KB file is ~10 s, bound
  by CHROUT, which is the one place an assembly inner loop (or a
  "print N bytes" ROM call that walks the string itself) would pay.
  Answer to "rewrite the ROM in assembly?": no — C ROM, assembly
  leaves, and CHROUT is the first leaf if TYPE ever matters.

**Release alpha-0.1 "Proof-of-concept" (same night).** Commit 356ccae on
`master`, tag `alpha-0.1`, pushed to GitHub and the ubuntu-s1 mirror.
README.md carries the release line. The card package was rebuilt on
p15 (`~/Projects/k4510-pi/pkg/`, kernel unchanged, `rom/` and `fs/`
refreshed from HEAD, README.txt gained a BENCHMARKS section) as
`bmc-k4510-pi3-alpha-0.1.zip` (41 files, 2.3 MB) and attached to
**github.com/mlongval/bmc-k4510/releases/tag/alpha-0.1** for Doc's
friend. Note: the laptop's branch is `master`, not `main`, and
non-interactive ssh there has neither `gh` on PATH
(`/home/linuxbrew/.linuxbrew/bin`) nor a git identity — set both
explicitly when scripting.

## 2026-08-23 (small hours) — K-01..K-04 built: programs bigger than the window

Doc: "go ahead and implement K-01 through K-04". Commit `358592e` on the
laptop, pushed. `make test` = 10 suites green; `fs/segdemo.prg` is the
proof: a cc65 program whose two overlays are both linked for `$4000`,
stored at 1 MB and 1 MB + 8 KB, called through the gate with arguments
and return values passing, each RTS restoring block 2.

- **K-01 bank registers, `$D600`** (`core/mem.c`, `core/io.c`): one
  4-byte register per 8 KB block; phys = base + (cpu & $1FFF), byte
  granularity; any byte write takes effect (so `STQ` works); byte 3 with
  bit 7 set turns the block off; `$D620` = banked mask, `$D621` = MAP
  mask. Whichever wrote a block last owns it; **MAP rewrites all eight
  blocks**, so prg0's "MAP off" at exit clears the banks too.
- **K-02 far-call gate, `$DF00`**: `JSR $DF00+4n` → descriptor n of the
  table at `$DF80` (8 bytes: base, block, flags, entry) → save the
  block's bank, bank it, `pc = entry`; the callee's RTS lands on `$DFF0`,
  which restores and returns to the caller. Depth at `$DF84`, errors at
  `$DF85`, 64 deep. Flags: leave banked / don't bank (long jump). No
  core change: the core sets `old_pc` before each opcode fetch, so
  `addr == old_pc` in the read callback *is* the fetch; the gate returns
  a NOP and sets `pc = entry - 1` because the core increments after the
  fetch (the first bug of the night).
- **K-03 segment loader**: `.prg` files starting `"K4SG"`: count (≤ 8),
  flags, entry, then a 12-byte table per segment (phys, len, bank
  block), then the bytes. The ROM reads the *whole table first* (second
  bug: I read table/data interleaved), FS-READs each segment straight
  to its physical address, sets any bank. `LOAD` reports "in N
  segments"; `INFO -m` lists banked blocks.
- **K-04 `demo/far.h`**: `far_peek/poke/16/32`, `far_copy/fill` (DMA),
  `bank_set/off/get`, `BANK_WINDOW(n)`, `far_table()`, `FAR_FN(slot,
  type)` — a slot as a function pointer; A/X pass through so
  `__fastcall__` works across the gate. `demo/seg.cfg` + a 40-line
  `segdemo-header.s` build the K4SG header from ld65's
  `__OVL1_START__/LAST__` symbols.

**Lessons.** (1) cc65 `#pragma rodata-name` set to the *code* segment
emits a function's local string *at the function label*, before the
code — the symbol then points at text. Overlay rodata gets its own
segment (`OVL1R`) in the same memory area. (2) `__PRG_LAST__` includes
BSS, which ld65 does not write; the file length is `__BSS_RUN__ -
__PRG_START__`. (3) An hour lost to a stale `test/headless` linked
against the old `mem.o`: the Makefile lists `$(CORE_OBJS)` as
prerequisites, but I had rebuilt the objects by name and not the
binary. `make all` before trusting a test tool. (4) Evaluation order
of `printf` arguments is unspecified — a probe lied to me for ten
minutes.

Not done, deliberately: llvm-mos investigation (FEATURES K footnote),
the B/C ideas, and a new release zip — alpha-0.1 stays as shipped.

## 2026-08-23 (afternoon) — ROM stage 4: directories, the boxed banner, @ and CHAIN

Doc's ten-item list. Commit `2b8f657`, pushed; 10 suites green; the
emulator on the t480i5 restarted on the new ROM.

1-3. **CD, MKDIR, RM/ERASE/DEL, RMDIR** in the shell. The `$D300` device
  grew `CHDIR MKDIR RM RMDIR GETCWD` (commands 11-15), a cwd, "/" and
  ".." (never above the sandbox root), a sorted listing with directories
  as `SIZE = $FFFFFFFF`, case-insensitive lookup when the exact name is
  absent (EhBASIC upper-cases everything), and reads of a bare name fall
  back to `/PRG` then `/BASIC`. The prompt shows the cwd: `/BASIC] `.
  DIR is two columns at 80 wide, `<DIR>` entries in white. Pi: the
  shim wraps `unlink`/`mkdir` but has no `rmdir` — guarded, says "failed".
4-5. **Yellow on blue** (`C_FG = 7`), and a **boxed banner** in CP437
  box glyphs (the Linux font is CP437-ordered): name and ROM stage on
  the title row, then CPU/MEMORY, VIDEO/SOUND, FILES/BASIC, TIME/ALSO in
  two columns, one hint line. The ROM's 12 KB code half overflowed by
  1 KB; INFO/HELP/banner moved to a `CODE2` segment in the `$E000` half.
9. **`MODE a b`**: `b = 1` keeps one blank cell row on top and one
  column on the left. Implemented as physical (`PCOLS/PROWS`) vs logical
  (`COLS/ROWS`) geometry with `OX/OY` offsets; `blank_row` and `scroll`
  work on whole physical rows so the margin column stays blank for
  free. Boots in `MODE 1 1`: 640x240, 79x29. romtest's `find()` skips
  the margin now.
6. **`fs/PRG/` and `fs/BASIC/`**; `BENCH.BAS`→`FLOAT.BAS`, `BENCH2`→
  `EXPR.BAS`; Makefile, `make-sd.sh` (`cp -r`), benchmarks.sh follow.
7. **`@command` in EhBASIC** — the prefix, as recommended: keywords
  would be tokenised inside variable names and each costs a table edit;
  the prefix is two five-line patches in `basic.asm` (the cruncher copies
  the rest of the line verbatim after `@`, like REM; the statement
  dispatcher jumps to `K_AT`) and a new ROM call **`SHELL $FF8F`** (A/X =
  pointer to a command line). So `@DIR`, `@CD BASIC`, `@TYPE X`, `@INFO`,
  `@MKDIR`... all of them, present and future, from immediate or program
  mode. Gotcha: the jump-table wrapper `zp_in` uses X as its loop
  index, so `w_shell` must PHX/PLX around it — the first pointer arrived
  with a dead high byte.
8. **CHAIN = `LOAD` in a running program runs the loaded program** (C64
  semantics, no new syntax). The feed that LOAD already uses types `RUN`
  after the file; `ccflag` inhibits EhBASIC's Ctrl-C sampling during the
  feed (it was eating the `R`). Two real bugs found on the way: the
  input vector must preserve X (the line editor's index — `PLX` also
  trashed the Z flag my `BEQ` wanted), and **EhBASIC's `NEW` flushes the
  stack but returns to the top frame**, which inside `IF … THEN LOAD` is
  the IF tail, whose RTS then jumps to `$0001` and the CPU walks up RAM
  into EhBASIC's entry — a "cold start" that took a PC trace to explain.
  LOAD now does `JSR LAB_1463 ; JMP LAB_127D` (the immediate loop).
  Variables do not survive a chain; `PEEK(1039)` is the "came from a
  menu" flag the menus POKE.
10. **`DEMOS.BAS` and `BENCH.BAS`** menus; every demo and benchmark ends
  with `IF PEEK(1039) THEN LOAD "<menu>.BAS"`; benchmarks wait for a key
  first. `test/headless` gained `~` = wait 30 frames in the key string,
  and a `K4510_DUMP=addr,len` memory dump; it now types slowly enough for
  menus.

Not done: a new card zip (alpha-0.1 stands); the `rmdir` shim on the Pi.

## 2026-08-23 (later) — menu return, DUMP, SID demos, a Ctrl-C that keeps its hands off

Commit `1a73d89`, pushed; 10 suites green; emulator restarted on the laptop.

- **"LINES does not return to the menu"** — it did; the prompt was
  invisible. `PALETTE 1..15` recoloured the ROM's own text colours (6
  blue, 7 yellow) and `GRAPHICS 2` left the chip in 640x480 under a
  240-line text layout. Fix at the root: **`VIDEO $FF92`**, a ROM call
  that re-applies the text mode and palette; `GRAPHICS 0` calls it. The
  demos use colours 16+ now. (First attempt saved the palette in
  `$0410-$043F` — which is the expression compiler's state; the
  resulting "copy: from.to dest" from nowhere cost an hour. The ROM owns
  its palette; EhBASIC asks.)
- **`DUMP [note]` / `@DUMP note`** (item 3): SYS `$D5F0` write → the
  host writes `dumps/dump-NNN.txt` with CPU, MAP/banks/far gate, VICKY
  layers, SID shadows, FS/DMA/MATH registers, the text screen, the
  shell log (every shell line + DUMP notes, via `$D5F1`), the last 256
  keys, the last 4096 opcode fetches (runs collapsed), zero page,
  stack, `$0300-$04FF`. Read it over ssh:
  `t480i5:~/Projects/BMC64k4502/k4510/dumps/`.
- **Three SID demos in BASIC** (item 4): `SIDWAVE` (one voice, four
  waveforms, a scale each), `SIDBEAT` (bass on SID0, noise drums on
  SID1), `SIDFILT` (a sawtooth chord through a swept low-pass with
  resonance). DEMOS menu entries 6-8. Subroutines at 9000+ because a
  900-line subroutine sits *between* 160 and 990 in line order and gets
  fallen into — an old BASIC lesson relearned.
- **Keys typed while a program is busy were lost.** EhBASIC's Ctrl-C
  check pops the input every statement and keeps the byte 32 statements;
  a frame-wait loop burns that in a millisecond. Now the keyboard
  device has a **peek register `$D102`** and `k4510_cc` only looks,
  popping nothing but `$03`/ESC. Needed the `PG2_TABS` table entry in
  `basic.asm` patched, not a store before `LAB_COLD` (which re-copies
  the table). Side effect: RF2 14 → 10 frames — the old check went
  through the ROM's GETIN every statement.
- **Item 2 (ROM vs RAM)** written up as **K-05** in FEATURES.txt with a
  DEFER suggestion: the C64 ROM-out trick maps onto the bank registers
  in a day, but only the BASIC Sieve has ever hit the 38 KB, and for
  BASIC the lever is EhBASIC's own workspace.
- **Item 5 (Pi speed switch): not needed.** The desktop frontend is
  vsync-locked at 60 Hz with the same 40.5 MHz cycle budget per frame;
  the Pi measured 40.42 MHz and 60 fps ("the desktop number exactly",
  2026-08-23 morning). What you see on the laptop *is* the Pi.

## 2026-08-23 (evening) — K-05 built: RAM under the ROM; DUMP ON

Doc: "I can already hear the critics saying the ROMs eat all the RAM …
if it's not expensive, implement it now." Commit `faf0196`, pushed,
10 suites green, emulator restarted.

**As built.** The ROM image no longer sits in the low 64 KB physically:
`mem_load_rom` puts it at `$0FFF0000 + addr` (top of the 256 MB) and the
unmapped CPU view reads it from there above `mem_rom_base`. So physical
`$A000-$FFFF` is plain RAM *under* the ROM. `rom_out()` (far.h) banks
block 5 onto `$A000` and block 7 onto `$E000` — identity banks — and the
program has `$0800-$BFFF` + `$E000-$FEFF` = **54 KB**. The page
`$FF00-$FFFF` is hard-wired to the ROM whatever is banked (the 6510's
vectors, generalised): it holds the jump table, the vectors and the
**stub** — every system call goes `JSR $FF80` → `rom_push` (saves bank
registers 5-7, 12 bytes, on the stack; banks the ROM in) → the wrapper
→ `rom_pop` (restores, byte 3 of each register last). The IRQ entry
does the same inline with no temporaries, so it may land anywhere,
including inside `rom_push`. A reset clears all banks first (F12 does
not reset the MMU). The ROM keeps `$C000-$CFFF` (it does not fit in two
blocks) and I/O keeps `$D000`. `romout.prg` proves it: fills both
ranges with the ROM out, prints through CHROUT meanwhile, 0 bad bytes,
pattern intact under the ROM afterwards. `INFO -m` says so.

**Bank-register semantic changed** to make byte-wise save/restore safe:
bytes 0-2 set the base, **byte 3 switches** (bit 7 = off). Before,
writing byte 0 of an "off" register switched it on with a partial base —
fatal when the register is block 6's (the I/O page vanishes under a
transient bank and the next STA never reaches the registers).

**Cost, measured:** CHROUT 4,083 → 4,471 cycles (+9%) — the 12-byte
save/restore is ~300 cycles, not the 40 I promised; compute unchanged
(sieve 0.93 s). RODATA moved to the `$A000` half; the stub page took
256 bytes of the `$E000` half.

**Three stub bugs, all mine, all found by `romout.prg` and the CHROUT
benchmark:** (1) a single fixed save slot — an IRQ inside a call saved
"off" over the program's "on": save on the stack instead; (2) the IRQ
path sharing the call path's temporaries — garbage A/X when the vblank
hit inside `rom_push`: the IRQ path is inline and temp-free now;
(3) restoring byte 0 first switched blocks on — hence the semantic
change above.

**`DUMP ON` / `DUMP OFF`** (`@DUMP ON` from BASIC): SYS `$D5F2`; the
host writes a dump every 900 frames (15 s of machine time) while on.

## 2026-08-23 (evening, 2) — what the dumps said

Doc ran the machine for two minutes with `DUMP ON` and reported: SID
demos 1 and 2 silent, STARS unstoppable. Twenty-two dumps. Commit
`da60fdc`, pushed, 10 suites green.

- **Silent SID demos — dump 019 had it.** `SID0` register 23 was still
  `$F7` from SIDFILT: voices 1-3 routed into the filter, and SIDWAVE /
  SIDBEAT never touch the routing, so with `$D418` mode bits clear the
  voices went into a filter with no output — silent on a real SID too.
  Headless both demos rendered fine on a *fresh* chip, which is why I
  had not seen it. Every demo now clears all 25 registers first and
  SIDFILT leaves the routing clear. Verified by recording the SDL
  frontend's actual output (`SDL_AUDIODRIVER=disk` + the new
  `K4510_KEYS` env var that types into the frontend): SIDFILT then
  SIDWAVE, RMS 755-1887 where it used to be 0.
- **STARS unstoppable — dump 022's PC history had it.** The Ctrl-C
  check ran and saw a key, but the key at the *head* of the FIFO was the
  `j` Doc typed first; the ESCs and Ctrl-Cs were queued behind it, and
  my "peek only" check looks at the head. Now the keyboard device has
  **`$D103` break-pending**: an ESC or `$03` anywhere in the queue is
  removed and returned; other keys stay for `GET`. Tested with junk
  typed ahead of both.
- **RUN/STOP on the desktop = Esc** (Shift+Esc quits the emulator, F12
  resets); Ctrl-C = STOP. Now in the README.
- **Banner:** a 16-colour bar under the title, its inverse (upper
  half-blocks) beneath, and every row inside the box keeps its
  verticals, blank ones included. First cut wrapped by one cell: the
  content width between the verticals is `COLS-3`.

## 2026-08-23 (evening, 3) — the logo, and MON

Doc's screenshot (`k4510/screenshots/Pasted image.png`): the box's
vertical glyph has gaps in the line-doubled 640x240 mode, so blank
rows looked broken — and he wanted the MEGA65 idea, not a box: a colour
triangle with the text beside it, and nothing but name, speed, RAM.
Commit (laptop) after `da60fdc`; 10 suites green; emulator restarted.

- **Logo:** five rows of colour blocks, widths 12/8/4/8/12 (an
  hourglass, left-aligned), red/orange/yellow/green/light blue; to the
  right: `BMC-K4510`, "a fantasy 8/16-bit computer", `45GS02 at 40.5
  MHz`, `256 MB`. No box. `INFO` has everything else.
- **`MON`:** the Wozmon grammar left the shell's fallback and became a
  command. `MON` opens the monitor at a `*` prompt (`addr`, `addr.addr`,
  `addr:b b`, `addrR`, shell commands too), `X`/`EXIT`/`Q` leaves;
  `MON E000.E00F` runs one line without the prompt. `@MON` from EhBASIC
  returns to BASIC on `X` (it is a nested readline inside the SHELL
  call). Unknown shell words now get "? (HELP lists the commands; MON is
  the monitor)". romtest's monitor lines carry `mon`. The ROM's BSS is
  full to the byte (the monitor reuses the shell's line buffer).

## 2026-08-23 (night) — six and twelve voices; the scrolling benchmarks

Commit `8178e76`, pushed, 10 suites green, emulator restarted.

- **AHL/FLOAT/EXPR "scroll off screen"**: their wait line was
  `900 PRINT: PRINT "ANY KEY...";: GET K$: IF K$="" THEN 900` — the
  PRINT re-ran on every poll and scrolled the results away. Split into
  900/905 in all twelve benchmark files (the same mistake I had already
  fixed once in SIDWAVE). README.BAS got a key wait; STARS leaves on any
  key (`PEEK(53505)` inside its loop); `RUN ehbasic` tries `ehbasic.prg`
  (the dumps showed Doc typing it).
- **`demo/sidorch.h`**: a SID orchestra — Pachelbel's progression in D,
  8 bars at 120 bpm, an eighth per 15 frames. `sid6.prg` (2 chips: bass
  through the low-pass, chord 3rd/5th, melody, 16th arpeggio, drums) and
  `sid12.prg` (4 chips: plus a slow triangle pad on the chord, the melody
  echoed two eighths late, a second bass an octave up on the off-beats,
  hi-hats) — one source, `NCHIPS`. A row per voice on screen with the
  note and a level bar. cc65 did not explode: 5.2 / 5.7 KB.
- **`SID6.BAS` / `SID12.BAS`**: the same music in EhBASIC, one note per
  eighth (no 16ths), generated from one template. Fast enough: the
  whole 12-voice step is ~40 POKEs in a 7-frame half-eighth. Gotcha:
  EhBASIC's `AND` is 16-bit signed, so `Q AND 255` on a frequency word
  above 32767 (the hi-hat) is a Function call error; use
  `Q-INT(Q/256)*256`. DEMOS menu: 9 and 0.
- **Sieve.bas out of memory — why:** `DIM FLAGS(8191)` is 8192
  six-byte floats = 48 KB; EhBASIC has 25.6 KB between its program area
  ($0800) and its own code ($6C00). The 256 MB are behind the CPU's
  64 KB view and EhBASIC (1980s design) keeps arrays in the view. The
  honest fixes: integer arrays (two bytes each — EhBASIC has none, a real
  addition), or moving EhBASIC's code above the window with the ROM out
  (K-05 makes $A000-$BFFF + $E000-$FEFF available, but EhBASIC is 12.8 KB
  in one piece and neither range holds it). Left as a ballot item.

## 2026-08-23 (night, 2) — voice toggles, DUMP ON by default, Escape is STOP

Commits `a9f0c3c`, `75de6c8`; pushed; 10 suites green; emulator restarted.

- **Auto dump on at reset** (Doc: "until further notice"); 100 files
  rotating in `dumps/`. `DUMP OFF` turns it off.
- **Voice toggles** in `sid6`/`sid12` (keys `1-9 0 A B`, the key stands
  in front of each row, a muted row shows "off"; `Q`/Escape leave) and in
  `SID6.BAS`/`SID12.BAS` (same keys, the list redraws; the play subroutine
  skips a muted voice via `U()` — `M()` was already the minor flags,
  "Double dimension Error").
- **Escape in BASIC = STOP** (Break to Ready, and `k4510_hush` zeroes
  all four SIDs' registers so nothing hangs), the same as Ctrl-C. It
  used to jump through the reset vector — Doc's "hangs and drops to the
  OS". **`@BYE`/`@EXIT`/`@QUIT`** leave BASIC for the shell deliberately
  (handled in `K_AT` before the text reaches the ROM shell).

## 2026-08-24 — SIDPLAY: real C64 music on the fantasy machine

Doc: "write a SID player, there is a whole folder of sid tunes" — 199
PSID/RSID files in `k4510/sidfiles/` (EC64SC collection, tracked).
Commit `d43453e`, pushed, 10 suites green, emulator restarted.
`fs/SID` is a symlink to the collection.

**`RUN SIDPLAY`.** A chooser first (Doc asked mid-build: no typing) —
two columns, cursor keys/PgUp/PgDn/Home/End, Enter plays, Esc leaves.
Playing: title/author/released from the header, load/init/play
addresses, song n of m (+/- switches), a clock, and a live meter per
voice (waveform name + a bar from the frequency register). Space jumps
to the next tune. RSID files (15) and play-address-0 tunes need a real
C64 (KERNAL, CIA) and say so honestly.

**How it runs a real C64 tune.** The tune's own 6502 code is the
player: copied to its C64 load address and called at init, then play at
50 Hz (PAL) or 60 (NTSC/CIA) from the frame counter (5 calls per 6
frames). For that the tune must own the C64's memory, so the player
itself lives *under the ROM* at `$E000-$FEFF` (sidplay.cfg + a K4SG
entry stub at `$0250` that banks block 7 and jumps — the loader must
not bank it: the ROM still runs from block 7 until the program is
entered; found the hard way, SP=$F398). The player swaps only its own
36 zero-page bytes ($40-$63) around each call into the tune — the tune
keeps the rest of the zero page. VICKY registers the tune pokes
(thinking they are the VIC-II) are put back every frame.

**The machine grew one rule for it:** the I/O page `$D000` and the stub
page `$FF00` are now *always* what they are, whatever MAP or the banks
say (before, I/O existed only in the unmapped view). So block 6 banks
to RAM like 5 and 7 — 57 KB with the ROM out, and the 19 tunes that
load into `$C000-$CFFF` (Armalyte, Last Ninja 2, R-Type...) work;
verified Armalyte playing from under the ROM. Commando (Rob Hubbard,
11 songs) is the smoke test: RMS 1500-2000 from the recorded SDL path.

**Exit** is its own trick: the player cannot un-bank block 7 from code
inside block 7, so `_exit` copies a MAP-off trampoline to `$0230` (the
first version used `$0200` — the ROM's DATA segment, oops) and jumps.
The caller's directory is restored (the player CDs to /SID).

## 2026-08-24 (b) — Cobra's revenge: load-range checks and the PAL crystal

Doc: "I played the first one, fine; then Cobra.sid and the emulator
exploded." Cobra loads at **$F000-$FFF0 — straight over the player**
at $E000-$FEFF; the far_copy overwrote the running code. Now the
player checks the range first: tunes outside $0300-$CFFF show their
header and "the player itself lives up there" instead of loading —
30 of 184 PSID files (the under-the-KERNAL crowd: ACE II, Spellbound,
Miami Vice...). Supporting them would need a player that does not
live at $E000; parked.

He also asked whether 50 Hz tunes are speed-adjusted: the *call rate*
was right (50/60 via an accumulator), but the *pitch* was 1.5% sharp —
our SIDs ran at exactly 1 MHz, a PAL C64's at 985248 Hz. New SYS
register **$D5F3**: SID crystal 0/1/2 = 1 MHz / PAL / NTSC (reSID
set_sampling_parameters per chip); the player sets it from the tune's
flags and puts 1 MHz back on exit. The play screen says "50 Hz PAL".
Commit `<this>`, pushed; 10 suites green; emulator restarted.

## 2026-08-24 (c) — the 45GS10, bare-name RUN, a kinder chooser, the Sieve solved

Doc's nine-item list. Commits `0e376d2` + the AHL seed fix; pushed;
10 suites green; emulator restarted.

1. **The CPU is the 45GS10 now** — Doc: it grew the 6510's ROM/RAM
  trick (and the rest of the K4510 MMU), so it earned its own number,
  and it lines up with the machine name. All user-visible strings and
  docs; the Xemu core file keeps its own name (it is byte-for-byte the
  45GS02 instruction core). Machine name stays BMC-K4510.
2. **A bare word at the prompt runs a program**: unknown commands try
  `name` then `name.prg` through the search path, so `SIDPLAY` works.
  (The OS has no name, by the way — the log calls it "the system ROM".
  Candidates if Doc wants one on the ballot: naming it is his call.)
3. **Prompt history** (answered): `]` is the Apple II lineage (Applesoft
  prompt; Integer BASIC used `>`), `>` is the CP/M-to-DOS lineage, C64
  BASIC had none ("READY."), Wozmon used `\\`. With a cwd in the prompt,
  `>` reads DOS-ish, `]` reads Apple-ish; ours is `]` after Wozmon
  heritage. Doc's call whether to switch.
4/7. **The chooser redraws only the two affected rows** on a cursor
  move (full redraw only on a page change), and **drains the whole key
  queue before drawing** — holding a key no longer floods the FIFO
  faster than redraws drain it.
5/6. **Plans, not code yet** (answered in the reply): play-address-0 /
  CIA tunes need a small CIA timer device + a RAM IRQ vector honored by
  the player; RSID needs a real KERNAL — both parked. The $E000-crowd
  fix is designed: keep the tune's image in far memory and have a low-RAM
  runner bank block 7 to the tune around each play call and back to the
  player after; tunes with data in $FF00-$FFF9 stay impossible (the
  stub page always reads ROM). ~22 of the 30 rejects would come back.
8. **SIEVE2.BAS**: the full 8191-flag Byte Sieve in BASIC after all —
  bank register 1 maps 8 KB of far memory into $2000-$3FFF and each
  flag is a POKEd byte instead of a 6-byte float. 4.79 s for one
  iteration (C: 0.093 s/iteration). SIEVE.BAS stays in the menu as
  "the lesson". This is K-01 doing real work from BASIC.
9. **Every benchmark explains itself** (REM header: what the loop
  measures) **and prints era comparisons** — C64/Apple IIe (1 MHz MS
  BASIC), BBC Micro B, MEGA65 estimate — marked stopwatch-era and
  approximate; Ahl's uses the 1984 article's own table. Merging the
  extra lines by line number replaced AHL's RND seed line (line 3) —
  caught by the runner (RANDOM 1000), renumbered.

## 2026-08-24 (d) — the card staged, the guide begun, and Space Invaders

(Recovered entry: this and the next were mistakenly committed into the
k4510-build clone; rewritten here where they belong.)
Commits `669b6b2`, `741035f` (guide), `8c19077`. The Pi kernel rebuilt
on p15 (the circle env is pinned to the 15.2 ARM toolchain; 14.2 gives
header soup); `pkg/` = the full card. SID stats: 155/199 play (77%).
EhBASIC `RUN "name"`; the OOM sieve retired; the User's & Programmer's
Guide begun in `doc/guide/` (A5 XeLaTeX, Neo6502-style, screenshots
captured from the machine at build time, builds on ubuntu-s1 in the
`k4510-build` clone, 17 pages, PDF sent). Benchmarks re-reported
(clean bodies, THIS MACHINE line, aligned era tables, metric stated;
S/E marks dropped; AHL/FLOAT/EXPR full reports). `INVADERS.BAS`:
Space Invaders in EhBASIC on the banked text screen — the screen is
9,600 bytes, TWO bank registers; one window left the player's row
poked into invisible plain RAM. EhBASIC has no ERASE (wave 2 would
have double-DIMmed); landing invaders end the game. DEMOS menu key I.

**Card written, 2026-08-24.** 3.7 GB FAT32 "K4510", firmware + new
kernel + rom + fs + 199 tunes, kernel and ROM verified byte-for-byte.
**But**: staged BEFORE the invaders/benchmark commits — INVADERS.BAS
is not on it (Doc noticed immediately). Re-staged in the next entry.

## 2026-08-24 (e) — the Pi's choppy SIDs, and the card rewritten

Doc from the TV: INVADERS.BAS missing (the card had been staged before
that commit — mine), and "the sid sounds on the RPI3 are very very
bad, choppy choppy choppy". The laptop was suspended, so the fix was
made in the ubuntu-s1 clone (`k4510-build`), pushed to the mirror
(commit `09919dd`); the laptop pulls when it wakes.

**The chop:** a performance regression, not an audio bug. The Pi 3 ran
the machine at ~17 ms against a 16.7 ms frame — no slack — and two
recent changes spent per-access time: the debug recorder stored every
opcode fetch address (a store per emulated instruction), and the
"I/O page always wins" rule put an extra compare first on *every*
memory read and write. Invisible on the laptop, over budget on the Pi:
the audio ring starves and stutters. Now the fast path is back to its
one-compare shape (the I/O check lives inside the unmapped/banked
branches, same semantics, all tests green) and the PC recorder is
armed only when dumping is on (`DUMP` / `DUMP ON`; armed at reset on
the desktop, off on the Pi).

**The card**, rewritten and verified: fixed kernel, current fs (26
BASIC files including INVADERS.BAS, 199 tunes), README. Along the way:
two log entries had been committed into the k4510-build clone instead
of this repo (wrong cwd) — clone reset, entries recovered above.

## 2026-08-24 (f) — no more Memory size ?, LOADED, and sprites in BASIC

Doc's three. Commits `162aaa6`, `711f6b5`; pushed; 10 suites green;
emulator restarted.

- **"Memory size ?" is gone** — the cold start falls straight into the
  auto-size probe (the machine knows its memory).
- **A plain `LOAD` prints `LOADED`** (chains stay silent). The message
  is printed from inside the input-vector feed with the line editor's
  X and Y preserved — the first attempt returned with X = message
  length and the editor committed stale buffer text ("OS.BAS"" →
  Syntax Error). A second attempt wiped the feed's byte-reading code
  by slicing at the wrong `k_fi_end` (the BEQ reference, not the
  label): LOAD printed LOADED and loaded nothing. Both found by the
  headless harness; the file was reset to git HEAD and patched once,
  cleanly. Lesson relearned: patch-stacking an asm file is how you
  make it lie to you.
- **EhBASIC has sprites**: `SPRITE n,x,y` (position + enable),
  `SPROFF n`, `SPRDEF n,page,w,h,bpp` — the data address is given in
  256-byte pages so a 16-bit argument reaches 16 MB; w/h from
  8/16/32/64; 4 bpp uses palette 16-31 (palofs 1). Three new tokens
  wired through all four EhBASIC tables (defs, CTBL vectors, crunch
  dictionary, LIST names); the attribute table lives at $03F800,
  cleared by DMA on first use, and every sprite statement re-points
  VICKY at it — so the ROM's video restore (sprites off) is undone by
  the next statement.
- **`INVADER2.BAS`** (DEMOS key J): Space Invaders with hardware
  sprites — 24 invaders + ship + bullet + bomb as 16x16 4-bpp shapes
  poked into $040000 through a bank window from DATA rows, pixel
  coordinates, smooth ship. One bug: shapes were poked 128 bytes apart
  but SPRDEF pages are 256 — the player wore the bullet's costume and
  the bullet pointed at zeroes.

## 2026-08-24 (g) — INVADER2 plays like the real thing

Doc: one bomber, too fast, slow ship, no bases. The first three were
ONE bug: the game never seeded EhBASIC's RNG, and an unseeded RND(0)
returns 0 forever — so the "random" column was always column 0 and
RND(0)<0.03 was always true (constant fire). Seeded; the firing rate
is 0.02+wave*0.005 per frame; and the classic rule: the LOWEST living
invader of a random column drops the bomb. The ship reads every queued
key per frame (step 8) instead of one. And four bases, 32x16 sprites
with an arch, each owning its own shape page: a hit (bomb from above,
bullet from below) pokes a hole into the shape through the bank
window — real erosion, visible in the screenshot. Commit on the
laptop, pushed; emulator restarted.

## 2026-08-24 (h) — the teleporting bullet, and the real invaders

Doc: the shot leaves the ship, then "comes out of the rightmost base".
The frame-by-frame sprite trace showed the bullet's X becoming 500 the
moment it entered the base band — and the cause is a museum piece:
**EhBASIC, like every MS-family BASIC, reads only the first two
characters of a variable name.** The base test's loop variable `BX0`
IS the bullet's `BX`; each pass left BX at the last base's X (500).
Renamed to QX, with a REM in the file so the next 8-year-old learns it
the easy way. (Two lesser bugs en route: a REM appended mid-line
swallowed the rest of the statement, and a rebuilt line 205 blew the
126-character line limit — "Syntax Error in line 205" from the feed.)

The sprites are the arcade originals now, encoded from the canonical
patterns (no need to scrape them: squid 8px, crab 11px, octopus 12px,
the stepped cannon, the zigzag bomb) — squid row white on top, crabs
yellow, octopuses green, 30/20/10 points by row as in 1978. Six shape
pages; the bases moved to pages 1030-1033 and the erosion offsets
followed. Commit `<laptop>`, pushed; emulator restarted.

## 2026-08-24 (i) — alpha-0.2 shipped, the card written, and the Tube

Doc: write the card, cut the release, then "port BBCSDL — and BBC BASIC
must see the 256 MB, not the 64K business; the ROMs are too big."

**alpha-0.2 "Octopodes"** is on GitHub with the full card zip; the SD
card itself written and verified on p15 (after one polkit stand-off:
ssh cannot mount removable media there — the desktop session's
auto-mount does it, so the card must be freshly inserted).

**BBC BASIC.** BBCSDL's interpreter is compiled C for x86/ARM — it
cannot run ON a 6502-descendant, and no memory trick changes that. But
Acorn answered this exact question in 1981: the **Tube**. BASIC runs on
a co-processor with its own flat memory; the host machine does the I/O.
So the K4510 grew a Tube at `$D800`: the emulator host runs Richard
Russell's BBCTTY console edition (vendored under `tube/`, zlib licence,
alterations marked; the name "BBC BASIC" is the BBC's, used by
Russell's permission — the docs say "running Russell's interpreter"
and claim nothing) on a pty; the ROM's `BBC` command is the console.
Three terminal puzzles on the way: the console edition asks
`ESC[6n` ("where is the cursor?") and hangs without an answer; a wrong
answer is worse — it probes the terminal WIDTH by jumping to column
999 and asking where it landed, so the guest must honour `ESC[r;cH`
with clamping and answer with its true cursor; and my first host-side
answerer replied "column 1" to everything, which made BBC BASIC
believe the terminal was one character wide and print the banner
vertically, one letter per line.

**The requirement, met:** `HIMEM=PAGE+250*1024*1024` → "250 MB FOR
BASIC"; `DIM A 200000000` succeeds; byte 199,999,999 pokes and reads
back. MAXIMUM_RAM patched (marked) from 4 GB to 256 MB to match the
machine. The Pi has no Tube yet — the C core would compile into the
Circle kernel (bbdata_arm_64.s exists) as phase 2, on the ballot.

## 2026-08-24 (j) — star commands, and the OS has a name: K/OS

BBC BASIC's `*` prefix now does what it did on a real BBC — talks to
the operating system. BBCTTY's OSCLI routed every unknown star command
into the host's `system()` (a host leak into the machine); the marked
patch emits `ESC ] K4510 ; <cmd> BEL` instead, and the console executes
it in the machine's shell. So `*INFO`, `*ECHO`, `*MODE`, even `*BALLS`
work from inside BBC BASIC; BBC keeps its own stars (`*QUIT`, `*FX`,
`*LOAD`...), and the co-processor is homed in `fs/`, so its own file
commands see the machine's filesystem. (`@` stays EhBASIC's escape —
in BBC BASIC `@` is the `@%` print-format variable and must not be
taken.) The ROM's BSS was 17 bytes short for an OSC buffer; the shell
line buffer serves.

**And the OS is named: K/OS** — pronounced "chaos" — Doc's pick from
the shortlist (FRED is reserved for the MATH unit's future, JIM for
whatever earns it; SHEILA already works here). Banner, INFO, README.
Commits `641d9ec` + one; pushed; emulator restarted.

## 2026-08-24 (k) — one star to rule them

Doc: "@ for EhBASIC and * for BBC BASIC — too confusing?" Agreed.
Unified on `*`, the Acorn-correct choice: `*DIR` now runs a K/OS
command from EhBASIC too. It could not be done at the crunch level the
way `@` was — `*` is multiplication everywhere except the start of a
line — so the cruncher remembers where the statement text starts
(`k_crx0`) and only a line-leading `*` is a prefix; `PRINT 2*SIN(1)`
is untouched. The reverse unification was impossible: in BBC BASIC `@`
is the `@%` print-format variable. `@` remains a quiet EhBASIC alias.
Menus and READMEs teach `*`. Commit pushed; emulator restarted.

## 2026-08-24 (l) — the full BBC BASIC: graphics and sound over the Tube

Doc asked for the full BBCBasic/SDL experience on the machine (with the
RPi3 port to follow). The SDL edition would have opened its own window
on the host — illusion gone — so instead the *console* edition grew the
missing chips, the Acorn way: it already serialises every graphics
command as VDU bytes (`PLOT` **is** `VDU 25,k,x;y;`), and SOUND was an
empty stub. `bbccos.c` now forwards the graphics codes as
`ESC]K4G;...BEL`, `sound()` emits `ESC]K4S;...BEL`.

First attempt interpreted them in the console ROM: **2 KB over the
24 KB ROM budget**. The fix is nicer than the plan: the **Tube ULA**
(core/io.c) — on a real Beeb just the FIFO chip — watches the byte
stream itself and executes the machine escapes before the console ever
sees them. K4G drives the VICKY blitter: 640x480 8bpp bitmap at
$200000 (EhBASIC's GRAPHICS surface), layer 1 over the text, colour 0
transparent, BBC 1280x1024 bottom-left coordinates scaled on, BBC
logical colours in palette 16-31 with per-MODE default maps. PLOT does
lines, points, triangle fill, rectangle fill, circles (outline+fill,
scanline + isqrt); GCOL, CLG, VDU19, VDU29 origin all work. K4S feeds
a new **sound sequencer at $D5E0-$D5E3**: the Beeb's four queued sound
channels in K4510 silicon — channels 1-3 pulse on SID 0, channel 0
noise on SID 1, quarter-semitone pitches, frame-clocked durations,
**64 notes deep per channel** (the Beeb blocked BASIC at four pending;
a one-way Tube can't block, so the queue got deep enough for a whole
tune instead). The ROM keeps only what it must: MODE switches the text
geometry (the ULA passes 22 through), SGR colours (COLOUR works now),
ESC[J (CLS clears), restore on exit — ~400 bytes.

`fs/BBC/`: seven period-style demos — KALEID, CIRCLES, ROSES, CLOCK
(runs on TIME$, i.e. the host clock), TUNE (Frère Jacques as a
three-voice round — the sequencer's party piece), BOUNCE, MOUNTAIN —
plus README.BBC. BBC BASIC LOADs the ASCII listings directly.
Verified headless: a MOVE/DRAW put its pixel exactly where the scaling
predicted (row 240, x 333, colour 19), triangle fill solid, the round
showed three voices live on SID 0. All 11 suites green. Commit
`73b6519`, pushed; emulator restarted. Pi note: the ULA + sequencer
live in core/io.c, which the Pi kernel compiles — when the co-processor
moves into the Circle kernel (phase 2), graphics and sound are already
there. Known edges, on the record: POINT/TINT read-back not
implemented; GCOL modes 1-4 (OR/AND/XOR) drawn as plain; VDU5
text-at-graphics-cursor prints as normal text; `*DUMP` inside BBC
BASIC is taken by BBC's own *DUMP (use it from the shell).

## 2026-08-24 (m) — the filesystem gets its names

Doc: `/BASIC` → `/EHBASIC`, `/BBC` → `/BBCBASIC`. Renamed (git mv), and
the bare-name search in the fs device now looks in `/PRG`, `/EHBASIC`
**and** `/BBCBASIC`; Makefile, shell HELP, README.md, README.BBC and
the DEMOS menu all follow. `RUN EHBASIC` still works (found by search),
`LOAD "BBCBASIC/KALEID.BBC"` is the new BBC path. Commit `eedde0f`,
pushed both remotes; emulator restarted.

## 2026-08-24 (n) — demos live with their language

Doc ratified the layout: no separate demos folder — each language's
demos live in its own directory. The tree already complied (`/EHBASIC`
holds the EhBASIC demos + interpreter, `/BBCBASIC` the Tube demos,
`/PRG` the machine-code ones, `/SID` the tunes); the convention is now
written into README.md so it stays that way. Commit `38ea68a`.

## 2026-08-24 (o) — K/OS grows up: commands, ARGS, !BOOT, hidden files

Doc's three asks became one release (`e918f8b`): new shell commands
**RENAME/REN/MV, CP, XD (alias HEX — classic hex+ASCII dump, Esc
stops), EXEC, HUSH, LS, WOZ** (Doc remembers the monitor as WOZ; now
it answers to it), **DIR A** for dot-hidden files (ratifying what the
fs device already did), **/!BOOT** as a power-on EXEC script
(half-second grace; any key — held or typed — skips silently without
eating the keystroke), and the **REXX rule completed**: an unknown
shell word runs `name.prg` from disk *with arguments*, readable via
the new **ARGS system call ($FF95** — the last free jump-table slot;
stub page and jump table are now full to the byte). `SAY HELLO` →
`fs/PRG/say.prg` prints HELLO. io.c gained fs commands 16–18
(RENAME, COPYFILE, DIR_ALL).

The evening's great debugging comedy: every new .prg looked crashed —
screens wiped, no output, hours of bisecting ROMs that were entirely
innocent. The culprit was `run_at()`'s **unconditional `cls()` when a
program returns** (why chrout.prg ends with "press a key"). SAY would
print and the ROM would erase it a millisecond later. Fix: snapshot
the VICKY controls around the call; only a program that actually drew
gets the video restore + clear. Print-only programs now behave like
commands — output stays.

ROM pressure: both halves ran out twice. HELP's text moved out of the
ROM into the dot-hidden **/.HELP** file (HELP = TYPE /.HELP), the new
file commands rebalanced into the $A000 half, and four initialized
statics went to BSS — under 100 bytes spare in each half now. The
next feature pays rent or lives host-side. test/headless gained
K4510_EXITDUMP=1 (dump on exit — found the cls bug); romtest boots 40
frames to cover the grace window. 10 suites green; emulator restarted.

## 2026-08-24 (p) — a third language: FORTH (Tali Forth 2, native)

Doc: "how about a simple Forth interpreter?" Chose **Tali Forth 2**
(Scot W. Stevenson / Sam Colwell — public domain, ANS-core, written
for the 65c02 with a three-routine porting surface) over the 1980
FIG-Forth listing. Vendored unmodified in `forth/tali/`;
`forth/platform.asm` is the *entire* port (~100 lines): CHROUT/CHRIN
through the ROM jump table (wrapped to preserve X/Y), `KEY?` peeking
$D101 without consuming, memory map, and the .prg header. Assembles
with the already-installed 64tass; no Rockwell opcodes anywhere, so
the 45GS10 runs it as a strict 65c02 (with `LDZ #0` pinned at entry).
Kept the interactive assembler and DISASM — on this machine they are
the point; dropped blocks and ed (no block device yet). 17 KB image
at $4000, dictionary RAM $0900–$3BFF (~12.8 KB), typed `FORTH` at the
shell thanks to the REXX rule — `/FORTH` joined the host-side
bare-name search for zero ROM bytes. The ROM did not grow at all.

Two real bugs, both instructive. First: typing `forth` printed `?` —
the fs device happily `fopen()`ed the `/FORTH` *directory* (Linux
allows it), the header read failed with "bad file", and the shell
only retries `.prg` after "not found". A directory is now not-found
when opened as a file. Second: BYE hung the machine — Tali's COLD
resets the stack pointer to $FF and claims the whole stack page, and
its default RAM map ($0200 buffer, $0300 dictionary) sat directly on
the ROM's data/bss/C-stack at $0200–$07FF. Forth's RAM moved to
$0800–$3FFF, and kernel_init now saves the stack bytes above the
entry SP (K/OS's call frames) so BYE can put them back: `bye` returns
to the shell with the screen, the shell log, and the session exactly
as they were. Verified: `2 3 + . 5 ok`, `: cube dup dup * * ; 7 cube
. 343`, DISASM listing our own kernel (flagging the LDZ byte with a
polite `?`), the assembler assembling, then `bye` → `dir` → `type`
all live. `/FORTH/README.TXT` on the disk is the crib sheet. 10
suites green; commit `bf0cfc3`, pushed both remotes. Parked: a
file-loading word (`INCLUDED` through the fs device at $D300) so
Forth source can live on disk like everything else.

## 2026-08-24 (q) — the Z80 second processor: CP/M on the Tube

Doc, after Forth: "can you make a tube out of this?" — *this* being
RunCPM (github.com/MockbaTheBorg/RunCPM). Which is historically
delicious: Acorn's Z80 Second Processor of 1984 existed to run CP/M
over the Tube, so plugging RunCPM into our Tube slot rebuilds a real
product. Vendored unmodified (MIT) in `cpm/src/`, built with
`CCP_INTERNAL` so **no DRI binaries ship** — the licence table stays
clean. The Tube start register grew a program code (1 = BBC BASIC,
3 = CP/M); the ROM's `CPM` command is just `cmd_bbcbasic(3)`; drives
A:–P: are `fs/CPM/A`..`P` with user areas as numbered subfolders, so
CP/M files are ordinary machine files. RunCPM's banner proudly
estimates the Z80 at ~3000 MHz — the fastest Z80 Acorn never sold.

The ROM extracted its rent for the ~30-byte `CPM` command: DATA
overflowed ROM2 by 33 bytes, so `cmd_hush` emigrated to the $A000
half and the no-Tube error lost its subclauses. (First relocation
attempt landed cmd_hush inside run_at's CODE2 island — the file's
pragma pushes are a minefield; anchor on the right pop.) Also fixed
en route for Forth's sake earlier today and now doubly load-bearing:
`fopen()` on a directory no longer masquerades as "bad file".

Verified end-to-end in headless: `cpm` → CCP banner → `dir`, `type
readme.txt`, `Bdos Err on D: Select` (no D folder — correct), `save 1
test.com` writing a real host file, `exit` → "the Tube co-processor
has left." → shell intact. /.HELP gained a **tongues:** line — the
machine now speaks EhBASIC, BBC BASIC, Forth and CP/M. 10 suites
green; commit `628c5f7`, both remotes. Parked: a CP/M software drive
(Zork, Turbo Pascal, WordStar need fuller VT emulation in the console
for the screen-oriented ones — line-mode programs work today).

### Addendum (q): the system disk

Doc remembered right: RunCPM ships `DISK/A0.zip` — 81 files, 898 KB,
the whole 1980s in one folder: DRI's ASM, MAC, DDT, ZSID, STAT, PIP,
ED, SUBMIT (patched per DRI's own fixes), the BDOS and CCP *source
code*, Z80ASM with its PDF manual, XMODEM, LU/UNARC/USQ/UNZIP, the
ZEXDOC/ZEXALL Z80 exercisers — and **MBASIC**. Unzipped into
`fs/CPM/` on the laptop and verified live: `STAT DSK:`, a full `D`
listing, then Microsoft BASIC-85 Rev. 5.29 printing squares over the
Tube. The machine now runs five language implementations across
three CPU architectures. The disk itself stays out of the public
repo (mixed provenance; `fs/CPM/.gitignore` documents the one-line
install), commit `d53981c`.

## 2026-08-24 (r) — the guide catches up with the machine

Doc asked for the User's Guide to continue in the established format
(A5 XeLaTeX, Neo6502-handbook style, every screenshot captured from
the running machine at build time — a shot that cannot be produced
fails the build). The shell chapter was rewritten for the current
K/OS (REXX rule with SAY, /!BOOT, dot-hidden files, XD/HEX, WOZ,
HUSH), chapter 3 became "EhBASIC", and three new chapters joined the
User's part: **The Tube: BBC BASIC** (with a live MODE 2 KALEID
screenshot), **Forth** (the session shot ends with the Tali kernel
disassembling itself, `?` at the LDZ byte), and **CP/M: the Z80
Second Processor** (the CCP's DIR over the system disk). 24 pages,
zero overfull boxes after \emergencystretch went into the style.

Build-machine archaeology: k4510-build on ubuntu-s1 has no cc65 and
no nasm, so rom/kernal.bin was a stale leftover (the first shot run
quietly used a pre-Forth ROM) and tube/bbcbasic didn't exist (the
BBC shot showed "the Tube co-processor has left"). Fixed by scp-ing
the ROM, fs and bbcbasic binary from the laptop; also untracked
cpm/runcpm, which an earlier `git add -A` had committed (build
product, host-arch binary, public repo). Commits a245b59..feccfff +
style fix; PDF delivered to Doc.

### Erratum (r), Doc's catch

The guide (and two README sites) taught `@DIR` as the way to reach the
shell from BASIC. The ruling on record is the reverse: `*` is the
universal OS-command prefix in both BASICs — BBC heritage — and `@` is
only an EhBASIC-side alias, a survivor. Fixed as `e7a6462`; the guide
now teaches `*DIR` and footnotes `@`. (Also: this log entry's
predecessor was committed into the wrong repo by a wandering working
directory — moved here, build checkout reset clean.)

## 2026-08-24 (s) — the diary moves in with the code

Doc: "keeping the docs in the main repo is a better idea for now." So
this file, the design doc, VICKY-SPEC, the decision records, FEATURES
and the curated images now live in `docs/` of the k4510 repo itself —
versioned and pushed with the machine they describe. The old
standalone docs repo on ubuntu-s1 stays behind as a local archive
(with its 71 commits of history, the 80 MB of full-resolution phone
photos in screenshots/, and the guide build area). Errata session
earlier tonight, all Doc's catches: * not @ as the BASIC shell prefix,
Memory size ? gone from EhBASIC's boot, the CP/M screenshot that was
secretly photographing Forth's README (make-shots now refuses to run
without its co-processors), and a knowing note on the 3 GHz Z80.

## 2026-08-24 (t) — install.sh, twice

Doc: the SD-card zip "will not be obvious to many" — and he is right,
because the trap is real: the Pi boot ROM reads only FAT32, SDHC cards
(<=32 GB) ship FAT32 and just work, SDXC cards (64 GB+) ship exFAT and
produce a silent black screen. So: **install-sd.sh** (unzips onto a
mounted card, refuses exFAT with an explanation; --format mode wipes a
device to FAT32 with belt-and-braces guards — whole-device only, never
the root disk, type-the-name confirmation) and **setup.sh** (desktop:
apt/dnf/pacman deps — gcc, SDL2, cc65, 64tass, nasm — then make all,
the Tube, CP/M, and the 10-suite battery, with honest reporting of
what is missing and what it is needed for). README gained an
Installing section; the guide's chapter 1 gained "Getting one" and an
SDHC aside before "First boot".

### Erratum (t), from Doc's bench

"Any old card is plenty" — no. Doc tried his 2 GB plain SD (pre-HC)
cards: they do not boot the Pi. So the guidance everywhere is now
*SDHC specifically, 4–32 GB*: older SD fails (field-tested), newer
SDXC ships exFAT and needs the --format treatment. Goldilocks cards.

## 2026-08-24 (u) — sideways ROM and RAM under the I/O page

Doc: "couldn't we move the real roms to far memory and just keep jump
tables in the main 64k?" — which is the BBC Master's sideways ROM,
rediscovered from first principles. Built tonight, both halves:

**Sideways ROM.** The ROM file is now the 24 KB base plus appended
8 KB banks living at phys $0FF00000; the $A000-$BFFF window pages
them via bank register 5. kernal.c was resegmented: resident core
(console, fs, dispatch, runtime, RODATA) in $C000-$CFFF + the $E000
half; cold commands in the window (bank 0 = the base image, no shim
needed); INFO and TIME moved to **bank 1** as proof, called through a
5-line sw_call() shim. The stub page needed zero changes — rom_push
already banks 5-7 off around every system call, so syscalls from
sideways code, and star-commands from inside EhBASIC reaching bank 1,
just work (verified: *TIME and *INFO from a running BASIC, PRINT 6*7
still 42 after the round trips). One cc65 landmine: string literals
pool at end of file and ignore rodata-name pushes unless
**--local-strings** is set — without it every "moved" command left
its strings behind. The famine is over: bank 0 has 660 B spare,
bank 1 has 4.3 KB, resident halves ~2.6 KB combined, and 15 more
banks are one append away. INFO now cheerfully reports its own bank
engaged (banks: 5=$0FF00000) while it runs.

**RAM under the I/O page (K-06).** A MAP of block 6 hides $D000-$DFFF
and exposes RAM — MAP is an instruction, so no register deadlock; the
bank-register view keeps I/O on top (SID players unaffected), and the
$D000 address never moves (199 C64 tunes hardcode $D400). With ROM
banked away too: 61.75 KB contiguous. maptest grew case K-06.

Also: CPM's greeting no longer claims to be BBC BASIC. Parked, next
stage of the pinned plan: user space to $CFFF by default and EhBASIC
relocated ("38911 BASIC BYTES FREE"). All 10 suites green.

## 2026-08-24 (v) — stage 3: the payoff. 45567 BASIC BYTES FREE

The memory plan's payoff stage, Doc's "ok do it". Programs now own
$0800-$CFFF and $E000-$FEFF **by default**: run_at() builds a 28-byte
trampoline in RAM at $02D8 that engages banks 5-7 onto the RAM under
the ROM (skipping blocks a K4SG load claimed), calls the program, and
turns every bank off on return. It must be RAM: the instant block 7
engages, the ROM half that built it is gone. Which was also the
evening's one real crash — the setup code used w32() on the bank
registers, whose byte-3 write IS the engage trigger, so run_at
vaporised its own ROM mid-flight (Z ended up 1 and the console spent
a while printing invisibly into the attribute plane — a new entry for
the phantom-crash genre). Byte 3 now stays in the trampoline, and the
K4SG loader got the same discipline for blocks 5-7.

EhBASIC moved above BASIC's RAM: three K4SG segments — $E000-$FEFF
(7839 bytes, cut at LAB_NLTO after an automated search for a boundary
no relative branch crosses), $C000-$CFFF (core tail + math + the
expression compiler), and $BA00-$BFFF (file + gfx glue) in the RAM
under the sideways window. Ram_top = $BA00 and the machine boots:
**45567 Bytes free** (was 25599). DIM A(9000) really allocates 36 KB;
demos, graphics, LOAD/SAVE, star commands, sidplay, sprites all
green. Tali Forth's image moved to $8C00: the dictionary grew from
12.8 to ~31.7 KB. The C64's 38911 is beaten without an asterisk.

Also on the record (relayed from Doc via the archive-side session):
**msbasic is coming** — Microsoft's MIT-licensed 6502 BASIC via
mist64/msbasic, eventually beside/replacing EhBASIC (whose
non-commercial licence is the murkiest thing in the ROM). Sideways
banks 2-3 are earmarked for it, though stage 3 opens a second door:
it could live EhBASIC-style in high K4SG segments instead. Decision
when it's ported; entry (w) below carries the full research.
## 2026-08-24 (w) — the BASIC research: what could replace EhBASIC (mirrored from the archive log, commit a7ccffc)

Discussion session (this host, archive folder — the coding session was
busy reshaping the sideways-ROM map and was left alone). It started as
"could adding elements of the 8510 give us 128k ram?" and ended, three
questions later, at "research other BASICs for the 4510 side." Along
the way: the 8510 is just an HMOS 6510 — the C128's 128K was the
**8722 MMU**, not the CPU; its `122365 BYTES FREE` was two 16-bit
pools summed through common-RAM trampolines; and the Tube BBC BASIC
already owns 250 MB, so the native side is the open question.

Two web-research agents ran in parallel: one on the Commodore lineage,
one on independent 6502 BASICs. **Outcome, decided by Doc the same
day: bring Microsoft 6502 BASIC (MIT since Sept 2025) into play;
the coding session was asked to reserve a sideways-ROM slot.** The
full reports follow, pasted verbatim for the record.

---

### Report 1 — Commodore-lineage BASIC options (45GS02-native)

#### The headline finding
**Microsoft open-sourced 6502 BASIC v1.1 (1976-78) under MIT in
September 2025** — github.com/microsoft/BASIC-M6502 (repo archived
read-only 2025-09-05, license stands). This resets the legal landscape
for everything MS-lineage.
([Hackaday](https://hackaday.com/2025/09/04/microsoft-basic-for-6502-is-now-open-source/),
[Tom's Hardware](https://www.tomshardware.com/software/bill-gates-48-year-old-microsoft-6502-basic-goes-open-source))

#### 1. C65 ROM BASIC 10 (910111)
- **License: PROPRIETARY** — all Commodore ROMs owned by Cloanto; the
  C65 ROM is licensed to MEGA65 per-device (fee per unit sold) and
  bundled in C64 Forever since 2022. No standalone redistribution
  right. ([generationamiga.com](https://www.generationamiga.com/2020/04/09/cloanto-licenses-commodore-65-rom-for-the-upcoming-mega-65/),
  [forum64 thread](https://forum64.de/index.php?thread%2F106568-about-the-license-of-the-c65-rom=))
- **Source:** never released; only community disassemblies
  (zimmers.net hosts binaries — legally grey to even mirror).
- **>64K:** yes, natively — program text in bank 0, variables/strings
  in bank 1 (the model BASIC65 inherited); graphics commands
  DMA-backed.
- **Porting: HIGH** — binary-patch-only (no legal source), deeply
  entangled with VIC-III, C65 F011 DOS, CIAs, C65 Kernal jump table.
  Also famously buggy (prototype ROM).
- **Footprint:** part of the 128 KB system ROM (BASIC+editor+graphics
  roughly half; Kernal/DOS/charset the rest).
- **Verdict for a public repo: unusable.**

#### 2. MEGA65 BASIC65
- **License: PROPRIETARY** — the "closed ROM."
  github.com/MEGA65/mega65-rom-public is a **bug tracker only**: "As a
  MEGA65 owner you have acquired a license to the ROM"; source repo
  access is by request on Discord, owners-only, no
  redistribution/derivative rights for other machines.
  ([mega65-rom-public](https://github.com/MEGA65/mega65-rom-public))
  Underlying Commodore rights still Cloanto's.
- **>64K:** yes, the best of the family. Commands per the MEGA65
  User's Guide ([memory.tex](https://github.com/MEGA65/mega65-user-guide/blob/master/memory.tex)):
  - `BANK n` — sets the bank used by `PEEK/POKE/SYS/BLOAD/BSAVE` etc.;
    with BANK 0–5 or address >$FFFF, PEEK/POKE go through the
    45GS02's base-page quad-pointer ([addr32],Z) far addressing —
    i.e. real 28-bit PEEK/POKE.
  - `DMA` — C65-style F018 DMA job from BASIC; `EDMA` — enhanced DMA
    with full 28-bit flat source/dest addresses.
  - Extended `PEEK, POKE, SYS, BLOAD, BVERIFY, SCREEN, SPRSAV…` are
    all bank-aware. Known limit: BANK/DMA can't reach colour RAM
    beyond the first megabyte.
  - Program/variable model: bank 0 text + bank 1 variables (BASIC 10
    heritage).
- **Porting: HIGH** — even ignoring the license wall: tied to VIC-IV
  registers, MEGA65 hypervisor traps, C65-DOS/SD controller, CIAs.
  The license alone disqualifies it.
- **Footprint:** 128 KB ROM total (BASIC is the majority).
- **Verdict: the feature model to imitate, not code to take.**

#### 3. MEGA65 Open-ROMs
- **License: CLEAN (GPLv3)** — documented clean-room process (spec
  from books like Mapping the C64, similarity tool flagging >2-byte
  matches against originals).
  ([open-roms](https://github.com/MEGA65/open-roms))
- **Completeness (per [STATUS.md](https://github.com/MEGA65/open-roms/blob/master/STATUS.md),
  still true in 2025-2026):** Kernal is substantially done (IEC,
  devices, screen partial); floating-point math package done; **but
  the BASIC interpreter core is not**: integer/float variables/arrays
  NOT DONE, expression handling only partial (strings mostly work),
  FOR/NEXT, GOSUB, IF/THEN NOT DONE. It boots to READY and runs
  trivial things; it cannot run real BASIC programs.
- **>64K:** extended-BASIC ideas exist for MEGA65 builds, but nothing
  bank-aware is usable yet.
- **Porting: HIGH as a working BASIC** (you'd be writing the
  interpreter yourself), **LOW-MED as a parts quarry** (GPL3 math
  package, string GC, Kernal patterns). Note GPLv3 would apply to
  your ROM if you incorporate it — fine for your repo, but it's viral.
- **Footprint:** C64-shaped (8K BASIC + 8K Kernal regions; MEGA65
  build larger).

#### 4. mist64/msbasic
- **License: MURKY-but-now-anchored.** The repo's README claims
  "2-clause BSD" — but that can only cover mist64's reconstruction
  scaffolding; the code itself is Microsoft's (byte-exact rebuilds of
  9 shipped ROMs). **Since Sept 2025 the Microsoft core is genuinely
  MIT** via microsoft/BASIC-M6502. Residual murk: the
  Commodore-specific patch levels (CBM BASIC 1/2 ROM-exact builds)
  contain **Commodore-authored modifications** that the MIT grant
  doesn't cover and Cloanto still claims.
- **Practical combo:** microsoft/BASIC-M6502 is the *legal anchor* but
  is written in PDP-10 MACRO-10 syntax; msbasic is the *buildable
  equivalent* — ca65/cc65 toolchain, same one your ROM already uses.
  Build a generic/OSI/KBD-style config (pure-MS feature set), avoid
  the CBM-ROM-exact targets, and cite the MS MIT release in your
  license table.
- **>64K:** no — 16-bit pointers throughout; strings, arrays, program
  text all in one 64K image. You'd bolt on far PEEK/POKE/DMA tokens
  yourself (your 45GS02 [zp],Z far pointers make the *data* side
  easy; making program/variable storage >64K is a rewrite).
- **Porting: LOW** — designed for retargeting: platform config files,
  I/O isolated to a handful of vectors (char in/out, ctrl-C check,
  LOAD/SAVE hooks). Zero VIC/CIA/Kernal entanglement.
- **Footprint:** ~8-9 KB.
- **Source:** github.com/mist64/msbasic +
  github.com/microsoft/BASIC-M6502.

#### 5. Commander X16 ROM BASIC
- **License: MIXED, core is PROPRIETARY.**
  [LICENSE.md](https://github.com/X16Community/x16-rom/blob/master/LICENSE.md)
  is explicit: the `basic` and `math` directories are "©1977 Microsoft
  Corp. ©1983 Commodore Business Machines" under a **commercial
  license from Cloanto valid only "in the context of the X16
  computer"** — outside users are told to contact Cloanto. Only the
  *additions* (new BASIC commands, CMDR-DOS, FAT32, audio API) are
  BSD-2, plus GPLv3 open-roms Kernal pieces. So no, it is not clean
  for reuse, and the community is honest about it.
- **>64K:** partial — `BANK` selects an 8 KB RAM bank at $A000 (up to
  2 MB) for PEEK/POKE/SYS; `BLOAD` auto-wraps across banks. Program +
  variables still live in the fixed low-RAM ~38 KB.
- **Porting: MED** technically (C64-BASIC-shaped, X16 banking
  hardware assumptions) — but license-blocked for the core; only its
  BSD-2 extension code (graphics/audio command implementations, DOS
  wedge) is reusable.
- **Footprint:** ~16 KB ROM bank + annex bank for the extensions.

#### Summary table (report 1)

| Candidate | License verdict | >64K | Porting | Footprint |
|---|---|---|---|---|
| C65 BASIC 10 (910111) | PROPRIETARY — Cloanto, per-device MEGA65/C64-Forever only | Yes (bank0 text/bank1 vars) | HIGH (no source, VIC-III/DOS-welded) | ~half of 128 KB ROM |
| MEGA65 BASIC65 | PROPRIETARY — owners-only source, Cloanto underneath | Yes — BANK, 28-bit PEEK/POKE, DMA, EDMA | HIGH (license + VIC-IV/hypervisor) | 128 KB ROM |
| Open-ROMs | CLEAN — GPLv3 clean-room | Not yet | HIGH as BASIC, LOW as parts quarry | ~16 KB |
| msbasic + MS MIT release | CLEAN for pure-MS configs; MURKY for CBM-ROM-exact builds | No (16-bit; far tokens bolt-on) | LOW — vector-isolated I/O, ca65/cc65 | ~8-9 KB |
| X16 ROM BASIC | PROPRIETARY core (Cloanto, X16-context-only); BSD-2 additions | Partial (BANK + 8K window) | MED, license-blocked | ~16-32 KB |

#### Other options found (report 1)
- **C128 BASIC 7.0 / Plus-4 BASIC 3.5** — the split-pool (bank-0 text
  / bank-1 vars) 7.0 model: same Cloanto-proprietary status as BASIC
  10; original Commodore engineering sources circulating on
  zimmers.net are leaks, not licensed. Dead end for a public repo.
- **microsoft/BASIC-M6502 as a direct base** — worth listing
  separately from msbasic: it is the one genuinely MIT-licensed
  Commodore-ancestor BASIC in existence (the exact code the PET ran).
  Legal gold; needs MACRO-10→ca65 translation, which msbasic has
  effectively already done.
- **A license-table caveat you already carry: EhBASIC.** Lee
  Davison's EhBASIC is "free for non-commercial use" only, and
  Davison died in 2013 leaving the rights orphaned — it is arguably
  the murkiest thing in your current ROM. The pragmatic clean-up path
  given everything above: migrate the native BASIC to an
  msbasic/BASIC-M6502-derived build (MIT), then add your own
  BANK/EDMA/far-PEEK-POKE tokens modeled on BASIC65's command
  surface.

---

### Report 2 — Non-Commodore-lineage 6502 BASICs

#### 1. EhBASIC 2.22 (Klaus2m5 fork) — the incumbent
- **License (exact):** Not OSI. "EhBASIC is free but not copyright
  free" — non-commercial use OK provided binaries/docs carry "Derived
  from EhBASIC"; commercial use required contacting Lee Davison, who
  died in 2013, so the commercial clause is now un-clearable and the
  license can never be regularized. Widely redistributed anyway
  (Klaus2m5, jefftranter, lgblgblgb forks all public on GitHub);
  community treats it as tolerated with attribution.
- **Implementation:** 6502 assembly (single source; many ports to
  ca65).
- **Memory model:** Classic 16-bit pointers everywhere (program,
  vars, strings all in one 64K image). No fork with banked/large
  memory was found — the only sighting is the PZ1 laptop running
  stock EhBASIC *under* a banking scheduler (banking is outside
  BASIC). **>64K: TEACHABLE** — far PEEK/POKE/COPY tokens using
  45GS02 [zp],Z is easy; making program/variable storage itself >64K
  is major surgery on ~16-bit pointer code throughout.
- **REPL:** Y. **Port effort: NONE** (already running).
  **2025-26 activity:** frozen (Klaus fork ~18 commits, bugfix-era).
- URL: https://github.com/Klaus2m5/6502_EhBASIC_V2.22

#### 2. BBC BASIC for 6502 (Acorn original + derivatives)
- **License:** Proprietary, orphaned. The 2018 Apache-2.0 RISC OS
  Open release covers **ARM BBC BASIC V only, not the 6502 ROMs**.
  Stardot consensus: the 6502 BASIC IP passed Acorn→Element
  14→Pace→Castle→(RISC OS Developments) so many times that "not even
  Sophie Wilson is sure who owns it now." J.G. Harston's assemblable
  source on mdfs.net still carries Acorn copyright; the Tube "BASIC V
  for 65C02" there is copyright Colin C Dean, also not free.
  BeebEater (github.com/chelsea6502/BeebEater, for Ben Eater builds)
  is a nice MIT wrapper but **ships the proprietary ROM binary
  inside**.
- **>64K:** NO (16-bit; the BBC's answer was the Tube/sideways RAM,
  outside BASIC). **REPL:** Y. **Port effort:** LOW technically (MOS
  entry-vector shims, BeebEater proves it) but **legally unusable for
  a clean public repo**. No open-source reimplementation of 6502 BBC
  BASIC exists as of 2026.
- URLs: https://mdfs.net/Software/BBCBasic/6502/ ,
  https://stardot.org.uk/forums/viewtopic.php?t=16087 ,
  https://github.com/chelsea6502/BeebEater

#### 3. FastBasic (dmsc) — strongest real candidate
- **License (exact):** GPL-2.0-or-later **with an explicit linking
  exception**: programs you compile with it may be distributed under
  any license. Clean for a public repo.
- **Implementation:** Parser/compiler in 6502 asm (on-machine IDE) +
  C++ cross-compiler on PC; runtime is a small bytecode **VM in ca65
  assembly** — it already builds with the cc65 toolchain the K4510
  uses.
- **Memory model:** 16-bit VM; 16-bit ints + Atari-ROM floating point
  (FP routines would need replacing or dropping for a port).
  **>64K: TEACHABLE, and more cheaply than anywhere else** — because
  all memory access funnels through ~10 VM opcodes, adding
  FARPEEK/FARPOKE/FARCOPY (or even a far string pool) means touching
  the VM, not a whole interpreter. The 45GS02's [zp],Z 32-bit mode
  slots straight into a VM opcode.
- **REPL:** Y-ish — full-screen IDE with editor + instant
  compile-and-run on the machine (not line-at-a-time immediate mode).
- **Port effort: MED** — author states "the libraries are fairly
  portable, so creating a version for other 6502s shouldn't be too
  much work"; work = console I/O layer, replace Atari FP, strip
  P/M-graphics statements. **Activity:** active — v4.6 (2024), v4.7
  discussed on AtariAge; 866 commits.
- URL: https://github.com/dmsc/fastbasic

#### 4. XC=BASIC 3 (neilsf)
- **License:** MIT. **Implementation:** cross-compiler written in
  **D** (runs on PC, emits 6502 via DASM). **REPL: N — compile-only**,
  which fails the core requirement. **>64K:** NO (16-bit codegen);
  teachable only by hacking the D codegen. **Port effort:** MED (add
  a target config + runtime shims; C64/VIC20/C16/Plus4/PET/C128
  supported, X16 via community target). **Activity:** mature, ~970
  commits, slow but alive.
- URL: https://github.com/neilsf/xc-basic3

#### 5. Tiny BASICs (Tom Pittman, DDJ-IL, CorshamTech, uBASIC etc.)
All are 2-8 KB toys with 16-bit (or 8-bit!) address spaces, no
strings/FP worth having, and nothing to say about large memory —
strictly a step down from EhBASIC. (CorshamTech's is GPL-3 and
maintained if a minimal fallback is ever wanted:
https://github.com/CorshamTech/6502-Tiny-BASIC ; uBASIC is BSD, in C,
cc65-compilable, and would run — but it's if/for/goto-only. The irony
is affordable, the language isn't.)

#### 6. dflat (6502Nerd)
- **License:** MIT. **Implementation:** 6502/65C02 assembly.
  **REPL:** Y (interactive, structured BASIC-like: def/enddef
  procedures, locals, recursion, while/repeat — no GOTO). **>64K:**
  NO as-is; TEACHABLE same as EhBASIC (it's conventional 16-bit asm
  inside). **Port effort: MED-LOW** — explicitly designed to port:
  "core language just needs character put and get routines."
  **Activity:** ongoing hobby refinement (Oric-1 branch, ~89
  commits); one-man project, non-standard dialect (existing BASIC
  listings won't run).
- URL: https://github.com/6502Nerd/dflat

#### 7. BASIC816 (pweingar, C256 Foenix) — the 65816 design reference
- **License:** **GPL-3.0** (author has said he'd consider MIT if
  asked). **Implementation:** 65816 assembly (64tass), clean-room,
  interactive REPL, shipped as the stock BASIC of the C256 Foenix U
  (a 65816 machine with up to 4 MB flat RAM). **>64K: YES —
  genuinely** — program text, variable table and string heap
  addressed with native 24-bit long pointers; this is the
  proof-of-existence for "an interactive BASIC whose *heap* lives
  above bank 0."
- **Not portable to the 45GS02** (65816 native mode ≠ 45GS02;
  opcode/register model differs), so **port effort: HIGH / treat as
  reference only**: what it did — keep interpreter code+ZP state in
  bank 0, use long-pointer addressing modes for every data structure
  — maps directly onto the 45GS02's [zp],Z 32-bit pointers.
  **Activity:** mostly 2019-2021; README still calls itself unstable
  though it ships on real hardware. No comparable Apple IIGS/SNES
  open BASIC found (GS BASICs are Apple-proprietary).
- URL: https://github.com/pweingar/BASIC816

#### 8. Wildcard that changed the landscape: Microsoft 6502 BASIC, MIT (Sept 2025)
Technically Commodore-lineage (it *is* the ancestor), but note:
Microsoft released the original 6502 BASIC 1.1 source (m6502.asm,
~6,955 lines, the pagetable multi-target source) under **MIT** on
2025-09-03. If the owner ever wants a license-spotless classic
Microsoft-style core to hack far-memory features into — with zero
attribution ambiguity — this now exists and EhBASIC's grey clause
stops being the only game in town.
https://opensource.microsoft.com/blog/2025/09/03/microsoft-open-source-historic-6502-basic/

#### 9. BASIC-in-portable-C compiled with cc65
Feasible but poor: uBASIC (BSD, dunkels.com/adam/ubasic/) compiles
under cc65 and would fit (~few KB code), but cc65-generated
interpreter code is 3-5x the size and far slower than hand asm, and
every richer C BASIC (MY-BASIC, etc.) blows the 24 KB ROM-bank budget
and assumes malloc/heap ≫ what's free. Verdict: only worth it for a
toy scripting sublanguage, never as *the* BASIC.

#### Comparison table (report 2)

| Candidate | License | >64K | REPL | Port | Note |
|---|---|---|---|---|---|
| EhBASIC (Klaus2m5) | non-commercial + attribution, un-clearable | TEACHABLE | Y | NONE | already running; far PEEK/POKE easy, far heap = surgery |
| BBC BASIC 6502 | proprietary, orphaned IP | NO | Y | LOW (tech) / blocked (legal) | ARM BASIC V is Apache-2.0, 6502 ROMs are not |
| FastBasic | GPL-2.0+ w/ linking exception | TEACHABLE (via VM opcodes) | Y (on-machine IDE) | MED | ca65-based, active 2025, best asm-effort/feature ratio |
| XC=BASIC 3 | MIT | NO | N | MED | compile-only kills it for this use |
| Tiny BASICs | varies | NO | Y | LOW | too small to matter |
| dflat | MIT | TEACHABLE | Y | MED-LOW | portable by design, one-man non-standard dialect |
| BASIC816 | GPL-3.0 | YES (24-bit long ptrs) | Y | HIGH (ref only) | the blueprint for a far-heap interactive BASIC |
| MS 6502 BASIC (2025 MIT) | MIT | TEACHABLE | Y | LOW-MED | Commodore-lineage but now license-clean |
| uBASIC via cc65 | BSD-3 | trivially (C far shims) | Y | LOW | too weak a language |

#### Top 3 for the BMC-K4510 (report 2's ranking)
1. **FastBasic** — the only active, license-clean (GPL2+exception),
   cc65-toolchain-native candidate where >64K support is
   architecturally cheap: add far-memory opcodes to a small bytecode
   VM using the 45GS02's [zp],Z pointers, rather than re-plumbing an
   entire interpreter. On-machine IDE satisfies "interactive."
2. **Keep EhBASIC, teach it far ops** — zero porting cost, users
   already have it; add FARPEEK/FARPOKE/FARBLOCK/bank-pool tokens
   (ROM budget permitting). Accept the grey "Derived from EhBASIC"
   non-commercial clause — fine in practice for a hobby public repo,
   but it can never be made properly open.
3. **dflat** — MIT, explicitly built to be ported to homebrew 6502
   machines, structured and interactive; the fallback if FastBasic's
   Atari FP/runtime extraction proves heavier than expected. Use
   **BASIC816** as the design reference for any far-heap work
   regardless of which core wins.

---

### The synthesis and the decision

Ranked across both reports for this machine: **migrate the native
BASIC to an msbasic/BASIC-M6502 build (pure-MS config, MIT anchor),
then add our own BANK / 28-bit PEEK-POKE / DMA tokens modeled on
BASIC65's command surface** — clean license table, authentic
Commodore-family dialect (EhBASIC is an MS-alike, demos mostly carry
over), ~8-9 KB core, far commands in code we fully own. The banner
can then print a C128-style number the C128's own way (pool
arithmetic) — backed by commands that really reach the 256 MB.
FastBasic stays the runner-up; BASIC816 the blueprint if a true
far-heap BASIC ever goes on the ballot.

Doc's call, same evening: do it. The coding session (then reworking
the sideways-ROM memory map) was messaged to reserve a slot for the
msbasic ROM plus token headroom; the vendoring itself is a later work
item. Decision also recorded in project memory
(`project-k4510-msbasic-decision.md`).
## 2026-08-24 (x) — the font research: a clean chargen (mirrored from the archive log, commit 9ea9086)

Same discussion session, next question from Doc: "research other 8x8
bitmapped fonts that are license compatible? ascii and petscii." The
motivation is the same trap the BASIC research walked around — the
original C64/C65 chargen ROMs are Commodore IP, Cloanto-claimed, the
same category as the BASIC ROMs. Two web-research agents ran in
parallel: one on PETSCII-flavored/Commodore-style fonts, one on
general open 8x8 ASCII faces. Full reports pasted verbatim below.

**The short version:** the drop-in winner is the MEGA65 open-roms
clean-room chargen (`bin/chargen_openroms.rom` — 4 KB, 512 glyphs,
both PETSCII sets, already in 8-bytes-per-glyph format, LGPL-3.0);
**unscii-8** is the public-domain powerhouse (complete Legacy
Computing/PETSCII repertoire, trivial .hex conversion, but the
512-glyph layout must be assembled by hand); **BESCII** (CC0) is the
most C64-flavored clean design. The prettiest matches — Pet Me and
Style64's C64 TrueType — are both license-excluded. And the legal rule
of thumb the community follows: *inspired-by with visible pixel
differences = safe; pixel-identical to the Commodore ROM = same bytes
as the ROM = don't put it in a public repo.*

---

### Report 1 — PETSCII-flavored 8x8 fonts with clean licenses

#### 1. Kreative Korp "Pet Me" family — NOT usable
- **Page:** https://www.kreativekorp.com/software/fonts/c64/ (the
  `/petme/` URL 404s; download:
  https://www.kreativekorp.com/swdownload/fonts/retro/petme.zip)
- **Coverage:** FULL, best-in-class — Pet Me (PET), Pet Me 2X
  (VIC-20), Pet Me 2Y (CBM2/80col), Pet Me 64/64 2Y, Pet Me 128/128
  2Y. "code points 0xE000-0x1FF encode the complete Commodore 64
  character set"; also mapped to Symbols for Legacy Computing since
  Oct 2019.
- **Format:** TrueType only.
- **License:** "Kreative Software Relay Fonts Free Use License
  v1.2f" — full text at
  https://www.kreativekorp.com/software/fonts/FreeLicense.txt. Free
  redistribution with credit is allowed (clause 1a), **but clause 2
  is fatal**: *"The User may not modify, reverse-engineer, or create
  any derivative works of the Software."* Converting the TTF into
  8-byte-per-glyph ROM data is a derivative work. Also clause 5:
  *"Kreative Software reserves the right to change this license at
  any time without notice."* **Verdict: excluded for ROM
  conversion.** (These fonts are also pixel-exact traces of the
  original ROMs, so the deeper Cloanto question below applies too.)

#### 2. Style64 "C64 TrueType (Pro)" — NOT usable
- **License page:** https://style64.org/c64-truetype/license
- Quoted terms: *"You MAY NOT: sell this font; include/redistribute
  this font in any font collection…; provide the font for direct
  download from any web site."* Embedding permitted only *"without
  any modification and using the same filenames"* (web @font-face),
  or *"as part of a software package but ONLY if said software
  package is freely provided to end users."* No modifications
  allowed in any case; anything more requires negotiating *"a
  (possibly commercial) license."*
- **Verdict: excluded.** No-modification + no-direct-download
  clauses are incompatible with converted ROM data sitting in a
  public git repo. Their https://style64.org/petscii/ page is still
  useful as a *reference* — it defines the "Direct PETSCII" PUA
  mapping (U+E000/E100/E200/E300 banks) but maps to PUA, not to
  U+1FB00, and offers no downloadable table.

#### 3. MEGA65 open-roms — CONFIRMED, ready-made ROM data
- **Repo:** https://github.com/MEGA65/open-roms — license per
  `LICENSE`: **LGPL v3 or later** (not plain GPLv3), copyright
  Gardner-Stephen / Standzikowski; some BASIC files MIT (Microsoft).
- **Charset exists and is complete:** `assets/8x8font.png` (8×4096
  px = 512 glyphs = **both charsets, 2×256, full PETSCII
  graphics**), built by `pngprepare` into
  `bin/chargen_openroms.rom` — a prebuilt **4096-byte chargen ROM,
  already in the exact 8-bytes-per-glyph format** needed. It is a
  distinct clean design, not a pixel copy of the Commodore ROM.
- Bonus: `bin/chargen_pxlfont_2.3.rom` (4 KB, also drop-in chargen
  format) — "PXLfont 88665b RF2.3" by Retrofan; `bin/README.md`
  states: *"PXL font was created by Retrofan, we got a permission to
  include it with Open ROMs under GNU Lesser General Public License
  3.0."* (Outside open-roms, PXLfont's own terms are
  permission-required — e.g. the Ozmoo copy at
  https://github.com/johanberntsson/ozmoo/blob/master/fonts/PXLfont-rf.license.txt
  is Ozmoo-only — so take it *via* open-roms under LGPL-3.0.)
- **Verdict: usable.** LGPL-3.0 on a 4 KB data blob is the only
  cost; for chargen data used as data (not linked code), LGPL's
  obligations reduce to shipping the license + source (the PNG).

#### 4. unscii-8 (Viznut) — CONFIRMED public domain, near-complete
- **Page:** http://viznut.fi/unscii/ (note: expired/invalid TLS cert
  as of 2026-08; content intact)
- **License:** quoted from the page: *"'unscii-16-full' falls under
  GPL because of how Unifont is licensed; **the other variants are
  in the Public Domain**."* So unscii-8 = public domain, no
  conditions.
- **Coverage (verified by downloading `unscii-8.hex`):** 3191
  glyphs; **213 glyphs in U+1FB00 Symbols for Legacy Computing**
  (Unicode 13 added 214 — effectively complete, and the page
  explicitly says the block includes "the missing PETSCII
  characters"); 256 glyphs across U+25xx (box drawing, block
  elements, geometric); card suits at U+2660/2663/2665/2666 all
  present. Full PETSCII repertoire reachable for both upper/graphics
  and lower sets via ASCII + these blocks; also ships `uns2uni.tr`
  (PUA↔Unicode mapping file).
- **Format/conversion:** HEX (Unifont hexdump — for 8x8 each line is
  codepoint + 16 hex digits = **exactly 8 bytes/glyph**; conversion
  is a 10-line script), plus PCF/TTF/OTF/WOFF. **Verdict: easiest
  and cleanest license of all; style is unscii's own, not
  Commodore-look.**

#### 5. CC0/MIT PETSCII-inspired fonts — two verified
- **BESCII** (Damian Vila) —
  https://github.com/damianvila/font-bescii (archived; moved to
  https://codeberg.org/Dmian/font-bescii). `LICENCE` file verified:
  **CC0 1.0 Universal** full legal text. README: "An 8x8 pixel font
  based on PETSCII… PETSCII symbols + some Amstrad CPC 464… PETSCII
  characters mapped using Direct PETSCII mapping" (style64 PUA
  scheme), plus Latin/Greek/Cyrillic/kana. **Coverage: full PETSCII
  graphics repertoire, deliberately *not* pixel-identical** (a
  redesign fixing C64 font flaws — see
  https://damianvila.com/blog/20240515-designing-the-bescii-font.html).
  Format: TTF/OTF/WOFF/WOFF2 **and FontForge .sfd source** (v2.0:
  `Bescii-Mono.sfd`) — conversion needs rasterizing the TTF at 8px
  or parsing the .sfd; moderate effort.
- **funscii** (Wuerfel21) — https://github.com/Wuerfel21/funscii.
  Verified: repo SPDX **CC0-1.0**; README: *"The font itself is put
  into the public domain - licensed under the terms of CC0 1.0
  Universal"* (builder is Apache-2.0). It is a fork of unscii with
  fixes + Japanese; source `font.txt`/`glyphs` in unscii's text
  format; community reports a C64-style binary build. Same coverage
  story as unscii-8.
- FontStruct "PETSCII Commodore"
  (https://fontstruct.com/fontstructions/show/1336244/petscii-commodore)
  is tagged CC0 but is a pixel-copy traced from the Wikipedia
  PETSCII chart — the uploader cannot launder the original bitmap
  into CC0; treat as unsafe.

#### 6. Unicode Symbols for Legacy Computing as mapping target — CONFIRMED
- Block U+1FB00–U+1FBFF, added in Unicode 13.0 (2020) specifically
  for PETSCII et al. Proposal **L2/19-025** ("Proposal to add
  characters from legacy computers and teletext", successor of
  L2/17-435):
  https://www.unicode.org/L2/L2019/19025-terminals-prop.pdf —
  **contains per-machine mapping tables (incl. Commodore PETSCII →
  Unicode)** that can drive a conversion; supplement L2/21-235
  (Unicode 16 additions) at
  https://www.unicode.org/L2/L2021/21235-terminals-supplement.pdf.
- Human-readable PETSCII→Unicode mapping table:
  https://www.kreativekorp.com/charset/map/petscii/
- Open 8x8 fonts implementing the block: **unscii-8** (213/214
  glyphs, verified), **funscii**, Pet Me 2019+ (license-blocked),
  Kreative's Fairfax HD (OFL, but 6x12 not 8x8).

#### 7. Other findings
- **VICE fallback charset:** none exists — VICE (GPLv2+) ships the
  *original* Commodore `chargen` ROM images on the old Usenet-era
  tolerance; the ROMs are not GPL and are exactly the
  Cloanto-claimed material (community discussion:
  https://www.lemon64.com/forum/viewtopic.php?t=73857). Nothing to
  reuse.
- **ZX Origins (DamienG):** ~hundreds of 8x8 fonts incl. "C64"
  export formats (C headers, 6502 asm); terms are informal —
  "freely available… in exchange for a mention in the credits"
  (https://damieng.com/typography/zx-origins/). ASCII-96 only, **no
  PETSCII graphics repertoire**; useful for alternate text glyphs,
  not for the chargen graphics half.

#### Ranked top 3 (report 1)
1. **MEGA65 open-roms `chargen_openroms.rom`** — already a 4 KB,
   512-glyph, 8-bytes-per-glyph chargen with both PETSCII sets,
   drop-in zero conversion; LGPL-3.0 (note in the license table).
   PXLfont 2.3 from the same `bin/` dir is a nicer-looking second
   option under the same license.
2. **unscii-8** — public domain, no strings at all; `.hex` converts
   trivially; complete Legacy Computing/PETSCII glyph repertoire,
   but you must build the 512-entry PETSCII layout yourself using
   the L2/19-025 mapping, and the look is unscii's, not Commodore's.
3. **BESCII** — CC0, deliberately C64-flavored (closest "feel" with
   a clean pedigree), full PETSCII graphics via Direct-PETSCII PUA
   mapping; needs TTF→bitmap extraction (it is a true 8x8 grid
   design, so 8px rasterization is lossless).

Excluded despite being the prettiest matches: Pet Me
(no-derivatives clause) and Style64 C64 TrueType (no-modification,
no-direct-download).

#### Legal caveat: pixel-exact clones of the Commodore charset
Honest summary: **unsettled, lean away.** Community/legal consensus
(e.g. the Lemon64 threads above): in the US, typeface *designs* are
not copyrightable, but the ROM as a data file is, so byte-copying
the chargen ROM is clearly off-limits; the open question is whether
an independently-typed but pixel-identical 8x8 bitmap is a "copy of
the ROM data" (it is byte-identical by construction) or an
uncopyrightable typeface rendering. No case law answers this for
8x8 chargen bitmaps; jurisdictions differ (UK/Germany protect
typefaces, though 25-year terms have expired for 1982 designs).
Cloanto/C64-forever actively license the ROMs, and open-roms chose
clean-room reimplementation precisely to avoid the argument.
Practical rule the community follows and open-roms/BESCII embody:
*inspired-by with visible pixel differences = safe; pixel-identical
= same bytes as the ROM = don't put it in a public repo.* All three
ranked picks satisfy this.

---

### Report 2 — General open 8x8 ASCII bitmap fonts

#### 1. dhepper/font8x8 — https://github.com/dhepper/font8x8
- **License:** Public domain (stated in repo README). *Caveat:*
  provenance chain is "directly derived from an assembler file" by
  Marcel Sondaar, itself based on "IBM public domain VGA fonts."
  IBM never formally dedicated these to the PD — the claim
  ultimately rests on the US doctrine that bitmap glyph designs are
  uncopyrightable. Community treats it as safe; used everywhere
  (OS-dev tutorials, embedded projects).
- **Coverage:** Basic ASCII (0x00–0x7F), extended Latin
  (0x80–0xFF), box drawing, block elements, Greek, Hiragana — as
  separate C arrays.
- **Format:** C header arrays, exactly 8 bytes/glyph, LSB =
  leftmost pixel. **Zero conversion needed** — already chargen ROM
  format (bit-reversal per byte may be needed depending on shift
  orientation).
- **Readability:** Classic IBM-ish face; lowercase without true
  descenders (CGA-style squash); 0 unslashed but distinguishable
  from O; 1/l/I distinct. Serviceable, very "PC."
- **Status:** Repo dormant (7 commits) but stable.

#### 2. Ultimate Oldschool PC Font Pack (VileR) — https://int10h.org/oldschool-pc-fonts/readme/
- **License:** **CC BY-SA 4.0**. Attribution: credit "VileR" + link
  to int10h.org. Adaptations (which ROM-converted glyph data is)
  must be distributed under a compatible license.
- **8x8 faces in the pack:** IBM CGA 8x8, AMI EGA 8x8, ATI 8x8,
  Verite 8x8, ToshibaTxL1 8x8, and dozens more OEM 8x8s (CGA
  thin/thick, EGA, Amstrad, Phoenix, etc.).
- **Mechanics:** Converting glyphs to a C array/ROM binary is fine
  under CC BY-SA with credit + the CC BY-SA 4.0 notice on the
  derived font data. The share-alike obligation attaches to the
  font data, not to the emulator code that merely loads it (fonts
  as data are generally treated as separate works — convention, not
  litigated certainty). One CC BY-SA row in the license table.
- **Underlying IBM/OEM designs:** VileR's own legal analysis: "The
  raw bitmap typefaces are not copyrightable, unlike fonts in
  specific formats such as .fon and TrueType (which qualify as
  software)" (citing *Eltra Corp. v. Ringer*); IBM's fonts were
  cloned by every BIOS vendor for decades without litigation.
  Well-founded **for the US**; some jurisdictions (Germany, UK)
  protect typefaces — essentially zero practical risk, small
  theoretical non-US risk.
- **Format:** TTF/OTB + PNG specimens + raw bitmaps in the extras;
  conversion easy.
- **Readability:** The CGA/EGA 8x8 faces are the gold standard for
  readable 8x8: distinct 1/l/I, O/0, decent pseudo-descenders.

#### 3. ZX Origins (Damien Guard) — https://damieng.com/typography/zx-origins/
- **License:** Informal: fonts are "freely available to be used in
  games you create in exchange for a mention in the credits section
  or perhaps a coffee." Commercial use explicitly allowed;
  recommended credit "*[fontname]* font by DamienG". **The one
  prohibited use: "redistributing the font as a font."**
- **The catch:** a chargen ROM in a public repo *is* redistributable
  font data — a raw 768-byte glyph table sits in a gray zone
  between "used in a product" (allowed) and "redistributed as a
  font" (not). He is explicitly open to email; one message would
  settle it. Not a drop-in for a strict license table without that.
- **Collection:** 263 original 8x8 typefaces, each shipped as TTF
  **plus C headers and Z80/6502/x86/68000 assembly** — already
  8-bytes-per-glyph. Coverage full printable ASCII (Spectrum
  heritage), typically no box drawing.
- **Standout readable faces:** **Envious** (very clean terminal
  face), **Localhost**, **Keytop**, **Clear Plan**, **Computer**.

#### 4. unscii-8 — http://viznut.fi/unscii/ (repo: https://github.com/viznut/unscii)
- **License:** "You can consider it Public Domain (or CC-0)" except
  the Unifont-derived files (unifont.hex, unscii-16-full) which are
  GPL. **unscii-8 is PD/CC0.**
- **Coverage:** Huge — best in this sweep. Full ASCII, Latin-1, box
  drawing, block elements, Teletext/Videotex mosaics, PETSCII
  pseudographics, shades, round corners. Variants: unscii-8 plus
  stylistic 8x8s (thin, alt, fantasy, mcr).
- **Format:** .hex (trivially parseable), plus BDF/PCF/TTF/OTF.
  Conversion to ROM data is a 10-line script.
- **Readability:** Designed as a *usable terminal font*, not just
  retro pastiche — good 0/O and 1/l/I distinction, consistent
  stroke weight; compressed descenders (8px cell limit).

#### 5. Spleen — https://github.com/fcambus/spleen
BSD 2-Clause. **Sizes: 5x8, 6x12, 8x16, 12x24, 16x32, 32x64 — no
8x8 exists.** Dismissed. (If an 8x16 is ever wanted for an
80-column mode, Spleen 8x16 with full CP437 + BSD-2 is a top pick.)

#### 6. Fantasy-console and homebrew fonts
- **TIC-80:** project MIT, but the system font is **6x6** in 8x8
  sprite cells. Dismissed on size.
- **PICO-8:** font and palette are **CC0** (official FAQ) — but
  glyphs are 3x5. Dismissed on size.
- **Pixel Operator** (Jayvee Enaguas) — **CC0 1.0**
  (fontlibrary.org; source notabug.org/HarvettFox96/ttf-pixeloperator).
  8px-height mono variants exist but ship **TTF only** — rasterize
  at 8px and verify the advance is actually 8. Usable with modest
  work; license perfect.
- **Kitchen Sink** (Polyducks, itch.io) — **6x8, not 8x8**, and
  "redistributing the font as an asset is prohibited" + an NFT
  clause. **Excluded** on both size and license.
- **Portfolio 6x8:** 6x8 (Atari Portfolio). Dismissed.

#### 7. Terminus
Sizes 6x12 through 16x32; **no 8x8**. SIL OFL 1.1. Dismissed.

#### 8. Linux consolefonts and other BDF/PSF sources
- **Kernel `lib/fonts/font_8x8.c`:** SPDX **GPL-2.0**, "generated
  by cpi2fnt," no origin credit. Same IBM-derived CP437 face as
  font8x8, but taking it from the kernel imports GPL-2.0 —
  pointless when dhepper/font8x8 offers equivalent glyphs as PD.
  Same for `font_pearl_8x8.c`. Skip.
- **IBM BIOS font recreations:** the canonical open ones are
  exactly the int10h pack (CC BY-SA) and dhepper/font8x8 (PD).
  Nothing cleaner-licensed found; nothing else notable at 8x8
  surfaced that beats the above.

#### Ranked top 3 (report 2)
1. **unscii-8** — PD/CC0, widest coverage by far (ideal raw
   material for a fantasy machine's full 256-glyph chargen), .hex
   converts trivially, genuinely readable. Cleanest license + best
   fit. Watch-out: don't grab the Unifont-derived files (irrelevant
   at 8x8).
2. **dhepper/font8x8** — already literally chargen-format C arrays,
   PD-labeled, ASCII+Latin+box+blocks. Slightly weaker provenance
   story but universally used; fine as fallback or "boring
   default."
3. **Ultimate Oldschool PC Font Pack (IBM CGA 8x8 / ATI 8x8 /
   Verite 8x8)** — the most authentic and most readable faces, but
   CC BY-SA 4.0 means attribution + share-alike on the converted
   glyph data — one viral-ish row in the license table. Use for the
   real CGA look if the flag is acceptable.

ZX Origins is the honorable mention: 263 original faces,
pre-converted 6502 source, but the "don't redistribute as a font"
clause needs one clarifying email before a raw glyph table lands in
a public repo.

---

### The combined ranking

For the K4510 chargen, both sweeps agree on the shape of the answer:

1. **open-roms `chargen_openroms.rom`** (LGPL-3.0) — the only
   ready-made, complete, 512-glyph PETSCII chargen in drop-in
   8-bytes-per-glyph format; zero conversion work. PXLfont 2.3 from
   the same directory (same license route) if a nicer face is
   wanted.
2. **unscii-8** (public domain) — the no-strings powerhouse for
   both ASCII and PETSCII repertoires; requires assembling the
   512-entry layout via the L2/19-025 PETSCII→Unicode mapping, and
   the look is its own.
3. **BESCII** (CC0) — the most Commodore-flavored clean design;
   TTF→bitmap extraction needed (lossless — it's a true 8x8 grid).

The prettiest candidates (Pet Me, Style64) are license-excluded;
pixel-identical recreations of the Commodore charset are avoided on
principle regardless of who typed them in. No decision taken yet —
this entry is the research record; the pick is Doc's.

## 2026-08-24 (y) — fonts vendored, credits written

The archive-side session staged the licence-clean fonts from the (x)
research and Doc asked for them in the repo: `data/fonts/` now holds
**openroms/** (LGPL-3.0 clean-room chargen, 512 PETSCII glyphs
drop-in, + PXLfont 2.3 via open-roms' permission, + the editable
8x8font.png shipped for LGPL source compliance), **unscii/** (public
domain; `font8-unscii.bin` is a generated byte-for-byte-contract
replacement for data/font8.bin -- adopting it would retire the ROM's
last GPL font row), **bescii/** (CC0, v3 Mono + glyphs source only;
the full clone stays in the archive staging), and the
`hex2chargen.py` converter. No wiring yet -- the text-font choice and
the VICKY PETSCII story are open decisions. **CREDITS.md** joined the
repo root: LICENSES.md is the legal record, CREDITS.md is the thanks
-- from Gábor Lénárt's CPU core to Wozniak's 256 bytes. And the
LICENSES.md table gained the row it had always been missing: tube/
BBCTTY (zlib, R.T. Russell), spotted by the archive session.

## 2026-08-24 (z) — 47103 BYTES FREE: the interpreter goes on the MATH diet

Doc: "do the ehbasic modification to get 47k free." Ram_top is $C000
now and the machine boots **47103 Bytes free** — from 25599 two days
ago, via 45567 last night. Getting the last 1.5 KB out was a proper
archaeology dig:

**The dead code behind the JMPs.** Since the MATH-unit port, SQR SIN
COS TAN ATN EXP LOG ADD MUL DIV have been `JMP K_x` redirects — with
Lee's original routine bodies still assembled behind them, plus their
Taylor-series constant tables, plus POWER still doing LOG-multiply-EXP
the long way. An automated excision (cut each body to the next section
comment; let ca65's undefined-symbol errors force back what live code
still shares) plus a global dead-chunk harvester with fallthrough
safety (a 0-reference label is only dead if nothing above can fall
into it) removed ~580 lines. POWER became a 26-byte K_POWER glue —
MATH_POW was already in the hardware, unused.

**Number output moved into the MATH unit.** The 224-line FOUT
(float→ASCII) is now `MATH_FTOA` — host-side C that formats in Lee's
exact style. First attempt printed 9 significant digits, MS-canon;
the OLD interpreter, tested side by side, prints **6** — so the
formatter was measured against Lee's own output until byte-identical:
` .5 .01 5E-03 12345.6 100000 3.14159`, `1.23457E+06` for 1234567,
LIST's line numbers intact. In the ROM: two 3-byte JMPs.

**The page-2 loan.** Final packing: expression compiler rides inside
the $E000 half (7880/7936), core tail + math/file/gfx glue fill
$C000-$CFFF (4083/4096), and the FOUT/POWER glue + banner live in a
formalized sliver at $0230-$02CF — between the ROM's DATA (which
rom/k4510.cfg now pins below $0230, so growth fails the LINK, not the
machine) and the launch trampoline at $02D8. Every region is
assert-guarded; the night's slicing bug (a python cut whose end
anchor matched in the K4SG header, duplicating half the file) was
caught by the duplicate-symbol wall and rebuilt from backup.

All 10 suites green; floats, graphics, LOAD/SAVE chain, star
commands, DIM A(9300) verified. The settings-menu request from the
archive session is queued next, after the terminal discussion Doc
asked for.

### Addendum (z): the library lands, and Zork answers

The paced mirror on hdieu (fresh IP, four seconds a file, the
embeddedfolderview trick beating the 50-entry listing cap) finished
what gdown's sixty rate-limited passes could not: **2,467 files,
375 MB — Mockba's entire CP/M library, drives A through O** — now
installed in fs/CPM/ (gitignored; the public repo carries only the
pointer). Fifteen drives: WordStar 3.3 and 4.0, Turbo Pascal 3.01A
with source, dBase II, MultiPlan, SuperCalc2, Fortran, COBOL x2,
PL/I-80, Janus/Ada, two Modula-2s, two LISPs, muSIMP, APL/Z,
Algol-M, MUMPS, BBC Basic v3 (the machine now owns it twice), three
more Forths — and the games. Acceptance test: `cpm`, `user 5`,
`zork1` — "West of House. You are standing in an open field west of
a white house, with a boarded front door." Opened the mailbox. Read
the leaflet. 1982 answered on the first try.

The screen-oriented half of that library (WordStar, TP's editor,
ZDE) now waits on exactly one thing: the terminal.
## 2026-08-24 (aa) — the folder says the machine's name, and the scrub finds its stragglers

Housekeeping. The project folder finally says what the machine is:
`~/Projects/BMC64k4502` became **`~/Projects/BMC-K4510`** on all three
machines that carry it (ubuntu-s1, the laptop, hdieu), each with a
compat symlink left at the old name so every old path in this diary
keeps resolving. ubuntu-s1's Claude project dir (memory + session
history) moved the same way; CLAUDE.md, the memory files and the
location tables in these docs were repointed — the Code row here had
still claimed the machine was a bmc64 fork, which it has not been
since the VICE route was dropped.

The rename's ripples then surfaced something better hidden: pulling
the docs fix into ubuntu-s1's k4510-build met "divergent branches" —
**both k4510-build clones (ubuntu-s1 and hdieu) were still on the
pre-scrub history**, leaked alpha tags included, missed by the earlier
remediation sweep because they hang off the mirror, not origin. No
merge base at all between the histories; but the tree at each stale
tip proved byte-identical to its rewritten twin, so nothing real was
at stake: reset to the clean history, tags force-updated, reflogs
expired, `git gc --prune=now`, and the old objects verified gone
(`cat-file` on the old heads: not a valid object). hdieu's k4510-build
had also inherited its origin as a ubuntu-s1 filesystem path from the
rsync — it now points at the public GitHub URL and can pull for
itself. p15, unreachable on scrub day, turned out to hold only a
plain rsync copy (no .git), and its release zips grep clean for
session URLs. Nothing known still carries the old hashes; the
pre-scrub bundles in ~/Backups/ remain the only intentional copies.

## 2026-08-24 (ab) — the second processor: BBC BASIC on the Pi's core 3

Doc, after supper was announced: "Do the BBCBasic tube for the RPi3B."
On the desktop the Tube co-processor is a child *process* on a pty —
`forkpty`, `execl ../tube/bbcbasic`, `read` — and a bare-metal Pi has
none of those words. What it has is a spare core: Circle runs devices
on 0, the emulator on 1, presentation on 2, and core 3 was parked
"for VICKY later". So the co-processor is now Richard Russell's
interpreter compiled INTO the kernel and running on core 3 — which
makes it, without any metaphor, the second processor. The Tube ULA in
io.c (graphics to the blitter, SOUND to the sequencer) is untouched;
it simply gained a second transport underneath.

**core/tube_cp.c** is that transport: two single-producer rings
(16 KB down for the co-processor's screen, 256 bytes up for its
keyboard), three atomic words (start request, alive, kill), and the
co-processor's "OS" — printf into the ring, getc from the other, a
millisecond clock, a sleep, and a file layer that prefixes every path
with the machine's filesystem root and keeps its own current directory
(the Pi's working directory is one global shared with the emulator;
the co-processor must never chdir). `tube_cp_run()` is core 3's whole
life: wait for `$D803 = 1`, run `tube_bbc_main()`, mark dead, wait
again; `$D803 = 2` sets the kill word, which `trap()` turns into
BASIC's own KILL flag, so the interpreter quits at its next trap
instead of being shot. The interpreter side is one build flag,
`K4510_TUBE`: bbccon.h maps printf/fflush/isatty and the file calls
to the Tube, bbccon.c drops the reader thread, the SIGALRM timer,
termios, mmap and dlsym, polls the Tube in `kbchk()` (where the
250 ms timer now ticks synchronously), and grows `tube_bbc_main()`, a
re-entrant `main()` without the process around it. The pty build and
`tube/bbcbasic` are unchanged; the Linux paths still compile as before.

The same transport builds on the desktop with the interpreter on a
pthread (`make tubetest`), so the whole path was exercised here before
a card existed: PRINT over the Tube, `*QUIT` and a restart (there is
no fresh process to hide stale state, so `entry()` re-running had to
be proven), MODE 2 / GCOL / PLOT / MODE 7 through the ULA, `*CD`,
`*DIR` and `LOAD "TUNE.BBC"` through the path layer. Ten desktop
suites still green, the pty BBC BASIC round trip still fine.

The Pi link then taught four things. (1) `bbdata_arm_64.s` names
every global `_liston`-style — the Apple convention; ELF has no
underscore, so C's `liston` never met it. Rather than edit 5000 lines
of vendored assembler, the Makefile assembles it and runs
`objcopy --redefine-syms` over the globals (locals like `b4fmt`
untouched). (2) The data file lays its variables out byte-packed,
and non-PIC AArch64 reaches a global with `adrp + ldr :lo12:`, which
needs the symbol aligned to the access: "relocation truncated to fit
R_AARCH64_LDST64_ABS_LO12_NC against errtxt". The Linux build is PIC
and goes through the GOT; so now do the six BBC objects (`-fPIC`).
(3) BBC's heap pointer is a global called `pfree`, and so is Circle's
free(): `-Dpfree=bbc_pfree` in C, the same rename in the symbol
file. (4) Circle's libc has no `chmod`, `stdatomic.h`, `clock_gettime`
on this path, or `ioctl`: the atomics are GCC builtins, the clock is
`SDL_GetTicks`, chmod is a no-op on FAT. One more that never reached
the linker: the shim's `SDL_Delay` off core 0 runs the SDL audio
callback from the calling context, and the SIDs belong to core 1 —
so core 3's waits are pure spins on the system counter, which is what
the shim itself does off core 0 and is the correct wait for a
dedicated core. Small archaeology: BBCSDL's headers are CRLF, which
ate one patch anchor; and MODE 2's 80x60 geometry hides output from
the headless harness's 80-column reader, which is why the test
returns to MODE 7 before it looks.

**kernel8.img: 1,496,320 bytes** (was 1,299,552 — the interpreter
costs 197 KB), staged in p15's `pkg/`; the ROM and the card's files
are unchanged, so only that one file needs copying to the card.
**Untested on hardware**: the card was in the Pi at supper. What the
first boot must show: `BBCBASIC` → the K4510 banner ("BBC BASIC for
K4510 Console v0.50"), `PRINT 6*7`, `*QUIT` → "has left" and back,
then `LOAD "TUNE.BBC"` / `RUN` for the sequencer and KALEID for the
blitter. Parked, on the record: CP/M on the Pi (RunCPM is the next
child through the same rings; `CPM` there still says "no Tube
(desktop host only)"); BBC's 6502 assembler — `CALL` into code —
is compiled in but relies on Circle mapping RAM executable, unproven;
the co-processor's 256 MB comes from Circle's heap after the
emulator's 256 MB, halving until it fits, so `HIMEM` on the Pi may
read lower than the desktop's.

## 2026-08-24 (ac) — sprites for BBC BASIC, the RISC OS way

Doc: "write some demos for bbcbasic that show off the graphics
capabilities including sprites." The Tube ULA knew lines, triangles,
rectangles, circles and the palette; VICKY's 128 hardware sprites were
out of BBC BASIC's reach. Acorn had already chosen the syntax in 1987:
RISC OS selected a sprite with `VDU 23,27,0,n|` and plotted it with
`PLOT &ED,x,y`. That is now ours, with one twist the Beeb never had
and VICKY makes free: a sprite is CAPTURED from the bitmap
(`MOVE x,y : VDU 23,27,1,n,w,h|`, 8/16/32/64 a side) after BBC BASIC
has drawn it with the words it already knows, and once placed it is a
register write — moving sixty-four of them each frame costs the
interpreter nothing but the PLOTs. The rest of the family: 2 hide,
3 flip (H/V bits), 4 depth (drawn after layer z, default over the
bitmap), 5 "n shows m's picture" (one capture, many sprites).
bbccos.c forwards VDU 23,27 as `ESC]K4G;23,27,...`, tula_parse grew
from 6 to 12 arguments, the attribute table sits at $260000 and the
pictures at $261000 + n×4 KB; MODE to a text mode and *QUIT switch
the sprites off. The plot re-asserts SPRCTL because the console's MODE
re-init clears it.

Three demos in /BBCBASIC, period style: **SPRITES** (eight balls drawn
once with CIRCLE FILL — logical colour 8 turned orange by VDU 19 —
captured, cloned to 64, bounced by VICKY), **INVADERS** (two frames of
the 1978 alien from DATA strings, 4-pixel cells laid with PLOT 101 at
y = k × 8.5334 so the 15/32 vertical scale lands each cell on exactly
four rows; ten sprites march, animate by re-cloning the other frame
every step, flip on the turn, and thump on the noise channel) and
**TUNNEL** (fifteen concentric circles drawn once, then only VDU 19
palette writes — nothing is ever redrawn). All three captured from the
running machine; README.BBC and the guide's Tube chapter carry the
table of VDU 23,27 calls. The Pi kernel was rebuilt with the sprite
ULA in: kernel8.img 1,498,400 bytes, md5 73702ee3, staged in p15
pkg/. Not done: the guide PDF is not regenerated (that is the
ubuntu-s1 k4510-build ritual, artifacts synced first).

## 2026-08-24 (ad) — the Z80 joins the second processor: CP/M on the Pi

Doc chose it over Fujinet: "Do the RunCPM port for the RPi3+ tube." An
evening's work, because the Tube built earlier tonight already had a
program code for it — `$D803 = 3` — and RunCPM is a single translation
unit whose every platform assumption lives in one header,
`abstraction_posix.h`. That header is now generated into
`abstraction_k4510.h` by `cpm/patch_cpm.py`: byte-identical except at
four seams. The console (termios, poll, getchar, `system("clear")`)
became the Tube rings — `_kbhit` peeks one byte, `_getch` waits on
the ring, `_clrscr` sends the ESC[J the K4510 console already knows.
The files kept every line of RunCPM's logic but `FILEBASE` is empty
and fopen/stat/rename/truncate/mkdir route through the co-processor's
path layer with its cwd set to `/CPM`, so "A/0/ZORK1.COM" lands in
fs/CPM/A/0 on the desktop and SD:/k4510/fs/CPM/A/0 on the card. The
directory search, which used glob(3) (Circle has none), walks
opendir() and sorts as glob sorted — the all-users "?" form scans the
sixteen user folders. The machine's kill, which BBC BASIC felt in
trap(), has no equivalent in a Z80 loop, so it longjmps out of the
emulator from the next console call; `-Dmain=tube_cpm_main` lets
main.c stay main.c. One symbol surfaced at the Pi link — the CPU
throttle's usleep — and was routed to the Tube's spin.

Verified on the desktop through the in-process transport: the CCP
banner and A0> prompt, DIR across the whole A: drive, and Zork
(user 5) opening the mailbox and reading the leaflet — "WELCOME TO
ZORK" from the Z80 through the rings. `make tubetest` now has five
legs. The Pi kernel: **1,571,520 bytes** (BBC BASIC + sprites + CP/M;
was 1,299,552 this morning), md5 65249308, staged in p15 pkg/. Still
untested on the Pi itself — Doc said hold the card. When it goes on:
`CPM`, `DIR`, `USER 5`, `ZORK1`. Open: whether fs/CPM (375 MB) is on
the card at all; the ROM's "no Tube (desktop host only)" wording is
now only ever true of an unfitted program; and the terminal (THE
QUEUE, item 1) is what CP/M's screen programs are waiting for, on the
Pi as much as on the desktop.

## 2026-08-24 (ae) — *DIR sees directories, LOAD forgives case, the banner names the machine

Doc, from the desktop with a screenshot: BBC BASIC's `*DIR` "always
looks for .BBC files and does NOT show subdirectories — is the problem
BBC BASIC or K/OS?" BBC BASIC: `*DIR` is one of the star commands the
interpreter keeps for itself (bbccos.c DIRCMD), and the stock version
appends `*.bbc` when given nothing and lists only readdir() entries
matching the pattern — a subdirectory never does, so fs/ root, which
is all directories, listed as empty. K/OS never saw the command.
Three [BMC-K4510] alterations, all in the vendored tree and noted in
ALTERED.md: (1) `*DIR` lists every subdirectory whatever the pattern,
with a trailing `/` (a stat through the co-processor's path layer in
the in-process build, `k4_stat`); (2) osload()/osopen() retry a missed
read with the extension in capitals — `fopen_rd` — so `LOAD "KALEID"`
finds KALEID.BBC on the case-sensitive desktop as it always would have
on the Pi's FAT; (3) Doc's second observation, "it says Linux in there
on startup — confusing": PLATFORM is now "K4510" in every build, so
the banner reads "BBC BASIC for K4510 Console v0.50". Also the
in-process `Directory of //*.bbc` lost its doubled slash. Verified on
both transports (`*DIR` at the root shows PRG/ SID/ EHBASIC/ BBCBASIC/
FORTH/ CPM/; `*CD BBCBASIC` then `LOAD "TUNE"` loads), tubetest five
legs green, Pi kernel rebuilt (1,571,536 bytes, md5 889edc7b) and
staged; the card still on hold.

Addendum, same night: Doc — "BBC BASIC does not have graphics commands (hence Console???)". It has them all; Console is the name of Russell's terminal-stream edition, the one whose byte stream IS the Tube. The banner now says what it is: **"BBC BASIC for the K4510 Tube v0.50"** (Doc's wording; szVersion in bbccon.c). Pi kernel re-staged, md5 bd1fceb3.

## 2026-08-24 (af) — the network, two ways: the Meatloaf rule and the N: device

Doc's other candidate, started once the card was written: "Add
Fujinet/Meatloaf capabilities to K/OS on the emulator (just for now)."
Two ideas from two machines, and they fit the K4510 without touching
the ROM's full jump table.

**The Meatloaf rule** (the C64 cartridge that made a disk name an
address on the internet): a URL is a file name. The ROM never looks at
a name — it hands it to the filesystem device at $D300 — so `net.c`
teaches that device that a name beginning http:// or https:// is
fetched (curl, no shell in between: argv straight to execvp) to a
temp file, opened, and unlinked, and served like any file. Zero ROM
bytes for `TYPE http://…`, `CP http://…/x.prg x.prg`, `RUN
http://…/x.prg` (the REXX rule finds the program on the internet),
EhBASIC's LOAD — and BBC BASIC's LOAD on the Tube, whose file layer
in tube_cp.c got the same three lines. The one ROM change is a
limit: getname() stopped at 63 characters and GitHub's raw URLs are
longer. NAMEMAX is now 96, the shell line's own length (cc65 refused
two 128-byte locals in CP: "Too many local variables"); BSS sits at
$1BD of $1C0, three bytes spare.

**The N: device** (FujiNet's network device, as the Atari and Apple
saw it) at $D900, for programs: four channels, each a URL opened for
reading and writing — tcp://host:port a connection, http(s):// a GET
whose body is then read. Registers mirror $D300 (CMD, STATUS, CHAN,
NAMEPTR, ADDR, LEN, SIZE); OPEN, READ, WRITE, CLOSE, STATUS (bytes
waiting, 4 = the peer has gone), and GET (the Meatloaf rule for
programs: the whole URL into memory). Reads never block the machine —
a program polls, as it would a UART; curl runs as a child on a pipe.
`TELNET host port` is the demonstration, telnet.prg in /PRG: keys
out, bytes in, ESC hangs up, telnet's IAC negotiation answered with
WONT/DONT so BBSes and MUDs talk.

Verified offline by test/nettest.sh (a Python http.server and a TCP
echo on loopback: TYPE of a URL, CP of a URL then TYPE of the copy,
a telnet line echoed back in capitals) — now part of `make test`,
eleven suites green — and live: Google's robots.txt, then the
machine's own README from GitHub, on the K4510's screen. BBC BASIC
LOADed a .BBC over HTTP and RAN it. Pi: net.c compiles to "not
fitted" (status 6, URLs are ordinary absent names); the way in is
Circle's TCP/IP stack and HTTP client, a later evening. Edges on
record: HTTP POST is not there (WRITE on an http channel is "bad
command"); DIR of a URL and CD into one, which Meatloaf does by
parsing listings, are not; the ROM's new kernal.bin (md5 e4ce4e98)
is staged on p15 for the next card, the card Doc just pulled still
carries the 63-character limit.

## 2026-08-24 (ag) — TNFS: the machine's current directory on the internet; and the Pi gets its network

Doc: "do tnfs. And then port all this to Pi/Circle." TNFS is the
Spectranet's little UDP file protocol (port 16384: mount, open, read,
readdir, stat — four-byte header, status byte, retries), and it is
what FujiNet and Meatloaf servers speak. The client is 150 lines in
core/net.c, one session per server kept open. The point is not the
fetch — http already did that — but the DIRECTORY: `CD
tnfs://host/dir` puts the machine's current directory on a server.
DIR lists it (OPENDIR/READDIR, a STAT per entry for sizes and
directories, sorted directories-first as the shell does), a bare
name loads from it — so the REXX rule runs programs straight off
the internet: `SAY FROM A TNFS SERVER` fetched say.prg from the
test server and printed its arguments — CD .. climbs, CD - comes
home, and the prompt reads `tnfs://fujinet.online]`. The server is
read-only from here; MKDIR/RM/RMDIR/SAVE there answer "error".

Along the way the network layer was split for the port: core/net.c
holds the URL grammar, the TNFS client and the N: device; the
platform underneath is net_plat.h — sockets and curl in
core/net_posix.c, Circle in pi/net_pi.cpp. Fetches now land in
memory, not /tmp (a Pi has no /tmp; fmemopen serves the Tube), and
the N: device reads http and tnfs files from that buffer.

Verified: test/tnfsd.py is a small TNFS server (read-only, ten
commands) for the tests; nettest's fourth leg does CD/DIR/TYPE/RUN/
CD SUB/CD ../CD - against it. Ten suites, tubetest, nettest green.
Then live: `CD tnfs://fujinet.online/` and `DIR` — ADAM APPLE2
ATARI CBM COCO LINKS LYNX MSDOS, the FujiNet library on the K4510's
screen.

**The Pi.** circle-libsdl2 constructs Circle's CNetSubSystem but
deliberately never starts it (so that a name lookup fails instead of
halting the board). pi/net_pi.cpp starts it — k4510_net_start() on
core 0 after SplitInit: Ethernet, DHCP under the scheduler core 0
already yields to — and implements net_plat.h on Circle: CSocket
for UDP and TCP, CDNSClient, CHTTPClient. Circle's subsystems are
core-0-only and the emulator is core 1, so every call runs inside
the shim's SDL2Circle_CallOn0; waits that would block (a UDP reply,
a TCP byte) are polled non-blocking from core 1 with a millisecond
nap, so core 0's servo is never held for longer than a connect. A
2 KB stash per socket gives STATUS its "bytes waiting". No TLS is
built into the kernel, so https answers 6; http, tnfs and tcp are
all there. kernel8.img 1,588,016 bytes (md5 in pkg/), staged with
the NAMEMAX-96 ROM for the next card. **Untested on hardware**, and
it needs an Ethernet cable on the 3B+: what to expect is a "k4510-
net: Ethernet up, DHCP running" line on the serial log, then `CD
tnfs://fujinet.online/` a few seconds after boot. Without a cable:
"not fitted", nothing halts. WiFi would need Circle's wlan addon,
its firmware files on the card and a wpa_supplicant config — not
this night.

## 2026-08-24 (ah) — JIM: a VT100 in hardware, for CP/M and the BBSes

Doc: "terminal for cpm (vt100 or vt220?) and terminal ansi/vt100 for
bbs telnet", "make it for emulator and pi" — and, seeing where it was
going, "is that chip called Jim?" It is now.

**Where the terminal lives.** The ROM's Tube session had a minimal VT
filter in cc65: cursor position, ED, SGR, and the `ESC[6n` answer the
BBC BASIC width probe needs — some 1 KB of sideways ROM, with 3 bytes
of BSS to spare in the whole image. A real VT100 (scroll regions,
insert/delete, tab stops, saved cursors, line drawing, two charsets)
does not fit there, and would have to be written twice: once for the
ROM's Tube loop and once for telnet.prg. But the Tube ULA already
parses this very byte stream on the host. So the terminal is a chip:
JIM, at $DA00 (FRED, JIM, SHEILA — the Beeb's three pages; SHEILA is
the backgrounds already). core/term.c, one C file shared by the
desktop and the Pi; it draws straight into the VICKY text32 map the
ROM console uses, inside the window the ROM programs into it (COLS,
ROWS, origin, stride, default colours — video_init writes them), so
the console and the terminal share one screen and one cursor.

Registers (core/term.h): DATA in at $DA00, STATUS $DA01 (bit7 a reply
waits, bit0 the stream moved the cursor), REPLY $DA02 (cursor-position
and identity reports, and translated keys), KEY $DA03 (a K4510 key code
in, its VT bytes out: arrows ESC[A.. or ESC OA.. under DECCKM, Home/End,
PgUp/PgDn/Ins ESC[n~, Del $7F, F1-F4 ESC OP.., F5-F12 ESC[15~..),
CTRL $DA04 (1 soft reset, 2 clear), the window at $DA05-$DA0D, FLAGS
$DA0E (cursor shown; it blinks on the frame tick), BASE $DA10, default
colours $DA14/15.

Repertoire: VT100 (CUP/CUU/CUD/CUF/CUB, ED/EL, DECSTBM, DECSC/DECRC,
IND/RI/NEL, HTS/TBC, DECAWM with the real last-column pending wrap,
DECOM, DECCKM, ESC(0 and SO/SI line drawing mapped onto CP437 box
glyphs, DSR/CPR, DA answering as a VT220 with colour, DECALN, RIS),
ANSI SGR incl. bold-as-bright, reverse, 30-37/90-97/40-47/100-107 and
38;5;n/48;5;n for the 16, VT220 ICH/DCH/IL/DL/ECH/SU/SD/CHA/VPA/CBT,
IRM, ESC[?25, ESC[s/u, DECSTR; OSC/DCS/APC strings swallowed; bytes
$80-$FF are CP437 glyphs, which is what BBS art is. VT100 or VT220 for
CP/M? Both: the VT220 additions are the editing set, which costs
nothing and which ZDE and WordStar's VT100 profiles never send anyway;
the answer to a CP/M install program is "VT100" (or "ANSI").

**The ROM** (rom/kernal.c): cmd_bbcbasic is now a pump — Tube bytes to
JIM, JIM's replies and translated keys to the Tube — plus a one-byte
lookahead after ESC so the two OSC strings the ULA forwards (K4510;
star commands, K4G;22 MODE) are still caught here. The SGR table and
sequence parser are gone: SWCODE0 shrank from $17EE to $13F7. run_at
hands the console cursor to JIM before a program starts and takes it
back if the program used it (the dirty bit), so telnet.prg's output
and the prompt after it line up.

**telnet.prg** writes to JIM instead of CHROUT, pushes keys through
KEY (arrows and F-keys become sequences a BBS understands), and sends
JIM's replies out (a BBS asking `ESC[6n` gets its answer). F12 hangs
up now; Escape belongs to the far end.

**Verified.** test/termtest (new, 12th suite): 30 checks — CR/LF,
cursor moves, SGR colours/bold/reverse, CPR, DECSTBM scrolling, IL/DL,
ICH/DCH, DEC line drawing and SO/SI, DECAWM off/on and the wrap at 79,
key translation and DECCKM, DA, the dirty bit, the cursor's reverse
bit, OSC/DCS swallowed, CP437 pass-through, DECALN. tubetest five legs
(the width probe answered by JIM now), nettest (telnet echo through
JIM), the other suites: all green. Live: `CPM`, `H:`, `USER 3`,
`TURBO` — Turbo Pascal 3's menu, reverse-video bar and all, on the
K4510 screen (test/capture, screenshot sent to Doc). One lesson
re-learned on the way: test/tubetest is not in `all`, and the stale
binary showed a Tube session printing nothing — "stale binaries lie".

**The Pi.** core/term.o added to pi/Makefile; kernel8.img built on p15
(1,598,736 bytes, md5 436a7d9e) and staged in pkg/ with the new ROM
(md5 e31cb285) and telnet.prg. Not on the card: Doc holds the card.
Untested on hardware, like the network it sits beside.

Not done: underline (text32 has no attribute for it), 132 columns,
UTF-8 from modern BBSes, telnet NAWS/TTYPE (we answer WONT to
everything; a BBS assumes ANSI 80x24, and JIM is 79x29 in MODE 1 1 —
fine for menus, MODE 1 0 gives it the full 80).

## 2026-08-24 (archive-side) — the BASIC research: what could replace EhBASIC

Discussion session (this host, archive folder — the coding session was
busy reshaping the sideways-ROM map and was left alone). It started as
"could adding elements of the 8510 give us 128k ram?" and ended, three
questions later, at "research other BASICs for the 4510 side." Along
the way: the 8510 is just an HMOS 6510 — the C128's 128K was the
**8722 MMU**, not the CPU; its `122365 BYTES FREE` was two 16-bit
pools summed through common-RAM trampolines; and the Tube BBC BASIC
already owns 250 MB, so the native side is the open question.

Two web-research agents ran in parallel: one on the Commodore lineage,
one on independent 6502 BASICs. **Outcome, decided by Doc the same
day: bring Microsoft 6502 BASIC (MIT since Sept 2025) into play;
the coding session was asked to reserve a sideways-ROM slot.** The
full reports follow, pasted verbatim for the record.

---

### Report 1 — Commodore-lineage BASIC options (45GS02-native)

#### The headline finding
**Microsoft open-sourced 6502 BASIC v1.1 (1976-78) under MIT in
September 2025** — github.com/microsoft/BASIC-M6502 (repo archived
read-only 2025-09-05, license stands). This resets the legal landscape
for everything MS-lineage.
([Hackaday](https://hackaday.com/2025/09/04/microsoft-basic-for-6502-is-now-open-source/),
[Tom's Hardware](https://www.tomshardware.com/software/bill-gates-48-year-old-microsoft-6502-basic-goes-open-source))

#### 1. C65 ROM BASIC 10 (910111)
- **License: PROPRIETARY** — all Commodore ROMs owned by Cloanto; the
  C65 ROM is licensed to MEGA65 per-device (fee per unit sold) and
  bundled in C64 Forever since 2022. No standalone redistribution
  right. ([generationamiga.com](https://www.generationamiga.com/2020/04/09/cloanto-licenses-commodore-65-rom-for-the-upcoming-mega-65/),
  [forum64 thread](https://forum64.de/index.php?thread%2F106568-about-the-license-of-the-c65-rom=))
- **Source:** never released; only community disassemblies
  (zimmers.net hosts binaries — legally grey to even mirror).
- **>64K:** yes, natively — program text in bank 0, variables/strings
  in bank 1 (the model BASIC65 inherited); graphics commands
  DMA-backed.
- **Porting: HIGH** — binary-patch-only (no legal source), deeply
  entangled with VIC-III, C65 F011 DOS, CIAs, C65 Kernal jump table.
  Also famously buggy (prototype ROM).
- **Footprint:** part of the 128 KB system ROM (BASIC+editor+graphics
  roughly half; Kernal/DOS/charset the rest).
- **Verdict for a public repo: unusable.**

#### 2. MEGA65 BASIC65
- **License: PROPRIETARY** — the "closed ROM."
  github.com/MEGA65/mega65-rom-public is a **bug tracker only**: "As a
  MEGA65 owner you have acquired a license to the ROM"; source repo
  access is by request on Discord, owners-only, no
  redistribution/derivative rights for other machines.
  ([mega65-rom-public](https://github.com/MEGA65/mega65-rom-public))
  Underlying Commodore rights still Cloanto's.
- **>64K:** yes, the best of the family. Commands per the MEGA65
  User's Guide ([memory.tex](https://github.com/MEGA65/mega65-user-guide/blob/master/memory.tex)):
  - `BANK n` — sets the bank used by `PEEK/POKE/SYS/BLOAD/BSAVE` etc.;
    with BANK 0–5 or address >$FFFF, PEEK/POKE go through the
    45GS02's base-page quad-pointer ([addr32],Z) far addressing —
    i.e. real 28-bit PEEK/POKE.
  - `DMA` — C65-style F018 DMA job from BASIC; `EDMA` — enhanced DMA
    with full 28-bit flat source/dest addresses.
  - Extended `PEEK, POKE, SYS, BLOAD, BVERIFY, SCREEN, SPRSAV…` are
    all bank-aware. Known limit: BANK/DMA can't reach colour RAM
    beyond the first megabyte.
  - Program/variable model: bank 0 text + bank 1 variables (BASIC 10
    heritage).
- **Porting: HIGH** — even ignoring the license wall: tied to VIC-IV
  registers, MEGA65 hypervisor traps, C65-DOS/SD controller, CIAs.
  The license alone disqualifies it.
- **Footprint:** 128 KB ROM total (BASIC is the majority).
- **Verdict: the feature model to imitate, not code to take.**

#### 3. MEGA65 Open-ROMs
- **License: CLEAN (GPLv3)** — documented clean-room process (spec
  from books like Mapping the C64, similarity tool flagging >2-byte
  matches against originals).
  ([open-roms](https://github.com/MEGA65/open-roms))
- **Completeness (per [STATUS.md](https://github.com/MEGA65/open-roms/blob/master/STATUS.md),
  still true in 2025-2026):** Kernal is substantially done (IEC,
  devices, screen partial); floating-point math package done; **but
  the BASIC interpreter core is not**: integer/float variables/arrays
  NOT DONE, expression handling only partial (strings mostly work),
  FOR/NEXT, GOSUB, IF/THEN NOT DONE. It boots to READY and runs
  trivial things; it cannot run real BASIC programs.
- **>64K:** extended-BASIC ideas exist for MEGA65 builds, but nothing
  bank-aware is usable yet.
- **Porting: HIGH as a working BASIC** (you'd be writing the
  interpreter yourself), **LOW-MED as a parts quarry** (GPL3 math
  package, string GC, Kernal patterns). Note GPLv3 would apply to
  your ROM if you incorporate it — fine for your repo, but it's viral.
- **Footprint:** C64-shaped (8K BASIC + 8K Kernal regions; MEGA65
  build larger).

#### 4. mist64/msbasic
- **License: MURKY-but-now-anchored.** The repo's README claims
  "2-clause BSD" — but that can only cover mist64's reconstruction
  scaffolding; the code itself is Microsoft's (byte-exact rebuilds of
  9 shipped ROMs). **Since Sept 2025 the Microsoft core is genuinely
  MIT** via microsoft/BASIC-M6502. Residual murk: the
  Commodore-specific patch levels (CBM BASIC 1/2 ROM-exact builds)
  contain **Commodore-authored modifications** that the MIT grant
  doesn't cover and Cloanto still claims.
- **Practical combo:** microsoft/BASIC-M6502 is the *legal anchor* but
  is written in PDP-10 MACRO-10 syntax; msbasic is the *buildable
  equivalent* — ca65/cc65 toolchain, same one your ROM already uses.
  Build a generic/OSI/KBD-style config (pure-MS feature set), avoid
  the CBM-ROM-exact targets, and cite the MS MIT release in your
  license table.
- **>64K:** no — 16-bit pointers throughout; strings, arrays, program
  text all in one 64K image. You'd bolt on far PEEK/POKE/DMA tokens
  yourself (your 45GS02 [zp],Z far pointers make the *data* side
  easy; making program/variable storage >64K is a rewrite).
- **Porting: LOW** — designed for retargeting: platform config files,
  I/O isolated to a handful of vectors (char in/out, ctrl-C check,
  LOAD/SAVE hooks). Zero VIC/CIA/Kernal entanglement.
- **Footprint:** ~8-9 KB.
- **Source:** github.com/mist64/msbasic +
  github.com/microsoft/BASIC-M6502.

#### 5. Commander X16 ROM BASIC
- **License: MIXED, core is PROPRIETARY.**
  [LICENSE.md](https://github.com/X16Community/x16-rom/blob/master/LICENSE.md)
  is explicit: the `basic` and `math` directories are "©1977 Microsoft
  Corp. ©1983 Commodore Business Machines" under a **commercial
  license from Cloanto valid only "in the context of the X16
  computer"** — outside users are told to contact Cloanto. Only the
  *additions* (new BASIC commands, CMDR-DOS, FAT32, audio API) are
  BSD-2, plus GPLv3 open-roms Kernal pieces. So no, it is not clean
  for reuse, and the community is honest about it.
- **>64K:** partial — `BANK` selects an 8 KB RAM bank at $A000 (up to
  2 MB) for PEEK/POKE/SYS; `BLOAD` auto-wraps across banks. Program +
  variables still live in the fixed low-RAM ~38 KB.
- **Porting: MED** technically (C64-BASIC-shaped, X16 banking
  hardware assumptions) — but license-blocked for the core; only its
  BSD-2 extension code (graphics/audio command implementations, DOS
  wedge) is reusable.
- **Footprint:** ~16 KB ROM bank + annex bank for the extensions.

#### Summary table (report 1)

| Candidate | License verdict | >64K | Porting | Footprint |
|---|---|---|---|---|
| C65 BASIC 10 (910111) | PROPRIETARY — Cloanto, per-device MEGA65/C64-Forever only | Yes (bank0 text/bank1 vars) | HIGH (no source, VIC-III/DOS-welded) | ~half of 128 KB ROM |
| MEGA65 BASIC65 | PROPRIETARY — owners-only source, Cloanto underneath | Yes — BANK, 28-bit PEEK/POKE, DMA, EDMA | HIGH (license + VIC-IV/hypervisor) | 128 KB ROM |
| Open-ROMs | CLEAN — GPLv3 clean-room | Not yet | HIGH as BASIC, LOW as parts quarry | ~16 KB |
| msbasic + MS MIT release | CLEAN for pure-MS configs; MURKY for CBM-ROM-exact builds | No (16-bit; far tokens bolt-on) | LOW — vector-isolated I/O, ca65/cc65 | ~8-9 KB |
| X16 ROM BASIC | PROPRIETARY core (Cloanto, X16-context-only); BSD-2 additions | Partial (BANK + 8K window) | MED, license-blocked | ~16-32 KB |

#### Other options found (report 1)
- **C128 BASIC 7.0 / Plus-4 BASIC 3.5** — the split-pool (bank-0 text
  / bank-1 vars) 7.0 model: same Cloanto-proprietary status as BASIC
  10; original Commodore engineering sources circulating on
  zimmers.net are leaks, not licensed. Dead end for a public repo.
- **microsoft/BASIC-M6502 as a direct base** — worth listing
  separately from msbasic: it is the one genuinely MIT-licensed
  Commodore-ancestor BASIC in existence (the exact code the PET ran).
  Legal gold; needs MACRO-10→ca65 translation, which msbasic has
  effectively already done.
- **A license-table caveat you already carry: EhBASIC.** Lee
  Davison's EhBASIC is "free for non-commercial use" only, and
  Davison died in 2013 leaving the rights orphaned — it is arguably
  the murkiest thing in your current ROM. The pragmatic clean-up path
  given everything above: migrate the native BASIC to an
  msbasic/BASIC-M6502-derived build (MIT), then add your own
  BANK/EDMA/far-PEEK-POKE tokens modeled on BASIC65's command
  surface.

---

### Report 2 — Non-Commodore-lineage 6502 BASICs

#### 1. EhBASIC 2.22 (Klaus2m5 fork) — the incumbent
- **License (exact):** Not OSI. "EhBASIC is free but not copyright
  free" — non-commercial use OK provided binaries/docs carry "Derived
  from EhBASIC"; commercial use required contacting Lee Davison, who
  died in 2013, so the commercial clause is now un-clearable and the
  license can never be regularized. Widely redistributed anyway
  (Klaus2m5, jefftranter, lgblgblgb forks all public on GitHub);
  community treats it as tolerated with attribution.
- **Implementation:** 6502 assembly (single source; many ports to
  ca65).
- **Memory model:** Classic 16-bit pointers everywhere (program,
  vars, strings all in one 64K image). No fork with banked/large
  memory was found — the only sighting is the PZ1 laptop running
  stock EhBASIC *under* a banking scheduler (banking is outside
  BASIC). **>64K: TEACHABLE** — far PEEK/POKE/COPY tokens using
  45GS02 [zp],Z is easy; making program/variable storage itself >64K
  is major surgery on ~16-bit pointer code throughout.
- **REPL:** Y. **Port effort: NONE** (already running).
  **2025-26 activity:** frozen (Klaus fork ~18 commits, bugfix-era).
- URL: https://github.com/Klaus2m5/6502_EhBASIC_V2.22

#### 2. BBC BASIC for 6502 (Acorn original + derivatives)
- **License:** Proprietary, orphaned. The 2018 Apache-2.0 RISC OS
  Open release covers **ARM BBC BASIC V only, not the 6502 ROMs**.
  Stardot consensus: the 6502 BASIC IP passed Acorn→Element
  14→Pace→Castle→(RISC OS Developments) so many times that "not even
  Sophie Wilson is sure who owns it now." J.G. Harston's assemblable
  source on mdfs.net still carries Acorn copyright; the Tube "BASIC V
  for 65C02" there is copyright Colin C Dean, also not free.
  BeebEater (github.com/chelsea6502/BeebEater, for Ben Eater builds)
  is a nice MIT wrapper but **ships the proprietary ROM binary
  inside**.
- **>64K:** NO (16-bit; the BBC's answer was the Tube/sideways RAM,
  outside BASIC). **REPL:** Y. **Port effort:** LOW technically (MOS
  entry-vector shims, BeebEater proves it) but **legally unusable for
  a clean public repo**. No open-source reimplementation of 6502 BBC
  BASIC exists as of 2026.
- URLs: https://mdfs.net/Software/BBCBasic/6502/ ,
  https://stardot.org.uk/forums/viewtopic.php?t=16087 ,
  https://github.com/chelsea6502/BeebEater

#### 3. FastBasic (dmsc) — strongest real candidate
- **License (exact):** GPL-2.0-or-later **with an explicit linking
  exception**: programs you compile with it may be distributed under
  any license. Clean for a public repo.
- **Implementation:** Parser/compiler in 6502 asm (on-machine IDE) +
  C++ cross-compiler on PC; runtime is a small bytecode **VM in ca65
  assembly** — it already builds with the cc65 toolchain the K4510
  uses.
- **Memory model:** 16-bit VM; 16-bit ints + Atari-ROM floating point
  (FP routines would need replacing or dropping for a port).
  **>64K: TEACHABLE, and more cheaply than anywhere else** — because
  all memory access funnels through ~10 VM opcodes, adding
  FARPEEK/FARPOKE/FARCOPY (or even a far string pool) means touching
  the VM, not a whole interpreter. The 45GS02's [zp],Z 32-bit mode
  slots straight into a VM opcode.
- **REPL:** Y-ish — full-screen IDE with editor + instant
  compile-and-run on the machine (not line-at-a-time immediate mode).
- **Port effort: MED** — author states "the libraries are fairly
  portable, so creating a version for other 6502s shouldn't be too
  much work"; work = console I/O layer, replace Atari FP, strip
  P/M-graphics statements. **Activity:** active — v4.6 (2024), v4.7
  discussed on AtariAge; 866 commits.
- URL: https://github.com/dmsc/fastbasic

#### 4. XC=BASIC 3 (neilsf)
- **License:** MIT. **Implementation:** cross-compiler written in
  **D** (runs on PC, emits 6502 via DASM). **REPL: N — compile-only**,
  which fails the core requirement. **>64K:** NO (16-bit codegen);
  teachable only by hacking the D codegen. **Port effort:** MED (add
  a target config + runtime shims; C64/VIC20/C16/Plus4/PET/C128
  supported, X16 via community target). **Activity:** mature, ~970
  commits, slow but alive.
- URL: https://github.com/neilsf/xc-basic3

#### 5. Tiny BASICs (Tom Pittman, DDJ-IL, CorshamTech, uBASIC etc.)
All are 2-8 KB toys with 16-bit (or 8-bit!) address spaces, no
strings/FP worth having, and nothing to say about large memory —
strictly a step down from EhBASIC. (CorshamTech's is GPL-3 and
maintained if a minimal fallback is ever wanted:
https://github.com/CorshamTech/6502-Tiny-BASIC ; uBASIC is BSD, in C,
cc65-compilable, and would run — but it's if/for/goto-only. The irony
is affordable, the language isn't.)

#### 6. dflat (6502Nerd)
- **License:** MIT. **Implementation:** 6502/65C02 assembly.
  **REPL:** Y (interactive, structured BASIC-like: def/enddef
  procedures, locals, recursion, while/repeat — no GOTO). **>64K:**
  NO as-is; TEACHABLE same as EhBASIC (it's conventional 16-bit asm
  inside). **Port effort: MED-LOW** — explicitly designed to port:
  "core language just needs character put and get routines."
  **Activity:** ongoing hobby refinement (Oric-1 branch, ~89
  commits); one-man project, non-standard dialect (existing BASIC
  listings won't run).
- URL: https://github.com/6502Nerd/dflat

#### 7. BASIC816 (pweingar, C256 Foenix) — the 65816 design reference
- **License:** **GPL-3.0** (author has said he'd consider MIT if
  asked). **Implementation:** 65816 assembly (64tass), clean-room,
  interactive REPL, shipped as the stock BASIC of the C256 Foenix U
  (a 65816 machine with up to 4 MB flat RAM). **>64K: YES —
  genuinely** — program text, variable table and string heap
  addressed with native 24-bit long pointers; this is the
  proof-of-existence for "an interactive BASIC whose *heap* lives
  above bank 0."
- **Not portable to the 45GS02** (65816 native mode ≠ 45GS02;
  opcode/register model differs), so **port effort: HIGH / treat as
  reference only**: what it did — keep interpreter code+ZP state in
  bank 0, use long-pointer addressing modes for every data structure
  — maps directly onto the 45GS02's [zp],Z 32-bit pointers.
  **Activity:** mostly 2019-2021; README still calls itself unstable
  though it ships on real hardware. No comparable Apple IIGS/SNES
  open BASIC found (GS BASICs are Apple-proprietary).
- URL: https://github.com/pweingar/BASIC816

#### 8. Wildcard that changed the landscape: Microsoft 6502 BASIC, MIT (Sept 2025)
Technically Commodore-lineage (it *is* the ancestor), but note:
Microsoft released the original 6502 BASIC 1.1 source (m6502.asm,
~6,955 lines, the pagetable multi-target source) under **MIT** on
2025-09-03. If the owner ever wants a license-spotless classic
Microsoft-style core to hack far-memory features into — with zero
attribution ambiguity — this now exists and EhBASIC's grey clause
stops being the only game in town.
https://opensource.microsoft.com/blog/2025/09/03/microsoft-open-source-historic-6502-basic/

#### 9. BASIC-in-portable-C compiled with cc65
Feasible but poor: uBASIC (BSD, dunkels.com/adam/ubasic/) compiles
under cc65 and would fit (~few KB code), but cc65-generated
interpreter code is 3-5x the size and far slower than hand asm, and
every richer C BASIC (MY-BASIC, etc.) blows the 24 KB ROM-bank budget
and assumes malloc/heap ≫ what's free. Verdict: only worth it for a
toy scripting sublanguage, never as *the* BASIC.

#### Comparison table (report 2)

| Candidate | License | >64K | REPL | Port | Note |
|---|---|---|---|---|---|
| EhBASIC (Klaus2m5) | non-commercial + attribution, un-clearable | TEACHABLE | Y | NONE | already running; far PEEK/POKE easy, far heap = surgery |
| BBC BASIC 6502 | proprietary, orphaned IP | NO | Y | LOW (tech) / blocked (legal) | ARM BASIC V is Apache-2.0, 6502 ROMs are not |
| FastBasic | GPL-2.0+ w/ linking exception | TEACHABLE (via VM opcodes) | Y (on-machine IDE) | MED | ca65-based, active 2025, best asm-effort/feature ratio |
| XC=BASIC 3 | MIT | NO | N | MED | compile-only kills it for this use |
| Tiny BASICs | varies | NO | Y | LOW | too small to matter |
| dflat | MIT | TEACHABLE | Y | MED-LOW | portable by design, one-man non-standard dialect |
| BASIC816 | GPL-3.0 | YES (24-bit long ptrs) | Y | HIGH (ref only) | the blueprint for a far-heap interactive BASIC |
| MS 6502 BASIC (2025 MIT) | MIT | TEACHABLE | Y | LOW-MED | Commodore-lineage but now license-clean |
| uBASIC via cc65 | BSD-3 | trivially (C far shims) | Y | LOW | too weak a language |

#### Top 3 for the BMC-K4510 (report 2's ranking)
1. **FastBasic** — the only active, license-clean (GPL2+exception),
   cc65-toolchain-native candidate where >64K support is
   architecturally cheap: add far-memory opcodes to a small bytecode
   VM using the 45GS02's [zp],Z pointers, rather than re-plumbing an
   entire interpreter. On-machine IDE satisfies "interactive."
2. **Keep EhBASIC, teach it far ops** — zero porting cost, users
   already have it; add FARPEEK/FARPOKE/FARBLOCK/bank-pool tokens
   (ROM budget permitting). Accept the grey "Derived from EhBASIC"
   non-commercial clause — fine in practice for a hobby public repo,
   but it can never be made properly open.
3. **dflat** — MIT, explicitly built to be ported to homebrew 6502
   machines, structured and interactive; the fallback if FastBasic's
   Atari FP/runtime extraction proves heavier than expected. Use
   **BASIC816** as the design reference for any far-heap work
   regardless of which core wins.

---

### The synthesis and the decision

Ranked across both reports for this machine: **migrate the native
BASIC to an msbasic/BASIC-M6502 build (pure-MS config, MIT anchor),
then add our own BANK / 28-bit PEEK-POKE / DMA tokens modeled on
BASIC65's command surface** — clean license table, authentic
Commodore-family dialect (EhBASIC is an MS-alike, demos mostly carry
over), ~8-9 KB core, far commands in code we fully own. The banner
can then print a C128-style number the C128's own way (pool
arithmetic) — backed by commands that really reach the 256 MB.
FastBasic stays the runner-up; BASIC816 the blueprint if a true
far-heap BASIC ever goes on the ballot.

Doc's call, same evening: do it. The coding session (then reworking
the sideways-ROM memory map) was messaged to reserve a slot for the
msbasic ROM plus token headroom; the vendoring itself is a later work
item. Decision also recorded in project memory
(`project-k4510-msbasic-decision.md`).

## 2026-08-24 (archive-side, 2) — the font research: a clean chargen

Same discussion session, next question from Doc: "research other 8x8
bitmapped fonts that are license compatible? ascii and petscii." The
motivation is the same trap the BASIC research walked around — the
original C64/C65 chargen ROMs are Commodore IP, Cloanto-claimed, the
same category as the BASIC ROMs. Two web-research agents ran in
parallel: one on PETSCII-flavored/Commodore-style fonts, one on
general open 8x8 ASCII faces. Full reports pasted verbatim below.

**The short version:** the drop-in winner is the MEGA65 open-roms
clean-room chargen (`bin/chargen_openroms.rom` — 4 KB, 512 glyphs,
both PETSCII sets, already in 8-bytes-per-glyph format, LGPL-3.0);
**unscii-8** is the public-domain powerhouse (complete Legacy
Computing/PETSCII repertoire, trivial .hex conversion, but the
512-glyph layout must be assembled by hand); **BESCII** (CC0) is the
most C64-flavored clean design. The prettiest matches — Pet Me and
Style64's C64 TrueType — are both license-excluded. And the legal rule
of thumb the community follows: *inspired-by with visible pixel
differences = safe; pixel-identical to the Commodore ROM = same bytes
as the ROM = don't put it in a public repo.*

---

### Report 1 — PETSCII-flavored 8x8 fonts with clean licenses

#### 1. Kreative Korp "Pet Me" family — NOT usable
- **Page:** https://www.kreativekorp.com/software/fonts/c64/ (the
  `/petme/` URL 404s; download:
  https://www.kreativekorp.com/swdownload/fonts/retro/petme.zip)
- **Coverage:** FULL, best-in-class — Pet Me (PET), Pet Me 2X
  (VIC-20), Pet Me 2Y (CBM2/80col), Pet Me 64/64 2Y, Pet Me 128/128
  2Y. "code points 0xE000-0x1FF encode the complete Commodore 64
  character set"; also mapped to Symbols for Legacy Computing since
  Oct 2019.
- **Format:** TrueType only.
- **License:** "Kreative Software Relay Fonts Free Use License
  v1.2f" — full text at
  https://www.kreativekorp.com/software/fonts/FreeLicense.txt. Free
  redistribution with credit is allowed (clause 1a), **but clause 2
  is fatal**: *"The User may not modify, reverse-engineer, or create
  any derivative works of the Software."* Converting the TTF into
  8-byte-per-glyph ROM data is a derivative work. Also clause 5:
  *"Kreative Software reserves the right to change this license at
  any time without notice."* **Verdict: excluded for ROM
  conversion.** (These fonts are also pixel-exact traces of the
  original ROMs, so the deeper Cloanto question below applies too.)

#### 2. Style64 "C64 TrueType (Pro)" — NOT usable
- **License page:** https://style64.org/c64-truetype/license
- Quoted terms: *"You MAY NOT: sell this font; include/redistribute
  this font in any font collection…; provide the font for direct
  download from any web site."* Embedding permitted only *"without
  any modification and using the same filenames"* (web @font-face),
  or *"as part of a software package but ONLY if said software
  package is freely provided to end users."* No modifications
  allowed in any case; anything more requires negotiating *"a
  (possibly commercial) license."*
- **Verdict: excluded.** No-modification + no-direct-download
  clauses are incompatible with converted ROM data sitting in a
  public git repo. Their https://style64.org/petscii/ page is still
  useful as a *reference* — it defines the "Direct PETSCII" PUA
  mapping (U+E000/E100/E200/E300 banks) but maps to PUA, not to
  U+1FB00, and offers no downloadable table.

#### 3. MEGA65 open-roms — CONFIRMED, ready-made ROM data
- **Repo:** https://github.com/MEGA65/open-roms — license per
  `LICENSE`: **LGPL v3 or later** (not plain GPLv3), copyright
  Gardner-Stephen / Standzikowski; some BASIC files MIT (Microsoft).
- **Charset exists and is complete:** `assets/8x8font.png` (8×4096
  px = 512 glyphs = **both charsets, 2×256, full PETSCII
  graphics**), built by `pngprepare` into
  `bin/chargen_openroms.rom` — a prebuilt **4096-byte chargen ROM,
  already in the exact 8-bytes-per-glyph format** needed. It is a
  distinct clean design, not a pixel copy of the Commodore ROM.
- Bonus: `bin/chargen_pxlfont_2.3.rom` (4 KB, also drop-in chargen
  format) — "PXLfont 88665b RF2.3" by Retrofan; `bin/README.md`
  states: *"PXL font was created by Retrofan, we got a permission to
  include it with Open ROMs under GNU Lesser General Public License
  3.0."* (Outside open-roms, PXLfont's own terms are
  permission-required — e.g. the Ozmoo copy at
  https://github.com/johanberntsson/ozmoo/blob/master/fonts/PXLfont-rf.license.txt
  is Ozmoo-only — so take it *via* open-roms under LGPL-3.0.)
- **Verdict: usable.** LGPL-3.0 on a 4 KB data blob is the only
  cost; for chargen data used as data (not linked code), LGPL's
  obligations reduce to shipping the license + source (the PNG).

#### 4. unscii-8 (Viznut) — CONFIRMED public domain, near-complete
- **Page:** http://viznut.fi/unscii/ (note: expired/invalid TLS cert
  as of 2026-08; content intact)
- **License:** quoted from the page: *"'unscii-16-full' falls under
  GPL because of how Unifont is licensed; **the other variants are
  in the Public Domain**."* So unscii-8 = public domain, no
  conditions.
- **Coverage (verified by downloading `unscii-8.hex`):** 3191
  glyphs; **213 glyphs in U+1FB00 Symbols for Legacy Computing**
  (Unicode 13 added 214 — effectively complete, and the page
  explicitly says the block includes "the missing PETSCII
  characters"); 256 glyphs across U+25xx (box drawing, block
  elements, geometric); card suits at U+2660/2663/2665/2666 all
  present. Full PETSCII repertoire reachable for both upper/graphics
  and lower sets via ASCII + these blocks; also ships `uns2uni.tr`
  (PUA↔Unicode mapping file).
- **Format/conversion:** HEX (Unifont hexdump — for 8x8 each line is
  codepoint + 16 hex digits = **exactly 8 bytes/glyph**; conversion
  is a 10-line script), plus PCF/TTF/OTF/WOFF. **Verdict: easiest
  and cleanest license of all; style is unscii's own, not
  Commodore-look.**

#### 5. CC0/MIT PETSCII-inspired fonts — two verified
- **BESCII** (Damian Vila) —
  https://github.com/damianvila/font-bescii (archived; moved to
  https://codeberg.org/Dmian/font-bescii). `LICENCE` file verified:
  **CC0 1.0 Universal** full legal text. README: "An 8x8 pixel font
  based on PETSCII… PETSCII symbols + some Amstrad CPC 464… PETSCII
  characters mapped using Direct PETSCII mapping" (style64 PUA
  scheme), plus Latin/Greek/Cyrillic/kana. **Coverage: full PETSCII
  graphics repertoire, deliberately *not* pixel-identical** (a
  redesign fixing C64 font flaws — see
  https://damianvila.com/blog/20240515-designing-the-bescii-font.html).
  Format: TTF/OTF/WOFF/WOFF2 **and FontForge .sfd source** (v2.0:
  `Bescii-Mono.sfd`) — conversion needs rasterizing the TTF at 8px
  or parsing the .sfd; moderate effort.
- **funscii** (Wuerfel21) — https://github.com/Wuerfel21/funscii.
  Verified: repo SPDX **CC0-1.0**; README: *"The font itself is put
  into the public domain - licensed under the terms of CC0 1.0
  Universal"* (builder is Apache-2.0). It is a fork of unscii with
  fixes + Japanese; source `font.txt`/`glyphs` in unscii's text
  format; community reports a C64-style binary build. Same coverage
  story as unscii-8.
- FontStruct "PETSCII Commodore"
  (https://fontstruct.com/fontstructions/show/1336244/petscii-commodore)
  is tagged CC0 but is a pixel-copy traced from the Wikipedia
  PETSCII chart — the uploader cannot launder the original bitmap
  into CC0; treat as unsafe.

#### 6. Unicode Symbols for Legacy Computing as mapping target — CONFIRMED
- Block U+1FB00–U+1FBFF, added in Unicode 13.0 (2020) specifically
  for PETSCII et al. Proposal **L2/19-025** ("Proposal to add
  characters from legacy computers and teletext", successor of
  L2/17-435):
  https://www.unicode.org/L2/L2019/19025-terminals-prop.pdf —
  **contains per-machine mapping tables (incl. Commodore PETSCII →
  Unicode)** that can drive a conversion; supplement L2/21-235
  (Unicode 16 additions) at
  https://www.unicode.org/L2/L2021/21235-terminals-supplement.pdf.
- Human-readable PETSCII→Unicode mapping table:
  https://www.kreativekorp.com/charset/map/petscii/
- Open 8x8 fonts implementing the block: **unscii-8** (213/214
  glyphs, verified), **funscii**, Pet Me 2019+ (license-blocked),
  Kreative's Fairfax HD (OFL, but 6x12 not 8x8).

#### 7. Other findings
- **VICE fallback charset:** none exists — VICE (GPLv2+) ships the
  *original* Commodore `chargen` ROM images on the old Usenet-era
  tolerance; the ROMs are not GPL and are exactly the
  Cloanto-claimed material (community discussion:
  https://www.lemon64.com/forum/viewtopic.php?t=73857). Nothing to
  reuse.
- **ZX Origins (DamienG):** ~hundreds of 8x8 fonts incl. "C64"
  export formats (C headers, 6502 asm); terms are informal —
  "freely available… in exchange for a mention in the credits"
  (https://damieng.com/typography/zx-origins/). ASCII-96 only, **no
  PETSCII graphics repertoire**; useful for alternate text glyphs,
  not for the chargen graphics half.

#### Ranked top 3 (report 1)
1. **MEGA65 open-roms `chargen_openroms.rom`** — already a 4 KB,
   512-glyph, 8-bytes-per-glyph chargen with both PETSCII sets,
   drop-in zero conversion; LGPL-3.0 (note in the license table).
   PXLfont 2.3 from the same `bin/` dir is a nicer-looking second
   option under the same license.
2. **unscii-8** — public domain, no strings at all; `.hex` converts
   trivially; complete Legacy Computing/PETSCII glyph repertoire,
   but you must build the 512-entry PETSCII layout yourself using
   the L2/19-025 mapping, and the look is unscii's, not Commodore's.
3. **BESCII** — CC0, deliberately C64-flavored (closest "feel" with
   a clean pedigree), full PETSCII graphics via Direct-PETSCII PUA
   mapping; needs TTF→bitmap extraction (it is a true 8x8 grid
   design, so 8px rasterization is lossless).

Excluded despite being the prettiest matches: Pet Me
(no-derivatives clause) and Style64 C64 TrueType (no-modification,
no-direct-download).

#### Legal caveat: pixel-exact clones of the Commodore charset
Honest summary: **unsettled, lean away.** Community/legal consensus
(e.g. the Lemon64 threads above): in the US, typeface *designs* are
not copyrightable, but the ROM as a data file is, so byte-copying
the chargen ROM is clearly off-limits; the open question is whether
an independently-typed but pixel-identical 8x8 bitmap is a "copy of
the ROM data" (it is byte-identical by construction) or an
uncopyrightable typeface rendering. No case law answers this for
8x8 chargen bitmaps; jurisdictions differ (UK/Germany protect
typefaces, though 25-year terms have expired for 1982 designs).
Cloanto/C64-forever actively license the ROMs, and open-roms chose
clean-room reimplementation precisely to avoid the argument.
Practical rule the community follows and open-roms/BESCII embody:
*inspired-by with visible pixel differences = safe; pixel-identical
= same bytes as the ROM = don't put it in a public repo.* All three
ranked picks satisfy this.

---

### Report 2 — General open 8x8 ASCII bitmap fonts

#### 1. dhepper/font8x8 — https://github.com/dhepper/font8x8
- **License:** Public domain (stated in repo README). *Caveat:*
  provenance chain is "directly derived from an assembler file" by
  Marcel Sondaar, itself based on "IBM public domain VGA fonts."
  IBM never formally dedicated these to the PD — the claim
  ultimately rests on the US doctrine that bitmap glyph designs are
  uncopyrightable. Community treats it as safe; used everywhere
  (OS-dev tutorials, embedded projects).
- **Coverage:** Basic ASCII (0x00–0x7F), extended Latin
  (0x80–0xFF), box drawing, block elements, Greek, Hiragana — as
  separate C arrays.
- **Format:** C header arrays, exactly 8 bytes/glyph, LSB =
  leftmost pixel. **Zero conversion needed** — already chargen ROM
  format (bit-reversal per byte may be needed depending on shift
  orientation).
- **Readability:** Classic IBM-ish face; lowercase without true
  descenders (CGA-style squash); 0 unslashed but distinguishable
  from O; 1/l/I distinct. Serviceable, very "PC."
- **Status:** Repo dormant (7 commits) but stable.

#### 2. Ultimate Oldschool PC Font Pack (VileR) — https://int10h.org/oldschool-pc-fonts/readme/
- **License:** **CC BY-SA 4.0**. Attribution: credit "VileR" + link
  to int10h.org. Adaptations (which ROM-converted glyph data is)
  must be distributed under a compatible license.
- **8x8 faces in the pack:** IBM CGA 8x8, AMI EGA 8x8, ATI 8x8,
  Verite 8x8, ToshibaTxL1 8x8, and dozens more OEM 8x8s (CGA
  thin/thick, EGA, Amstrad, Phoenix, etc.).
- **Mechanics:** Converting glyphs to a C array/ROM binary is fine
  under CC BY-SA with credit + the CC BY-SA 4.0 notice on the
  derived font data. The share-alike obligation attaches to the
  font data, not to the emulator code that merely loads it (fonts
  as data are generally treated as separate works — convention, not
  litigated certainty). One CC BY-SA row in the license table.
- **Underlying IBM/OEM designs:** VileR's own legal analysis: "The
  raw bitmap typefaces are not copyrightable, unlike fonts in
  specific formats such as .fon and TrueType (which qualify as
  software)" (citing *Eltra Corp. v. Ringer*); IBM's fonts were
  cloned by every BIOS vendor for decades without litigation.
  Well-founded **for the US**; some jurisdictions (Germany, UK)
  protect typefaces — essentially zero practical risk, small
  theoretical non-US risk.
- **Format:** TTF/OTB + PNG specimens + raw bitmaps in the extras;
  conversion easy.
- **Readability:** The CGA/EGA 8x8 faces are the gold standard for
  readable 8x8: distinct 1/l/I, O/0, decent pseudo-descenders.

#### 3. ZX Origins (Damien Guard) — https://damieng.com/typography/zx-origins/
- **License:** Informal: fonts are "freely available to be used in
  games you create in exchange for a mention in the credits section
  or perhaps a coffee." Commercial use explicitly allowed;
  recommended credit "*[fontname]* font by DamienG". **The one
  prohibited use: "redistributing the font as a font."**
- **The catch:** a chargen ROM in a public repo *is* redistributable
  font data — a raw 768-byte glyph table sits in a gray zone
  between "used in a product" (allowed) and "redistributed as a
  font" (not). He is explicitly open to email; one message would
  settle it. Not a drop-in for a strict license table without that.
- **Collection:** 263 original 8x8 typefaces, each shipped as TTF
  **plus C headers and Z80/6502/x86/68000 assembly** — already
  8-bytes-per-glyph. Coverage full printable ASCII (Spectrum
  heritage), typically no box drawing.
- **Standout readable faces:** **Envious** (very clean terminal
  face), **Localhost**, **Keytop**, **Clear Plan**, **Computer**.

#### 4. unscii-8 — http://viznut.fi/unscii/ (repo: https://github.com/viznut/unscii)
- **License:** "You can consider it Public Domain (or CC-0)" except
  the Unifont-derived files (unifont.hex, unscii-16-full) which are
  GPL. **unscii-8 is PD/CC0.**
- **Coverage:** Huge — best in this sweep. Full ASCII, Latin-1, box
  drawing, block elements, Teletext/Videotex mosaics, PETSCII
  pseudographics, shades, round corners. Variants: unscii-8 plus
  stylistic 8x8s (thin, alt, fantasy, mcr).
- **Format:** .hex (trivially parseable), plus BDF/PCF/TTF/OTF.
  Conversion to ROM data is a 10-line script.
- **Readability:** Designed as a *usable terminal font*, not just
  retro pastiche — good 0/O and 1/l/I distinction, consistent
  stroke weight; compressed descenders (8px cell limit).

#### 5. Spleen — https://github.com/fcambus/spleen
BSD 2-Clause. **Sizes: 5x8, 6x12, 8x16, 12x24, 16x32, 32x64 — no
8x8 exists.** Dismissed. (If an 8x16 is ever wanted for an
80-column mode, Spleen 8x16 with full CP437 + BSD-2 is a top pick.)

#### 6. Fantasy-console and homebrew fonts
- **TIC-80:** project MIT, but the system font is **6x6** in 8x8
  sprite cells. Dismissed on size.
- **PICO-8:** font and palette are **CC0** (official FAQ) — but
  glyphs are 3x5. Dismissed on size.
- **Pixel Operator** (Jayvee Enaguas) — **CC0 1.0**
  (fontlibrary.org; source notabug.org/HarvettFox96/ttf-pixeloperator).
  8px-height mono variants exist but ship **TTF only** — rasterize
  at 8px and verify the advance is actually 8. Usable with modest
  work; license perfect.
- **Kitchen Sink** (Polyducks, itch.io) — **6x8, not 8x8**, and
  "redistributing the font as an asset is prohibited" + an NFT
  clause. **Excluded** on both size and license.
- **Portfolio 6x8:** 6x8 (Atari Portfolio). Dismissed.

#### 7. Terminus
Sizes 6x12 through 16x32; **no 8x8**. SIL OFL 1.1. Dismissed.

#### 8. Linux consolefonts and other BDF/PSF sources
- **Kernel `lib/fonts/font_8x8.c`:** SPDX **GPL-2.0**, "generated
  by cpi2fnt," no origin credit. Same IBM-derived CP437 face as
  font8x8, but taking it from the kernel imports GPL-2.0 —
  pointless when dhepper/font8x8 offers equivalent glyphs as PD.
  Same for `font_pearl_8x8.c`. Skip.
- **IBM BIOS font recreations:** the canonical open ones are
  exactly the int10h pack (CC BY-SA) and dhepper/font8x8 (PD).
  Nothing cleaner-licensed found; nothing else notable at 8x8
  surfaced that beats the above.

#### Ranked top 3 (report 2)
1. **unscii-8** — PD/CC0, widest coverage by far (ideal raw
   material for a fantasy machine's full 256-glyph chargen), .hex
   converts trivially, genuinely readable. Cleanest license + best
   fit. Watch-out: don't grab the Unifont-derived files (irrelevant
   at 8x8).
2. **dhepper/font8x8** — already literally chargen-format C arrays,
   PD-labeled, ASCII+Latin+box+blocks. Slightly weaker provenance
   story but universally used; fine as fallback or "boring
   default."
3. **Ultimate Oldschool PC Font Pack (IBM CGA 8x8 / ATI 8x8 /
   Verite 8x8)** — the most authentic and most readable faces, but
   CC BY-SA 4.0 means attribution + share-alike on the converted
   glyph data — one viral-ish row in the license table. Use for the
   real CGA look if the flag is acceptable.

ZX Origins is the honorable mention: 263 original faces,
pre-converted 6502 source, but the "don't redistribute as a font"
clause needs one clarifying email before a raw glyph table lands in
a public repo.

---

### The combined ranking

For the K4510 chargen, both sweeps agree on the shape of the answer:

1. **open-roms `chargen_openroms.rom`** (LGPL-3.0) — the only
   ready-made, complete, 512-glyph PETSCII chargen in drop-in
   8-bytes-per-glyph format; zero conversion work. PXLfont 2.3 from
   the same directory (same license route) if a nicer face is
   wanted.
2. **unscii-8** (public domain) — the no-strings powerhouse for
   both ASCII and PETSCII repertoires; requires assembling the
   512-entry layout via the L2/19-025 PETSCII→Unicode mapping, and
   the look is its own.
3. **BESCII** (CC0) — the most Commodore-flavored clean design;
   TTF→bitmap extraction needed (lossless — it's a true 8x8 grid).

The prettiest candidates (Pet Me, Style64) are license-excluded;
pixel-identical recreations of the Commodore charset are avoided on
principle regardless of who typed them in. No decision taken yet —
this entry is the research record; the pick is Doc's.

## 2026-08-24 (archive-side, 3) — reset chord and the C64u-style menu: the design

Doc, adding an element to the project: *"1) change reset key from
F12 to something like Commodore-Restore. 2) wire commodore F7 to
open a settings menu like BMC64 and the C64u where we can adjust
border width, change screen font etc — please architect a logical
scaffold for future additions, please make it look more like the
C64u menu than the BMC64 one."*

Archive-side session checked the seams in the mirror first:
`sdl/main.c:108` is the F12 → `cpu65_reset()` binding, and
`core/io.h:143` shows the machine receives F1–F12 as $90–$9B — so F7
currently reaches guest programs (and BBC BASIC uses function keys).
The design below went to the coding session as the work order;
implementation judgment stays with it. Pasted for the record.

### 1. Reset: F12 → "Commodore-Restore" chord

Unbind SDLK_F12. Replace with a two-key chord evoking C= + Restore:
suggested Left-Super ("Commodore key") + PageUp ("Restore"), or
Left-Ctrl+PageUp if Super fights the window manager — final pick from
the existing keymap. Requirements: chord = hard reset, cannot fire
from a single accidental keypress, and the binding lives in the
settings registry so the menu can rebind it later. F12 ends up
unbound.

### 2. F7 → settings menu, C64u-style

**Module layout (portability first — Pi/Circle must reuse it):**
`core/ui/` with `settings.c/h` (registry + persistence), `menu.c/h`
(tree + navigation + state machine), `ui_draw.c/h` (text-cell
drawing primitives). No SDL types anywhere in core/ui — the host
(sdl/main.c now, Circle later) feeds key events in and composites
the overlay buffer out, same pattern as the rest of core/.

**Settings registry (the actual scaffold):**
- Typed entries: `{ id "video.border_width", type
  BOOL|INT|ENUM|CHORD, default, min/max/step or enum labels, apply
  callback, flags LIVE|NEEDS_RESTART }`.
- Persisted to a human-editable key=value file (`k4510.cfg` beside
  `fs/`; on the Pi, the SD root), with a version key. Unknown keys
  preserved on rewrite. Load at boot, save on menu close (the C64u
  behavior: leaving the menu persists).
- Adding a future setting = one registry row + one menu row + an
  apply hook. That is the whole extension contract.

**Menu tree (declarative):** `MenuItem { label, kind
SUBMENU|TOGGLE|ENUM|INT|ACTION|INFO, settings id or action fn }` —
static const arrays, no runtime construction.

**Modality:** F7 opens/closes. While open: emulation frozen (core
stops ticking), audio muted, all keys routed to the menu. Close =
resume exactly where frozen. The menu must not touch machine RAM —
it has to open even when the guest has crashed (the Ultimate's
freeze-menu virtue).

**The F7 conflict, flagged:** the machine receives F7 as $96 and BBC
BASIC uses function keys, so the menu shadows a guest key.
Mitigation: only *unmodified* F7 opens the menu (Shift+F7 still
reaches the guest), and `input.menu_key` is itself a registry
setting so it can be moved. Doc chose F7 knowingly; the escape
hatch is there.

**Look (C64u, not BMC64):** not a full-screen takeover — a framed
window (double-line box) centered over the live screen, machine
display dimmed ~50% behind it. Title bar in the frame top. Items
listed with values right-aligned in the same row. Cursor bar
inverse-video. ENUM values edited via a small centered popup window
listing the options (the C64u context-menu feel), not by left/right
cycling. Bottom row inside the frame: function-key legend ("F1 Help
F7 Close  ESC Back"). ESC backs out one level; at top level ESC/F7
closes. Palette: the Ultimate's dark-blue panel / light-blue frame /
white text vibe, taken from the machine palette, not hardcoded RGB.
Drawn with an 8x8 font from `data/fonts/` — `font8-unscii.bin` is
drop-in and public domain; the menu must NOT depend on the machine's
current chargen (crash-proofness again).

**Initial content (small, proves the scaffold):**
- *Video:* border width (INT, live); screen font (ENUM: kernel8 /
  unscii / openroms / pxlfont — live-swaps the chargen, which turns
  the undecided font swap into an A/B doable from the menu);
  scale/fullscreen if cheap.
- *Audio:* master volume.
- *Input:* reset chord (CHORD), menu key.
- *Machine:* reset now, power-cycle, Tube restart (ACTIONs).
- *Info:* version, ROM build date, fs path.

**Future hooks (design for, don't build):** save-state slots,
palette editor, CRT/scanline effects, Pi-specific audio/overscan,
per-machine profiles.

**Pi note:** draw only while open and only on dirty state — the
menu must cost zero frames when closed (the frame-budget lesson).

Sent to the coding session 2026-08-24; it logs its own entry when
built.

## 2026-08-25 (ai) — the F7 menu and the reset chord

Doc: "ok do f7 menu then mad pascal". The work order was the archive
session's design (archive BUILD-LOG, "reset chord and the C64u-style
menu"); this is what got built from it.

**core/ui/**, no SDL anywhere in it, three files:
- `settings.c/h` — the registry: a table of typed rows `{ key, label,
  BOOL|INT|ENUM|CHORD, default, min/max/step or labels, flags }` and
  the `set_id` enum. Persisted to `k4510.cfg` beside fs/ (the Pi's cwd
  is SD:/k4510, so the same relative name): `key = value`, comments
  and unknown keys preserved on rewrite, missing keys appended,
  enums by name. Load at boot, save when the menu closes (and at
  quit), only if something changed.
- `menu.c/h` — the tree as static tables (Video / Audio / Input /
  Machine / Info), a stack of (menu, cursor), an ENUM popup, ACTION
  rows reported to the host through `menu_take_action()`, INFO rows
  the host fills. Draws only when dirty.
- `ui_draw.c/h` — cells into a 640x480 8-bit overlay (0 = see
  through), double-line box, its own palette by name (the machine's
  C64 set, fixed, so a guest that zeroed the VICKY palette cannot
  hide the menu), its own font (font8-unscii.bin, public domain;
  falls back to the kernel font in RAM).

**The seam that makes it one implementation:** `kbd_push` in core/io.c
— every key on both hosts passes through it (SDL on the desktop, the
C64 keyboard scan on the Pi). The menu key (unshifted) opens the menu
there; while open, every key is the menu's and the machine sees
nothing. Shift+F7 still reaches BBC BASIC; the key itself is a setting
(F7 / F8 / F11 / Pause).

**The host** (sdl/main.c, which is also the Pi's core 1): while open,
no CPU, no VICKY, no SID (the audio ring drains to silence) — the
frame buffer holds the last picture, composited at half brightness
under the overlay. Border width/colour = the texture drawn inset over
a cleared background; volume = the samples scaled into the ring; font
= mem_load at $010000 (the ROM points VICKY there) — the two PETSCII
chargens (open-roms, PXLfont) rearranged into ASCII/CP437 order on
the way (letters and digits from the lower-case set, the box glyphs
the ROM and JIM use from the graphics set), which turns the deferred
font A/B into a menu row; full screen = SDL_SetWindowFullscreen.
Actions: reset, power cycle (host_zero the RAM, mem/io reset, font,
ROM, reset), stop the Tube ($D803 = 2), quit. F12 is unbound; the
reset chord is Super+PageUp by default ("Commodore + Restore"), or
Ctrl/Alt+PageUp, or Ctrl+Alt+Del — from the setting.

A dev knob on the way: `K4510_SHOT=file.ppm:N` writes what is on the
glass after N frames and quits, so the SDL frontend itself can be
screenshotted headless (`SDL_VIDEODRIVER=dummy`; the renderer falls
back to software). That is how the pictures were taken.

**Verified.** test/uitest (13th suite): load/clamp/wrap/save with
comments and unknown keys kept and missing keys appended; keys
through kbd_push (plain key passes, Shift+F7 passes, F7 opens, keys
swallowed while open, draw-once-then-clean, frame and bar in the
overlay, see-through outside); navigation, INT steps by Left/Right,
the ENUM popup, Reset acting and closing, close reported once, the
menu key moved to F8. All 13 suites + tubetest + nettest green.
Screenshots: the main page over the dimmed boot screen; Video with
the font popup.

**The Pi.** core/ui objects in pi/Makefile; kernel built on p15 and
staged in pkg/ with data/fonts/ (already on the card layout). The
C64 keyboard's F7 (c64kbd.cpp pushes KEY_F1+6) opens it; the reset
chord needs a USB keyboard there (no Super/PageUp on a C64 keyboard —
a Restore-key chord for the GPIO keyboard is a future row).

Not done: save-state slots, palette editor, CRT effects, per-machine
profiles (the hooks the design listed as "design for, don't build");
the ~50 % dim is a shift, not a blend with the panel colour.

## 2026-08-25 (aj) — Mad Pascal: the K4510 is a target

Doc: "then mad pascal". The pinned plan (a cross-toolchain like cc65,
the target modelled on the Neo6502 one, a k4510 unit, a Mandelbrot,
the MATH unit later) — built as planned, with one change of model.

**The shape.** Mad Pascal is not vendored: `pascal/mp/` holds only
the K4510 target as it lives inside a Mad-Pascal checkout, and
`pascal/install.py` copies it in, patches `src/Targets.pas` (the
target's id, cpu 65c02, zpage $22, buffer $0300, code $0800, the
.prg header), `src/include/syntax.inc` (the -target parser),
`src/mp.pas` (the expression stack at $64 unless -stack says
otherwise) and the seven `lib/targets/*.inc` dispatchers, then
rebuilds `mp` with FPC. Idempotent, `make clean` first (FPC kept a
stale Targets unit once, and the "new" compiler compiled the old
header — an hour's lesson). Doc's checkout is
~/Projects/neo6502_dev/Mad-Pascal (from the Neo6502 guide work);
MADS beside it. `make pascal-install`, `make pascal`.

**The model changed.** The plan said Neo6502; the Neo target routes
everything through the Neo's mailbox API. The C64 target is the
true relative — a KERNAL jump table, CHROUT with A = char — and the
K4510 has the same shape at $FF80. Then the better idea: `@putchar`
does not go to the ROM's CHROUT at all but to JIM ($DA00). Write and
WriteLn land on a VT100, so the CRT unit is real: GotoXY and
TextColor are register stores (JIM's CX/CY/FG/BG), ClrEol/InsLine/
DelLine are escapes, ReadKey/KeyPressed read the keyboard device,
Pause/Delay count frames at $D50D, Sound plays SID 0. The ROM's
run_at hands the console cursor to JIM before the program and takes
it back after (the JIM dirty bit — built for telnet.prg yesterday,
now earning its keep), so `HELLO` prints and the prompt follows on
the next line.

**The .prg header, the day's bug.** `org [a($0800),a(START)],$0800`
(the C64 target's idiom for its 2-byte load address) emitted the
four header bytes at the *current* address — inside the zero-page
block at $22 — and `opt f+` then filled $FF up to $0800: a 2647-byte
file whose code the ROM loaded 2010 bytes too high; JIM saw nothing,
the machine came back to the prompt (the ROM's stack discipline
held). Seven MADS experiments to see what actually happens; the fix
is the plain form: `org $07FC / dta a($0800),a(START) / org $0800`,
so the first emitted byte is the header. hello.prg: 637 bytes.

**system_k4510.inc / crt_k4510.inc.** The interface declarations in
lib/system.pas and lib/crt.pas fix each routine's modifiers
(`assembler` or not), so ParamCount/ParamStr are assembler
(ParamStr builds its string[32] at adr.Result, from the ROM's ARGS
call: A = length, $F0/$F1 the tail), Random(byte) is an 8-bit LFSR
stirred by the frame counter, ClrScr & co are Pascal with asm
blocks. The k4510 unit: no arrays of records (Mad Pascal supports
only arrays of ^record), so the SIDs are a byte array; FarPeek/
FarPoke are the 45GS02's `NOP + LDA/STA (zp),Z` on :bp/:bp2.

**Demos** (demo/pas/, committed as fs/PRG/*.prg): HELLO (colours,
ParamStr), PSIEVE (1899 primes × 10 in 46 frames = 736 ms at 40.5
MHz), PMANDEL (78×28, 16 colours through JIM, 22.10 fixed point in
32-bit integers, 250 frames). test/pastest.sh (14th leg of make
test) runs HELLO and PSIEVE through the ROM and checks the text.
Debug knob added on the way: K4510_TERMLOG=file logs every byte JIM
receives (desktop only).

Guide: chapter 7, Pascal. Not yet: the MATH unit behind `single`
(the flag-plant), a graph unit on VICKY, sysutils/graph are raw's
stubs.

## 2026-08-25 (ak) — the flag-plant: Pascal's single on the MATH unit

Doc: "wire the MATH unit into Pascal's single". The runtime's SINGLE
routines (base/common/single.asm, David Schmenk's IEEE-754 library
from VM02) have a fixed contract the compiler emits calls against:
operands in eax (FP1MAN) and edx (FP2MAN), results in ecx (FPMAN);
@FCMPL answers sign(B - A) in A with B = FPMAN; @F2I/@I2F/@FFRAC in
place on FPMAN; @FROUND from FP2MAN to an integer in FPMAN (round
half away from zero). base/k4510/single.asm keeps the names and the
registers and replaces the bodies: copy to F0/F1 at $D700, one write
to FOP ($D720: ADD 1, SUB 2, MUL 3, DIV 4, CMP 18, ROUND 17, ITOF
19, FTOI 20), copy back. FFRAC is FTOI, ITOF into F1, SUB. The
compiler also addresses @FCMPL.A and @FCMPL.B (the software routine's
local equates), so those two lines stay.

rtl6502_k4510.asm now carries rtl_default's include list inline with
the one substitution, under `.ifdef SOFTFLOAT` (a MADS -d symbol; a
`.ifdef MAIN.@DEFINES.X` there — the compiler's own define mechanism —
is a forward reference into a .local and half-assembled both branches).

Two bugs on the way, both in the contract: the compare's sense
(I computed A - B; the answer is B - A: "1.5 < 2.25: NO" until it was
swapped), and the missing @FCMPL.A/.B equates.

Numbers (demo/pas/pfloat.pas): 5000 rounds of mul, div, add, sub —
**5 frames on the MATH unit, 24 in software** (4.8x), same results to
the printed digit; sqrt(2) = 1.414210, ln(10) = 2.302581, 2^10 =
1024, atan2(1,1)*4 = 3.141589 from the unit's transcendentals, now
in the k4510 unit as MathSqrt/MathSin/MathCos/MathTan/MathAtan/
MathAtan2/MathExp/MathLn/MathPow/MathFloor. pastest checks PFLOAT's
lines. pfloat.prg 2878 bytes against 3271 with the software library:
the unit is also smaller code.

Not done: SYSTEM's own Sqrt/Sin/Cos (Pascal polynomials in
lib/system.pas) still run in software — routing them means patching
system.pas itself, a step further into the checkout than install.py
takes; a graph unit on VICKY.

## 2026-08-25 (al) — save states: the menu's first "future hook"

Doc's order for the day: 5, 6, 3, 2, 4 of the list — save-state slots
first. core/state.c: a chunked file (tag, length, bytes; the header
K4510ST1), each module contributing its own chunks through a hook
(mem: MAP, the bank registers, the far gate; vicky: registers,
palette, raster compare, SHEILA's pc/wait; io: frames, the keyboard
queue, fs cwd and registers, DMA, MATH, SYS, the SID shadows, the
sequencer, the Tube ULA's graphics state; term: JIM whole) plus the
CPU struct as it is and every non-zero 4 KB page of RAM (a page scan
of the 256 MB per save: 66 KB for a machine at the prompt). A chunk
whose size differs from this build's is refused, so a file from
another version fails cleanly (-2) instead of loading garbage. Not in
the file: the Tube co-processor (another core, another program: a
load stops it first) and network connections (dropped on load). SID
state comes back by replaying the 25 shadow registers into reSID —
envelopes restart, which is the honest compromise.

The menu: Machine → Save state / Load state, four slots, each row
showing the file's date or "empty" (the host fills it: menu_slot).
Save keeps the menu open and refreshes the row; Load closes it and
the machine continues from the file on the next frame. Files:
k4510-slotN.k4s beside k4510.cfg (SD:/k4510 on the Pi).

Verified: test/statetest (14th suite) — the ROM runs, `ECHO SAVED
HERE`, save; CLS + reset + 60 frames; load; screen hash, PC, JIM's
cursor, a MATH and a DMA register back; `ECHO STILL ALIVE` prints
after the load; a foreign header refused, a missing file -1. Then
end-to-end through the SDL frontend (K4510_KEYS through the menu):
run one saves slot 1 after ECHO STATE ONE; run two, a cold boot,
loads it — the screenshot shows STATE ONE on the screen it never
printed. Pi kernel built (1,631,776 B, md5 60ec4b3b), staged.

## 2026-08-25 (am) — the small ones: SYSTEM's transcendentals, the legend, NAWS

Item 6 of Doc's list. (1) pascal/install.py now also patches Mad
Pascal's lib/system.pas: Sqrt/Sin/Cos/ArcTan on single and Exp/Ln on
Float get a `{$ifdef k4510}` body — the argument into F0, one write
to FOP, F0 back — with the original polynomial kept under `{$else}`;
`Sqrt(2.0)` from SYSTEM now answers 1.414210 from the unit, and the
k4510 unit's MathXxx twins remain for the ones SYSTEM lacks (Tan,
Atan2, Pow, Floor). (2) The menu's legend names the actual menu key
and says "Shift+F7 to the machine"; the window grew to 52 columns to
hold it. (3) telnet.prg negotiates: DO NAWS is answered WILL + the
size from JIM's COLS/ROWS registers, DO TTYPE with WILL and, on the
server's SEND, IS "ANSI"; everything else still WONT/DONT; IAC IAC
passes as a byte. nettest's echo leg unchanged.

## 2026-08-25 (an) — WordStar 4 on JIM: already installed, given the whole screen

Item 3. The premise ("WordStar needs a WSCHANGE pass to VT100") was
wrong in the best way: WSCHANGE on E: user 3 reports *WordStar is
currently installed for: ANSI Standard*, and `WS` → `D` → `READ.ME`
opens the editor on JIM — the edit menu, the ruler, the text, all
there (screenshot sent). The pass that was worth making: Console →
Monitor → Screen sizing, height 24 → 29, width 80 → 79, so the editor
uses the console's whole 79×29. WS.COM on E:3 rewritten by WSCHANGE
(in Doc's CP/M library, fs/CPM is gitignored; the card copy staged;
hdieu's master untouched — that is Doc's mirror.py). The whole thing
was driven through test/tubetest with `~` waits, reading the screens
as text; test/capture learned the same `~` waits on the way, because
a spawned co-processor is slower to answer than an in-process one and
keystrokes were outrunning WordStar's opening menu.

## 2026-08-25 (ao) — GRAPH on VICKY

Item 2. lib/graph_k4510.inc + graphh_k4510.inc: Mad Pascal's GRAPH unit
gets VICKY's layer 1 — the 640×480 8-bit bitmap at $200000 that BBC
BASIC's ULA draws on — over the console. The primitives the unit
requires of a target: InitGraph (layer registers, DMA clear, the video
control byte's line-doubling bits cleared, because the ROM's MODE 1 is
640×240 doubled — the first shot had every circle twice as tall and
the colour bars off the bottom), SetColor/SetBkColor, PutPixel/GetPixel
(bounds-checked in asm; y*640 by the MEGA65 multiplier at $D770, the
45GS02's [bp],Z store), LineTo (the blitter's LINE op, registers
$D084-$D08B), CloseGraph (layer off, control byte back), OutTextXY (the
ROM's font at $010000, pixel by pixel). Added, declared in the header
include: DrawLine, FillBar (FILL op, clipped), FillTriangle (TRIANGLE
op, $D084-$D08F), ClearDevice (DMA fill 0). Mad Pascal's generic Line
funnels into LineTo, so Rectangle/Circle/Ellipse/Bar all end on the
blitter or on PutPixel. PGRAPH: sixteen bars, a 36-ray star, six
circles, two triangles, a frame, two text lines, the console's own
line on top (screenshot sent). Also fixed: the runtime now says `opt
c+` itself (a program without the CRT unit reached single.asm's `stz`
before the compiler's own `opt c+` line). pastest runs PGRAPH and
checks the shell comes back.

Not done: the lower half of the screen under a 480-line picture is
the text map's uninitialised rows (black cells) — a graphics program
paints over it or lives with it; a pattern/line-style; FloodFill is
Mad Pascal's generic one on GetPixel/PutPixel (slow).

## 2026-08-25 (ap) — the guide, rebuilt: 37 pages

Item 4. The guide builds on ubuntu-s1 (xelatex lives there): a fresh
clone of the mirror, the machine's artifacts brought over by tar
(rom/, tube/bbcbasic, fs/PRG, fs/BBCBASIC, fs/EHBASIC, fs/FORTH, the
CP/M drives the shots need — A:, E:3, H:3 — data/font8.bin), then
test/capture, cpm/runcpm and sdl/k4510 built there. Two things the
laptop never showed: reSID's version.cc wants VERSION from autoconf's
config.h (the laptop's version.o predates that header; the resid rule
now passes -DVERSION), and doc/guide/shots/ is gitignored so a fresh
clone has no directory for make-shots.sh to write into (it mkdirs
now). Five new shots: turbo (JIM in the Tube chapter), wordstar (CP/M),
pmandel and pgraph (Pascal), and the menu — which is the frontend's,
not the machine's, so it comes from sdl/k4510 under SDL's dummy
driver through K4510_SHOT, a PPM converted with PIL. Chapters 1, 4, 6
and 7 place them. XeLaTeX twice: 37 pages, no warnings.

## 2026-08-25 (aq) — the BASICs tested from the inside

Doc: "how about test suites for bbcbasic and ehbasic so we can confirm
proper operation inside those environments?" Yes: every layer under
them had a suite; nothing asserted that a program *in* either BASIC
computes, prints, files and reaches the hardware right.

fs/EHBASIC/TEST.BAS (32 checks) and fs/BBCBASIC/TEST.BBC (28): self-
checking programs — arithmetic, the MATH unit's SQR/SIN/COS/EXP/LOG/
ATN/TAN against known values, strings, FOR/REPEAT/WHILE/CASE, DEF FN
and PROC/FN, arrays, logic, the SYS registers (40500 kHz, 256 MB),
the frame counter, POKE/PEEK, GRAPHICS/PLOT/LINE/TRI/PALETTE/GCLS and
back (EhBASIC), MODE 2/GCOL/PLOT/DRAW/CIRCLE FILL/VDU 19, sprites
(VDU 23,27), SOUND and MODE 7 back (BBC, through the ULA), OPENOUT/
PRINT#/OPENIN/INPUT# through the Tube's file layer, SAVE through the
ROM, and the * escape (ECHO STAR OK read off the screen). Silent
unless a check fails ("FAIL AT CHECK n"), one verdict line each.
test/basictest.sh drives them headless and is in make test (16th
leg); the in-process Tube when built, the desktop one otherwise.

Three things the tests taught: 300 empty FOR iterations finish inside
one frame at 40.5 MHz (the frame-counter probe loops until it moves);
EhBASIC's SAVE writes to the shell's cwd (/), not the program's
directory; BBC BASIC's CHAIN wants a tokenised file — LOAD then RUN
for text — its integers are 64-bit (no wrap at 2^31), and CASE ... OF
must end its line. Run here on ubuntu-s1 in the guide-build clone
while the laptop was off the tailnet; committed from here.

## 2026-08-25 (ar) — CAPSLOCK, and the BASIC tests made legible

Doc, after a session on the emulator: EhBASIC "hangs on startup, no dump";
BBC BASIC's tests pass but he wants them to explain themselves; and no
Shift-lock on his keyboard, so holding Shift for uppercase keywords is a
chore.

**The EhBASIC "crash" was a stale build.** EhBASIC starts and computes
fine at HEAD (headless and the SDL frontend, verified). The tell is in
Doc's own screenshot: the BBC banner still reads "for Linux Console",
a string renamed days ago -- his emulator is running old binaries
(source pulled, `make` not fully re-run; note bbcbasic needs nasm, so a
partial build keeps the old one). Fix on his side: `make clean && make`,
delete a stale k4510.cfg, and `git pull ubuntu-s1 master`.

Aside: cc65 built from source into the scratch area so the ROM could be
rebuilt on ubuntu-s1 at all (the laptop, the only cc65 host, was off the
tailnet). ubuntu-s1 can now build the ROM -- worth keeping.

**CAPSLOCK** (rom/kernal.c): a one-byte flag and caps(), through which
every key the ROM reads passes -- k_getin (the shell, EhBASIC and any
GETIN/CHRIN caller) and the Tube key-forward loop (BBC BASIC). On, a-z
come up A-Z; digits and symbols untouched (a caps lock, not shift lock).
`CAPSLOCK` toggles, `CAPSLOCK ON`/`OFF` are explicit; `*CAPSLOCK` reaches
it from inside either BASIC. Verified: `*capslock` then lowercase
`print "hi"` -> `PRINT "HI"` -> runs, in both BASICs. BSS still fits.

**The BASIC self-tests, verbose** (fs/EHBASIC/TEST.BAS 34 checks,
fs/BBCBASIC/TEST.BBC 28): each area now prints its name, goal, expected
result and a sample computed value, then [PASS]/[FAIL]. At startup a 5s
window: press a key for step-by-step (a pause between tests), else it
runs straight through -- EhBASIC times the window on the frame counter
($D50D), BBC on INKEY(500). The harness presses nothing, so both run
auto; basictest.sh budgets the wait (3000 / 4200 frames). Two lessons:
EhBASIC's line-input buffer is short (~72 chars -- the first verbose
draft overflowed and truncated header lines into syntax errors, so the
strings are terse and split across lines); and a poll-and-reprint pause
loop spams its prompt (print once, then poll).

## 2026-08-25 (as) — BBC BASIC lives inside fs, and starts where the shell is

Doc: "annoying to prepend BBCBASIC to LOAD a .BBC file ... BBCBASIC
should see .../fs as just / (not the whole tree above fs)."

The desktop Tube is R.T.Russell's bbcbasic as a real process on a pty,
using real host paths: it was chdir'd to fs/ and showed the full host
path (/home/.../fs/*.bbc), so a demo in fs/BBCBASIC needed
LOAD "BBCBASIC/NAME.BBC". Two fixes:

- core/io.c starts the co-processor in the machine's current directory
  (fs_root/fs_cwd), not at fs_root -- so CD BBCBASIC then BBC lets
  LOAD "KALEID.BBC" work bare. It also sets K4510_ROOT to the absolute
  fs path, and (the bug that cost the most) resolves the bbcbasic and
  runcpm binaries to absolute paths with realpath(...,NULL) BEFORE the
  chdir: the old exec used "../tube/bbcbasic" relative to fs/, which the
  deeper chdir broke; and a fixed-size realpath buffer tripped glibc's
  __realpath_chk ("*** buffer overflow detected ***") -- realpath(x,NULL)
  mallocs and sidesteps it.
- tube/src/bbccos.c: a k4_rel() strips K4510_ROOT from the paths *DIR
  and *CD print (fs root -> "/", no doubled slash), and *CD snaps back
  if a climb would leave the root. Keyed on the env var, so the
  in-process Tube (K4510_ROOT unset, already sandboxed, getcwd already
  "/...") is unaffected. Needed rebuilding bbcbasic -- nasm built from
  source on ubuntu-s1 (like cc65 earlier) for bbdata_x86_64.nas.

Verified on the desktop Tube: cd bbcbasic; bbc; *dir -> "Directory of
/BBCBASIC/*.bbc"; LOAD "KALEID.BBC" bare; *cd .. -> "/"; a second *cd ..
stays at "/". CP/M still spawns (runcpm via the new absolute path).
basictest and tubetest green (the in-process Tube start-in-cwd was
tried and reverted -- bbccon.c's own startup chdir("/") overrides it,
and it broke tubetest; the desktop fix is what Doc needs). A note for
the harness: it still enters BBC at the root and LOADs
"BBCBASIC/TEST.BBC", which continues to work.

nasm and cc65 now both build on ubuntu-s1 from source (in the session
scratch) -- the ROM and the Tube binary can be rebuilt here, not only
on the laptop.

## 2026-08-25 (later): the black bar, the menu's grid, two more modes

Three things Doc found on the t480i5, from screenshots.

**The black bar at the top of the screen.** One character row, always
there, whatever was running. `cls()` blanked `PROWS` rows starting at
`i - OY` with `i` a `uint8_t` and `OY` the one-cell margin: the first
iteration computed 0 - 1 = 255 and wrote its blank row 226 rows off the
bottom of the screen, so physical row 0 was never touched and kept
whatever the RAM held -- fg 0 on bg 0, black. The comment above the loop
("every physical row, margins included") said what it meant to do; the
arithmetic did something else. `blank_row()` now takes a *physical* row
and `cls()` counts 0..PROWS-1; `scroll()` adds OY itself. Confirmed by
capturing a frame before and after: lines 0-15 go from (0,0,0) to the
C64 blue. Four bytes of ROM2 came back with it.

**The F7 menu now takes the machine's row grid.** It was 60 rows of 8x8
cells over a machine showing 30 rows of doubled ones, so its text was
half the size of everything under it. `ui_cell_h()` sets the overlay's
cell height (8 or 16, the glyph rows drawn twice for 16) and `UI_ROWS`
follows; the frontend reads VICKY's CTRL each frame and passes 16 for any
mode with doubled lines, marking the menu dirty when it changes. Columns
stay 8 wide -- 40 would have squeezed the two panes.

**Two more resolutions.** VICKY's CTRL grew two bits: bit3 shortens the
field to 200 lines (40 blank lines above and below, BGCOL) and bit4
quarters the columns. With the existing bit1/bit2 that gives MODE 3 =
320x200 (40x25 text -- the C64's geometry) and MODE 4 = 160x200 (20x25,
four screen pixels per pixel of the machine). The glass and the raster
are unchanged at 640x480 and 0-479, so in MODE 3 the picture's first line
is raster line 40 -- said out loud in vicky.h and in the guide, because
SHEILA lists and raster IRQs count the glass. vickytest gained checks 8
and 9. ROM2: 25 bytes free.

`tools/romfree.py` is the free-space parser, now in the tree instead of
being rewritten into /tmp every time.

## 2026-08-25 (later still): the menu previews what it changes

**Scanlines are the menu's now.** Choosing them did nothing while the menu
was up, because the scanline path writes two texture rows per line of the
machine and never composited the overlay -- so the frontend forced them
off whenever the menu was open, and you chose blind. One loop now does
both, with four palettes built per frame (the machine's colours and the
menu's, each at full and at scanline brightness) so the composite costs no
branch. Scaling already applied live; it just had nothing to show against.

**Resolution is in the Video menu**, and it is the first setting the host
cannot perform by itself: PCOLS, PROWS, the stride and the margin are the
ROM's, and writing VICKY's CTRL alone would leave the text laid out for
the old mode. So the menu *asks* -- $D521 bits 5-7 are (mode + 1), bit 1
is the margin, MODE's two parameters in one byte -- and `mode_do()` in the
ROM performs it on the next key poll. Resident on purpose: banked commands
read keys too and sw_call does not nest.

Two consequences worth writing down. The machine has to be *running* to
notice, so an outstanding request thaws it from behind the menu until
VICKY's CTRL says the mode took, or two seconds pass (a program that never
reads a key). And the request must be held for ten frames rather than
retired the moment CTRL matches -- for a margin-only change the mode
already matches, so the obvious test cleared the request before the guest
had ever run with it. The row also reads back: type `MODE 3` at the prompt
and the menu shows 320x200.

**320x200 and 160x200 are live only.** Doc's call, and a good one: what
reaches k4510.cfg is clamped to 320x240 on the way out *and* on the way
back in, so a hand-edited file cannot leave the machine somewhere it is
hard to steer out of after a power cycle. uitest check 4 covers both.

ROM1C 16 bytes free, ROM2 15. The first cut of this cost 43 bytes in
ROM1C -- the cc65 spill of one local -- and putting the margin in its own
$D521 bit instead of sharing the mode's field bought most of it back.

## 2026-08-25: a way out of a bad STARTUP.BAT

Doc asked what stops a STARTUP.BAT that crashes the machine from crashing
it at every boot. Half the answer already existed and said nothing about
itself: hold any key through the half-second grace window at the banner
and the file is skipped. Silent in both directions -- no prompt telling
you the window is there, and no confirmation when it fires, so a stray
keystroke in the queue skips your startup file and you wonder where your
aliases went.

Added the other half, which turns out to be the cheaper one: F7 -> Shell
-> Run STARTUP.BAT, $D521 bit 2, seven bytes of ROM. Cheap precisely
because all the text lives in the host -- a printed countdown would cost
~40 bytes of RODATA in ROM1C, which has 16. And it is the stronger guard:
the menu belongs to the host, so a guest that has wedged itself cannot
take it away from you, and the setting persists, so you boot clean, fix
the file and switch it back on. Off skips the grace window too, so the
machine boots quicker.

One sequencing fix went with it: io_set_opts() now runs *before* the
machine steps, not after. The ROM reads $D521 while it boots, and a byte
written at the end of the frame arrived a frame late -- for the boot read,
a whole power-on too late.

Still to do, in Doc's order: free space in ROM1C by moving a cold command
into a sideways bank, then spend it on the visible countdown, so a
keyboard-only Pi with no menu gets told the window exists.

## 2026-08-26 — the book's back matter, the licence, and one place for the PDF

Doc: *"draw up a draft Thank You chapter for the handbook. We must not
forget anyone whose work has contributed to this project. Also we need a
similar Licences chapter."* Then, after reading it: his name is
**Michael**, not Michel; add a disclaimer page; and *"I always want the
new handbook.pdf available in the repo on github, not just in the
releases .zip."*

Three chapters of end matter, unnumbered (`\backmatter`, secnumdepth 0,
a `numberless` titlesec format, explicit `\markboth` because titlesec
otherwise stamps "Chapter 10." on an unnumbered chapter):

- **Disclaimer** — the GPL's §11/§12 in full, the plain-language version
  (a toy, allowed to be wrong, keep backups), a no-endorsement line, and
  a closing page: constructive comments to the issue tracker, all
  complaints, criticisms and negativity to `/dev/null`.
- **Thank You** — everyone whose code, fonts, or heritage is in the
  machine, by section: the CPU core and reSID; the tongues; the fonts;
  Circle and circle-libsdl2; the workshop (cc65, 64tass, NASM, SDL2,
  XeLaTeX and the book's own two faces); heritage. It ends admitting the
  list is incomplete and asking to be corrected.
- **Licences** — the same ground as `LICENSES.md`, in prose, with the two
  traps a redistributor forgets: EhBASIC is non-commercial-only, and the
  "BBC BASIC" name is licensed to this project and not to forks.

`LICENSE` had been referenced by `LICENSES.md` since the repo went
public and never existed. It does now: the full GPL-2.0 text.
`CREDITS.md` and `LICENSES.md` caught up with the chapters they seeded
(build tools, the book's fonts, Wozmon and the planned MS BASIC, the
deliberately-not-shipped list: the CP/M system disk, the HVSC symlink, a
Commodore chargen, your `STARTUP.BAT`).

**The built handbook now lives in the repo.** `doc/guide/*.pdf` stays
ignored, with a `!doc/guide/k4510-guide.pdf` negation after it, so
`doc/guide/k4510-guide.pdf` is versioned and reaches GitHub with the
source. It is built on ubuntu-s1 and only there — the laptop has no TeX
at all, and its `doc/guide/shots/` is empty — so the rule is: rebuild on
ubuntu-s1, commit the PDF with the change that caused it, and every other
machine gets the book by `git pull`.

Also merged back: three "archive-side" sections of 2026-08-24 (the BASIC
research, the font research, the reset-chord and C64u-menu design) that
had been written into the archive folder's own copy of this log and never
reached this one. The archive copy is retired; this file is the diary.

## 2026-08-26: three faults Doc found in the menu build

**640x480 did not survive a reboot, and the saved margin never applied.**
One root cause, and an ugly one: VICKY's CTRL is 0 at reset, and 0 is also
exactly the bit pattern for 640x480, so the frontend read a machine that
had not booted yet as "already in 640x480".  With that mode saved it posted
no request at all -- and then, when the ROM came up in its own MODE 1, the
follow-the-guest branch fired and *overwrote the saved setting* with
640x240.  The margin rode on the same request, so it was never applied
either, which is why the left column stayed occupied in CP/M.  CP/M was
never the problem; the margin simply never reached the ROM.  Fixed by
reading CTRL bit 0 (display enable) first: before video_init there is no
mode, not mode 0.

**320x200 and 160x200 were unusable, and this one was mine.**  When
mode_do() had to fit the ROM budget I dropped the "is it already that
mode?" test and wrote that doing it twice is doing it once.  That is false.
mode_do() runs on every key poll, the host held the request up for ten
frames, and each cls() wiped whatever the machine had printed since the
last one -- exactly Doc's "first command displays part of the result, then
nothing".  The guard is back, and better than the one I removed: the ROM
now *acknowledges* the request by writing $D521, io.c drops it on the spot
and tells the host (io_mode_acked), so a request stands for one frame
instead of ten and the ten-frame heuristic is gone.  A store is cheaper
than a compare, so this cost less ROM than the version that was wrong.

**And the modes leave the menu.**  Doc's call: 40x25 and 20x25 are not a
shell, so MODE 3 and MODE 4 are for games and languages.  settings_choices()
lets an ENUM have values the menu will not steer into but will still
display, so the row reads "160x200" when a game has put the machine there.
With the save floor already at 320x240 there is now no way to end up stuck
in 20 columns.

ROM1C 9 bytes free, ROM2 8.  All 13 tests green; uitest 4 covers the
restriction.

## 2026-08-26: the alias engine moves to a bank of its own

ROM1C had 9 bytes free and ROM2 had 8, which is not a place to work from.
The only movable thing in ROM1C was the alias engine -- about 1.4 KB of
it -- and it was resident for a real reason: the alias table lived in
sideways bank 2, alias_map() wrote the same bank register sw_call uses, so
banked alias code would have unmapped itself mid-instruction.

The fix is to stop mapping anything. The engine and its table now live in
the SAME bank: a new SW2 in the ROM image (the loader needed no change --
mem_load_rom already copies every appended bank to $0FF00000 in order), the
table moved from $A000 to $B000 so the code can have the bottom of the
bank, and sw_call(2, ...) puts both in the window at once. SW2 is filled
with zeroes rather than $FF, so an untouched table reads as empty and costs
no initialising code at all.

Two details. sw_call takes void(*)(const char*), so alias_expand's answer
comes back in `alias_hit', a byte of resident BSS. And the engine calls
only resident helpers (getname, puts_, error, k_chrout) -- while a bank is
engaged, bank 0's commands are not in the window.

That freed 1486 bytes of ROM1C, and since ROM1C and ROM2 are both always
mapped, moving video_init and blank_row from CODE2 to CODE is pure link
placement and cost nothing. The result:

    ROM1C   9 -> 836 free
    ROM2    8 -> 631 free
    SW2     6762 free (new)

Verified beyond the test suite, because this is the code every unknown
command goes through: STARTUP.BAT's aliases still arrive at boot; defining,
listing, expanding and removing all work; arguments still append (D /PRG ->
DIR /PRG); chained aliases resolve; and a self-referential one still stops
at depth 4 and leaves the machine alive.

Spent some of it at once: the grace window now announces itself
("STARTUP.BAT -- hold a key to skip") and says "STARTUP.BAT skipped" when
it fires, which is what the silence cost us in the first place. It only
speaks when there is a file to skip -- fs_cmd(8) is a plain existence
check, so the test moved ahead of the message. ROM1C 782 free, ROM2 609.

## 2026-08-26 (b) — three names that should have been there from the start

Doc: *"We also have to thank Randy Rossi (Kawari and BMC64 which
inspired this project) and the new maintainer of the BMC64 repo ->
minch. Please also thanks to Kim Lemon of lemon64.com for 28+ years of
dedication to the C64 community."*

All three added to `CREDITS.md` and to the guide's Thank You chapter.
Randy Rossi goes in the bare-metal section, where he belongs twice over:
BMC64 is why this machine has a Pi port at all (the route was always
meant to be its `emux_api` seam), and the VIC-II Kawari is the precedent
for VICKY — extending an 8-bit machine's video chip is working inside
the tradition, not outside it. minch (aminch) has BMC64 now. Kim Lemon
goes in heritage: Lemon64 since 1998, twenty-eight years, and a good
half of the small facts this project needed.

Spelling checked against the local bmc64 clone before printing it: 758
commits by **Randy** Rossi, not Randi. Guide now 51 pages.

## 2026-08-26 (c) — one left edge in the thanks

Doc, from a screenshot of page 44 on hdieu: the names are not
consistently positioned, some flush left, some indented. He was right,
and the cause is plain LaTeX: the first paragraph after a heading is not
indented, every later one is. So Gábor Lénárt and Lee Davison sat at the
margin while Dag Lem, the VICE team, Richard T. Russell and Marcelo
Dantas were pushed in by a `\parindent`. In running prose nobody
notices; in a list of names the eye is looking for exactly that edge.

The chapter is now wrapped in a group with `\parindent` at 0 and
`\parskip` doing the separating, so every entry starts at the margin.
The fonts section, which had run four contributors together in one
paragraph, is split into one entry each like every other section --
same complaint, one step earlier. And `lib/fonts/font_8x8.c` was
overflowing the measure (monospace does not hyphenate); shortened to
`font_8x8.c`.

## 2026-08-26 (d) — nothing runs off the right margin any more

Five more screenshots from hdieu, and this time the fault was not in the
new chapters alone: text was running past the right margin in four
places, three of them older than anything written today. XeLaTeX had
been saying so all along in `k4510-guide.log` — nine overfull boxes,
which nobody had read. Parsed the log, mapped each to its page, fixed
all nine, and the build now reports **zero**.

What was wrong, worst first:

- **The I/O map** (ch. 10, 191pt over): a three-column `tabular` whose
  description cells for `N:` and `JIM` were sentences. The third column
  is now a wrapping `p{0.60\linewidth}`, set `\small`.
- **The VDU sprite table** (ch. 4, 142pt over): same disease, same cure.
- **`make pascal-install`** (ch. 7, 91pt over): a verbatim line carrying
  the Mad-Pascal default path in a comment. The path moved into the
  prose above, where it can break.
- **Four bullets in Licences** (73, 33, 21, 11pt over): long monospace
  paths in `\verb`, which never breaks. The style file now defines
  `\pth` (a `\DeclareUrlCommand` in tt) that breaks at `/` and `.`, and
  the licence chapter uses it for every path. This is the general fix —
  reach for `\pth` for any path in prose.
- **`ParamCount`** (ch. 7, 2pt over): `\verb|RUN HELLO one two|` cannot
  break at its spaces; `\texttt` can.

The lesson worth keeping: `grep -c "^Overfull" doc/guide/k4510-guide.log`
after every build. A5 is a narrow measure and monospace does not
hyphenate, so this will happen again.

## 2026-08-26 (e) — the build now refuses a book with text off the page

The margin bugs were in the log all along; the build printed the PDF path
regardless and nobody read the log. So `make-guide.sh` now checks, in the
same spirit as the screenshot rule: an awk pass over `k4510-guide.log`
counts every `Overfull \hbox`, works out which page it lands on (TeX
prints `[N` as it ships each page, so the last one seen before the
complaint is the page), prints amount and the offending text, and exits
non-zero. `OVERFULL_OK=1 ./make-guide.sh` builds anyway, for work in
progress.

Two things now fail this build, both for the same reason -- a book you
cannot trust is worse than no book: a screenshot that cannot be produced,
and a line that runs off the right margin. The header comment says what
to reach for: `\pth{}` for paths, a `p{}` column at `\small` for wide
tables, short `\verb`.

Verified both ways: the detector against a synthetic log with two
overfull boxes (reported both, exit 1), and a full end-to-end
`./make-guide.sh` on this host -- shots recaptured from the running
machine, XeLaTeX twice, margin check clean, 57 pages, exit 0.

## 2026-08-26: *VI and *EDIT edit the program in memory

Doc asked for a way to edit the running EhBASIC program in one of the
machine's editors and come back. `*VI` and `*EDIT`, with nothing after
them, now do it: SAVE to a temp file, run the editor, LOAD it back. With
an argument they are the ordinary `*` escape, so `*VI notes.txt` still
edits that file -- the distinction costs nothing and keeps the surprise
out.

Three things had to be true, and all three were already there. SAVE
writes plain text (so the editor needs to know nothing about BASIC). SWAP
is what makes the round trip possible at all: vi.prg lives at $5FFC-$7C64
and EhBASIC at $7000, so the editor lands on top of the interpreter, and
SWAP puts all 64 KB and the screen aside first. And k4510_load does not
return -- it feeds the file through the input vector and ends at Ready,
which is exactly where the user should be left.

**EhBASIC had no room.** Both interpreter slices are hard against
hardware ceilings ($D000 is the I/O page, $FF00 the ROM stub): the $C000
slice had 13 bytes free, the $E000 half 56, the page-2 loan 34. 103 bytes,
fragmented, against a ~220-byte hook. So EhBASIC gained a fourth K4SG
segment -- the header always carried a segment count -- a 512-byte tail at
$BE00 in what used to be the top of BASIC's RAM, with Ram_top lowered to
pay for it. 47103 bytes free becomes 46591.

Two traps, both about where memory is visible from:
- The shell line cannot live in the tail. $BE00 is inside the sideways
  window, and when the stub hands control to the ROM, block 5 is the
  ROM's view, not BASIC's -- the ROM read garbage and answered "?". The
  line is copied into Ibuffs (page 3, plain RAM both sides agree on, the
  same reason fname lives at $03C0) before the call.
- The command pointer cannot live in ut1_pl/ut1_ph. SAVE runs k_getname
  and LIST, which both use them. It lives in zero page $07-$08 instead.

Verified end to end: `10 PRINT "ZAPZAP"`, `*VI`, `j x :wq`, and EhBASIC
lists `0 PRINT "ZAPZAP"`. `*EDIT` brings up the nano-like one. `*ECHO ...`
and `*SWAP VI NOTES.TXT` still pass through untouched. basictest green
(34 EhBASIC checks + 28 BBC) -- after rebuilding test/tubetest, which was
a day-old binary and failed on nothing at all.

## 2026-08-26 (f) — the video chip is VICKY

Doc: *"I think we have to change VICKY to VICKY (everywhere) because the
Kawari 'e'xtension is moot now."* The lower-case `e` was a nod to the
VIC-II Kawari's extended modes, from back when this machine was going to
be a VICE fork with Kawari support. That route was dropped on 2026-08-21
and the chip has been entirely its own thing since, so the `e` was
carrying a dead reference.

583 occurrences in 70 tracked files, done in one sweep — `VICKY`,
`VICKY`, `vicky` and `Vicky` all folded to the one spelling — plus four
file renames: `core/vicky.c/.h` to `core/vicky.c/.h`,
`test/vickytest.c` to `test/vickytest.c`, and `docs/VICKY-SPEC.md` to
`docs/VICKY-SPEC.md`. The Makefile target, the `.gitignore` entry, the
Pascal unit's `VICKY[]` array, the BBC BASIC demos and the ROM's own
comments all moved with it. Nothing named `vicky` remains anywhere in
the tree.

Note for anyone reading upward: entries above this one were written when
the chip was called VICKY and were renamed with everything else, so the
log now reads as if it always had this name. The rename happened today,
not then.

Verified on this host as far as it can be: `core/vicky.o`, `sdl/k4510`,
`test/vickytest` and `test/statetest` all build, and both tests print
ALL OK. The 6502 side (ACME, cc65) is not installed here, so the ROM
and the .prg builds are checked on the laptop.

## 2026-08-26 (g) — the cover says which draft, and when

Doc's copy, exactly: `Draft version: xxxx · CPU 45GS10 · VICKY · 4
reSIDs · 256 MB RAM` on one line, `Date: DD.MM.YYYY` on the next,
interpuncts as the real character (`\textperiodcentered`).

Both fields are stamped at build time by `make-guide.sh`, so neither can
go stale: the date is today in DD.MM.YYYY, and the version is the tag
plus commits since it, `alpha-0.2+88` — short enough to keep the whole
first line on one line, which the full `git describe` was not.
`GUIDE_VERSION=0.3 ./make-guide.sh` overrides it. The values land in
`doc/guide/version.tex` (generated, ignored); `k4510-guide.tex` defines
fallbacks so a bare `xelatex` still builds.

## 2026-08-26 (h) — a pass over the book itself: what it got wrong, and what it never said

Doc settled the division of labour: the coding session writes code, this
session owns the handbook. So: an audit, not a patch. Checked the book
against the machine rather than against itself.

**The machine's own BASIC has hardware sprites and the book never said
so.** `SPRITE n,x,y`, `SPRDEF n,page,w,h,bpp` and `SPROFF n` have been in
`basic/k4510gfx.asm` for days, `fs/EHBASIC/INVADER2.BAS` uses
thirty-one of them, and chapter 3 listed only GRAPHICS/PLOT/LINE/TRI/
PALETTE/GCLS. New section 3.3 documents all three statements --- what
*page* means (page*256, so a 16-bit BASIC number reaches 256 MB), the
{8,16,32,64} sizes, 4 or 8 bpp, colour 0 transparent, no per-line limit,
and why a `GRAPHICS 0` in between does not lose your sprites. The claim
was verified the way this book is supposed to verify things: ran
INVADER2 under `test/capture`, and the shot is now in the chapter and in
`make-shots.sh`.

**Six commands were undocumented**, found by diffing `is_cmd()` in
`rom/kernal.c` against the guide: `RESET`, and the synonyms `CHDIR`,
`DEL`, `ERASE`, `MV`, `CAPS`. Chapter 2 now lists the synonym set --- and
says plainly that `CP` and `COPY` are *not* synonyms (one copies a file,
the other copies memory), which is the kind of thing a reader discovers
the hard way.

**`*SWAP EDIT DEMO.BAS`** appeared twice as an example. There is no
DEMO.BAS; there is a DEMOS.BAS, which is the demo menu and not something
you want to open in an editor by following the book. Now `MYPROG.BAS`.

Also checked and found correct: the I/O map against `core/io.h` (every
base address, including the bank mask at $D620); every file the book
names by name exists (only the two illustrative ones did not); the MODE
table; the F7 menu's items against `core/ui/menu.c`; save states.

**Typesetting.** The book was set `\flushbottom` --- LaTeX stretching
short pages to the bottom margin --- and `\screen` was a fixed block, so
a screenshot that did not fit left half a page of white and the text
around it was pulled apart to hide it. Now `\raggedbottom`, `\screen` is
a float, and the float fractions are loosened (topfraction 0.92,
textfraction 0.07) so a picture sits at the top of a text page instead of
claiming a page of its own. The book lost five pages without losing a
word: 59 to 54.

Still missing, and the honest gap: Part II is five pages. There is no
register-level documentation of VICKY, the SIDs, the DMA engine, the
MATH unit, the storage device or the N: device --- chapter 10 says
outright that it "will be generated from `core/io.h`", the way the
Neo6502 handbook generates its keyword reference from firmware source.
That generator is the next real piece of work on the book.

## 2026-08-26 (i) — the book catches up with a day of the machine's changes

`docs/AGENTS.md` now says who owns what and how the two sessions talk;
this is the handbook session working the coding session's list of
user-visible things that had shipped and were not in the book. Which is
the protocol doing exactly what it was written for.

- **VI** had grown most of an editor while chapter 8 still described the
  small one. The key block is rewritten from the header comment in
  `demo/vi.c`, which is the authoritative list: counts before almost
  anything, `w b e ^` added to the motions, `d c y` with any motion or
  doubled, `s S C`, `x X r ~ J D p P` and the unnamed register, unlimited
  undo and redo, `/pat ?pat n N`, `:s` and `:%s`, `:map` and `:imap`.
  Plus the two deliberate behaviours a reader would otherwise trip over:
  charwise operators clamp to the line, and search is a plain substring,
  not a pattern.
- **`LOGO`** in chapter 2, and the boot caption in chapter 1 rewritten:
  the banner is five stepped colour bars and four lines about the
  machine now, not the old hourglass. The figure itself needed nothing —
  screenshots are recaptured from the running machine every build, so
  the picture had already changed under the caption.
- **STARTUP.BAT** announces itself and says when it is skipped;
  chapter 2 quotes both messages.
- **Caps lock**: Shift gives the other case, and it is suspended while a
  program runs, so `:q` reaches VI.
- **Video → Scaling** in chapter 1 was describing the bug rather than
  the behaviour: `sharp-fit` is nearest *and* integer scaling — equal
  pixels, black border for the remainder — not "a whole multiple then
  smoothed".

Checked and already correct, no edit needed: MODE 0–4 and the
raster-counts-480 rule, Video → Resolution and Left/top margin,
Shell → Run STARTUP.BAT, and the `*VI`/`*EDIT` section the coding
session wrote into chapter 3, which is kept as written.

## 2026-08-26 (j) — the register appendix generates itself, and the build stops lying

Two things Doc asked for, and one bug they uncovered.

**Appendix A, generated.** Chapter 10 has promised since it was written
that the register reference "will be generated from `core/io.h`, the way
the Neo6502 handbook generates its keyword reference from the firmware
source". `doc/guide/mkregs.py` does it: every top-level block comment in
`core/io.h`, `vicky.h`, `net.h`, `term.h` and `mem.h` that documents
registers — the test is two or more lines carrying a `$XX` — comes out
verbatim, titled, into `generated/registers.tex`, which the appendix
inputs. Nine blocks, twelve pages: the page map, banks and the far gate,
MATH, SYS and the SIDs, the keyboard, storage, DMA, the Tube, VICKY with
SHEILA and the sprite table, the N: device, JIM, and the ROM window.

Verbatim on purpose. Those comments are what the machine is written
against; anything the script rewrote could drift from them, and a
register reference that drifts is worse than none. The only liberty it
takes is folding lines past 76 characters, because A5 is narrow — and it
fails the build if a fold cannot bring a line under, rather than letting
it run off the page. **A device added to a header appears in the book at
the next build with nothing to edit here.**

**Figures are now kept, not recaptured.** They were most of the build
time, and they only change when the machine's screens do. Default: keep
what is there, capture what is missing. `--shots` recaptures everything
(before a release, or after the screens change); `--no-shots` refuses to
capture and fails if a figure is missing, and it works out which figures
those are by grepping the chapters, so a new one cannot be forgotten.
When shots are kept and `rom/kernal.bin` is newer than one of them, the
build says so — a picture older than the machine it shows is the one
real risk of not recapturing. **The edit-and-look loop went from about
four minutes to 5.4 seconds.**

**The bug.** Adding the LaTeX-error check to the build turned up a
figure that had been silently missing since the float change this
morning: `\screen` inside an `aside` is illegal — a float cannot go in a
box — so LaTeX said "Not in outer par mode. You've lost some text" and
dropped the WordStar screenshot from chapter 6. `nonstopmode` wrote the
PDF anyway and returned 0, so nothing complained. There is now
`\screeninline` for use inside a box (chapter 6 uses it, the picture is
back), and `make-guide.sh` greps the log for `^! ` and fails. Three
things can now fail this build: a missing screenshot, a line off the
right margin, and any LaTeX error at all.

Also documented, from the coding session's note: `k4510 --no-startup.bat`
(and `--no-startup`, and `K4510_NO_STARTUP=1`) — skip STARTUP.BAT for one
run without touching the F7 setting or `k4510.cfg`.

## 2026-08-26 (k) — Appendix A set as a reference, and a page for filing an issue

Doc: "from page 42 to 50 the font and layout is ugly and not consistent
with the rest of the handbook." Right, and the cause was entry (j)'s
founding argument. Printing the header comments verbatim bought nine
pages of footnotesize monospace, machine-folded at 76 columns, with
fragments like `read/write` and `op)` hanging on lines of their own. A
reference that cannot be read is not a reference.

`mkregs.py` parses now. The comments already have a shape —
`$D720  FOP  description` — so the generator reads that shape and sets
it: the address in a fixed column, the name in bold monospace, the
description in the book's own face, prose between entries set as prose.
Nothing is reworded; the parser only decides which column a word belongs
in. Where a comment's own spacing was doing the work of a table (VICKY's
mode table, the MATH op list, SHEILA's instructions) the table is kept
exactly as written, in monospace, at the largest size that fits A5. The
76-column rule is gone with the folding: there is no width a header can
now exceed. Appendix A is one page longer and reads like the rest.

Also that afternoon: Paul Scott Robson credited for the Neo6502's
firmware and handbook (an omission Doc caught); the hold-a-key window
removed from the book when the machine lost it (it was in 2.7, 2.10 and
the boot figure); the one-way bank rule written into chapter 9 after the
coding session found it the hard way.

**Filing an issue.** Doc's observation: any issue filed here is going to
be handed to a coding session as written, so the template should be a
prompt, not a form. That became a chapter, then — after the coding
session built `BUG` — Appendix B: `DUMP ON` before you reproduce it,
`BUG` interviews you and writes the report, attach two files. The
hand-filled block lives in `issue-form.txt` for the GitHub template
alone; `mkissue.py` generates that template and **fails the guide build
if `bug_labels[]` in `demo/bug.c` and the book ask different things** —
a check rather than a generator, because the interview only exists for
someone at a running machine and the block must work for someone who
never ran it. Tested in both directions before it shipped.

One correction from the coding session mattered: the page had shipped
saying `INFO -v` identifies your build, when both version strings named
a generation. They stamped the git hash into `sys_version` on the back of
it, so it now does — and the `+` for a dirty tree is trustworthy since
they added `make check-artifacts`, which fails on a stale tracked binary
(content, not size: the offending `.prg` files were the same length).

## 2026-08-26 (l) — Doc read the whole book

Nineteen items from one reading, done the same evening (0897650), then
four machine changes they triggered, then the book following those.

**Wrong.** The book said, twice, that "BBC BASIC" was used *by
permission*. Doc: "I did not contact the BBC for this. I just asked
Claude to check out BBCBasic for SDL." Thanks, Licences and the coding
session's `tube/ALTERED.md`, `LICENSES.md` and `THIRD_PARTY_SOURCES.md`
all said it; all now say the name is Richard Russell's interpreter's,
published under his own arrangement with the BBC, that this project has
not sought and does not hold any licence to it, and that it appears only
to identify what is vendored. `ALTERED.md` carries a note that the
earlier claim was untrue, rather than a silent edit. The `@` escape
(confusing) is gone from chapter 3. MODE 3 and 4 are gone from the
user's guide, and by the end of the night from the command itself: Doc
chose to restrict rather than guard — "a bad call to mode will screw up
the screen for someone coming in" — so `MODE` and `.HELP` offer 0-2 and
VICKY keeps all five for programs. The DIR figure still showed the
hold-a-key line: every figure recaptured.

**Unclear.** Chapter 7 now opens with a table separating Turbo Pascal 3
(a program of 1985 on the Z80 under CP/M, user-supplied, makes `.COM`)
from Mad Pascal (a cross-compiler on the desktop, makes `.prg`) before a
word of prose. Chapter 8's key lists are tables, one key per row. 8.3
states the rule the book had been inconsistent about: `*EDIT` alone
edits the program in memory and does the SWAP for you; any other file
wants `*SWAP EDIT name`. 2.1 rewritten from the current `fs/` and
`.HELP`; 2.5 lists exactly what the network speaks and where, and says
there is no FTP; 2.11 is three lines pointing at Appendix B.

**Missing.** An alpha notice as the first page. Meatloaf, FujiNet and
SDL thanked. Chapter 1's sections retitled as asked.

**`jk`.** Doc: "I cannot figure out how to get jk to produce ESC." Tested
on the machine with `test/capture` before answering: `:imap jk <Esc>`
then `ihello worldjk0x` leaves `ello world`, so the mapping worked; the
trouble was that it had to be typed every session. The coding session
added `/SYSTEM/VI.RC` the same night.

**The four machine changes, and the book after them.** Arrows in CP/M
(sent as the WordStar diamond; Home/End/PgUp/PgDn deliberately not, and
the book says exactly which); `TELNET` black while connected; `VI.RC`;
the ALTERED.md correction. Each turned a sentence from true to false and
each sentence was changed the same hour (50df478, 16aa386). The one I
had said needed nothing — MODE — needed one sentence: 2.9 still said
"in MODE 3 the first line is raster line 40" as if you could type it.

alpha-0.3 "Colophon" is published; the handbook asset was rebuilt with
`GUIDE_VERSION=alpha-0.3` after all of this and uploaded over the old
one. The README was rewritten to match the machine as released — it
still announced alpha-0.2, a held key at the banner, the `@` escape, and
the Tube as desktop-only.

## 2026-08-27 — how fast could it go? Three hosts, one line each

Doc: "the cpu speed has been 40.5 from the outset. my impression is that
was set because of the mega 65" — right; `sdl/main.c` says "MEGA65-class;
the ceiling is ours, per the design". He asked for a test that ramps the
clock and watches what happens to sound and video. `test/bench` already
measured the right thing (host ms per emulated frame, split cpu / VICKY /
reSID / palette, headless); it gained `K4510_CPU_HZ=` to sweep past the
menu's ceiling, and a call to `sid_set_cpu_hz()` it had been missing —
without it a sounding SID at a swept clock was rendered at the wrong
rate, by exactly the clock multiple.

What ramping the clock does: nothing to sound or video until the host
cannot finish a frame in 16.67 ms. The SID is 1 MHz and VICKY is 60 Hz
whatever the CPU clock; raising it only means more cycles per frame. So
the measurement is a straight line — a fixed cost plus a cost per
emulated MHz — and the knee is where the line crosses the budget.

A busy EhBASIC `SIN*COS` loop, 300 frames, four SIDs gated on a sawtooth
("sounding") or never written ("silent"):

    host                          ms/MHz   fixed   reSID x4   ceiling
    ubuntu-s1  i7-6700  3.4 GHz    0.124    0.9     0.56       ~125 MHz
    t480i5     i5-8350U 1.7 GHz    0.125    0.7     0.61       ~125 MHz
    p15        i9-11950H 2.6 GHz   0.074    0.4     0.40       ~210 MHz
    Pi 3B+     A53 1.4 GHz         (BENCH: holds 15 MHz, not 20)

reSID does not move with the clock — by construction, the archive session
confirmed in `core/sid.cc`: `sid_acc += cycles * SID_HZ / cpu_hz`. It is
a fixed cost, and the largest of them. But it is paid from the first
write to a chip until reset, sounding or not (`active[c]` is latched by
`sid_write`): 0.02 ms with no SID touched, 0.56 with all four written
once. Nothing on a desktop; half a millisecond of a tight frame on the
Pi, for a program that set up a chip and went quiet. Left for the coding
session in the handbook note.

So the desktop runs 40.5 at under 40% of a frame, and the i9 could hold
five times the MEGA65's clock. Whether the menu should offer more than
40.5 is policy; the design record's line is that the timings are
suggestions. p15's numbers were taken in a scratch clone in /tmp: its
working tree holds the aarch64 objects of the Pi kernel build and was
not touched.

## 2026-08-29 — the resident ROM gets its air back

`romfree.py` had ROM1C at 35 bytes free and ROM2 at 30. That is not a
budget, it is a tripwire: the next resident line of C fails the link.
Meanwhile ROM1A sat on 1650 free and SW2 on 6762. The problem was never
total space — it was that everything hot had accumulated in the two
halves that are always mapped.

Doc asked the prior question first: how much work would it be to rewrite
the ROM in assembler? The measurement said ~20.3 KB of cc65-generated
code across five segments, which hand-written 6502 would bring to 8-10 KB
— eight to twelve sessions of work, and no Pi gain at all, because the
emulator burns a fixed cycle budget per frame whatever the guest is
doing (~0.124 ms of host time per emulated MHz, from the 2026-08-27
sweep). Denser guest code does not make the frame cheaper. So the
rewrite is an aesthetic project, not a resource one, and the resource
problem had a cheaper answer sitting in `docs/TODO.md` already.

Five functions moved, no behaviour changed:

- `video_init` (434 B), `page_break` (130), `mode_do` (54): `CODE` to
  `CODE2`. Resident either way — the comments that put them in ROM1C
  said "where the room is", and that stopped being true.
- `do_load` (1175): `CODE2` to `SWCODE0`. Cold, and every caller is
  resident or bank 0. Its rodata stays in ROM1C, which is mapped
  whatever the sideways window holds.
- `cmd_save` (268) and `cmd_type` (241): `SWCODE0` to bank 1, through
  `sw_call`. Both are only ever reached from `shell_line`, and both call
  nothing but resident helpers. Their rodata went to `SWRODATA1` with
  them — bank 0 is not in the window while bank 1 is engaged.
- `cmd_help` is gone; `HELP` is `sw_call(1, cmd_type, "/.HELP")` in the
  dispatch, which is all the wrapper ever was.

ROM1C 35 -> 646 free, ROM2 30 -> 547, ROM1A 1650 -> 1110, SW1 1594 ->
973. Every area now has room for real work.

`dump`, `peek` and `poke` stayed put, against the TODO's suggestion:
`peek` is called by `info_mem` from bank 1, so it has to be resident.
Worth writing down as the general rule — before moving anything out of
the resident half, check whether a banked command calls it. The tool for
that is a segment-attributed grep of the call sites, not memory.

The mistake worth recording: the first attempt broke `TYPE` so
thoroughly that the machine booted the shell and then found itself
inside WordStar. The cause was not cross-bank at all — an editing script
computed its line numbers before a deletion and applied them after, so
the `sw_call` conversions landed five lines down, on the `EXEC` and
`HUSH` dispatch lines, leaving the original direct `jsr _cmd_type` in
place. A direct call to a bank-1 function without engaging the window
jumps into whatever bank 0 has at that address, and bank 0's neighbour
there was the CP/M launcher. `nettest.sh` caught it on the first run;
the generated `kernal.s` named it in one grep, by showing both a
`jsr _cmd_type` and a `sw_call` in the same procedure.

Doc closed the other half of that TODO bullet the same day: **Wozmon
stays.** The 1.1K it would free is the in-shell `MON`/`WOZ` command, and
a monitor's value is highest exactly when the machine is too broken to
load `SUPERMON.prg` off disk — retiring it trades away the case it
exists for.

The first draft of this entry offered a consolation prize: if ROM1A were
ever short, a key held at power-up could boot the standalone
`rom/wozmon.bin` instead of the kernal, so retiring `MON` would cost
nothing. **Doc took that apart, and he is right.** A boot-swapped monitor
is a different tool from a resident one, because getting to it costs a
reset. What survives a reset and what does not, measured rather than
assumed:

- **RAM survives.** The reset chord (`sdl/main.c:424`) calls
  `cpu65_reset()` and nothing else — RAM zeroing is a separate action
  (`ACT_POWER_CYCLE`, line 587). So the screen at `$030000`, the C stack
  at `$0600-$0800` and the shell log are all still readable afterwards.
- **CPU state does not.** PC, A/X/Y/SP and the flags at the moment of the
  fault are gone, and that is usually the thing worth having.
- **Zero page does not.** `$02-$21` is trampled by whatever image boots.
- **The sideways banks do not.** `mem_load_rom` writes to `$0FF00000`,
  which *is* bank RAM — loading a different image overwrites the alias
  table and any user bank.

So the hold key is not a substitute for `MON`; it is a narrower thing —
a way in when the kernal will not reach a prompt at all. Recorded that
way, and `MON` stays resident on its own merits.

### Parked: a SUPERMON kernal

Doc's second thought, and the better one: if a monitor is ever going to
be boot-selectable, build it from SUPERMON rather than Wozmon. Wozmon in
the ROM already covers *always there, no reboot*; a bootable image should
cover the other case — *the machine will not reach a prompt, and I want
the good tools*: the full 45GS02 disassembler, the assembler, hunt,
transfer, compare. Complementary, not competing, which is why both can
exist without either being redundant.

What it costs, from `mon/README.md` and `mon/supermon.asm`:

- `supermon.asm` is a `.prg` and calls the **ROM jump table** — `$FF80`
  CHROUT, `$FF86` GETIN, `$FF89` LOAD, `$FF8C` SAVE, `$FF8F` SHELL. As a
  boot ROM there is no K:OS underneath it, so it needs its own console:
  VICKY text init, keyboard poll, cursor, reset vector. That shim is
  exactly what `rom/wozmon.a` already is (374 lines, self-contained), so
  the pattern is proven — but it is a session's work, not a link-order
  change.
- `LOAD`, `SAVE` and the `@` shell escape have no host to call and would
  be stubbed or dropped. `L`/`S` are the ones worth thinking about: a
  monitor that cannot save what it recovered is half a tool.
- Build lives outside the Makefile like the `.prg` does — `mon/build.sh`
  needs 64tass, not cc65.
- Host side is cheap: a menu action that is `ACT_POWER_CYCLE` minus the
  `host_zero`, so the image is reloaded and the CPU reset with RAM left
  alone, plus a setting for which image. Roughly ten lines. Prefer that
  to a held key — you want to *choose* the image, and the Pi's early-boot
  key read is the fiddly part.

Not built. The reason to write it down now is that the console shim is
the whole cost, and `wozmon.a` is the worked example of it.

## 2026-08-29 (b) — one name was doing two jobs

Doc: the emulator should be the K4510 fantasy computer, and the Pi
version the BMC-K4510. Written up as `docs/NAMING.md`.

The reason it is obviously right is sitting in `K4510-Design.md`, from
the day the name was chosen: *"BMC is for Randy Rossi's BMC64 and its
family on the Pi 3B and earlier boards — the platform this is built on."*
`BMC-K4510` has always parsed as "the K4510 on the bare-metal Pi". The
project used it for everything only because, for a while, there was one
way to run the thing. So this is not a rename; it is a name that grew
over its own meaning being pushed back inside it.

The rule that decides the cases: **would the sentence still be true if
you unplugged the Pi and opened the desktop build?** Then it is the
K4510. Only what needs a board with a card in it is the BMC-K4510 — boot
time, the four Circle cores, HDMI, `make-sd.sh`. Performance belongs to
the host, not the machine, which is why `core/calib.c` measures it: "the
BMC-K4510 holds 15 MHz", never "the K4510 runs at 15 MHz".

The count came out cleaner than expected. `BMC-K4510` appears about 160
times across the tree; **seven of them are in `pi/`**, and those are the
seven that were right all along. The rest are the machine.

The sharpest consequence is in the guest. The ROM banner, the status bar
and `INFO` all say `BMC-K4510` — and **the same ROM bytes boot on both
hosts**, so the machine has been claiming to be a Raspberry Pi appliance
while running on a laptop. Those three become `K4510`, which also gives
four bytes of rodata back. Whether the guest should instead *learn* which
host it is on — `$D521` has spare bits, and the host already publishes
video state through it — is recorded in `NAMING.md` and not decided; it
is a branch and a second string in a ROM that only just clawed back to
646 and 547 bytes free.

Nothing renamed yet: the file is the decision, not the edit. `docs/` is
the handbook session's area, so `NAMING.md` was handed to them in
`docs/notes/coding.md` along with the one question it deliberately does
not answer — the handbook's own title.

## 2026-08-29 (c) — alpha-0.4 'Imprint': the split, carried out

`NAMING.md` was the decision; this is the edit. Doc asked for all of it
in one pass — code, README, handbook — and for the release to be cut on
it.

**The name.** *Colophon* was the note at the back of a book saying how it
was made. An **imprint** is the name a work is published under, and it is
also the mark a press leaves in the paper. This release settles which
name goes on which thing, so it names itself.

**What moved.** `BMC-K4510` appeared about 160 times; it now appears in
`pi/`, in `install-sd.sh` (it writes cards), and in the handful of
sentences that are genuinely about the appliance. Everything else is the
`K4510`.

The guest strings went first and matter most: the banner, the status bar
and `INFO`'s *"the K4510 operating system"*. The same ROM bytes boot on
both hosts, so the machine had been announcing itself as a Raspberry Pi
appliance while running on a laptop. Four bytes of rodata came back.

**The handbook is now *The K4510 User's and Programmer's Guide*.** The
cover reads `K4510`; §1.3 *Two names, one machine* carries the argument
for readers, with the test in it (would the sentence still be true if you
unplugged the Pi?); the Pi section is retitled for the appliance; and the
thanks page now says out loud what was only ever in the design document —
that Randy Rossi's initials are in the appliance's name. **Every figure
was recaptured**, because the banner is in a dozen of them and the
build's own guard said so before we did: *"rom/kernal.bin is newer than
bbc.png"*. 84 pages, cover stamped `alpha-0.4`.

**Two things worth remembering for next time.** The issue template is
generated from the book (`doc/guide/issue-form.txt` → `mkissue.py` →
`.github/ISSUE_TEMPLATE/report.md`) — editing the generated file
accomplishes nothing, which we proved by doing it. And `check-artifacts`
fails for as long as a rebuilt `.prg` is uncommitted; that is the design,
not a fault, but it means `make test` cannot go green in the middle of a
change like this one. The thirteen test binaries, `basictest`, `pastest`,
`nettest` and `tubetest` were all run directly instead, and all pass.

**A postscript, and the best part of the day.** `NAMING.md` had recorded
"should the guest know which host it is on?" as open, on the assumption
that it needed a new mechanism and a second string in a tight ROM. It
needed neither: **`$D522` already carries it**, and `INFO` has been
branching on it since the Pi port. So the banner branches on the same
byte — one ROM image that prints `BMC-K4510 -- A FANTASY 8/16-bit
COMPUTER` from a card and `K4510 -- ...` on a desktop. Seventeen bytes of
ROM1A, and the machine now tells the truth about which of its two selves
you are looking at. The status bar stays `K4510  K/OS` either way; it
names the machine, not the box.

The lesson is the ordinary one: the estimate that made it look expensive
was made without reading `rom/kernal.c:770`, where the answer was already
sitting.

## 2026-08-29 (d) — building alpha-0.4 on ubuntu-s1, and what the build found

Doc asked for a build of the emulator and the manual on this host, the
laptop being asleep. A clean build is worth more than an incremental one,
and this one earned its keep immediately: **`make clean` broke the tree.**

    make: *** No rule to make target 'demo/prg0.s', needed by 'demo/prg0.o'

`clean-demos` ran `rm -f demo/*.s`, and six of those `.s` files are
**hand-written sources**, not cc65 output: `prg0.s`, `romcalls.s`, the
three `*-header.s`, and `sidplay0.s`. So `make clean` on a fresh clone
left a tree that could not be built at all, and it had presumably been
that way for as long as those files have existed — nobody runs `clean` on
a checkout they have not already built. Clean now removes one `.s` per
`.c` plus the three built under another name.

Underneath it was a second one. `fs/PRG/setup.prg` is in `$(DEMOS)`, so
`clean` deleted it, but it was **missing from `all`**, so nothing rebuilt
it. It had therefore been committed stale: its banner still read
`BMC-K4510 SETUP` after this morning's rename, because the rename edited
the source and the build never touched the binary. `check-artifacts`
would have caught it at the next commit — that is exactly what that
target is for — but only because the rename happened to touch it.

Everything else rebuilt byte-identical to the commit, which is the
answer to the question a clean build is really asking.

Then: `make clean && make all` in 14 s, all thirteen test binaries,
`basictest`, `pastest`, `nettest`, `tubetest`, `check-artifacts` green,
the SDL binary up under Xvfb, and the handbook rebuilt at 84 pages with
the cover stamped `alpha-0.4`.

### SIDPLAY drew in the wrong place

Doc, in passing, mid-build: *sidplay.prg has a bug, it hardcoded the empty
left column, and when that column is turned off in the menu it leaves the
previous screen's first column on screen.* Exactly right, and the fix
took longer than the diagnosis.

`put()` had the origin baked in as `(y + 1) * 80 + x + 1`. With the margin
off the console's origin is (0,0), so the program drew one cell in and
never touched column 0 or row 0 — which kept the shell's last screen. The
ROM has published the real origin at `$DA07`/`$DA08` since `video_init`,
and KOMMANDER already reads it (`demo/kommander.c:487`); SIDPLAY did not.

The first fix read `COLS`, `ROWS`, `OX`, `OY` and `STRIDE` the way
KOMMANDER does — and **overflowed `demo/sidplay.cfg`'s PRG area by 11
bytes**, because a variable stride turns a constant multiply into a real
one in the hottest function in the program. Worth remembering: in a `.prg`
as full as this one, `y * 80` and `y * stride` are not the same price.

The version that shipped keeps the stride constant, reads only the origin,
and makes the screen clear **physical** — all 80x30 cells rather than the
program's own 79x29 window — so a leftover is impossible whatever the
origin turns out to be. Folding the two `clear_rows(0, ROWS-1)` calls into
one `clear_all()` paid for the rest.

Verified against the old binary, margin off, entering SIDPLAY from a `DIR`:
column 0 read `/dBEPS1/` — the listing showing through — and now reads the
program's own text. Margin on is unchanged.

**The lesson for the other `.prg` programs**, and it is not just this one:
anything that writes the text map directly must read the origin. The F7
menu can move it, and a hardcoded (1,1) is a bug waiting for someone to
turn the margin off.

## 2026-08-29 (e) — RANGER: the other kind of file manager

Doc: *a ranger clone with copy paste rename and delete (to trash), exit
drops you in the folder you were exploring, vi movements, enter on a text
file launches vi via the swap mechanism.*

KOMMANDER, built the day before, is this machine's Norton Commander: two
panels, function keys. RANGER is the other tradition, and the two are not
competing implementations of one idea — they are different ideas about
what a file manager is for. So RANGER is a new `.prg` alongside it rather
than a replacement, and the machine can afford both.

Three miller columns: the parent, the current directory, and a preview of
whatever the bar is on — a directory's contents if it is a directory, the
first lines of the file if it is a file. Three levels of the tree at once,
and vi's fingers on them: `hjkl`, `gg`/`G`, `yy`/`dd`/`pp`, `DD`, `r`, `m`,
Space to mark.

**Everything it needed already existed.** The `$D300` device has RENAME,
COPY, MKDIR, CHDIR and GETCWD; KOMMANDER had already worked out the
SWAP-to-VI dance and written down why the command line has to live at
`$0300` rather than in the program's own image. The geometry comes from
JIM at `$DA05-$DA0D` the way KOMMANDER reads it. Almost none of this was
new mechanism; it was a new arrangement of what the machine had.

Quitting leaves the shell in the directory you ended in, which is the
point of the exercise. That took no work at all: the program CHDIRs as it
browses, so it only has to *not* put the old directory back.

### Three bugs, and two of them are the same bug

**The columns had a one-cell gap between them.** Tidier in the source; on
the screen it was a stripe of the previous program showing through, since
nothing drew the gap. This is the SIDPLAY bug from this morning wearing a
different hat, found the same way — by looking at the actual screen and
seeing characters that belonged to something else. The columns now abut,
and each column's own blank first cell is the separator.

**The trash ate a file.** `to_trash()` renamed into `/.TRASH` and treated
a failed rename as "the name is taken, try `~1`". But the device's RENAME
takes the host's semantics and **overwrites in silence**, so the failure
never came, the `~1` path was dead code, and deleting two files that
shared a name destroyed the first. The fix is to ask first — `C_STAT`
returns 0 when a path exists — and the same question was missing from
paste and rename, which would both have overwritten just as quietly.
Nothing in the program overwrites anything now; paste says *"some names
are already here"* and leaves them alone.

The general lesson, and it is not confined to this program: **on this
device, "did the call fail?" is not a collision test.** Anything that
writes a name someone else might already be using has to STAT it first.

**A test assertion that matched the wrong line.** `^ BBCBASIC` was meant
to find the listing at the left edge and also matched the status bar
naming the selected entry. Worth recording because it passed for the
wrong reason first and only failed once the layout changed.

### The column count is an option because MODE 2 exists

Doc, while it was being built: *needs an option to limit number of columns
to 3.* It already had three — but fixed at three, which is not the same as
choosing three. `c` now cycles 3 → 2 → 1 and `RANGER 2` sets it from the
shell. The option earns its place on this machine specifically: **in MODE 2
the console is forty columns**, and three miller columns would be thirteen
characters each — a listing you cannot read of a directory you cannot see.
With no argument RANGER now picks its own count from the console width.

`test/rangertest.sh` covers all of it in ten cases and is in `make test`.
Two of them exist only because of the trash bug: *paste OVERWROTE an
existing file* and *the first trashed file was destroyed by the second*.

## 2026-08-29 (f) — the machine gets a trash, and RM stops destroying

Doc: *can we implement delete.prg / rm.prg = move to .trash.*

Half of that could be a program and half could not. `DELETE` is a new
`.prg`; `RM` is a ROM built-in, and `shell_line` checks built-ins **before**
the "unknown word runs `name.prg`" rule, with aliases checked dead last and
a comment saying why — *so an alias never shadows a real command*. So no
`rm.prg` and no `ALIAS RM DELETE` could ever have fired. Making `RM` safe
meant opening the ROM.

Put to Doc as a choice, because it changes what the machine's most
destructive command means. He took **RM trashes, RM -f destroys**, which is
the right answer: a default you cannot get out of is not a default, it is a
restriction, and a script clearing its temporary files wants the real thing.

So there are now three ways into one trash — `RM`, `DELETE`, and RANGER's
`DD` — all using `/.TRASH` and the same `~1`/`~2` rule for a name already
taken. `test/deletetest.sh` asserts they agree rather than trusting that
three implementations of one idea stayed in step.

`DELETE` also makes the trash a place rather than a hole: `-l` lists it,
`-r name` restores into the current directory, `-e` empties it. Nothing
expires on its own — a trash that quietly disposes of things after a while
is one you cannot trust either.

### The ROM change cost 438 bytes, and the room was there because of the morning

ROM1A had 1097 free after the segment rebalance and has 643 now. Worth
noticing: the rebalance was done because ROM1C and ROM2 were at 35 and 30
bytes, and the argument for it was that the machine could not take another
line of resident code. The first thing it actually bought was room to make
`RM` safe eight hours later.

### Two faults, and I already had notes on both

**`fs_name("/.TRASH")` does not work.** The device reads the name from
physical memory at that address; a string literal lives in the ROM image,
which is not the RAM underneath it. Every other `fs_name()` in
`rom/kernal.c` passes a buffer, and mine was the only literal in the file —
a fact one grep would have told me before I wrote it, and which is sitting
in the session notes as *"fs names from RAM"*. The path is built in the
stack buffer now.

**`DIR1` opens a directory, it does not return the first entry.** Treating
its result as an entry made `DELETE -l` report one file more than the trash
held. `listdir()` in RANGER had it right the day before.

Both are the same shape of mistake: reaching for an interface I had already
used correctly somewhere else in the tree, and not looking at how.

### And a test that broke for the right reason

`deletetest.sh` walks to its fixture with RANGER's `G` (last entry) rather
than counting keypresses. A leftover `fs/ZRM` from hand-testing sorted after
`ZDTEST`, so `G` went there instead and the failure read *"RANGER did not
apply the same ~1 rule"* — a trash bug that was not a trash bug. The case
now asserts which directory it actually landed in before blaming anything,
which is what `rangertest.sh` already did and this one had not copied.

## 2026-08-30 (a) — the filers learn to run things, and the keyboard gets a buffer

Three from Doc in one message: SIDPLAY is broken, the CP/M network
slots want reserving, and "in ranger and kommander: enter on a .prg
starts the program and exits ranger or kommander, while enter on a
.com starts the program in cpm ... (do you think this is a good
idea)".

### SIDPLAY drew in a window it had stopped believing in

"It does not respect the top and bottom borders and they are gone
after exit" — one fault, both halves. SIDPLAY read the console's
*origin* from JIM ($DA07/$DA08), because the F7 menu can turn the
one-cell margin off underneath it, but it kept its own idea of the
*size*: `COLS 79, ROWS 29`, and it cleared by wiping all 80x30
physical cells. Turn the status bar on and the console is a 25-row
window between two static bands — so the list ran 29 rows into the
bottom band, and the clear ate both bands for good.

The geometry comes whole from JIM now ($DA05-$DA08: COLS ROWS OX OY);
rows-per-column and the footer follow ROWS, the second list column
appears only if COLS allows it, and `put` drops anything outside the
window. The clear is JIM's own ($DA04 = 2), which by construction
touches the console window and nothing else — and is *smaller* than
the loop it replaces, which mattered: the player lives under the ROM
in $E000-$FEFF and had 43 bytes spare.

`test/capture` honours `K4510_SYSOPT` now — the byte the frontend
publishes at $D521 — so a shot can be taken of a machine that booted
with the bands up. All three layouts checked that way.

### Enter runs a program, and Doc's "exits" was right

The first answer was that SWAP already does this and brings the filer
*back*: it is how Enter on a text file reaches VI. It is the wrong
answer, and the reason is worth keeping. SWAP restores the screen on
the way back, so a program that prints and exits would flash past
underneath the redraw. For the output to survive, the filer has to
get out of the way — which is what Doc proposed.

Getting out of the way and *then* running something needs a hand-off
the ROM does not have. It does not need one from the ROM: **a write
to KBD ($D100) now pushes a key into the keyboard queue**. Type-ahead
— the C64's keyboard buffer, and how a program there handed a command
back to BASIC. The filer leaves, and types the command at the prompt
it returns to; what you see afterwards is exactly what you would have
seen had you typed the name yourself. Zero ROM bytes. A guest's key
goes straight into the FIFO rather than through `kbd_push`: a program
may not open the F7 menu, and the debugger's key log is for keys a
person pressed.

Enter in RANGER and KOMMANDER: a directory descends as before, a
`.prg` leaves and runs, a `.com` leaves and runs under CP/M, anything
else edits (RANGER) or views (KOMMANDER) as before.

The `.com` launcher is `try_com`'s: a `K-RUN.SUB` on A:0 carrying an
optional drive-change line, the program, and an `EXIT` — the EXIT is
what ends the round trip at the K:OS prompt instead of at `A0>`. The
limit came out of RunCPM's own source rather than experiment:
`_CheckSUB` forces drive A (`BATCHA`) but *not* user 0 (`BATCH0` is
commented out), and `_FCBtoHostname` puts the current user in the
path, so the CCP opens `$$$.SUB` in whatever user area is current. A
submit may therefore change DRIVE but never USER. For a `.com`
outside user 0 the filer opens CP/M *at* it (`CPM E3:`) and you type
the name at the CCP. Flipping `BATCH0` would fix it, but DRI's
SUBMIT.COM writes `$$$.SUB` into the current user area, so the two
halves would then disagree; left alone.

Verified end to end in `rangertest`: `hello.prg` through RANGER, with
the assertion on the program's *own* output — the thing SWAP would
have lost — and a copy of `STAT.COM` sorted last on A:0, checking that
it ran, that it ran from a submit (`A0$`), and that the EXIT landed
back at `/CPM/A/0]`. KOMMANDER's half with `test/capture`; it has no
suite. One lesson re-learned on the way: a `$$$.SUB` left by a killed
run hijacks the next boot, exactly as the note says, and it cost two
confusing screenshots before the penny dropped. Both test legs clean
it up now.

### N: is the network drive

Doc proposed reserving two CP/M slots, "n14: and n15:", for FujiNet
and Meatloaf — drive N: user 15, as it turned out. One drive, not
two, and the whole letter rather than a user area: on this machine
the SCHEME lives in the NAME and not in the device. That is the
Meatloaf rule, and `CD tnfs://host/dir` extends it to directories.
FujiNet and Meatloaf are not two kinds of place; they are two URL
schemes over one namespace, and splitting them across two drives
would divide what the machine deliberately joined. N: is also the
letter that has meant "network" on every machine FujiNet touched.

CP/M's 8.3 names cannot hold a URL, so N: will be a *mount point*
rather than an address: `N:0` shows the machine's current remote
directory, moved from the K:OS shell before you come in. The letter
is claimed now (`fs/CPM/N/0/README.TXT`, whitelisted in
`fs/CPM/.gitignore`) so nothing takes it before the plumbing exists;
the plumbing — CP/M over the N: device — is still unbuilt.

## 2026-08-30 (b) — SID12 lagged, and it was the mix

Doc, on the laptop: "I ran sid12. There is something you are missing. On
this machine the sid12 demo should play without any drops. But not only
does it drop, it lags. So even though you are trying to reassure me,
there is something you are missing. If it's not the timing, is it the 12
to 1 combinatorial part that gets the final waveform out?"

He was right on both counts. The question that opened the thread — are
the SIDs on their own thread? — has the boring answer: no, they are
clocked inline in the scanline loop, and the only other thread is SDL's
callback, which does nothing but drain the ring. The interesting answer
was two lines further down.

**The phases do not agree.** A `sid_render` call advances every clocked
chip by the same number of SID cycles, but each reSID chip carries its
own resampling phase. A chip is clocked only once the machine has
written to it — the optimisation that bought the Pi 2.5 ms of a 16.7 ms
frame — so a chip that starts sounding later starts from a different
point inside the sample period and stays there. Ask four chips for 34
cycles and three hand back two samples while the fourth hands back one.
Instrumented on SID12: **4.8% of calls**, always by exactly one sample.

**The mix ran to the longest chip.** Two faults out of the one line.
It read the short chip's buffer past what that chip had written — a
stale sample from an earlier call, about 2,400 corrupted samples a
second at 48 kHz, in anything that sounds more than one chip. And it
emitted more samples than the chips made, so the machine manufactured
sound faster than the device consumed it. Nothing bounded the ring but
its own size, 683 ms: the lead walked up and stayed up.

Measured on ubuntu-s1, SID12, ring depth every two seconds:

    before   56 ms -> 226 ms in 38 seconds, still climbing
    after    41-55 ms, oscillating, no drift, no gaps, over a minute

That is the lag, it gets worse the longer a four-SID program plays, and
at the top of the ring the writers begin discarding — the drop.

**The fix.** The mix runs to the shortest chip now, and the surplus is
carried into that chip's next call, so no sample is invented and none is
lost; the chips share one average rate, so a carry never holds more than
two. And the ring gained the ceiling it never had: `RING_CAP` is the lead
plus a frame. The chips are clocked past it either way — pitch is theirs
and does not move — but the samples are let go, so the lead cannot drift
late however the two rates disagree. `SID render` still costs 1.22 ms of
a frame with four chips sounding: the fix is free.

`K4510_RINGLOG=1` now prints the lead, the gap count and the clock every
two seconds. A lead that climbs is sound arriving later and later behind
the picture; a lead at zero with gaps rising is sound the device asked
for and did not get. From the chair they sound alike, which is most of
why this took a measurement to find rather than a look.

**The lesson, and it is the same one as 2026-08-26.** The first two
answers in this thread were reassurance built on reading the code —
"the SIDs cost 0.04 ms", "the lead is 38 ms by design" — and both were
true of the machine at the prompt and false of the machine Doc was
listening to. The profile that said 0.041 ms was taken with no chip
sounding. Doc's "there is something you are missing" was worth more than
either reading, and the way to honour it was `fs/STARTUP.BAT` with
`SID12` in it and a counter in the mix.

## 2026-08-31 — MS BASIC arrives: the 1977 interpreter, ported

The decision of 2026-08-24 finally gets its code. Microsoft's 6502
BASIC — the real one, not an MS-alike — now runs on the machine as
`/MSBASIC/msbasic.prg`, beside EhBASIC rather than in place of it.

**What was vendored, and what was not.** `basic/msbasic/` is
`mist64/msbasic` at `2a0bc2f`, unmodified — but only the files a
*pure-Microsoft* configuration assembles. The OEM material stayed
upstream: `orig/*.bin` (the original Commodore, Apple, AIM-65 and
MicroTAN ROM images), the per-machine `defines_*.s`, and the OEM
`ISCNTC`/`LOAD`/`SAVE`/`EXTRA` reconstructions. This is exactly the
line the licence research drew: Microsoft's 2025 release is MIT and a
pure-MS build rests on it alone, while a byte-exact OEM ROM rebuild
would rest on something murkier. The licence table's "planned" row is
now a real one.

Nothing inside `basic/msbasic/` is edited, and that turned out to cost
nothing. `iscntc.s` includes `cbm_iscntc.s` — an empty file — for every
configuration that is not `CONFIG_CBM_ALL`, and `loadsave.s` and
`extra.s` emit nothing at all when no OEM flag is set. So a machine that
defines none of the nine OEM symbols simply gets holes where the OEM
code would be, and `basic/k4510msbasic.asm` fills them from outside.
That file is the whole port: the configuration block that
`defines_<machine>.s` would be, the console glue, and the `.prg` header.

**The configuration is `CONFIG_2C`** — the newest Microsoft base in the
tree, 9-digit floating point, every bugfix through 2C, and none of the
OEM additions. Three things were deliberately left off, and the reasons
are in the file: `CONFIG_ROR_WORKAROUND` (the workaround is for the
broken `ROR` of the 1975/76 6502s; the 45GS02's works),
`CONFIG_MONCOUT_DESTROYS_Y` (cheaper to preserve Y in our stub than to
make BASIC save it at every call site), and `CONFIG_PRINT_CR` — BASIC
would emit a CR on reaching the last column, but `k_chrout` already
wraps at `COLS`, so setting it would double-space every full line.

**Two things the port had to solve.**

*The newline.* `k_chrout` makes a full newline of CR **and** of LF
(`rom/kernal.c`), and BASIC ends every line with the pair. `MONCOUT`
therefore swallows LF, which is what EhBASIC's glue has always done for
the same reason.

*"MEMORY SIZE?"* A pure-MS cold start asks where memory ends, and if you
answer with an empty line it finds out by walking RAM upwards, probing.
That walk would march straight through the image at $7000. The fix is
not a fork of `init.s` but a canned answer: while a flag is set,
`MONRDKEY` returns bytes from a string instead of the keyboard —
`28672`, then `80` for the `TERMINAL WIDTH?` that `CONFIG_2C` asks next.
Six lines, no vendored file touched, and it uses BASIC's own documented
input path.

Ctrl-C is the keyboard queue's break flag at `$D103`, the same trick
EhBASIC's `k4510_cc` uses: a key typed while a program runs is not lost
the way polling the queue would lose it. `ISCNTC` cannot fall into
`STOP` from outside the vendored tree, so it sets Z and C and jumps
there instead — the flags the OEM versions fall through with.

**What it is not, yet.** There is no `LOAD` or `SAVE` — the ROM has both
at `$FF89`/`$FF8C` and wiring them is a contained job, but it is the next
one; for now the words print a line saying so rather than raising a
misleading `?SYNTAX ERROR`. There is no way out either: MS BASIC has no
`BYE`, and `COLD_START` has reset the stack pointer before BASIC is up,
so the shell's frame is gone. The reset chord, for now. The designed
exit is `USR` — 1977's own vendor hook — which needs a patch applied
after init has pointed it at `IQERR`.

Typed lower case is folded up, as in EhBASIC: the 1977 tokenizer knows
only upper-case keywords.

8,570 bytes at $7000-$9175, 26,623 bytes free for programs. The image
sits where EhBASIC's is documented to sit, which leaves $9000-$CFFF
unused — raising the load address is free program RAM whenever it is
wanted, and costs one number in `basic/msbasic.cfg` and the matching
`MEMTOP`. `test/msbasictest.sh` drives it from the shell the way a user
would and checks the cold start, `FOR`/`NEXT`, 9-digit `SQR`, strings,
case folding and the Ctrl-C break; it is in `make test`.

## 2026-08-31 (b) — MS BASIC looked dead at the keyboard, and it was the echo

Doc, within the hour: "the msbasic you built does not respond to keyboard
input on the emulator."

It responded perfectly. It just never showed anything back, which from
the chair is the same thing.

**MS BASIC does not echo.** `INLIN` reads a line through `GETLN` ->
`MONRDKEY` and prints nothing at all — the only thing it ever emits is a
BEL when the buffer overflows (`msbasic/inline.s`). On a KIM or a PET it
was the *monitor's* input routine that echoed, and the OEM ports all
inherited one that did. The K4510's does not: the ROM's echoing lives in
`readline()`, and BASIC bypasses it by calling `CHRIN` directly. So you
type a whole line into a blank screen, press Return, and only the answer
appears. `PRINT 6*7` printed ` 42` with no `PRINT 6*7` above it.

`MONRDKEY` now echoes every key it returns, the canned cold-start
answers included — so the boot reads `MEMORY SIZE? 28672` and
`TERMINAL WIDTH? 80` instead of two bare questions, which is both
prettier and the cheapest possible proof the echo is alive.

Backspace was the same fault from the other end. BASIC's delete character
is `_` ($5F) and its handler is a bare `DEX` — it erases nothing on the
glass (`inline.s`, L2420) — while the host's Backspace is $08, below $20,
which `INLIN` discards outright. So $08 is now translated to `_` and the
destructive erase (BS, space, BS) is done here. `@` still kills the whole
line, as it did in 1977.

**Why the tests did not catch it.** `test/headless` pushes keys straight
into the FIFO and then prints the text screen, so every assertion was
about *output* — `42`, `1.41421356`, `BREAK IN 10` — and all of it was
correct. Nothing asserted that the input was ever visible. The test now
greps for the typed line itself, and that check was confirmed to fail
against a build with the echo removed before being kept.

Reproducing it needed the real SDL binary under Xvfb with `xdotool`
typing actual X key events, because both the headless harness and the
emulator's own `K4510_KEYS` feed go through `kbd_push` — the same door,
and neither of them can show you a screen with nothing on it. Worth
remembering: a passing headless test says the machine computed the right
answer, not that a person could have used it.

---

## 2026-09-01 — the consolidation, first round

Doc asked for a capability inventory he could rule on before any more
building: "consolidating things and removing some other things, then
more rigorous testing". `docs/CAPABILITIES.md` is that inventory — every
capability the machine claims, one line each, with a disposition token
(KEEP / WORK / OFF / CUT / ?) he edits. It stays in the tree after the
ballot, because a list of what the machine does turns out to be a thing
the repo did not have.

The advice that went with it, and which shaped the order of work: most
of what is listed costs the ROM nothing (every `.prg` is on disk), so
CUT mostly buys attention rather than bytes; the scarce resources are
ROM1A/1C/2, zero page and BSSR, and those are consumed almost entirely
by the shell. And `OFF` is the underused answer — the Pi's SIDs are the
model, where code that still builds and still passes its tests simply
stops being offered.

**What Doc ruled, and what was built for it:**

### The SIDs are OFF, the OPL2 is the machine
Not on the Pi only — everywhere. `audio.chip` now defaults to the OPL2
on both hosts, and the "Sound chip" and "Active SIDs" rows are gone from
the F7 menu entirely (they were `#ifndef K4510_PI` before). Nothing SID
is deleted: reSID still builds, `test/sidtest` still exercises all four
chips, `SIDS`/`SID6`/`SID12`/`SIDPLAY` still drive them, and
`audio.chip = reSID` in `k4510.cfg` gives them the sound back. That
escape hatch is the whole point of OFF rather than CUT.

Consequences worth writing down: **the SID demos are silent as the
machine now boots.** They are not broken and they are not gone; they
play into a muted chip. Anyone reaching for them edits one line of
config first. And the boot banner said `CHIPS: 1-4 reSID, ...`, which
had become a lie, so it now says `CHIPS: OPL2, 4 SIDs, ...`. `INFO`'s
sound section still describes only the SIDs and does not say which chip
actually has the machine — that is still owed.

A live bug fell out of the same file: `audio_menu` was declared with a
hard-coded item count of 4 while its item array was conditionally 2
items long on the Pi, so the Pi's Audio menu had been reading two items
past the end of the array. It counts with `sizeof` now.

### FastSID is CUT
`core/fastsid/` and `core/fsid.[ch]` are gone, with the Makefile rules,
the `SID_ENGINE_*` API and the engine-switch test. It was vendored on
2026-08-30 to make four cycle-accurate SIDs affordable on a Pi; a Pi
that sounds no SIDs at all does not need it, and of the three chips it
was the one Doc liked least. `THIRD_PARTY_SOURCES.md` keeps a section
saying it was removed and why, because a vendored thing silently
vanishing is worse than a vendored thing recorded as gone.

A config file written before this says `audio.chip = FastSID`, a label
that no longer exists — and `atoi("FastSID")` is 0, which is reSID. So
the parser reads that one string as the OPL2: the person who chose the
cheapest sound does not get the most expensive one handed back.

### The boot speed test is CUT
`core/calib.c`'s two-phase probe had been `else if (0)` since the boot
was made instantaneous (2026-08-27) — compiled, documented, unreachable.
An engine that never measures reads as a capability the machine has and
does not, so it is out. What it also held, the host fingerprint, is
still needed (it is how a cached clock is trusted only on the machine it
was measured on) and moved to `core/hostid.c` under its own name.
`SETUP.prg` remains the way a machine gets measured, from inside, with
sound and video and the network really running.

### MS BASIC has a way out, and star commands
It had none: MS BASIC has no `BYE`, and `COLD_START` resets the stack
pointer before BASIC is up, so the shell's frame is gone by the time
anything of ours runs. Doc asked whether `*BYE` and other star commands
were possible. They are, and it is the better design anyway.

`k4510_start` now copies the live hardware stack out before
`COLD_START` — the stack pointer and the bytes above it, which are the
RAM trampoline's JSR, `_call_prog`'s, and the ROM C code above them.
`*BYE` puts them back and returns normally, so the ROM's trampoline
turns the banks off and `_call_prog` restores the ROM's zero page on the
way, exactly as for any program that ends. The shell comes back to its
own prompt in its own working directory. That is better than EhBASIC's
`@BYE`, which reaches the shell by cold-starting the machine.

Everything else after `*` goes to K:OS through the SHELL call at $FF8F,
so `*DIR`, `*TYPE`, `*CD` all work from inside BASIC. This is the BBC's
arrangement, borrowed deliberately: 1977 Microsoft BASIC has no vendor
words, and the alternative — adding tokens — means editing the vendored
interpreter, which the licence research was careful to avoid. The catch
is in `MONRDKEY`, one level below BASIC, so not a byte of `msbasic/`
knows it happened.

Two things had to be right and were not, first time:

- **The read loop cannot keep its index in Y.** `ROM_CHRIN` is a
  jump-table stub and the stubs do not preserve Y — this file's own
  header comment says so about `k4510_out`/`k4510_in`, and I wrote the
  new loop as if it did. The symptom was maddening: the line echoed
  perfectly, the shell was called, and nothing happened, because the
  buffer length came back as zero and K:OS was handed an empty string,
  which it correctly does nothing with, silently. The index lives in
  `k4510_lp` now.
- **A `*` typed at an INPUT prompt is data.** The guard is
  `CURLIN+1 = $FF`, MS BASIC's own direct-mode marker, so a star command
  is only recognised at the READY prompt. `test/msbasictest.sh` checks
  all three cases now, including that one.

### RENAME and CP no longer overwrite in silence
The device takes the host's semantics, which is to overwrite without a
word — the same trap that cost RANGER's first trash a file on
2026-08-29. At the shell it is worse, because this is what a person
types. `cmd_two` now STATs the destination first and refuses; `-f`
overwrites, spelled the way `RM` spells it. 94 bytes of ROM1A.

### DIR streams
Doc, on the Pi with an 826-file `/OPL`: "takes about 10 seconds before
it starts". Two causes, both in `fs_dir_first`. The sort was an
insertion sort — 340,000 `strcasecmp` calls at 826 names, against about
8,000 for `qsort`. And every entry was `stat`ed before the first name
was served, to learn a size the guest had not asked for yet: on a card,
826 round trips through FAT. The stat now happens in `FS_DIR_NEXT`, on
the one entry being served, so the listing streams and an `Esc` out of a
long `DIR` never pays for the rest. A TNFS listing is the exception —
its sizes arrive with its names — so `fs_list_size` survives as the
"already known" case.

### The settings file has a version, and it means something now
Turning the SIDs off would have changed nothing on any machine that had
ever been run. Every existing `k4510.cfg` says `audio.chip = reSID` —
that was the default when it was written — and an explicit setting
rightly beats a new default, so the machine would have gone on sounding
through the SIDs everywhere while appearing to have been changed. Worse
on the Pi, which used to force the OPL2 in code and, after this round,
does not.

So `settings_load` reads the `version` line (it was written and never
read) and treats a version-1 file's `audio.chip` as never chosen,
resetting it to the default. `settings_save` rewrites the line as
version 2 — it is the one unknown key that is *not* passed through
verbatim, because a file that stayed at version 1 would be migrated
again on every boot, undoing any choice made after the first migration.
From version 2 on, the setting is taken at its word, which is what makes
it a real escape hatch. Cost to someone who genuinely wanted the SIDs
before today: one line of editing, once.

### The testing pass, desktop half
Twenty-one automated tests green, including the new star-command checks.
Then a smoke pass over every program in `fs/PRG`, plus the shell's
commands and all four guest systems: does it start, do something, and
hand the shell back?

Everything does, with two exceptions. `KEYTEST` asks for named keys and
a scripted harness cannot answer it — not a fault. **`ROMOUT` is
genuinely broken**, and was before today: it prints three lines and
hangs, on the committed ROM as well as this one, so it is not a
regression from the consolidation. One cause is certain and satisfying
— it calls `fill_check(0xA000, 0xD000, ...)`, and `$CC00-$CFFF` is its
own C stack (`demo/prg.cfg` puts PRG at `$6000` for `$7000`, with
`__STACKSIZE__` `$0400`). The RAM under the ROM is the *same RAM*:
banking blocks 5-7 does not move the stack out of the way, it stops the
ROM covering what is already there. So the fill overwrote the return
addresses of the call doing the filling. Stopping at `$CC00` gets past
that and it then dies differently, with a blank screen, so there is a
second fault behind the first. Nothing points at the banking itself —
`BANKTEST` and `MAPTEST` pass and the machine is healthy afterwards.
The fix was attempted and **reverted**: half a repair to a demo whose
whole job is to demonstrate that banking works is worse than a known
broken demo on the list.

Also confirmed by the same pass: EhBASIC's `@BYE` really does cold-start
the machine (the banner reprints), which is the contrast `*BYE` was
built against; and BBC BASIC on the Tube already says `*QUIT`. MS BASIC
answers to `*QUIT` as well as `*BYE` now — same family, same word.

### Retiring what the change stranded
Doc, after reading the above: move the SID demos out of `fs/`, and
ROMOUT with them — "I never used it anyway". So `retired/` now holds
`sids.c`, `sid6.c`, `sid12.c`, `sidorch.h`, the four `sidplay` files and
`romout.c`, with a README saying what each was and why it left. Out of
`fs/PRG`, out of `DEMOS`, out of `all`, sources kept and history intact.
The reasoning is the one that runs through this whole round: the
machine's filesystem is what a person browses with `DIR`, and it should
hold what the machine actually does. A demo playing into a muted chip
demonstrates nothing.

`fs/SID` is left alone for now — it is a symlink to
`sidfiles/EC64SC_SID_Files`, and with SIDPLAY gone nothing reads it.
Flagged for Doc rather than moved: it is data, and that is his call.

### The languages that make sound
Doc then asked the question this round should have prompted on its own:
does any language need changing now that the OPL2 is the machine's chip?
Audited all five. **Forth, CP/M and MS BASIC have no sound words** and
are unaffected. Three are:

- **BBC BASIC's `SOUND` and `quiet`** (`tube/src/bbccon.c`) send an OSC
  escape over the Tube; `tula_snd` feeds the host-side four-channel
  sequencer in `core/io.c`, and `seq_start`/`seq_off` write SID
  registers. Silent now. This is the *cheap* one — the sequencer is host
  C and a single chokepoint, so `SOUND` could be restored for every BBC
  program without touching the Tube or any guest code. What it needs is
  a decision, not a design: what should a BBC `SOUND` note sound like on
  FM?
- **Mad Pascal's `Sound`/`NoSound`** (`pascal/mp/lib/crt_k4510.inc`)
  write SID 0 from guest assembler, and `k4510.pas` publishes
  `SID_BASE`/`SIDREG`. A real port: an FM patch and a different register
  layout, in asm.
- **EhBASIC** has no SOUND keyword at all — programs POKE `$D400`, which
  is what the handbook teaches. Nothing to modify; those examples now
  need `audio.chip = reSID`, and saying so is a handbook job.

Two things in the ROM were fixed on the spot, because they were not
choices but faults the change had created:

- **`HUSH` did not hush.** It zeroed the four SIDs and flushed the
  sequencer — which used to be the whole of the machine's sound — and
  left nine FM voices sounding. It keys off all nine now and drops their
  levels, and still does the SIDs and the sequencer, because HUSH is
  what you type when you do not want to have to know which chip is
  making the noise.
- **`INFO` said "OPL2 at $D480: not fitted yet"**, which had been untrue
  for a while and was now the opposite of the truth. Its SOUND section
  leads with the OPL2 and marks the SIDs "off unless chosen".

**Not yet heard on hardware.** The `DIR` fix, the keyboard layout and
the OPL millisecond pacing all need a card and a real Pi.

## 2026-09-05 — the SIDs are gone

Doc, in one line: "nuke anything having to do with SIDs".  They had been
muted since 2026-09-01 with `audio.chip = reSID` as the way back; today the
way back went too.  `core/resid/` (Dag Lem's reSID, 1.1 MB), `core/sid.cc`,
the four-chip mix with its phase carry, the SID clock select at `$D5F3`,
`SYS+$2C`, the `audio.chip`/`audio.sids` settings and their version-1→2
migration, `test/sidtest`, the retired SID demos and player, `fs/SID`,
`sidfiles/`, five EhBASIC SID programs, and every register write to `$D400`
from a demo, the ROM, EhBASIC's Ctrl-C hush, or Mad Pascal's `crt`.

What that forced, and what it gave:

- **The sound sequencer had to learn the OPL2**, because it was the SIDs'
  last real user: BBC BASIC's `SOUND`, Mad Pascal's `Sound` and INVADERS
  all reach it.  Four channels are OPL2 voices 0-3; channel 0 (the Beeb's
  noise) is a feedback-7 FM patch, 1-3 a plain two-operator tone; the
  patch is rewritten on every note because a program may zero the chip
  and the sequencer must still sound after it.  A new `opl2_write_reg()`
  saves and restores the address latch so a note landing between a
  program's ADDR and DATA writes does not misroute its DATA.  `SOUND` has
  worked on this machine for the first time since 2026-09-01 —
  `test/seqtest` holds, releases and queues.
- **The render seam is `core/audio.c`**, a page of C that owes nothing to
  any chip: cycles in, samples out at the device's rate, the microsecond
  clock advanced.  `sidq` became `sndq` and carries port writes only.
  The desktop binary no longer links libstdc++; the Pi build loses its
  `.cc` rule.
- **Save states are `K4510ST2`.**  Old ones refuse cleanly.
- **Mad Pascal's `Sound` changed its contract** (channel, quarter-semitone
  pitch, holds until `NoSound`); it is honest about it in its doc comment.

ROM headroom after: ROM2 185, SW1 1241, SW2 454 — INFO lost its four SID
lines and gained one.  Pi kernel: built on p15 (see the commit).  The
K4510x live image on the stick predates this and needs a rebuild.

## 2026-09-06 — `!` at the prompt

Doc asked for a way to call a Linux program from the machine's prompt on
K4510x, a bang prefix, and no colour change while it runs.  Built on the
Tube: program 4 forks the host's shell on the pty the co-processors
already use, so JIM renders it and the same ROM loop pumps the keys.  No
telnet, no login, no password; the machine's colours stay because there
is no BBS trick in this path.  Gated: the ROM asks `$D800` bit 2 and
refuses unless the frontend was started `--host-shell`, which only
K4510x's launcher does.  Two things learned: the Tube loop left as soon
as the child died and dropped what it had just printed (now it drains
first, and the device does not reap while the ring is full); and `ansi`
is the terminfo that matches JIM (vt100's has no colour).  nvim runs in
the window, `ls --color` lands in the palette.  `test/bangtest.sh`.

## 2026-09-06 — gamepads

Doc: "we need to start testing usb joysticks and gamepads".  The desktop
side is SDL's game-controller layer OR'd into `$D104`, which is why that
register was worth having yesterday: LODE and BOMBER get a pad for free.
Start = Enter, Back = Esc, so a game runs from the sofa.  PADTEST shows
the bits.  No pad on the build host, so this is built blind; hdieu or the
K4510x laptop is where the first one gets plugged in (the k4510 user is
in `input` already).  The Pi needs Circle's gamepad class, owed.

## 2026-09-06 — the mouse

Doc: "what about mouse support?"  Eight bytes at `$D108`, fed a frame at
a time like the held keys; the F7 menu clicks and wheels; MOUSETEST.
Two things settled on the way.  The register reports in the pixels of
the mode VICKY is in, not the glass: the first capture put the sprite at
Y=400 for a mouse at 200, because the 240-line mode doubles lines and
the program had no honest way to know.  And the machine draws no
pointer: the menu draws its own arrow into the overlay (so it will look
the same on the Pi), and a program uses a sprite.  Also learned that a
demo cannot `\r` to rewrite a line -- CR is newline since JIM took the
console -- so the readouts move the cursor through `$DA09/$DA0A`.

## 2026-09-06 — SKYFIRE and FLUFFY

Doc: "look around the net for a galaxian and mario type platformer, with
permissable licenses, that you could port ... surprise me!"  The net has
no permissively licensed 8-bit-shaped source for either (the clones are
GPL and want OpenGL), but it has the art: Kenney's Pixel Shmup (CC0) and
Chloe Wolfe's Game Boy platformer set (CC0).  So the port is of the
designs, with new code, in the TINY/BOMBER pipeline: a converter makes a
K4SG data segment, the game is a C program on `$D104`.  SKYFIRE has the
Galaxian rules (formation sway, divers, one shot); FLUFFY has variable
jump height, stomping, spikes, two levels.  Both took an evening because
the machine already had everything they needed: sprites with flips,
scrolling tile layers, a caption layer, the sequencer, the held keys.

## 2026-09-06 — CHESS

Doc: "build a chess game ... UI similar to KoboChess ... use the Tube to
Linux for Stockfish where possible ... KoboChess artwork but allow
multicolour pieces ... your judgement for anything else (FujiNet?)".
The board is KoboChess's, the pieces its drawn set (his own), sorted
into ink/paper/edge by the converter so a palette bank per side colours
them.  Stockfish is Tube program 5; the judgement call for the network
was a UCI server over TCP through N:, because that is what gives the Pi
Stockfish and it is one shell script on any Linux box.  The built-in
engine is the KoboChess search cut to what a 6502 in C can do: depth 4
in about five seconds at Intermediate.  Two traps: the harness binary
was stale after the device change (again), and `pkill -f` killed my own
shell (again).  A K4SG program can start at $2000, which this one does
for the room.

## 2026-09-06 — the browser

Doc: "a wasm / html version ... hosted on ubuntu-s1".  Emscripten took the
tree as it stood: the only surgery was a no-process switch for the Tube,
a network half that answers "not fitted", and one line in the pacer so a
page yields to the browser every frame under ASYNCIFY.  It booted in
headless Chrome on the first link.  Hosted like TeXbrain: an nginx
container on a loopback port and a tailscale serve in front, which needs
root, so that last step is Doc's.

## 2026-09-07 — "why does it show 4 SIDs?", and the day's list

The ROM images were never tracked, so a host that did not rebuild them
kept September's ROM while the emulator reported the newest commit; the
Pi card got it twice.  Tracked now, deterministic, guarded.  Then Doc's
list in his order: the palette snapshot around a program run and the
sixteen back at RESET; mouse capture on click, freed by the menu; three
SKYFIRE levels (the original became Hard); K4510x as a distrobox with
its own home, which is the place to add programs; sudo on the stick.

## 2026-09-07 — the container, twice

The distrobox flavour worked in an hour and was wrong in a minute: Doc
did `!sh`, `cd /`, and saw his host.  Distrobox integrates; it mounts
the host's root at /run/host and shares /tmp and the sockets, and a
separate home only moves one directory out of the way.  Replaced the
same afternoon by a plain rootless podman container from a Containerfile
that is handed exactly four things -- display, sound, /dev/dri,
/dev/input -- and one folder, which the machine sees as /SHARE.  The
lesson for the record: "container" is not "sandbox"; ask what crosses
the wall.

## 2026-09-07 — RX, a REXX for the machine

The Amiga's ARexx was two things: the REXX language, and the port system
that let one script drive Deluxe Paint and a terminal in the same ten
lines.  The language is here now as `RX` (`demo/rexx.c`, 37 KB, loaded at
$0800 because it needs nearly the whole program area).  It has strings
and whole numbers, PARSE with its templates, DO in all its forms, SELECT,
internal functions with PROCEDURE EXPOSE and recursion, compound
variables, SIGNAL, and about fifty built-in functions; an unknown clause
is a command for the current environment, which is what makes it glue.
`ADDRESS COMMAND` types at K/OS's shell and reads the result back in RC
(the shell now keeps a result byte at $03FF, set by `error()`); `ADDRESS
TUBE` runs a command on the co-processor's shell and brings its output
back in RESULT.  An unknown word at the prompt with a matching `name.RX`
runs the script, the same rule that already made every `.prg` a command.

The port half could not be copied as it was -- ports were message ports
between running tasks, and this machine runs one program at a time -- so
the file is the mailbox, which was Doc's own suggestion.  A script sends
`/RX/MAIL.CMD`, SWAPs to the program with `@/RX/MAIL.CMD` as its
argument, and reads `/RX/MAIL.RPL`: the result code on the first line,
the text after it.  A program joins by honouring one argument.  CHESS
does, as the worked example: NEW, MOVE, GO, LEVEL, BOARD, FEN, STATUS,
with the position kept in `/CHESS/PORT.GAM` because a swapped-in program
starts fresh every time -- the file is its memory.  `ADDRESS CHESS` now
plays a game from a script, engine and all.

What it cost to find out:

- **Values and buffers had to shrink twice.**  cc65 allows 255 bytes of
  locals in a function, and the C stack grows down from the top of the
  program area straight at this program's own arrays.  Deep recursion
  overran them and corrupted the variable pool, which then reported
  nonsense ("out of variable space" with 30 variables in it).  There is a
  guard now: `demo/rxasm.s` hands the interpreter cc65's stack pointer and
  a call that would go too deep is refused in words.
- **cc65 miscompiled a compact loop.**  `w[i++] = (c >= 'a' && c <= 'z')
  ? c - 32 : c;` with the character declared inside the loop body stored
  the buffer size instead of the character -- every letter came out $0C.
  Written the long way it is right.  A nested ternary picking a glyph did
  the same.  When output is uniformly wrong by a constant, suspect the
  compiler, not the logic.
- **`r = kw(r, "VALUE")` nulls the pointer when it does not match**, and
  that one line made ADDRESS silently do nothing at all.  It was in
  SIGNAL too.
- **A program run from a script is loaded at $6000, through the middle of
  the interpreter.**  So RX asks the filesystem whether the first word
  names a file: a built-in runs directly and its output stays on the
  screen; a program is swapped in.

Both of the day's open faults were closed the same evening, and the first
one was worth the hunt.

## 2026-09-07, later — what SWAP had been doing all along

The console that vanished after a script had swapped was not a console
fault at all.  SWAP keeps a copy of the whole 64 KB, and the 64 KB
includes the zero page, and the zero page holds cc65's software stack
pointer.  The save fired from *inside* `dma_copy`, so the copy recorded
that function's stack pointer -- twelve bytes below the one `cmd_swap`
itself was using.  The restore is written inline, deliberately, so it
fired with the real one.  Twelve bytes of drift, handed to every frame
above: after a swap, K/OS was reading its own locals out of place.  At the
prompt nothing showed; under a program it did, because `run_at` then
compared video registers it had never saved, decided the program had
changed the mode, and cleared the screen.  RANGER's stray cursor is very
likely the same twelve bytes.  The save fires from `cmd_swap` now.

Finding it took a probe rather than a theory: `$D5F1` appends a character
to the shell log, the log comes out in a dump, and three characters
written from the failing branch said which program was returning and what
the stack pointer was at both ends.  Snapshot at entry $87, comparison at
$7B.  Everything before that -- the ROM's stack, the banks, the loader --
had been ruled out by guessing, which is to say not ruled out at all.

The second fault was a decision, not a bug: SWAP restores the screen so a
file manager gets its display back, which also threw away anything a
program printed for a script.  `SWAP -k` keeps it, and RX uses that form.

Then the demos, because a language with nothing written in it proves
nothing.  `/RX` now holds eight scripts: the machine reading its own
registers, a word count, a column adder, the host asked about itself over
the Tube, a guessing game, Hunt the Wumpus in 150 lines, and a game of
chess played against the engine through the port -- the board drawn by the
script from what CHESS sends back.  Wumpus alone found three real
interpreter bugs: EXPOSE of a stem *assigned* the stem, which silently
dropped every member of the caller's map; LEAVE stopped at the first END
it met instead of the loop's, so leaving from inside SELECT ... WHEN ...
THEN DO went nowhere; and a plain DO group counted as a loop for LEAVE.
Games are good tests.
