# TODO — the next-week list (from the 2026-08-28 state review)

Raised in the organization review with Doc, 2026-08-28. Address, then
strike through or delete; when the file is empty, delete it.

## Operations

- [ ] **docs/HOSTS.md** — one row per machine: checkout path, toolchain
      location, the PATH line, the build command.  Ends the per-host
      archaeology (p15's checkout is `~/Projects/k4510-pi/k4510`, NOT the
      standard path; hdieu needs `~/opt/cc65/bin` on PATH; p15 must use
      the 15.2 ARM toolchain ONLY — mixing with 14.2 breaks arm_neon.h).
- [ ] **Move operational truth into the repo** — ROM budget rules
      (romfree.py first; segment map), the machine-sync recipes, the
      settings-save-on-exit behaviour, the two-agent tree conventions.
      Today much of this lives only in Claude session memory.
- [ ] **Pi verification** — write a card and run everything since
      alpha-0.3-105 on real hardware: the games, SUPERMON, KOMMANDER,
      the video-mode rebuild.  The desktop is verified; the Pi is not.

## Code debt

- [ ] **core/io.c split** into per-chip files (agreed earlier; unblocked).
- [ ] **Zero page relief (not urgent, fail-loud):** ROM ZP slice $02-$21
      is 32/32.  When convenient, widen into $22-$3F (grow crt0's zp_rom
      save buffer to match) or evict a non-hot crt0 zp var to BSS.
- [ ] **BSS relief (nearer):** BSSR $0440-$05FF is at 447/448.  Rebalance
      against the C stack above it, or audit for evictable statics.
- [x] **ROM2/ROM1C headroom plan** — ~~done 2026-08-29~~ (coding session;
      see `docs/notes/coding.md`).  `do_load` went to SWCODE0,
      `video_init`/`page_break`/`mode_do` from CODE to CODE2, and
      `cmd_save`/`cmd_type` to bank 1 through `sw_call`.  ROM1C 35 -> 646
      free, ROM2 30 -> 547, ROM1A 1110, SW1 973.  `peek` had to stay
      resident (INFO calls it from bank 1), so it and `dump`/`poke` did
      not move.  **Wozmon stays** (Doc, 2026-08-29): the 1.1K is the
      in-shell `MON`/`WOZ` command, and its whole value is being there
      when the machine is too broken to load `SUPERMON.prg` off disk.
      A boot-swapped monitor is **not** a substitute — the reset keeps
      RAM but loses the registers, zero page and the sideways banks (see
      `docs/BUILD-LOG.md`, 2026-08-29).
- [ ] **User banks** — document the convention: sideways banks 3-15 are
      user RAM banks; the ROM never claims above bank 2.  (msbasic was
      the penciled first tenant of bank 3; it landed as a plain `.prg`
      at $7000 instead — see below — so bank 3 is still unclaimed.)

## The Pi, after the first real run (2026-09-01)

- [x] ~~**OPL playback speed wanders on the Pi**~~ — addressed 2026-09-01 with
      the millisecond counter (option (a) below); **needs hearing on hardware
      to confirm.**  Original note kept because the diagnosis is the useful
      part and option (b) is still the correct end state:
