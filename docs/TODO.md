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

- [ ] **The handbook does not build.**  `make-guide.sh` stops in mkweb on
      `\ref{cha:cpm}`: chapter:09-cpm and chapter:05-msbasic are `nope` and ten
      references in seven chapters still point into them (01-machine:241,
      02-shell:50,122,124, 04-ehbasic:11, 06-tube:9, 10-pascal:27,45,
      11-editors:336, z2-thanks:44).  Each is a sentence to rewrite or wrap in
      `\shipif` -- Doc's words, so Doc's decision.  Until then the three
      editions are at their 2026-09-17 state: VI's `:set wrap` and WALL are in
      the source (11-editors.tex, mkref.py) and in no edition.
- [ ] **mkweb empties doc/site/docs before it knows it can finish.**  A failed
      build leaves the web edition deleted (git checkout brings it back).
      Build into a temporary folder, move on success.
- [ ] **mkweb's `nope` is not mkship's**: mkship hides `nope` and `nuke`,
      mkweb.py:57 only `nope`.  A nuked chapter would stay on the web.
- [ ] **ANSIDEMO and SEGDEMO are still described**: 02-shell.tex:146-147,
      20-memory.tex:124, docs/CAPABILITIES.md; test/jimtest.sh:42-49 runs
      ANSIDEMO and the Makefile lists both (APP_C_NAMES, APP_SEG_NAMES, the
      segdemo rule).  They pass here because the untracked .prg files are
      still on this disk; a fresh clone without cc65 will not.
- [ ] **Deploy**: VI's wrap, WALL, the nvim swap setting -- one layer, the
      Dell was on its Fedora side all of 2026-09-18.  Then on the Dell:
      `test/remote` smoke, `k4510-remote wall --choices 'yes|no' --wait 120
      'Can you read this?'`, and a long line in VI.
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
      card suits have no glyph at any code in a CP437-ordered font, so they
      render as spaces.  The real repertoire means a second glyph page --
      from unscii, whose .hex has the Symbols for Legacy Computing block,
      since unscii is the one font (2026-09-14) -- switched with the mode.
- [ ] **The cursor.**  JIM can blink its own (`FLAGS` bit 0) but the ROM
      still draws one, so `draw_cursor` and the `k_getin` workaround are
      both still there.  Handing it over closes that bug class.
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

- [x] (done 2026-09-17, rehearsed four ways: test/install-rehearsal.sh) install-k4510.sh: make two partitions on a new internal install (K4510LIVE for /live, K4510 for persistence and /DISK), as k4510-split-live does for an existing one (2026-09-17)
- [ ] SHIPPING.CFG: a ram/disk column, and the layer build refusing anything marked disk

- [ ] Prune the RAM image (parked by Doc 2026-09-17, "more involved than I anticipated"): measurements and the order to do it in are in docs/PRUNING-THE-IMAGE.md
