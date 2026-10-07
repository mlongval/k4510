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

## From the 2026-09-18 marks (SHIPPING.CFG: two chapters `nope`, two demos `nuke`)

The handbook build was fixed 2026-09-19 (Doc: "Go for the handbook build") --
what is left here is the build system, not the book.

- [ ] **Deploy**: VI's wrap, WALL, the governor, the new book pages -- one
      layer, the Dell was on its Fedora side all of 2026-09-18.  Then on the
      Dell: `test/remote` smoke, `k4510-remote wall --choices 'yes|no' --wait
      120 'Can you read this?'`, and a long line in VI.
- [ ] **The terminal to Claude on tty2/tty3** -- docs/notes/host-terminal-design.md.
      Waits on Doc: a restricted key for the k4510 user, and which console.

## To discuss: XC=BASIC 3, a compiled BASIC (pinned 2026-09-19)

Doc: "what do you think about porting xcbasic to the k4510?" --
github.com/neilsf/xc-basic3 -- then "pin it as to discuss later".  Nothing
is built; this is what the reading found, so it is not read twice.

- [ ] **Decide whether to port it, and what it replaces.**  It would be the
      ninth language, proposed the day after `nuke` was invented to start
      jettisoning things, so the question is not only whether it fits.

What it is: a cross-compiler (host side, like C and Pascal -- see
docs/PROG-LANGUAGES.md), written in D, emitting DASM assembly (Debian
packages dasm; no prebuilt compiler binaries, so DMD + DUB build it once on
p15 and we ship the binary).  MIT, alive: v3.1.13, 2026-03-25.  The README
lists only Commodore 8-bits, but the source already has **mega65** and
**x16** targets -- and the MEGA65 is this CPU.

Why it fits: the one shape of BASIC the machine has not got (three
interpreted, none compiled -- and a compiled row in MARK would be worth
seeing).  The compiler names a target in ~10 places (source/app.d start and
top addresses, intermediatecode.d, charset/memset/memmove/sprite/poke
statements); lib/ is 13 600 lines of assembly with ~127 target
conditionals; it wants ~18 ROM entry points (CHROUT, CHRIN, GETIN, PLOT,
OPEN/CLOSE/LOAD/SAVE/SETNAM/SETLFS...), and K/OS has most of them.  Carry
it as a patch applied at build time, as linux/tek40xx does.

What it would cost: the Commodore-shaped half.  Its screen routines write
straight into screen RAM through KERNAL_SCREEN_ADDR ($0288) -- ours is far
memory, four bytes a cell; lib/sfx is SID, which went 2026-09-05; strings
assume PETSCII.  PRINT/INPUT/arithmetic/strings/files port; TEXTAT, SPRITE,
CHARSET, SOUND are rewrites (VICKY, OPL2) or omissions.  It has its own
software floats (lib/math/_fplib.asm) -- the MATH unit is a second project.

The order, if yes: (1) console-only target + tools/k4510-xcb + a case in
tools/k4510-errfmt, so F9 and `:make` work -- a day or two by the reading,
not by trying; compile the sieve and DROGON.BAS, put a compiled row in MARK,
and see whether Doc enjoys writing in it.  Stop there if not.  (2) floats on
the MATH unit.  (3) sprites and graphics on VICKY, only if games in it.

## From the 2026-09-05 review (`docs/notes/review-2026-09-05.md`)

All thirteen closed on 2026-09-11; how each landed is at the top of the
file.  One thing it leaves: a network fetch still makes the *machine* wait
(the window stays alive now).  A true background fetch needs a "busy" bit
every guest caller polls -- the ROM, the demos, both BASICs, Pascal, CP/M.

- [ ] **Hear it**: during a slow `LOAD http://...` the window should keep
      responding and the picture stay put; sound goes quiet while it waits.

## From the 2026-09-12 review (`docs/notes/review-2026-09-12.md`)

Fixed the same day except these:

- [x] **The stick keeps the Tailscale node key and Wi-Fi PSKs in clear** on
      its persistence partition — Doc ruled 2026-09-15: leave it; he runs
      `tailscale logout` before lending the stick.  No build change.
- [ ] **STAT/CHDIR on ftp/sftp/http fetch the whole file** — a HEAD
      request (`curl -sI`) for the size; cache one listing per CD→DIR.
- [ ] **Chess's port reply buffer is unbounded** (~40 BOARD lines in one
      .CMD); RX `RANDOM(5,4)` divides by zero; PARSE patterns copy a clause
      into 160-byte temps; `EX/SPIRAL.LGO` is empty.
- [ ] **`nav_list`'s `b[256]`** on the shell's stack — list into the
      resident `line` with CAP = its size, as GETCWD does.
- [ ] **Header dependencies**: `-MMD -MP` in CFLAGS and `-include *.d`;
      today no `.d` is ever produced and the three cleanups clean nothing.
- [ ] `tekplay`/`tekmenu` break on plot paths with spaces; `tek40xx/build.sh`
      clones an unpinned upstream; `git archive | tar` masks a git failure
      (POSIX sh, no pipefail) in build-live/podman.
- [x] `patch_cpm.py` is not re-runnable; the in-process Tube is test-only
      -- resolved 2026-09-14: the in-process Tube, `tube_cp.*`,
      `patch_cpm.py` and the K4510_TUBE code are removed.
- [ ] Tests worth adding are listed at the end of the review note.

## Battery, later (2026-10-06; 1-5 of the review are done -- docs/BUILD-LOG.md)