- [ ] ~~superseded~~ **OPL playback speed wandered on the Pi** (Doc, 2026-09-01: "speeds are
      inconsistent, speed up and slow down for unclear reasons").  Diagnosed,
      not fixed.  OPLPLAY walks its stream one step per *frame*, and the frame
      counter it waits on (`sys_frames`, bumped by `io_frame_tick`) counts
      RENDERED frames, not real time -- so when the Pi's frame loop runs long,
      the music slows with it, and speeds back up when it catches up.  The
      OPL2 itself renders host-side at a fixed 48 kHz, so the sound is not
      resampled; only the register writes arrive late.
      Two ways out, and they are different sizes:
        (a) DONE: SYS+$36..$39 is a free-running millisecond counter read
            from the host's clock at the moment the guest asks, and OPLPLAY
            paces on it -- stepping more than once in a frame when it is
            behind, clamping a gap over 250 ms as a stall rather than making
            it up.  Tempo is right on average; it is still jittery frame to
            frame, which is the honest limit of a guest-side player.
        (b) play .OPL host-side: the frontend walks the stream against the
            audio clock and the guest only asks for a file.  Correct, and it
            would also survive the machine being paused or slow, but it moves
            the player out of the machine, which is a real change in what this
            demo is demonstrating.
- [ ] **The C64 keyboard on the GPIO connector is buggy** (Doc, on hardware).
      Not diagnosed — no symptom recorded beyond "buggy", so the first job is
      to write down what it actually does.  `pi/c64kbd.cpp`, and the USB
      keyboard is a working fallback in the meantime.
- [x] ~~**The USB keyboard came up German**~~ — done 2026-09-01.  Circle takes
      `keymap=` from `cmdline.txt` and the card had no `cmdline.txt`, so it
      fell back to `sysconfig.h`'s `DEFAULT_KEYMAP`, which in this Circle is
      `"DE"` — hence y and z swapped.  `pi/cmdline.txt` carries `keymap=us`
      and `make-sd.sh` copies it; `keymap_us.h` was already compiled in, so no
      Circle rebuild.  The other maps present are `uk`, `fr`, `de`, `es`,
      `it`, `dv` if one is ever wanted.
- [x] ~~**Core 3 raced the OPL2**~~ — fixed 2026-09-01.  The core-3 handover
      was built for the SIDs: their register writes go through a stamped
      queue so the two cores never touch chip state at once.  The OPL2 never
      had that path, but `sid_render` calls `opl2_render`, so with core 3 on,
      core 3 rendered from OPL2 state that core 0 was mutating.  Harmless
      while the Pi was a SID machine; a live race the moment it became an
      OPL2 one.  The OPL2 now rides the same queue (`chip = K4510_SIDS`).
- [x] ~~**The SIDs on the Pi**~~ — settled 2026-09-01, and further than the Pi:
      Doc ruled the SIDs OFF on **both** hosts and the OPL2 the default, with
      the Sound chip and Active SIDs rows out of the menu.  reSID still builds
      and still passes its tests; `audio.chip = reSID` in k4510.cfg is the way
      back.  FastSID was CUT outright.  See `docs/CAPABILITIES.md`.
- [ ] **The SID demos are silent as the machine boots.**  SIDS, SID6, SID12 and
      SIDPLAY write to a muted chip now.  Not broken, not gone — but a person
      running them gets nothing and no explanation.  Decide: a line of output
      from each saying how to give the SIDs the sound back, or leave it to the
      handbook.
- [ ] **INFO does not say which chip has the machine.**  Its SOUND section still
      describes only the four SIDs, which is now the arrangement that is NOT
      sounding.  It needs the OPL2, and ideally the chip actually selected —
      which means a byte the guest can read, and there is no register for it.

## JIM, the console (2026-08-31)

- [x] ~~**PETSCII's graphics half**~~ — done 2026-08-31.  The cause was that
      the machine's font is *always* ASCII/CP437-ordered: a 4096-byte chargen
      is permuted on the way in by `petscii_to_ascii()` (`sdl/main.c`), so
      there is no screen-code-ordered font to index and the textbook
      PETSCII -> screen code arithmetic could only ever land on letters.
      `pet_glyph()` maps onto CP437 instead; the line-drawing set is exact,
      because the same loader lifts those glyphs into their CP437 positions.
- [ ] **PETSCII's *full* graphics set.**  The diagonals, quarter-blocks and
      card suits have no glyph at any code in an ASCII-ordered font, so they
      render as spaces.  Giving PETSCII the real repertoire means loading a
      chargen a second time, unpermuted, and switching the font with the mode
      — which is a clean design (a PETSCII mode that uses the PETSCII font)
      but touches the Pi's font path as well as the desktop's.
- [ ] **A PET chargen as a screen font.**  VICE's PET chargen is 2048 bytes,
      two sets of 128, where the C64's is 4096, two sets of 256.  `apply_font`
      only permutes at 4096 and `memcpy`s anything else straight in as an
      ASCII-ordered font, so a PET chargen currently renders every letter
      wrong.  It needs its own permutation and its own menu entry — size
      cannot distinguish it from a plain 8x8 font.
- [ ] **The cursor.**  JIM can blink its own (`FLAGS` bit 0) but the ROM
      still draws one, so `draw_cursor` and the `k_getin` workaround for the
      two of them are both still there.  Handing it over is where that whole
      bug class finally closes.
- [ ] **BESCII as a screen font.**  It is vendored (CC0) but only as a TTF
      and glyph source, so it cannot be chosen in the menu.  If it were
      rendered to an 8x8 .bin it would be the licence-clean PETSCII chargen
      the machine currently lacks.

## MS BASIC — the stages after the port (2026-08-31)

Microsoft BASIC runs (`/MSBASIC/msbasic.prg`, `docs/BUILD-LOG.md`
2026-08-31).  What it still owes, roughly in the order it should be paid:

- [ ] **LOAD and SAVE.**  The words currently print "NOT YET ON THIS
      BASIC".  The ROM has both at $FF89/$FF8C (name pointer $F0/$F1,
      28-bit address $F2..$F5, length $F6..$F9), so this is contained;
      the BASIC side is `TXTTAB`/`VARTAB` and `FIX_LINKS`, and the OEM
      `*_loadsave.s` files upstream are worked examples.
