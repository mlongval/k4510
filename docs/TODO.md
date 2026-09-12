# TODO — the standing list

Started in the 2026-08-28 organization review with Doc; pruned 2026-09-11
(everything about the bare-metal Pi, the SIDs and the naming split went: the
Pi port was removed 2026-09-07, the SIDs 2026-09-05, the names settled
2026-09-08).  Address, then delete the line; when the file is empty, delete it.

## Operations

- [ ] **docs/HOSTS.md** — one row per machine: checkout path, toolchain
      location, the PATH line, the build command.  Ends the per-host
      archaeology (hdieu needs `~/opt/cc65/bin` on PATH and its origin is
      GitHub with the mirror as `ubuntu-s1`; the laptop's only remote is
      `ubuntu-s1`; p15 builds the internal Linux payload in
      `~/Projects/k4510-pi/k4510`, NOT the standard path).
- [ ] **Move operational truth into the repo** — ROM budget rules
      (romfree.py first; segment map), the machine-sync recipes, the
      settings-save-on-exit behaviour, the two-agent tree conventions.
      Much of this still lives only in Claude session memory.
- [ ] **The T480 stick** is still on a build from before the line editor.
      A full `linux/build-live.sh` on the laptop (sudo needs Doc's password).

## From the 2026-09-05 review (`docs/notes/review-2026-09-05.md`)

All thirteen closed on 2026-09-11; how each landed is at the top of the
file.  One thing it leaves: a network fetch still makes the *machine* wait
(the window stays alive now).  A true background fetch needs a "busy" bit
every guest caller polls -- the ROM, the demos, both BASICs, Pascal, CP/M.

- [ ] **Hear it**: during a slow `LOAD http://...` the window should keep
      responding and the picture stay put; sound goes quiet while it waits.

## Small, known

- [ ] **FORTH has no break key** — poll `$D103` like RX and LOGO do.
      EhBASIC keeps its own Ctrl-C (touching it overflowed the `$C000`
      slice once).
- [ ] **LOGO lists** — phase 2 (a009fee) brought the sprite turtle, FILL
      and EDIT; lists are what is still owed.
- [ ] **KEYTEST's bugs** (deferred 2026-09-09).
- [ ] **LODE sound and music** ("maybe later").
- [ ] `fs/LANG/EHBASIC/README.BAS` ↔ `EX/DEMOS.BAS` chain by absolute path
      now (2026-09-11) — check it on a real run, the tests do not cover it.

## Code debt

- [ ] **core/io.c split** into per-chip files (agreed earlier; unblocked).
- [ ] **Zero page relief (not urgent, fail-loud):** ROM ZP slice $02-$21
      is 32/32.  When convenient, widen into $22-$3F (grow crt0's zp_rom
      save buffer to match) or evict a non-hot crt0 zp var to BSS.
- [ ] **BSS relief (nearer):** BSSR $0440-$05FF is 448/448.  Rebalance
      against the C stack above it, or audit for evictable statics.
- [ ] **ROM2 has 16 bytes free.**  New resident code goes in a bank.
- [ ] **User banks** — document the convention: sideways banks 4-15 are
      user RAM banks; the ROM never claims above bank 3 (bank 3 = the line
      editor and `DIR -l` since 2026-09-08/11).
- [ ] **Wozmon stays** (Doc, 2026-08-29) — recorded here so nobody spends
      its 1.1K: it is the in-shell `MON`/`WOZ` for when the machine is too
      broken to load `SUPERMON.prg` (`docs/BUILD-LOG.md`, 2026-08-29).

## JIM, the console

- [ ] **PETSCII's *full* graphics set.**  The diagonals, quarter-blocks and
      card suits have no glyph at any code in an ASCII-ordered font, so they
      render as spaces.  The real repertoire means loading a chargen a
      second time, unpermuted, and switching the font with the mode.
- [ ] **A PET chargen as a screen font.**  VICE's PET chargen is 2048
      bytes (2x128); `apply_font` only permutes at 4096 and copies anything
      else in as ASCII-ordered, so a PET chargen renders every letter
      wrong.  It needs its own permutation and its own menu entry.
- [ ] **The cursor.**  JIM can blink its own (`FLAGS` bit 0) but the ROM
      still draws one, so `draw_cursor` and the `k_getin` workaround are
      both still there.  Handing it over closes that bug class.
- [ ] **BESCII as a screen font.**  Vendored (CC0) only as a TTF; rendered
      to an 8x8 .bin it would be the licence-clean PETSCII chargen.
- [ ] **The widget table** for the status bands: a table of (cell, source,
      format) the IRQ walks, so a program can put a live readout in a band
      without running to paint it.

## MS BASIC — what it still owes

- [ ] **LOAD and SAVE.**  The ROM has both at $FF89/$FF8C (name pointer
      $F0/$F1, 28-bit address $F2..$F5, length $F6..$F9); the BASIC side is
      `TXTTAB`/`VARTAB` and `FIX_LINKS`, and upstream's OEM
      `*_loadsave.s` files are worked examples.
- [ ] **The K4510 words.**  `GRAPHICS`, `PLOT`, `LINE`, `TRI`, `PALETTE`,
      `SPRITE`, the far `PEEK`/`POKE`.  ~1,850 lines exist for EhBASIC
      (`basic/k4510*.asm`) and none transfers mechanically.  The big one.
- [ ] **More program RAM, free.**  The image sits at $7000, leaving
      $9000-$CFFF unused.  `basic/msbasic.cfg`, `MEMTOP` and the canned
      `MEMORY SIZE?` answer in `basic/k4510msbasic.asm` must move together.
- [ ] **Parked — a SUPERMON kernal** (Doc's idea, 2026-08-29): a
      boot-selectable monitor image for when the kernal will not reach a
      prompt.  Needs a console shim (`rom/wozmon.a` is the worked example)
      and a decision on `L`/`S`.  Reasoning in `docs/BUILD-LOG.md`,
      2026-08-29.

## The testing pass

- [ ] Doc's sequence: consolidate, then test rigorously, then rule on the
      rest.  Everything still marked `?` in `docs/CAPABILITIES.md` waits on it.

## Strays

- [ ] `EDITTMP.BAS` in the machine filesystem root, and Doc's scratch
      files on the laptop (`fs/BBCBASIC/TEST.BBC.laptop-draft`,
      `fs/EHBASIC/EDITTMP.BAS`) — Doc to keep or delete.
- [ ] hdieu's untracked `fs/SYSTEM/PERF.TXT`, `BENCH-01.TXT`, `SETUP.TXT`
      sit outside `fs/SYSTEM/LOG/` (the ignored place) — old paths from
      before the disk layout; delete there.