Measured first with k4510-power (and powertop/turbostat once a full base build
carries them: packages.list has them since 2026-10-06).
- [x] **Tailscale through a relay** -- not a fault (checked 2026-10-06).
      "relay" was the idle state: with nothing to send, the tailnet keeps a
      peer on DERP, and the first packets move it to a direct path (here
      via the home's public address, 70.49.94.64, hairpinned by the Bell
      hub; the LAN path is not taken, probably the Wi-Fi extender, but the
      detour costs 4 ms).  tailscaled's 3.4% was the minutes after a boot:
      over 92 minutes it used 2 s of CPU.  The hosts timer stays at a minute.
- [x] **Housekeeping, on battery** (2026-10-06, POWER_SAVE=aggressive in
      k4510-power-policy): Bluetooth soft-blocked, the NMI watchdog off,
      pcie_aspm powersupersave, PCI runtime PM, no turbo -- all at run time,
      put back on mains.  The camera is off for good (2026-10-06: udev
      de-authorizes any video-class USB device, uvcvideo blacklisted).  Still
      open: `workqueue.power_efficient=1` (kernel line only).
- [ ] **The frame loop at rest.** After a few still seconds, 30 or 20 frames
      a second instead of 60 (back at once on a key or a change): fewer
      wakeups for the CPU and the GPU.
- [ ] **Frame buffer compression.** i915 says "FBC disabled: pixel format not
      supported" for the plane SDL draws on; find a format it compresses
      (XRGB8888) and the panel's refresh costs less.
- [ ] **The charge limit for a trip.** BAT0 stops at 80% (kind to the
      battery); an F12 switch to 100% before a long day away, and back.
      The battery is at 66% of its design capacity: worth more than any
      setting.

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

- [x] **The ROM's C stack was nearly full** (512 bytes, $0600-$07FF;
      2026-10-05, the *PROG fix).  Done 2026-10-06 on branch
      kos-workspace: the stack fence (core/mem.h, INFO -m) measures it, and
      K/OS now runs on its own base page and 6502 stack (B register, SPH)
      with its C stack in a 1 KB workspace in the I/O page, $DB00-$DEFF:
      1024 bytes of C stack (base page at $0600 since the same day), and a
      system call costs ~1000 cycles less.
      docs/BUILD-LOG.md 2026-10-06 has the numbers.

- [ ] **core/io.c split** into per-chip files (agreed earlier; unblocked).
- [x] **Zero page relief:** the ROM's base page is its own since
      2026-10-06 ($0600): ZP is $02-$2F (46 bytes, was 32), no copying.
- [ ] **BSS relief:** BSSR $0440-$05FF, 381 of 448 bytes (the 64-byte zp
      save buffers went 2026-10-06).  $063A-$06FF, the rest of the base
      page, is free for a second BSS segment if it is needed.
- [ ] **ROM2 has 461 bytes free** (2026-10-06, with the workspace stub; 156 on 2026-10-01, after the band's MHz went;
      `rom/kernal.map`, CODE2 end to $FEFF).  New resident code goes in a bank.
- [ ] **User banks** — document the convention: sideways banks 4-15 are
      user RAM banks; the ROM never claims above bank 3 (bank 3 = the line
      editor and `DIR -l` since 2026-09-08/11).
- [ ] **Wozmon stays** (Doc, 2026-08-29) — recorded here so nobody spends
      its 1.1K: it is the in-shell `MON`/`WOZ` for when the machine is too
      broken to load `SUPERMON.prg` (`docs/BUILD-LOG.md`, 2026-08-29).

## JIM, the console

- [ ] **PETSCII's *full* graphics set.**  The diagonals, quarter-blocks and
      card suits have no glyph at any code in a CP437-ordered font, so they
      render as spaces.  The real repertoire means a second glyph page --
      from unscii, whose .hex has the Symbols for Legacy Computing block,
      since unscii is the one font (2026-09-14) -- switched with the mode.
- [ ] **The widget table** for the status bands: a table of (cell, source,
      format) the IRQ walks, so a program can put a live readout in a band
      without running to paint it.  (Since 2026-10-05 the bands are JIM's,
      drawn by the emulator: such a table would be JIM's too, not the IRQ's.)
- [ ] **libghostty-vt for the second screen -- once it is beta.**  Doc,
      2026-10-05: "Put in on a todo list for future consideration once it
      becomes beta."  Ghostty's terminal engine as a library (zero
      dependencies, C API; alpha, needs Zig 0.16 to build), behind the second
      screen's four calls in core/term.c (term2_feed / term2_key /
      term2_replies / term2_fit) -- the first screen stays JIM, which K/OS
      depends on.  It would bring Unicode widths, synchronized output, mouse
      reporting, scrollback and the Kitty keyboard to the tmux / Claude Code
      session; the glyphs and colours still end in CP437 and the palette.
      Before building: record a real session (K4510_TERMLOG), replay it into
      JIM and into libghostty-vt, and compare the screens -- if JIM agrees
      where it matters, it can wait.  Could run as a Tube-style helper process
      sharing memory, to keep Zig out of the emulator's build.
      mitchellh.com/writing/libghostty-is-coming, github.com/ghostty-org/ghostling

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

- [x] (done 2026-09-17, rehearsed four ways: test/install-rehearsal.sh) install-k4510.sh: make two partitions on a new internal install (K4510LIVE for /live, K4510 for persistence and /DISK), as k4510-split-live does for an existing one (2026-09-17)
- [ ] SHIPPING.CFG: a ram/disk column, and the layer build refusing anything marked disk

- [ ] Prune the RAM image (parked by Doc 2026-09-17, "more involved than I anticipated"): measurements and the order to do it in are in docs/PRUNING-THE-IMAGE.md