- [ ] **A way out.**  There is none: MS BASIC has no `BYE`, and
      `COLD_START` resets the stack pointer before BASIC is up, so the
      shell's frame is gone and the reset chord is the only exit.  The
      designed hook is `USR` — 1977's own vendor escape — which needs a
      patch applied *after* init has pointed it at `IQERR`.
- [ ] **The K4510 words.**  `GRAPHICS`, `PLOT`, `LINE`, `TRI`,
      `PALETTE`, `SPRITE`, the far `PEEK`/`POKE`, the shell escape.
      ~1,850 lines of them exist for EhBASIC (`basic/k4510*.asm`) and
      none of it transfers mechanically: it is written against
      EhBASIC's expression evaluator and token table.  This is the big
      one, and it is what the 2026-08-24 decision was actually aiming at
      (BASIC65-style `BANK` / 28-bit `PEEK`-`POKE` / DMA tokens in code
      we fully own).
- [ ] **More program RAM, free.**  The image sits at $7000 because that
      is where EhBASIC's is documented to sit, leaving $9000-$CFFF
      unused.  Raising it is one number in `basic/msbasic.cfg` plus the
      matching `MEMTOP` and the canned `MEMORY SIZE?` answer in
      `basic/k4510msbasic.asm` — those three must move together.
- [ ] **Parked — a SUPERMON kernal** (Doc's idea, 2026-08-29).  A
      boot-selectable monitor image, for when the kernal will not reach a
      prompt: SUPERMON rather than Wozmon, because resident `MON` already
      covers "always there" and a boot image should bring the good tools
      (full 45GS02 disassembly, assembler, hunt, transfer, compare).
      Cost is the console shim — `mon/supermon.asm` calls the ROM jump
      table ($FF80/$FF86/$FF89/$FF8C/$FF8F) and as a boot ROM has no K:OS
      under it, so it needs its own VICKY text init, key poll and reset
      vector; `rom/wozmon.a` is the worked example.  `L`/`S` need a
      decision (a monitor that cannot save what it recovered is half a
      tool).  Host side is ~10 lines: `ACT_POWER_CYCLE` without the
      `host_zero`, plus a setting for which image — a menu action, not a
      held key.  Reasoning in `docs/BUILD-LOG.md`, 2026-08-29.

## Naming — done 2026-08-29, shipped as alpha-0.4 'Imprint'

- [x] ~~**The guest strings**~~ — the ROM banner, the status bar and `INFO`
      say `BMC-K4510` from a ROM whose same bytes boot on both hosts.
      Three strings to `K4510` (`rom/kernal.c` 123, 765, 1526).  Changes
      three handbook figures, so coordinate with the handbook session
      before the next capture.
- [x] ~~**Shared host chrome**~~ — SDL window title, F7 menu heading,
      settings-file header, `core/io.c`'s dump header: `K4510`.
- [x] ~~**`README.md`'s opening**~~ — lead with the machine; the bare-metal
      Pi is one of the two ways to run it, not the definition.
- [x] ~~**File-header comments**~~ across `demo/`, `pascal/`, `basic/`,
      `forth/`, `tube/`, `cpm/`, `mon/`, `test/`, `tools/` — the bulk of
      the ~150 occurrences, and the least urgent.  `pi/`'s seven stay.
- [x] ~~**The handbook's title**~~ — now *The K4510 User's and
      Programmer's Guide*, cover reads `K4510`, new §1.3 "Two names, one
      machine", the thanks page names Randy Rossi in the appliance's
      name, and every figure was recaptured (the banner is in a dozen).
      Done by the coding session because Doc asked for the whole job in
      one pass — handbook session, it is yours to revise.

## Raised 2026-09-01, not yet decided

- [ ] **K4510x** — a third delivery: a minimal Linux distribution that boots
      straight into the emulator, no Wayland or X, for old laptops and thin
      clients, with the Linux underneath still reachable so the cross-compilers
      work.  Named and defined in `docs/NAMING.md`; nothing built.  Note it is
      a *distribution*, not a port: same binary, same ROM as the hosted build.
      The Pi 4 is wanted for BMC-K4510 at the same time.
- [ ] **Software-definable status bands, and what goes in them** —
      `docs/notes/status-bars.md` is the write-up: the mechanism (mostly
      already there, since JIM's OX/OY/COLS/ROWS are writable and the console
      is a scroll region between the bands), what earns a place up there, and
      user-definable clock/date format including 12h/24h.  Needs Doc's answers
      to the three questions at the end of that file before any of it is code.
      One finding worth carrying: **`$D521` is full**, all eight bits, so any
      further menu setting that must reach the guest needs a second options
      byte -- `$D52D` is free.

## The consolidation (2026-09-01)

`docs/CAPABILITIES.md` is the standing inventory: every capability, one line,
with a disposition Doc edits.  Round one is built (SIDs off, FastSID and the
boot speed test cut, MS BASIC's exit, RENAME/CP guards, DIR streaming).
Round two happens after the testing pass.

- [ ] **THE LANGUAGES THAT MAKE SOUND ARE NOW SILENT.**  Doc asked, 2026-09-01,
      whether any language needed changing for the OPL2 becoming the machine's
      chip.  Three do, and each is a different size.  Audited: Forth, CP/M and
      MS BASIC have no sound words at all and are unaffected.
      1. **BBC BASIC's `SOUND` / `quiet`** (`tube/src/bbccon.c`) sends an OSC
         escape over the Tube; `tula_snd` in `core/io.c` feeds the host-side
         four-channel sound sequencer (`seq_write`/`seq_start`/`seq_off`),
         which writes SID registers.  **This is the cheap one**: the sequencer
         is host C and one chokepoint, so teaching `seq_start`/`seq_off` to
         emit OPL2 key-on/key-off when the OPL2 has the sound fixes `SOUND`
         for every BBC BASIC program without touching the Tube or the guest.
         What it needs from Doc is an instrument: what should a BBC `SOUND`
         note *sound* like on FM?
      2. **Mad Pascal's `Sound` / `NoSound`** (`pascal/mp/lib/crt_k4510.inc`)
         write SID 0's registers from guest assembler, and `k4510.pas` exposes
         `SID_BASE` / `SIDREG` for anyone who wants the chip directly.  A real
         port: an FM patch and a different register layout, in asm.
      3. **EhBASIC has no SOUND keyword** — programs POKE $D400 themselves,
         which the handbook teaches.  Nothing to modify; it is a documentation
         and design question, and the honest answer today is "those examples
         need `audio.chip = reSID`".
      Fixed already in the same sweep: the shell's `HUSH` (it zeroed the SIDs
      and the sequencer and left nine FM voices sounding) and `INFO`'s SOUND
      section (it said "OPL2 at $D480: not fitted yet", which stopped being
      true some time ago and is now the opposite of the truth).

- [ ] **ROMOUT is broken** (found by the 2026-09-01 smoke pass, and broken
      before it).  Prints three lines, then hangs.  Certain first cause: it
      fills $A000-$CFFF and its own C stack is $CC00-$CFFF -- the same RAM the
      banking exposes, so it overwrites the return addresses of the call doing
      the filling.  Stopping at $CC00 gets past that and it dies differently
      (blank screen), so there is a second fault behind it.  Nothing suggests
      the banking itself is wrong -- BANKTEST and MAPTEST pass and the machine
      is fine afterwards.  The fix was attempted and reverted rather than left
      half-landed.

- [ ] **The testing pass.**  Doc's sequence: consolidate, then test rigorously,
      then rule on the rest.  Everything still marked `?` in CAPABILITIES.md is
      waiting on it, and the Pi half of it has not been run since alpha-0.3-105.

## Strays

- [ ] `EDITTMP.BAS` in the machine filesystem root (and Doc's scratch
      files on the laptop: `fs/BBCBASIC/TEST.BBC.laptop-draft`,
      `fs/EHBASIC/EDITTMP.BAS`) — Doc to keep or delete.
- [ ] `fs/SYSTEM/PERF.TXT` — generated; add to .gitignore.
