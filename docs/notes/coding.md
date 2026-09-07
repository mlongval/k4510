# Coding session — status

Protocol: `docs/AGENTS.md`. I write only this file.

**Updated: 2026-09-07**

## Now

- Waiting on Doc's ruling over two reports written today:
  `docs/notes/review-2026-09-05.md` (13 findings, two reproduced: a DIR
  buffer overflow in the ROM, a text32 out-of-bounds store in VICKY) and
  `docs/notes/triage-2026-09-05.md` (every program and package smoke-tested
  after the SID cut: nothing to retire, one hint fixed in OPLPLAY).
  Nothing from the review is changed until he says which.
- The K4510x live image on the stick predates the SID cut and the review.

## Done 2026-09-07 (late) — CHESS piece sets; the container bug

**For the handbook agent:**

- **CHESS has three piece sets** under Options → Piece set: *Drawn* (the
  default), *Icons* (Vecteezy, credited in CREDITS.md as its licence
  asks) and, on Doc's own machines only, *Line icons* (unknown licence,
  never shipped: `data/chess/lines/` is gitignored and the converter
  skips it when absent).  The screen no longer says "KoboChess"; the
  panel reads "K4510 chess".  The board is drawn before the engine is
  started, with "starting the engine..." while it answers.
- **A container bug in the Tube** (`core/io.c`): the fork guard
  "getppid() == 1 means the parent died" was always true when the
  emulator IS pid 1, as it is under `podman start`.  Every `!` child and
  Stockfish exited at birth; CHESS waited 20 s for an engine and showed
  no board.  Fixed by comparing with the recorded parent pid.  Nothing
  else was wrong with the container.

## Done 2026-09-07 — the day's five: palette, mouse capture, SKYFIRE levels, the distrobox, sudo

**For the handbook agent:**

- **A program's colours end with the program.**  The ROM snapshots all 256
  palette entries before it runs a program and writes back what differs
  afterwards; a `PALETTE LOAD` made at the prompt survives a game.  And
  **RESET restores the VIC-II sixteen**, so a warm reset after a game no
  longer keeps the game's palette (Doc's report).  Games need no cleanup
  of their own any more, though it does no harm.
- **Mouse capture** (F7 → Input → Mouse capture, on by default): a click
  on the picture confines the host pointer to the window; opening the
  menu or losing focus (alt-tab) frees it, closing the menu takes it
  back.  Confine, not relative: `$D108` stays absolute.  Not in the
  browser build.
- **SKYFIRE has Easy / Normal / Hard** on the title (left/right), the HUD
  shows the letter, and `/PRG/SKYFIRE.CFG` moves the numbers behind Normal
  (DIVEGAP, DIVEMIN, DIVERS, BOMB, DIVE, SWAY).  Hard is what shipped
  first; Doc found it too hard.
- **K4510x has a third flavour: the podman container** (`k4510x/podman.sh`
  + `Containerfile`, create/run/shell/update/rm).  The same Debian as a
  rootless container on your own desktop, and a SANDBOX: it sees the
  display and sound sockets, `/dev/dri`, `/dev/input` and one host
  folder, `~/k4510x-share`, which is `/SHARE` at the machine's prompt —
  `COPY /SHARE/FOO.BAS /PRG/` brings a file in.  No home, no /tmp, no
  host root.  The container is kept, so `!sudo apt install` and SAVE
  persist; `update` pushes the checkout's HEAD in and rebuilds.  A
  launcher lands in the desktop's application menu.  (A distrobox was
  tried first and withdrawn the same day: it mounts the host's root by
  design.)
- **The stick's k4510 user has passwordless sudo** (next image).  Installs
  there last a boot: persistence keeps `/home` only.
- Also: **the ROM images are tracked now** (`rom/*.bin`), after a host
  without cc65 kept a pre-SID-cut ROM on the Pi card for a week.

## Done 2026-09-06 (late) — the K4510 in a browser

**For the handbook agent** (a short section under "hosts": the fourth host is a web page):

- `wasm/build.sh` compiles the same core and SDL2 frontend with Emscripten
  (emsdk in `~/opt/emsdk` on ubuntu-s1) into `wasm/dist/` — index.html,
  index.js, index.wasm, index.data (the ROM, fonts and `fs/` minus CP/M,
  3.4 MB).  Served on ubuntu-s1 by an nginx container
  (`~/containers/k4510web`, 127.0.0.1:18687) and, once Doc runs the
  `tailscale serve` line, at **https://ubuntu-s1.auroch-universe.ts.net:8687**.
  Click the screen to switch on (the browser wants a click before sound).
- The page has **no Tube** (no BBC BASIC, CP/M, `!`, Stockfish — no
  processes), **no N:** (no sockets), and **SAVE lasts only until the tab
  closes** (the filesystem is in memory).  Everything else runs: VICKY,
  JIM, OPL2, the sequencer, the F7 menu, save states within the session,
  all the games, CHESS with its built-in engine.
- Build switches: `-DK4510_NOPROC` takes the Tube's no-process branch
  (core/io.c), `core/net_wasm.c` is the N: platform half that says "not
  fitted", and the frame pacer hands the browser its turn every frame
  (`emscripten_sleep(0)`, ASYNCIFY).
- Verified: headless Chrome boots it to the prompt, sound device opens.
  Speed in a real browser is unmeasured; the CPU clock setting is the knob.

## Done 2026-09-06 (night, again) — CHESS

**For the handbook agent** (a chapter of its own, I think; CREDITS has KoboChess and Stockfish):

- **CHESS.PRG**: the KoboChess board on a 640x480 screen — the coordinates
  inside the squares, "White to move · Intermediate" above, the moves
  below, Game/Undo/Hint/Flip/Options as F1-F5 buttons (clickable), and
  a panel on the right with the whole move list, the engine and the
  clocks.  Pieces: KoboChess's drawn set as 64x64 4 bpp sprites with a
  palette bank per side, so **Options → Piece colours** gives Classic,
  Wood, Red and blue, Green and purple.  Input: arrows/pad to a square
  and Enter/space, or click the square with the mouse; right click or
  Esc backs out; Esc at rest leaves.
- **Three engines.**  Built-in: alpha-beta + quiescence, material + PST,
  the six KoboChess levels by name (Beginner..Maximum) with its noise so
  weak levels vary; depth 1-6, 0.5-25 s.  **Stockfish on the Tube**:
  Tube program 5 (`$D803` = 5), fitted when `$D800` bit 3 is set — the
  device finds `K4510_UCI`, else `/usr/games/stockfish` and friends,
  else `~/opt/stockfish-bin`; K4510x's package list now installs
  stockfish.  **Network engine**: `/CHESS/ENGINE.CFG` holds
  `tcp://host:port` and `tools/uci-server.sh` serves Stockfish from any
  Linux box, so **the Pi gets Stockfish too**.  UCI levels: the KoboChess
  Elo table (1320..2600, Maximum unlimited) and movetimes.  The engine is
  chosen automatically at start (Tube, else network, else built-in) and
  in Options → Engine.
- Hint (the engine's move for you, framed green), Undo (two plies against
  an engine), Flip, New game as White/Black/two players, Resign, Export
  PGN to `/CHESS/GAME-<date>-<time>.PGN` (SAN, with headers), a clock for
  two-player games (5/10/15 min, flag falls), coordinates on/off.  Mate,
  stalemate, fifty moves and insufficient material are called.
- **Captured pieces** (Doc's ask, same night): two trays under the move
  list, `W:` what White has taken and `B:` what Black has, as 16-px
  minis sorted by value, with the material difference (`+3`) beside the
  side ahead.  Options → Captured pieces hides them.
- `fs/CHESS/README.TXT` explains ENGINE.CFG.  Sound: a click per move.
- Verified on screenshots: the board, a game against the built-in engine,
  Stockfish over the Tube (1. e4 e6 — the same reply as the KoboChess
  screenshot), Stockfish over TCP, a mouse pick with its target dots,
  the Options menu, Flip, Hint, PGN export.

## Done 2026-09-06 (last) — the Pi kernel built and on the card; the stick rewritten

- **Pi build VERIFIED** on p15 after two fixes in `sdl/main.c` (the gamepad
  probe, the K4510x marker/power-off calls: all desktop-only now).  The
  SID cut, `$D104` from the C64 matrix, and the games are on the card.
  The Pi has no gamepad, mouse or `!` yet.
- **The K4510x stick is current** (built and written on p15).

## Done 2026-09-06 (night) — two games: SKYFIRE and FLUFFY

**For the handbook agent** (the games chapter, CREDITS has both):

- **SKYFIRE.PRG** — a Galaxian.  Kenney's *Pixel Shmup* planes (CC0,
  `data/pixelshmup/`) over a scrolling ground of its islands.  22 in
  formation, swaying; divers peel off, home on you and bomb; one shot on
  screen at a time; a diver pays double; waves get faster and dive more.
  Left/right or a pad, space/fire, Esc.  Title screen names the art.
  32x32 8 bpp sprites, a 16x16 8 bpp tile ground on layer 0 whose map is
  two identical halves so a 512-px scroll never shows a seam, text8
  caption on layer 1, the sequencer for shots, bursts, the death and
  the wave fanfare.  `tools/mkskyfire.py` cuts the sheets and lays the
  islands (seeded, so the ground is the same every build).
- **FLUFFY.PRG** — a platformer in Game Boy green.  Chloe Wolfe's tileset
  and Fluffy (CC0, `data/gbplatformer/`); the two levels, the slugs and
  the gems are ours (`tools/mkfluffy.py`, level 1 by hand, level 2 from a
  seed, longer).  Run, jump (hold for height), stomp slugs from above,
  spikes kill, gems score, the smiling block ends the level; every gem
  taken pays 2000 more.  `2` at the title starts at level 2.  4 bpp tiles
  and sprites — the whole game is four greens plus the sky.
- Both are K4SG programs (code at $6000, art dropped at $110000), both
  read `$D104` so a gamepad just works, both hand the screen back clean.
- Learned on the way: cc65 rounds `(40 - n) / 2` DOWN for n > 40 (so a
  41-char line "centred" at column 255); a map whose air is tile 0 needs
  tile 0 blank; a game must set `$D001` BGCOL to 0 itself, the ROM
  leaves its blue there; a demo cannot use CR to rewrite a line.
- No existing open-source Galaxian or Mario clone was portable: the ones
  found (nasos, Critical Mass, the GL clones) are GPL or unlicensed and
  desktop-shaped.  The designs are the arcade originals', the code is new.

## Done 2026-09-06 (evening) — the mouse: `$D108-$D10F`, the F7 menu, MOUSETEST

**For the handbook agent:**

- **New registers `$D108-$D10F`** (core/io.h), read-only, fed once a
  frame: X (`$D108/9`) and Y (`$D10A/B`) **in the pixels of the mode VICKY
  is in** (0-639 x 0-479 at full size, 0-319 x 0-239 in 320x240, the
  200-line field's top taken off); `$D10C` buttons (bit0 left, 1 right,
  2 middle); `$D10D` wheel, `$D10E/F` movement, all signed and "since
  last frame".  **The machine draws no pointer**: a program that wants
  one uses a sprite (MOUSETEST shows how, 16x16 4 bpp, PALOFS 1).  While
  the F7 menu is open the position still reads, buttons/wheel/deltas
  are 0.  Register map, appendix A; a paragraph in the input chapter.
- **The F7 menu takes the mouse**: hover highlights a setting, click is
  Enter (an enum opens its list, a number steps), right click steps a
  setting back or goes back a level, a right click in the open closes;
  the wheel moves the cursor.  Categories on the left are click-only, so
  passing the pointer over them does not throw a submenu away.  The menu
  draws its own arrow.  Keys work exactly as before.
- **MOUSETEST.PRG** (new): the registers live and a sprite arrow on the
  pointer.  Esc leaves.
- PADTEST and MOUSETEST rewrite their line through JIM's cursor
  registers (`$DA09/$DA0A`), not CR: CR is folded onto newline.
- Hosts: desktop and K4510x (SDL).  **The Pi has neither mouse nor
  gamepad yet**; Circle has classes for both.  Owed with p15.
- Tests: uitest leg 9 (the menu's mouse); `test/capture` takes
  `K4510_MOUSE=x,y,buttons` for screenshots.

## Done 2026-09-06 (later) — USB gamepads into `$D104` (desktop; Pi owed)

**For the handbook agent:**

- **A USB gamepad or joystick works on the desktop and on K4510x** with no
  setup: SDL's controller layer recognises Xbox, PlayStation, 8BitDo,
  Logitech and the rest, hot-plug, first pad wins.  It is OR'd into the
  `$D104` held-keys register, so every program that reads `$D104` (LODE,
  BOMBER) plays on it unchanged.  Mapping: d-pad or left stick = the four
  directions; A, Y or right trigger = FIRE (space); X or left shoulder = A
  (Z); B or right shoulder = B (X), so LODE digs on the shoulders.  Start
  types Enter and Back/Select types Esc into the key queue, so a game
  starts and leaves without touching the keyboard.
- **PADTEST.PRG** (new, `fs/PRG`) shows the seven bits as lamps; the first
  thing to run with a new pad.
- **The Pi does not have it yet**: Circle's USB gamepad class is the route
  (`pi/`), owed when p15 is back.  INVADER2 (EhBASIC) still reads keys, not
  `$D104`.
- **Untested on real hardware**: no pad is attached to the build host.
  Built and smoke-tested with the keyboard path only.  Doc is testing.

## Done 2026-09-06 — `!`: the host's shell from the prompt (K4510x only)

**For the handbook agent:**

- **`!command` and `!`** at the shell prompt.  `!ls -l` runs the command in
  the host's shell, in the machine's current directory, output through JIM
  in the machine's own colours (no black-screen trick, unlike TELNET); a
  bare `!` is an interactive shell and `exit` comes back.  Ctrl-C reaches
  the command.  nvim runs full-window.  **Only where the emulator was
  started `--host-shell`**, which K4510x's tty1 launcher does and nothing
  else does: on a plain desktop or the Pi the ROM answers `no host shell
  on this machine`.  This is the one deliberate exception to "no host
  escape anywhere": K4510x is the build whose Linux is the user's.
- **The Tube device grew** (`core/io.h`): `$D800` bit 2 = host shell
  fitted; `$D803` program 4 = the host shell; `$D804-7` the command
  string's address (0-terminated, empty = interactive); `$D808/9` the
  window rows/columns the child gets.  Register map, appendix A.
- The Tube loop now drains the last bytes before leaving (a `!pwd` used to
  lose the end of its line); `$D800` bit 7 stays honest after the child ends.
- Child environment: `TERM=ansi` (JIM has colour; vt100's terminfo does
  not), `K4510_TERM` overrides; `$SHELL`, else `/bin/sh`.
- Test: `test/bangtest.sh` (refused when not fitted; runs, output on
  screen, prompt back), in `make test`.  HELP has a line.  The K4510x
  telnet path is unchanged (still password; still loopback).
- ROM1A 528 free after (about 100 spent).

## Done 2026-09-05 (later) — a joystick register, and the two games play

**For the handbook agent:**

- **New register `$D104` KBDHELD** (core/io.h): the keys DOWN right now,
  bit0 up, 1 down, 2 left, 3 right, 4 space, 5 Z, 6 X.  Read-only, live,
  0 while the F7 menu is open.  The queue at `$D100` only ever reported
  presses, so a game could not know when to stop — LODE ran until SPACE
  and BOMBER's arrows toggled on every autorepeat; Doc: "neither is
  playable".  Both now steer on `$D104` (run while held, stop on release).
  Register map, the games' chapter, and the demo header's `keys_held()`.
- **INVADER2 walks**: the aliens alternate two arcade frames on each march
  step.  **It reads `/EHBASIC/INVADER2.CFG`** (new, shipped): `NAME VALUE`
  lines for PLAYER, BULLET, BOMB, STEP, MARCH, FASTER, FASTEST, FIRE,
  FIREWAVE; comments are any line not starting with a capital.  The game
  reads it through the `$D300` device from BASIC (lines 9500+), which is
  a worked example of a BASIC program reading a text file.  Its sprite
  window moved from bank 1 (`$2000`) to bank 4 (`$8000`): the program text
  starts at `$0800` and had outgrown `$2000`, which is why it threw random
  "Function call" errors.  Worth a sentence wherever the handbook teaches
  banking from BASIC: leave `$2000-$3FFF` alone once the program is big.
- The Pi side of `$D104` (`pi/c64kbd.cpp`, from the C64 matrix) is written
  but NOT built: p15 was unreachable all day.

## Done 2026-09-05 — THE SIDs ARE GONE

Doc: "nuke anything having to do with SIDs, tell handbook agent".  Done, in
one commit.  The machine has ONE sound chip, the OPL2 at $D480.

**For the handbook agent — every one of these is a handbook change:**

- **$D400-$D47F is empty.** Reads $FF, writes are ignored.  The register
  map's SID block, the four-chip diagram, `$D5F3` (the SID clock select)
  and `SYS+$2C` (SIDs active) are all gone.  `doc/guide/mkregs.py` still
  says "System registers, and the SIDs" for the SYS block and lists SID in
  `NOT_A_NAME`; `docs/CAPABILITIES.md` and `docs/TODO.md` (items 84-101,
  229-254) describe an arrangement that no longer exists.  Appendix C.
- **`audio.chip` and `audio.sids` are not settings any more.**  A k4510.cfg
  that still has them is fine (unknown keys are kept), but the handbook
  must not tell anyone to write `audio.chip = reSID`: it does nothing.
- **The sound sequencer ($D5E0) plays through the OPL2 now**, so BBC
  BASIC's `SOUND`, Mad Pascal's `Sound`/`NoSound` and INVADERS.BAS all
  SOUND AGAIN with no setting.  Channel 0 is a fed-back FM "noise", 1-3 a
  plain two-operator tone; amplitude 0 to -15 as before, pitch in quarter
  semitones as before.  `test/seqtest` proves it.
- **Mad Pascal's `Sound(Chan,Freq,Dist,Vol)` changed meaning**: Chan 0-2
  are the tone channels, 3 the noise channel; Freq is the sequencer's
  pitch (53 = middle C, 4 per semitone), no longer a SID frequency byte;
  the note holds until `NoSound` or the next `Sound` on that channel.
  `SID_BASE`/`SIDREG`/`k4_sid` are gone from `k4510.pas`/`k4510.hea`;
  `k4_seq` ($D5E0) is new.
- **EhBASIC examples that POKE 54272** are wrong now, all of them: SIDWAVE,
  SIDBEAT, SIDFILT, SID6, SID12 are deleted from fs/EHBASIC; INVADERS.BAS
  was rewritten to POKE the sequencer (54752-54755).  Any handbook listing
  that teaches `POKE 54272+…` teaches a dead address; teach 54752 instead.
- **INFO's SOUND section** is one OPL2 line ("voices keyed on: n of 9;
  sequencer at $D5E0") — recapture.  **The LOGO banner** says "CHIPS: OPL2,
  VICKY, SHEILA, FRED, JIM" — recapture.  **HUSH** flushes the sequencer and
  keys off nine voices, nothing else.
- **PERF.TXT** says "OPL2 render" where it said "SID render"; BENCH and
  SETUP sweep the clock with an OPL2 note, SETUP's audio page plays "four
  notes, rising" on the OPL2 instead of one per SID.
- **`fs/SID`, `sidfiles/`, `SIDPLAY`, `SIDS`, `SID6`, `SID12`** are gone
  from the tree, `retired/` included.  `fs/OPL` + `OPLPLAY` is the player
  now; the SD card README says so.
- **Save states**: magic is `K4510ST2`; a state from before today refuses
  to load (the SID chunks are gone from the format).  The OPL2's registers
  are not in a state — it loads with the chip reset.
- Credits: reSID is now "was" in CREDITS.md, LICENSES.md,
  THIRD_PARTY_SOURCES.md and README.md (I edited those four; shared area,
  saying so here).  The `alpha-0.5 'Timbre'` paragraph at the top of
  README.md still describes the release as it shipped, which is right.

What moved in the code, for the record: `core/sid.cc`, `core/sid.h`,
`core/resid/` deleted; `core/audio.[ch]` is the render seam the frontends
call (`audio_init/reset/render/drain_to/set_cpu_hz`); `core/sidq` is
`core/sndq` and carries OPL2 port writes only; `opl2_write_reg()` is how
the sequencer writes a register without disturbing a program's address
latch.  Both Makefiles lose the C++ objects; the desktop binary no longer
links libstdc++.  20 tests green plus the new `seqtest`.

## Done today (2026-08-29) — the ROM segment rebalance

`docs/TODO.md`'s "ROM2/ROM1C headroom plan", done.  No behaviour change,
no new commands: five functions moved between segments so the resident
halves are no longer one line of C from a link failure.

| area | was | now |
|---|---|---|
| ROM1C | 35 | 646 |
| ROM2  | 30 | 547 |
| ROM1A | 1650 | 1110 |
| SW1   | 1594 | 973 |
| SW2   | 6762 | 6762 |

What moved:

- `video_init`, `page_break`, `mode_do`: `CODE` -> `CODE2`.  Resident
  either way; ROM1C was full and ROM2 had the room.  `page_break` keeps
  its resident guarantee.
- `do_load` (1175 B): `CODE2` -> `SWCODE0`.  Cold, and every caller is
  resident or bank 0.  Its rodata stays in ROM1C, which is mapped
  whatever the window holds.
- `cmd_save`, `cmd_type`: `SWCODE0` -> `SWCODE1`, dispatched with
  `sw_call(1, ...)`.  Both only ever called from `shell_line`, and both
  only call resident helpers.  Their rodata moved to `SWRODATA1` with
  them.
- `cmd_help` is gone: `HELP` is now `sw_call(1, cmd_type, "/.HELP")` in
  the dispatch, which is what the wrapper did.

Green: the whole `make test` suite, plus `nettest.sh` and `tubetest.sh`.

**For the handbook session:** nothing user-visible.  Every command
behaves as before, `HELP` included.  I struck the TODO bullet in
`docs/TODO.md` (your file, one line) rather than leave it stale.

**Wozmon stays in** — Doc decided 2026-08-29, so the retirement
question is closed, and on its own merits: a boot-swapped monitor is not
a substitute for a resident one.  A reset keeps RAM (the chord is
`cpu65_reset()` alone) but loses the registers, zero page, and the
sideways banks — `mem_load_rom` writes to `$0FF00000`, which is bank RAM,
so it overwrites the alias table.

## NEW COMMAND: DELETE — a trash for the shell, not just for RANGER

`DELETE name [name...]` moves to `/.TRASH` instead of destroying — the same
trash RANGER's `DD` uses, with the same `~1`/`~2` rules, because two
trashes would be worse than none.  `DELETE -l` lists it, `-r name` restores
into the current directory, `-e` empties it (that one really deletes).

Nothing expires on its own: a trash that quietly disposes of things after a
while is one you cannot trust either.

**`RM` NOW TRASHES TOO — user-visible ROM change, Doc's call, same day.**
`RM name` moves to `/.TRASH`; **`RM -f name` is the one that really
removes it**.  `DEL` and `ERASE` follow, being the same dispatch entry.
Refusing a directory and a missing name is unchanged.  Costs 438 bytes of
ROM1A (643 free after).

This one matters for the book: `RM` is documented as destroying, and it no
longer does.  `fs/.HELP` is updated.  The three trash routes — `RM`,
`DELETE`, RANGER's `DD` — share `/.TRASH` and the `~1`/`~2` rule, and
`test/deletetest.sh` asserts they agree.

## NEW COMMAND: RANGER — for the handbook, announcing it the same day

Doc asked for a ranger clone.  `RANGER` is a new `.prg` (`demo/ranger.c`,
`fs/PRG/ranger.prg`), **alongside KOMMANDER, not replacing it** — they are
different ideas about what a file manager is, and both fit.

Three miller columns: parent, current, preview.  vi's fingers, no function
keys.  The preview shows a directory's contents or a file's first lines.

    h l j k / arrows   walk           Enter  descend, or edit a file in VI
    gg G Home End      ends           Space  mark / unmark
    yy  dd  pp         copy cut paste DD     delete to /.TRASH
    r rename   m mkdir   c columns    .      dotfiles      q Esc  leave

Three things worth a paragraph each in the book if you cover it:

- **Quitting leaves the shell in the directory you ended in.**  That is the
  reason to use it rather than typing `CD`.
- **`DD` is not a delete.**  It RENAMEs into `/.TRASH`, undone by walking in
  there and moving it back.  A name already taken gets `~1`, `~2`.
- **Nothing overwrites.**  Paste and rename both refuse an existing name
  and say so.  The device's RENAME/COPY overwrite silently, so this is the
  program's own guard, not the device's.

`c` cycles 3 -> 2 -> 1 columns, and `RANGER 2` sets it from the shell.  It
is not decoration: **in MODE 2 the console is 40 columns**, three of them
would be 13 characters each, and RANGER starts at 2 (or 1) on a narrow
screen by itself.

`test/rangertest.sh` is in `make test` (ten cases, owns and removes its own
fixture).  Not in `fs/.HELP` — that file does not list the `.prg` programs,
and KOMMANDER is not in it either.  **KOMMANDER is still undocumented in the
book**, so if you take one you may want both.

## alpha-0.4 'Imprint' — the naming split, carried out

Doc asked for the whole job in one pass and for a release on it, so this
is done, not proposed.  **K4510** = the machine (and the desktop build);
**BMC-K4510** = the bare-metal Pi appliance.  ~160 occurrences reviewed;
`BMC-K4510` now survives in `pi/`, `install-sd.sh`, and the sentences
genuinely about the appliance.

**I edited your area, and more of it than the protocol's "small and say
so" allowance covers.** Specifically: `doc/guide/k4510-guide.tex` (title
and cover), `chapters/01-machine.tex` (new §1.3 *Two names, one
machine*, and the Pi subsection retitled), `04-tube`, `89-disclaimer`,
`90-thanks` (Randy Rossi's initials named in the appliance's name),
`91-licences`, `93-issues`, `issue-form.txt`, `make-guide.sh`, and the
rebuilt PDF.  **Revise any of it freely** — I wrote it to Doc's ask, not
over your judgement.

- The book is now **The K4510 User's and Programmer's Guide**, 84 pages,
  cover stamped `alpha-0.4` (`GUIDE_VERSION=alpha-0.4`).
- **Every figure was recaptured** (`make-guide.sh --shots`) — the banner
  changed, and it is in a dozen of them.  Takes ~4 minutes.
- `.github/ISSUE_TEMPLATE/report.md` is generated from
  `doc/guide/issue-form.txt`; I edited the generated file first and it
  was overwritten, as designed.  Noted so you do not repeat it.

**For you:** the alpha notice and the release titles are still yours, and
the book says `alpha-0.4` only on the cover.

## docs/NAMING.md — the decision behind it, and it lands in your area

Doc decided 2026-08-29 that **K4510** is the machine (architecture, K/OS,
ROM, handbook, every `.prg` — and the desktop build, which simply *is*
the K4510) and **BMC-K4510** is the bare-metal Pi appliance (the card,
Circle, the cores, no OS underneath).  Not a rename: `K4510-Design.md`
already said *BMC* names Randy Rossi's BMC64 platform, not the machine.

I wrote `docs/NAMING.md` — your area, and bigger than the "small and say
so" allowance, so: **it is yours now, edit or overrule it freely.**  I
wrote it because Doc asked for the write-up in this session and the rule
needs to exist before either of us writes another sentence with a name in
it.

What it asks of you, and it is one open question I deliberately did not
answer: **the handbook's title.**  The book documents the machine, so
`K4510` is the consistent answer with the appliance as its own chapter —
but the cover, the alpha notice and the release titles are yours.

The rule, if you only read one line: *would the sentence still be true if
you unplugged the Pi and opened the desktop build?*  Then it is K4510.

Nothing is renamed in code yet — the file is the decision, not the edit.
The guest-facing strings are the part I would do first (the banner, the
status bar and `INFO` all say `BMC-K4510` from a ROM whose same bytes
boot on both hosts).  Say if you want that before the next figure
capture, since it changes three screenshots.

**Parked, and it may reach `core/` and `sdl/` when it happens:** a
boot-selectable **SUPERMON kernal** (Doc, 2026-08-29).  Needs a console
shim, since `mon/supermon.asm` calls the ROM jump table and a boot ROM
has no K:OS under it; `rom/wozmon.a` is the pattern.  Host side is a menu
action shaped like `ACT_POWER_CYCLE` minus `host_zero`.  Written up in
`docs/BUILD-LOG.md` and listed in `docs/TODO.md`.  Nobody is on it.

**Still open from that TODO section:** ZP is 32/32 and BSSR 447/448.
Those need `crt0.s` changes, not segment moves, and are untouched.

## Waiting on

- Nothing from the handbook session.
- From Doc: whether `CPM K-TURBO` still gives `A0>` interactively. Both
  of us have now failed to reproduce it from a script, so if it persists
  it is something about the live session, not the launcher.

## Parked for the Pi 4 / core-3 work — cpmemu (2026-08-27, added at Doc's request)

Doc found <https://github.com/rsta2/cpmemu> — René Stange's own CP/M 2.2
emulator. rsta2 wrote Circle, so this is the same two-target shape we
have: `kernel.cpp` + `multicore.cpp` for bare-metal Pi via Circle, and a
`Makefile.linux` for a plain Linux build from the same sources. MIT
throughout, clean per-file headers.

**Not a RunCPM replacement.** It runs the real DRI CCP/BDOS
(`system/ccp.hex`, `system/bdos.hex`, assembled from the PL/M source with
MAC) over rsta2's own `bios.asm`, on Lin Ke-Fong's `z80emu` core. Real
BDOS means real sectors, so drives are CP/M disk images built with its
`cpmdisk` tool. Our K:/P:/D: symlink drives, `CPM [command]` via
AUTOEXEC.TXT and the `K-*.SUB` launchers all exist *because* RunCPM traps
BDOS and points it at host directories — none of that survives a swap.
Keep RunCPM.

Worth reading anyway, for three things:

- **`multicore.cpp`** — running the Z80 on its own Circle core, by the
  person who wrote Circle. Same technique as SID on core 3.
- A second, independent worked example of emulator-on-Circle bring-up.
- **Real-BIOS fidelity.** If a CP/M program ever misbehaves under RunCPM
  because it calls the BIOS jump table directly instead of going through
  BDOS, this is the reference for what should happen.

Its "no terminal control sequences, characters pass through unchanged"
caveat does not apply to us — JIM at $DA00 already does VT100/ANSI.

If any of it is ever vendored: the MIT header covers rsta2's code only.
`ccp.hex` and `bdos.hex` are DRI binaries under the Caldera/Lineo grant
and would need their own line in `THIRD_PARTY_SOURCES.md`.

## Every machine current, on binaries as well as the tree (2026-08-27)

Doc, today: "I move around a lot from machine to machine, thus all
machines must be in sync on repo and binaries."

He sits down at whichever machine is nearest and runs what is on it. A
`git pull` leaves a host looking current while its `sdl/k4510`,
`rom/kernal.bin` and `fs/PRG/*.prg` are from whenever it last built.
Three separate faults today came from exactly that: he tested a reported
regression against an emulator thirty commits old; p15's `rom/kernal.bin`
was a day behind and would have gone onto an SD card; and thirteen tracked
`.prg` files were stale on every host at once because `make` skipped them.

So, for both of us:

- **Pulled is not built.** After a change lands, rebuild every host that
  can build, not just the one you are sitting at. A host left
  pulled-but-unbuilt is worse than one left behind, because it lies.
- **`rom/kernal.bin` is untracked and needs cc65.** It does not arrive with
  a pull. p15 has no cc65, so it must be sent the binary; `pi/make-sd.sh`
  now refuses a card whose ROM is older than `rom/kernal.c`.
- **`rm -f demo/*.o` before trusting a rebuild**, and before believing
  `make check-artifacts`. An intermediate that looks newer than its source
  is skipped, and the guard then compares a stale artifact against itself.
- **Tracked artifacts get built once and confirmed twice**: build on one
  host, verify byte-identical on a second, then commit.
- **Say which hosts are current** when you report, and which are not. "It
  is pushed" is not the same statement as "it runs there".

Toolchains count as machine state too: cc65 on ubuntu-s1, t480i5 and
hdieu; the aarch64 15.2 toolchain on p15; ACME being installed on
ubuntu-s1 and hdieu today. A host that cannot build a thing should be
written down as such rather than discovered at the moment it matters.

## REQUIREMENT: every host carries every tool (2026-08-27)

Doc: "all required tooling must be available on all hosts." A host that
cannot build part of the tree is a host that will surprise someone, and
since he sits down at whichever machine is nearest, the surprise is his.
`make all` and `make test` must both complete on all four.

Two ways a tool goes missing, and the second is the one that keeps
catching us:

1. **It is not installed.** hdieu had no Mad Pascal, so `make all` died on
   `fs/PRG/hello.prg` there; p15 had no cc65, nasm or SDL2 until today.
2. **It is installed somewhere a non-interactive shell cannot see.** cc65
   lives in `~/opt/cc65/bin` on ubuntu-s1 and hdieu, ACME and nasm in
   `~/.local/bin`, and none of those are on the PATH an `ssh host command`
   gets. Every build on those hosts today needed the PATH set by hand.
   **A tool off the PATH is a missing tool** as far as any script, cron
   job or agent is concerned.

The state as of 2026-08-27, after installing what was absent:

| tool | what needs it | ubuntu-s1 | t480i5 | hdieu | p15 |
|---|---|---|---|---|---|
| cc65 / ca65 / ld65 | ROM, all `.prg` | `~/opt/cc65/bin` | `/usr/bin` | `~/opt/cc65/bin` | `/usr/bin` |
| SDL2 dev | the desktop emulator | yes | yes | yes | yes |
| nasm | `tube/bbcbasic` | `~/.local/bin` 2.16.03 | `/usr/bin` 2.16.03 | `/usr/bin` 3.02 | `/usr/bin` 3.02 |
| ACME 0.97 | `rom/wozmon.bin`, `rom/demo.bin` | `~/.local/bin` | `~/.local/bin` | `~/.local/bin` | `~/.local/bin` |
| 64tass 1.60.3243 | `fs/FORTH/forth.prg` | `~/.local/bin` | `/usr/local/bin` | `/usr/bin` | `/usr/bin` |
| Mad Pascal 1.7.8 | `demo/pas/*.pas` | `~/Projects/neo6502_dev/` | `~/Projects/neo6502_dev/` | `~/Projects/neo6502_dev/` | `~/Projects/neo6502_dev/` |
| Mad Assembler 2.1.8 | the same | `~/Projects/neo6502_dev/` | `~/Projects/neo6502_dev/` | symlink to `~/src/` | `~/Projects/neo6502_dev/` |
| Free Pascal | rebuilding `mp` | `~/opt/fpc` 3.2.2 | `/usr/bin` 3.2.2 | `/usr/bin` 3.2.3 | `/usr/bin` 3.2.3 |
| aarch64 15.2 | the Pi kernel | - | - | - | `~/opt/arm-gnu-...` |
| XeLaTeX | the handbook | `/usr/bin` | **MISSING** | **MISSING** | **MISSING** |

Closed 2026-08-27. Ubuntu has no 64tass 1.60 and ubuntu-s1 has no
passwordless sudo, so it is built from the SourceForge source into
`~/.local/bin`; Fedora's package is the same version. XeLaTeX is the one
open line: a large install for a document only ubuntu-s1 has ever built,
and worth asking Doc about rather than assuming either way.

**Mad Pascal is no longer a precious binary (2026-08-27, second pass).**
Free Pascal is now on all four, so `pascal/install.py` does the whole job
and the compiler can be rebuilt anywhere. The recipe, in full:

    git clone https://github.com/tebe6502/Mad-Pascal.git DIR
    git -C DIR checkout 4a0e5bcd          # the pin: upstream 1.7.8 "optimizations"
    python3 pascal/install.py DIR         # patches the source, copies pascal/mp/*, builds mp

Nothing in it is unbackupable: the K4510 half (`lib/k4510.pas`,
`graph_k4510.inc`, `base/k4510/`, `rtl6502_k4510.asm` and the rest) is
copied out of `pascal/mp/` in THIS repository, and the eleven upstream
files it edits are edited by that script. Only the upstream commit needed
pinning, and 4a0e5bcd is it.

**Tested, because the fpc versions differ**: p15 built `mp` with Fedora's
fpc 3.2.3 and ubuntu-s1 with the official 3.2.2 tarball, and both produce
`hello.prg` and `pgraph.prg` BYTE-IDENTICAL to what is committed. So the
fpc version does not reach the generated 6502 code, and needs no pin of
its own.

One wrinkle: `demo/pas/*.lst` records mads's `-i:` path in its second
line, so the listings only reproduce when Mad Pascal sits at the canonical
`~/Projects/neo6502_dev/Mad-Pascal`. Build it elsewhere and you get a
one-line diff that is not a code difference.

ubuntu-s1 has no passwordless sudo, so its fpc is the official
`fpc-3.2.2.x86_64-linux.tar` installed into `~/opt/fpc`, with a symlink at
`~/.local/bin/fpc` so a non-interactive shell finds it.

**Historical, and still true of a stock clone:** I tried, and it is a
trap: `make pascal-install` runs `pascal/install.py`, which PATCHES the
Mad Pascal source (`src/include/syntax.inc` gains `-target:k4510`,
`src/mp.pas` moves the stack, `lib/system.pas` puts the transcendentals
on the MATH unit) and then rebuilds `mp` with Free Pascal — which is on
no host. On a stock clone the rebuild fails and leaves no working
compiler at all. Worse, a stock `bin/linux_x86_64/mp` is 1.7.5-Test and
generates DIFFERENT CODE from the patched 1.7.8: `demo/pas/pgraph.lst`
came out 1023 lines different on hdieu before I noticed. **Copy the whole
patched tree** (`rsync -a --exclude=.git`, 72 MB) from a host that has
one; the patches live in the tree, not only in the binary. All four now
reproduce `hello.prg`, `pgraph.prg`, the listings and `forth.prg` byte
for byte.

**Version pins that matter**, learned the hard way: ACME must be 0.97 or
later (0.96.4 does not know `--cpu m65`, which `rom/wozmon.a` needs, and
the GitHub mirror is stuck there); nasm 2.16.03 reproduces `tube/bbcbasic`
byte-identically where 3.02 produces a different, working binary.

## p15: the Pi kernel and the desktop build fight over the same objects

`pi/Makefile` and the desktop Makefile both write `core/*.o`,
`core/xemu/*.o`, `core/ui/*.o`, `core/resid/*.o` and `demo/*.o` — one set
aarch64, the other x86-64. Whichever ran last owns them, and the other
link fails or, worse, does not. **On p15, clean those before switching
between `make -C pi` and a desktop `make`.** It is the only host where
both are built.

## For the handbook agent — user-visible, shipped today, undocumented

**The "hold a key to skip STARTUP.BAT" message is gone**, and so is the
half-second grace window it announced (Doc: "F7 will suffice"). Boot goes
straight in. The two ways to skip it are now F7 -> Shell -> Run
STARTUP.BAT, which persists, and `--no-startup.bat`, which does not. If
the book mentions holding a key at the banner, that is now wrong.

**INFO reports the architecture, and pages.** It now reads:

    SYSTEM  K/OS stage 4 (the BMC-K4510 operating system)
            build 0.3-044bac5+, desktop emulator

or `bare metal on a Raspberry Pi 3B+`. It used to say "emulator"
unconditionally, which was simply wrong on the Pi. It is 28 lines and did
not fit MODE 3's 25 rows, so it pages like TYPE now.

**INFO stays in the ROM** (sideways bank 1) rather than becoming a .prg,
if the book ever has cause to say where things live: a .prg has to load
from the filesystem, and INFO is the thing you want when the filesystem
is what is broken. Same reason DUMP is in ROM.

**`BUG` is `fs/PRG/bug.prg`, not a shell command** (Doc's design: an
interview, not a printed template). `BUG` or `*BUG` from anywhere — the
REXX rule finds it. It asks seven questions, fills in five lines itself,
and writes a finished report.

- File: **`/SYSTEM/BUGREPORTS/BUG-YYYYMMDD-HHMMSS.TXT`** from the machine,
  `fs/SYSTEM/BUGREPORTS/` from the host (which is the path a user needs to
  upload it). The directory is made if absent.
- Machine-answered: `Machine` (desktop / Raspberry Pi 3B+, from `$D522`),
  `Version`, `Screen` (size and MODE), `Dump` (the last DUMP, or a nudge to
  go and type DUMP first), `When`.
- Asked: the labels are `bug_labels[]` in **`demo/bug.c`**, marked with a
  `BUG-LABELS:` comment for your check. That is the file and the symbol.
- Zero ROM: it is a program.

**`Version` now identifies the exact build.** `sys_version` is stamped by
the Makefile from `git rev-parse --short`, with `+` for a dirty tree — so
`INFO` and every BUG report now read e.g. `0.3-044bac5+` instead of
`k4510 0.3`. That is the change you flagged as the one that would make
reports traceable; it cost nothing and only `core/io.o` rebuilds.

**`$D522`** is in `core/io.h` as `IO_SYS_HOST`, commented in the shape the
rest of that block uses.

**`TYPE` (and so `HELP`) pages** — a screenful at a time, `-- more --`,
any key continues, `q` or Esc stops. **Not** when a script is running it,
or STARTUP.BAT would hang waiting for a key nobody is there to press.

**`fs/PRG/logo.prg`** — the banner as a program (`RUN PRG/LOGO`), beside
the ROM's `LOGO` command. It writes text32 cells directly, so it can set
backgrounds, and it reads the geometry from JIM so it follows the MODE
and the margin. The bars, colours and text are four tables at the top of
`demo/logo.c`, meant to be edited.

**MODE 4 no longer hangs the machine.** `pad()` could be asked for a
column past the right margin, which `cx` can never reach.

**`k4510 --no-startup.bat`** (also `--no-startup`, or `K4510_NO_STARTUP=1`
in the environment) skips `/STARTUP.BAT` for that run only. The F7 switch
does the same but persists, and holding a key at the banner needs you to
be there; this is the one-shot for a script, or for a startup file that
wedges the machine and you want one clean boot to go and fix it. It does
not touch `k4510.cfg`.

**Video → Scaling** in the F7 menu: the three choices are now what their
names say. `sharp` is nearest with free scaling (sharp, but at a window
size that is not a whole multiple it drops pixel rows unevenly);
`soft` is linear; `sharp-fit` is nearest **and** integer scaling, so every
pixel of the machine is a whole number of pixels on the glass — exact,
with letterboxing instead of artefacts. sharp-fit was picking linear as
well, which threw away the point of it.

**`LOGO` (new command)** clears the screen and reprints the startup
banner. It lives in the sideways window (SWCODE0), not the resident ROM,
so it cost 24 bytes of ROM2 for its dispatch and nothing else.

**The banner itself changed.** Five colour bars stepped 4:3:2:3:4, and
new text beside them. (Tapered ends were tried and removed: at this cell
size a one-pixel diagonal beside a solid bar reads as dirt.)

    BMC-K4510 -- A FANTASY 8/16-bit COMPUTER
    CPU: 45GS10 at 40.5 MHz + runCPM Tube
    RAM: 256 000 000 bytes
    CHIPS: 4 reSID, VICKY, SHEILA, FRED, JIM

If the book has a picture of the old banner it is now wrong.

Everything here is in the machine and is not in the book yet.

**VI (`demo/vi.c`, 270e8c6)** grew most of an editor. The header comment
in that file is the authoritative key list. In brief:

    counts   3dd 5j 2dw 10G      before almost anything
    move     w b e ^  (added to h j k l 0 $ G gg PgUp PgDn)
    operate  d c y + any motion, or doubled: dd cc yy
    insert   s S C  (added to i a I A o O)
    change   x X r ~ J D  p P (unnamed register, filled by d/x/y)
    undo     u    redo Ctrl-R      (unlimited; the journal is far memory)
    search   /pat ?pat n N         plain substrings, not regex
    ex       :s/old/new/[g]   :%s/old/new/[g]
             :map lhs rhs    :imap lhs rhs      (:imap jk <Esc>)

Two things worth a sentence in the book: charwise operators **clamp to
the line** (`dw` at a line end does not eat into the next), and search is
substrings, not patterns — both deliberate.

**EhBASIC `*VI` and `*EDIT` (3d2967b)** with nothing after them edit the
program in memory: SAVE to a temp file, run the editor under SWAP, LOAD
it back. With an argument they are the ordinary `*` escape. Variables do
not survive (it is a LOAD). Costs 512 bytes of BASIC RAM: 47103 free
becomes 46591. I put a section in `doc/guide/chapters/03-basic.tex` —
yours to keep, rewrite or move.

**F7 menu (ed0904d, and earlier)** gained Video → Resolution and
Video → Left/top margin, and Shell → Run STARTUP.BAT. Resolution shows
what the machine is actually in and follows a `MODE` you type. The menu
offers only the first three modes: MODE 3 and 4 are reachable by command
but not from the menu, and never saved.

**`MODE 0-4`** — 640x480, 640x240, 320x240, 320x200 (40x25, the C64's
geometry), 160x200. Raster lines always count the full 480, so in MODE 3
the first line of the picture is raster line 40.

**STARTUP.BAT** has no grace window and no message any more (Doc: "F7
will suffice"). The F7 menu's *Run STARTUP.BAT* toggle and
`--no-startup.bat` are the two ways out of one that wedges the machine.

**Caps lock** behaves like a caps lock: shifted gives the other case. It
is also suspended while a program runs, so `:q` reaches VI.

## Recently finished

- `prctl(PR_SET_PDEATHSIG)` so the Tube's children die with the emulator
  (6998246, include now guarded by `__linux__` as you suggested).
- `THIRD_PARTY_SOURCES.md` (7706dce) — provenance per vendored
  component, and a list of the six whose upstream version is unpinned.
- Deleted `test/shot`, the stray tracked x86 binary. Thank you.

## alpha-0.3 "Colophon" — released

Released at 05fafb7, as a pre-release, with two assets: the Pi SD-card
zip and the handbook PDF. I rebuilt `doc/guide/k4510-guide.pdf` with
`GUIDE_VERSION=alpha-0.3 ./make-guide.sh --no-shots` so the cover carries
the release rather than `alpha-0.2+117`, and committed just that file —
no figures recaptured, and `mkregs.py`/`mkissue.py` produced no drift.
That is the only thing I have touched under `doc/`. The PDF is also a
release asset, alongside the Pi SD-card zip.

Three things there that touch your side:

- The PDF asset is the book as of that commit, cover stamped `alpha-0.3`
  rather than `alpha-0.2+117`. If you rebuild it, `GUIDE_VERSION` is how
  the cover gets a release name before the tag exists.
- `fs/.HELP` was stale in the machine, not just in the book: it offered
  `MODE 0-2`, still told you to hold a key to skip STARTUP.BAT, and had
  never heard of `BUG` or `LOGO`. Fixed. If the book quotes HELP
  anywhere, it is worth a look.
- The card's CP/M drives ship empty on purpose (mixed provenance), and
  the Pi's network side has never run on real hardware. Both are said
  plainly in the release notes and in `pi/README-SD.txt`; if the book
  claims either works on the Pi, it is ahead of the evidence.

---

## Status-bar text mode (2026-08-27)

Built the first of the three "terminal" features Doc scoped: a real
hardware status-bar mode, F7-selectable. 640x240 / 80x30, but the middle
25 rows are a scroll region between two static bands (Doc's 2 top + 3
bottom). It is a DECSTBM done in the machine's own layout: `scroll()`
already only moved `OY..OY+ROWS-1`, so once the geometry carves a top band
(OY) and a bottom band (`bband`), the console scrolls in the window while
the bands sit still. Proven headless: two `DIR PRG` listings scroll the
first up and off the top of the window and the bands never move.

Wiring, for whoever extends it:
- **Orthogonal BOOL, like `margin`** — `SET_VIDEO_STATUSBAR` (settings.c/h,
  a Video menu row), host packs `SYSOPT_STATUS` ($D521 bit 3) every frame
  (main.c), and suppresses `SYSOPT_MARGIN` while it is on (bands frame the
  screen, no margin with them). `status_shown` reconciles like `margin_shown`
  so toggling it issues a mode request; the boot branch requests it too.
- **ROM (kernal.c):** `OY`/`bband` are now variables (OY was `#define
  margin`). `video_init` reads `$D521 & SYSOPT_STATUS` live and, for the
  80-column modes only, sets `OY = PROWS/15`, `bband = PROWS/10` (→ 2+3 at
  640x240, 4+6 at 640x480 = the future 80x50). `bband != 0` is the "status
  is on" flag the rest of the ROM reads. `cls` clears only the window and
  calls `draw_bands`; `mode_do` forces `margin = 0` under status.
- **`draw_bands` must be RESIDENT** (ROM1C) — `cls` runs inside bank-1
  context (cmd_mode calls it) and `sw_call` does not nest, so it cannot be
  banked. It cost ~570 bytes of ROM1C; watch that headroom. Trap I hit:
  `modename()` lives in SWCODE1, so a resident caller gets garbage — the
  resolution string had to be inlined, then dropped for budget. Phase-1
  content is "BMC-K4510 K/OS" (top) and "status mode ... NN MHz" (bottom,
  the live clock). The blank spacer rows are the phase-2 widget slots.
- Geometry falls out for free: the console reports `ROWS = 25`, so NC / EDIT
  / VI confined to it just work (they read `REG(TERM+6)`); they cannot draw
  into the bands. That is the [[project-k4510-terminal-mode-and-filer]]
  geometry rule, satisfied by construction.
- Not done: live band refresh (the clock is drawn at mode entry, not per
  frame — change the F7 clock and the band is stale until the next `cls`);
  configurable widgets; the 80x50 tall variant is reachable (640x480 +
  status) but untested. `MODE` from the shell still can't toggle status
  (F7 only); INFO's 28 lines page inside the 25-row window.

---

## ZX Origins fonts in the menu + the native clock tick (2026-08-28)

**Clock now ticks from the IRQ.** The status-bar clock rode the console key
poll, so it froze inside a running program (a BASIC compute loop never asks
for a key). Moved it to the vblank IRQ that already blinks the cursor
(crt0.s clk_paint): every ~half-second it checks the RTC minute and repaints
the eight digit cells of HH:MM DD.MM straight into the text map, borrowing
$02-$05 like the blink, never touching the ZP-swap core. Proven ticking
inside `10 GOTO 10` across a midnight rollover. Gotcha that cost time:
adding code before `_speed_loop` shifted it so its inner branch spanned a
page (+1 cycle/iter x 37000 = INFO -c reading 38.3 not 40.4 MHz) -- moved
_speed_loop ahead of the IRQ so later edits can't move it. Measurement is
placement-sensitive; keep it early.

**Twelve ZX Origins fonts (F7 -> Screen font).** FONT_ZX_* in settings.h,
font_names[] in settings.c, paths[] in apply_font (sdl/main.c) -- three
lists in lockstep, plus tools/mkzxfonts.py. Each ZX Origins zip ships a
4096-byte C64/<name>.bin; the machine's petscii_to_ascii reads the
lower/upper charset from offset 2048, but ZX Origins stores it first, so the
tool SWAPS the two halves. Without the swap the text renders as line-drawing
characters (set 1 = "both"/lower-upper at offset 0, set 2 = "upper"/graphics
at 2048 -- opposite of a standard C64 chargen).

**Licence -- the files are NOT committed.** ZX Origins (Damien Guard, (c)
1988-2023) is free to use but forbids re-hosting the files. The repo is
public, so data/fonts/zx/*.bin is gitignored, like Commodore's chargen.bin
("names them, does not ship them"). Regenerate with tools/mkzxfonts.py from
your own download; absent -> the menu entry falls back to kernel8. make-sd.sh
now copies data/fonts/ so the Pi gets alternate fonts too (also fixes
unscii/openroms never reaching the card before).

## For the handbook agent -- please credit Damien Guard

Doc asked (2026-08-28) that the handbook thank the ZX Origins author. When
the font menu is documented, add a credit like:

  "Screen fonts from ZX Origins by Damien Guard (https://damieng.com/zx-origins),
   used with thanks."

The dozen offered are Bauhaus, Broadway, Computer, Cyberwire, NLQ, Benguiat,
Chicago, Courier, Eurostile, OCR-A, Pristine, Anvil. The licence's own
suggested form is "<fontname> font by DamienG https://damieng.com/zx-origins".
Note the fonts are NOT shipped with the machine (licence forbids re-hosting) --
the book should say the machine offers them and where to get them, not imply
they are bundled.

## Roadmap note, 2026-08-28

Doc raised nine ideas at once (Turbo Pascal/VI, a TNFS server, hosting
resources, a font tool, the codepage, HELP pages, USB, a mouse cursor, a
GUI). Thought through in `docs/notes/roadmap-2026-08-28.md` — nothing built
yet, no code touched. Audited against the tree the same day after Doc
challenged one claim; seven corrections are marked in place there.

**For the handbook agent — §6, HELP pages.** Doc would like `HELP DIR` to
explain `DIR`. Proposal: pages as `/SYSTEM/HELP/<CMD>.TXT`, plain text, 80
columns, NAME/SYNOPSIS/DESCRIPTION/EXAMPLES/SEE ALSO; the handbook's command
reference and those files generated from one source, plus a build guard that
compares the pages against the `is_cmd(&p, "…")` dispatch table in
`rom/kernal.c` and fails on either kind of gap. Coding side owes the
`HELP <topic>` dispatch and the directory. Read §6 before starting; say in
your note if you would rather shape it differently.

**Found by the audit, worth acting on — `petscii_to_ascii` blanks 136 glyph
slots.** `sdl/main.c:79` fills 0x20-0x7F plus a hand table of 24 box glyphs
and zeroes the rest, so selecting *any* 4096-byte chargen (open-roms, PXLfont,
ZX Origins, a real C64 chargen.bin) loses 134 of the glyphs `data/font8.bin`
carries — 27 of them in CP437's 0xB0-0xDF line/block range — and draws the
double-line box with single-line glyphs. KOMMANDER happens to use only six
glyphs, all mapped, which is why it has not shown. Fix planned: extend the
table and fall back to the kernel font for unmapped slots. **Handbook: any
figure shot with a non-default font may show blanks — worth knowing before
recapturing.**

**Decision that gates the font work — §5.** Recommendation: CP437 stays the
machine's native codepage; PETSCII becomes a VICKY charset + screen-code
translation *mode*, not a second encoding. Consequence for the handbook: the
F7 font menu will only offer fonts normalised to full CP437 coverage, so a
ZX Origins font there is ZX letters with borrowed box-drawing — worth a
sentence when that menu is documented.

## KOMMANDER — a two-panel file commander, 2026-08-28

Built `demo/kommander.c` → `fs/PRG/kommander.prg`, typed `KOMMANDER`. The
Norton Commander of this machine: two directory panels, Tab switches the live
one, arrows/PgUp/PgDn/Home/End walk it, Enter descends a directory or views a
file, `..` climbs. Function keys **F3** View, **F5** Copy, **F6** Move/rename,
**F9** MkDir, **F10** Delete; `.` toggles dotfiles, **F2** refreshes, **Esc**
leaves. (F7 and F8 never reach a program — they are the menu and pause keys —
so MkDir/Delete moved off Norton's F7/F8 to F9/F10.)

Two design facts drove it, both worth remembering:

- **A `.prg` cannot launch another `.prg`.** Both load at $6000, so shelling
  out to EDIT would overwrite the running program and crash on its return
  (`run_at` invokes a program with a plain `jsr $6000` and expects `rts`). So
  KOMMANDER is wholly self-contained: every file operation goes straight to the
  storage device at $D300 (DIR_FIRST/NEXT, STAT, LOAD, CHDIR, MKDIR, RM, RMDIR,
  RENAME, COPYFILE, GETCWD), and it has its own read-only pager for View.
- **The device has one current directory**, but a commander needs two. Kept an
  invariant: the device cwd always tracks the *active* panel. To list the idle
  panel it `CHDIR`s there (absolute paths work — `fs_resolve` honours a leading
  `/`), lists, and `CHDIR`s back. Each panel remembers its own absolute path
  from GETCWD.

Drawn straight into VICKY text32 at $030000 (glyph-lo, glyph-hi, fg, bg per
cell) inside the ROM console window — geometry from JIM's registers
(COLS/ROWS/OX/OY/PCOLS at $DA05..$DA0D), so it lives correctly under the status
bands and in the margin. Single-line CP437 frames (the default font8 carries the
whole box set). Blue field, cyan bar on the active panel, grey on the idle one.
Exits with the EDIT idiom — `REG($DA04)=2; rom_video()` — to hand the shell a
clean screen.

Entry tables are BSS: 192 entries/panel, 30-char names (bigger and the image
overruns the $6000–$CFFF program region). Long names truncate; deep dirs cap at
192 with the rest unseen. The `.prg` is ~11 KB.

**For the handbook agent:** unbuilt when you last looked — this is now a real,
shipping program worth a short section (a file manager alongside EDIT/VI). It is
keyboard-only, self-contained, and does not launch other programs (the $6000
reason above). Recapture any figure that shows the program list — `KOMMANDER` is
in `/PRG` now.

**Roadmap §5b added (2026-08-28) — multicolour fonts.** Text/text32 are
1 bpp hard-coded in `core/vicky.c:122`; only tile mode does 2/4/8 bpp.
VICKY-SPEC §5 promises bpp-aware text; unbuilt. Proposal is bpp-aware
text32 (pixel 0 = bg, 1 = fg, 2..N = palette). **Handbook:** the spec and
Appendix A should not be read as describing shipped behaviour here —
text mode is 1 bpp until this lands.

## TINY — a scrolling tile map with sprites, 2026-08-28

Doc: "a nice scrolling tile map demo with sprites -- check the internet for
example and assets you can download freely". Built `demo/tiny.c` →
`fs/PRG/tiny.prg`, typed `TINY`. Kenney's **Tiny Dungeon** (CC0) vendored in
`data/tinydungeon/`; `tools/mktiny.py` turns the 132-tile sheet into VICKY
16x16 8 bpp tiles and the Tiled sample map into a 64x40 tile map (the sample
tiled 2x2, flip bits carried over); both ride in as a K4SG segment at
$00110000, so the program is 4.6 KB of code and 38 KB of art. Layer 0 tile
mode in 320x240, a text8 caption on layer 1, 41 sprites (a knight you steer
plus 40 wanderers) — 8 bpp sprites and 8 bpp tiles share one layout, so the
sprite DATA pointers aim straight into the tile set. Camera follows the
knight; arrows turn him, space stops, Esc/Q leave. Walls are found by colour
(`walkable[]` in the generated tiny.h). 60 fps on the desktop; not yet run on
the Pi. kenney.nl itself refuses curl (403); the OpenGameArt mirror served
the zip.

**I edited three shared files — CREDITS.md, LICENSES.md,
THIRD_PARTY_SOURCES.md — one row/record each for Tiny Dungeon.**

**For the handbook agent:** a new program in `/PRG`, worth a figure — it is
the machine's first tile-mode picture and the first sprites that are not
drawn by code. Kenney asks for nothing (CC0) but a credit line is the decent
thing: "Tiny Dungeon by Kenney (kenney.nl), CC0". Recapture any figure that
lists `/PRG`. Capture recipe: `test/capture rom/kernal.bin 420 out.png "run tiny\n"`.

## The resolution-switch mechanics, reviewed and rebuilt — 2026-08-28

Doc (on t480i5, with screenshots): GRAPH2D/3D cut the 640x480 screen in
half with the status band stranded in the middle; a 640x480 startup showed
no logo; and a menu switch 640x480 → 640x240 once left a dead blank screen
that even Power cycle would not clear. His verdict: "I think the whole
resolution switch mechanics have to be reviewed." They were. The mode lived
in three places — the ROM's `vmode`, VICKY's CTRL, and the frontend's
`mode_shown` + `video.mode` — and two of the writers bypassed the others.

- **$D521 bits 5-7 now always publish the wanted mode (+1, 0 = old host);**
  the ROM boots straight into it, so there is no boot-time mode request and
  nothing to wipe the banner with (that was the missing logo: the request
  landed after the banner and `mode_do` ends in `cls`). `SYSOPT_MODEREQ`
  only flags a live change now. Old encoding was raw-mode-during-request.
- **EhBASIC `GRAPHICS` goes through the ROM** instead of writing CTRL
  behind its back: `SHELL "MODE 0"` / `"MODE 2"` (the line is copied to
  `gargs` first — this image is invisible to the ROM inside a system
  call), then layer 1 is set up fresh; `GRAPHICS 0` restores the mode it
  found on the way in (`gprev`). That fixes the half-screen graphers *and*
  stops the frontend from "adopting" a CTRL the ROM never knew about into
  `video.mode` — which is how Doc's cfg silently became 640x480.
- **Frontend:** a mode request that times out puts the menu setting back
  (the menu must not lie); Power cycle forgets the mode tracking and
  re-adopts after the reboot.
- **Latent bug found while in there:** the gfx vars overlapped — `gw+1`
  *was* `gsprinit`, and the tokenizer's `k_crx0` ($03B3) sat on `gh`, so
  typing any graphics statement interactively corrupted the blit height.
  Vars moved to $03B5-$03BA.
- **EhBASIC layout:** `Ram_top` came down to $BD00 (the $BE00 tail was
  full and the $C000 slice 21 bytes over); `K_SPROFF` and the new MODE
  plumbing live in the 512-byte tail. EhBASIC's "Memory size" banner
  number shrinks by 256 bytes.
- ROM budget after: ROM2 4 free, ROM1C 40 — measure before touching.
- Verified headless: boot 640x480 shows the banner; live switch + LOGO in
  the new mode; power cycle honours the new mode; GRAPH2D full-height with
  the band at the bottom; GRAPHICS 1/2/0 round-trip with SPRDEF/SPROFF.
  All 13 unit tests green. NOT yet run on the Pi.

**For the handbook agent:** user-visible changes. GRAPHICS 2 now gives a
full-height console under the bitmap (80x50 between bands, 80x60 without)
and the F7 menu's Resolution row follows GRAPHICS; startup in any saved
mode shows the banner. The $D521 description changed (core/io.h) —
regenerate Appendix A. Any figure of GRAPH2D/GRAPH3D taken before today
shows the half-screen bug: recapture.

## Menu mode changes: apply at close, repaint with the logo — 2026-08-28

Doc, after trying the rebuilt mechanics: returning from a resolution or
status-bar change should "call logo.prg to reset the screen", and stepping
through resolutions inside the menu and landing back on the original must
not wipe what was on screen. Both done:

- The frontend applies mode/margin/status-bar changes only at **menu
  close**, and only if the setting is *net* different from what the
  machine is in — stepping away and back costs nothing at all.
- When a change *is* performed, `mode_do` sets a flag; `k_getin` returns a
  CR to unstick `readline`, and the shell loop prints the **banner** (the
  LOGO screen) before its next prompt. So the answer to "what does the
  screen show after a menu mode change" is now: the machine's face and a
  fresh prompt, never a bare console.
- Budget: `putdec2` moved to SWCODE1 (its only caller is INFO, bank 1).
  ROM1C 35 free, ROM2 30, BSSR 1 (mode_note took a byte).

**For the handbook agent:** the F7 chapter's description of the Resolution
and Status bar rows changed: they take effect when the menu closes, and
the machine greets you with the logo screen. A program that is running
instead of the shell gets a phantom CR when the change is performed and
the logo waits until the next shell prompt.

## SIDPLAY respects the window: the status bands survive it — 2026-08-30

Doc: "the sidplay.prg is broken, it does not respect the top and bottom
borders and they are gone after exit". Both halves were the same fault.
SIDPLAY read the console's *origin* from JIM ($DA07/$DA08) but kept its
own idea of the *size* — `COLS 79, ROWS 29` — and cleared by wiping every
one of the 80x30 physical cells. With the status bar on, the console is a
25-row window between two static bands: the list ran 29 rows and spilled
into the bottom band, and the clear took both bands away for good.

- The whole geometry now comes from JIM ($DA05-$DA08: COLS ROWS OX OY).
  The list's rows-per-column and the footer row are computed from ROWS,
  the second column appears only if COLS allows it, and `put` drops
  anything outside the window rather than writing past it.
- The clear is now `$DA04 = 2` — JIM's own, which by construction touches
  the console window and nothing else. Leaving SIDPLAY does the same,
  instead of the old full-screen wipe.
- Verified with `test/capture` in all three layouts (status bands, margin,
  full screen). To take a shot of a machine that booted with the bands up,
  `test/capture` now honours `K4510_SYSOPT` — the byte the frontend
  publishes at $D521 (bit 2 skip STARTUP.BAT, bit 3 status bar, bits 5-7
  mode+1), e.g. `K4510_SYSOPT=0x0C test/capture rom/kernal.bin ...`.

**For the handbook agent:** no register or command changed. Any figure of
SIDPLAY is still accurate for the full-screen layout; there is now a
status-bar layout of it too (24 -> 20 tunes a column) if a figure wants it.

## Enter runs a program; the keyboard gets a type-ahead register — 2026-08-30

Doc: "in ranger and kommander: enter on a .prg starts the program and exits
ranger or kommander, while enter on a .com starts the program in cpm,
exiting ranger or kommander". Built, and his instinct about the *exiting*
was right for a reason worth writing down: SWAP could have run a .prg and
brought the filer back, but SWAP restores the screen on the way back, so a
program that prints and exits would have flashed past under the redraw. The
filer has to get out of the way for the output to survive.

- **New register.** A write to KBD (`$D100`) pushes a key into the keyboard
  queue — type-ahead, which is the C64's keyboard buffer and how a program
  there handed a command back to BASIC. A guest's key goes straight into the
  FIFO, not through `kbd_push`: a program may not open the F7 menu, and the
  debugger's key log is for keys a person pressed. Costs the ROM nothing.
- **RANGER and KOMMANDER.** Enter: a directory descends as before; a `.prg`
  leaves and runs; a `.com` leaves and runs under CP/M; anything else edits
  (RANGER) or views (KOMMANDER) as before.
- **The .COM launcher** is `try_com`'s: a `K-RUN.SUB` on A:0 carrying an
  optional drive-change line, the program, and an `EXIT` that brings the
  machine back to the K:OS prompt. Limit, from RunCPM's own source: the
  submit file is opened on A: in whatever user area is *current*
  (`BATCHA` is defined, `BATCH0` is not), so a submit may change DRIVE but
  never USER. For a `.com` outside user 0 the filer opens CP/M *at* it
  (`CPM E3:`) and you type the name at the CCP. The `K-RUN.SUB` is left
  behind — the filer is gone by then — and the next launch overwrites it.
- **Tests.** rangertest legs 5 and 12: `hello.prg` through RANGER, and a
  copy of `STAT.COM` on A:0 with the `EXIT` landing back at `/CPM/A/0]`.
  KOMMANDER's half was checked with `test/capture` (it has no suite).

**For the handbook agent:** two things. `core/io.h`'s KBD line changed —
**regenerate Appendix A**; $D100 now reads *and* writes. And the file-manager
chapter's key tables are out of date: Enter on a `.prg` or `.com` now leaves
the filer and runs the program, where before it opened VI (RANGER) or the
viewer (KOMMANDER).

## N: is the CP/M network drive (reserved) — 2026-08-30

Doc proposed reserving CP/M slots for FujiNet and Meatloaf; settled on
**one drive, N:, the whole letter**. On this machine the scheme lives in the
name and not in the device (the Meatloaf rule; `CD tnfs://…` extends it to
directories), so FujiNet and Meatloaf are two URL schemes over one namespace
and do not want a drive each. 8.3 names cannot hold a URL, so N: will be a
mount point: `N:0` shows the machine's current remote directory, moved from
the K:OS shell. The letter is claimed now (`fs/CPM/N/0/README.TXT`,
whitelisted in `fs/CPM/.gitignore`); the plumbing is still unbuilt.

**For the handbook agent:** the CP/M chapter's drive table gains N: —
reserved, empty, network. Nothing else changed about the drives.

## Four SIDs drifted late: the 4-to-1 mix, and the ring had no ceiling — 2026-08-30

Doc heard SID12 drop and lag on the laptop, and asked whether it was "the
12 to 1 combinatorial part that gets the final waveform out". It was.

Each reSID chip carries its own resampling phase, and the phases do not
agree — a chip is clocked only once the machine has written to it, so one
that starts sounding later sits at a different point inside the sample
period for good. On SID12, 4.8% of `sid_render` calls had one chip hand
back a sample fewer than the others. The mix ran to the *longest* chip, so
it (a) read the short chip's buffer past what it had written — a stale
sample, ~2,400 corrupted samples a second — and (b) emitted more samples
than the chips made, which walked the audio lead up with nothing to stop
it: 56 ms to 226 ms in 38 seconds, heading for the ring's own 683 ms, and
discarding at the top. Mix now runs to the shortest chip and carries the
surplus; the ring gained a cap at the lead plus a frame. Same cost
(1.22 ms a frame, four chips sounding), stable at 41-55 ms over a minute.

`K4510_RINGLOG=1` prints the lead, gaps and clock every two seconds — a
climbing lead and a starving ring sound the same from the chair.

**For the handbook agent:** nothing user-facing changed in the machine —
no register, no command. If Appendix C (the sound) says anything about how
the four chips are mixed, it can now say that the mix is paced by the
chip that has produced least, so nothing is invented. `K4510_RINGLOG` is a
developer's environment variable, not a feature; it does not need a page.

## Two more sound chips, and a switch for them — 2026-08-30

Doc asked for FastSID from BMC64's VICE, an OPL2, a way to switch between
them, and the SIDs moved to the Pi's core 3. All four are in.

- **FastSID** (`core/fastsid/`, unaltered from BMC64's VICE 3.3; wrapper
  `core/fsid.c`). The same four chips at `$D400`, stepped once per output
  sample instead of per cycle. Measured under SID12: **0.153 ms a frame
  against reSID's 1.266** — 8.3x. The wrapper blocks DC, because FastSID's
  mix is not centred and the step against reSID's would click at a switch.
- **OPL2** (`core/opl2/`, MAME's fmopl by way of VICE; device `core/opl2.c`)
  at `$D480`, wired the AdLib's way: `$D480` ADDR/STATUS, `$D481` DATA,
  `$D482` ID (reads `$02`). Its two timers work. **OPL2.PRG** plays the same
  Pachelbel progression as SIDS/SID6/SID12, so the chips can be compared.
- **The switch is one row, not three toggles**: Audio -> Sound chip =
  reSID | FastSID | OPL2. All of Doc's rules reduce to "exactly one has the
  sound", and one three-valued setting cannot reach an illegal state.
  Active SIDs (1-4) applies to whichever SID engine is chosen.
- **Sound on core 3** (Pi, off by default): writes are queued with a
  timestamp and performed by the rendering core; the handover is a
  rendezvous at a menu close or when the ROM starts the Tube. **Untested on
  hardware** — it needs a Pi kernel built on p15 and run.

**For the handbook agent:** this is a real chapter's worth of change.
`core/io.h` gained the OPL2's three registers at `$D480` — **regenerate
Appendix A**. Appendix C (the sound) now has two SID engines and an FM chip
to describe, and the F7 chapter's Audio menu has two new rows (Sound chip,
Sound on core 3). There is a new demo, OPL2.PRG. `CREDITS.md`,
`LICENSES.md` and `THIRD_PARTY_SOURCES.md` were edited by this session —
flagging that here as the convention asks, since they are shared.

## The Sound chip row was not reaching the machine — 2026-08-30

Doc asked whether the new chips had been compared in the real emulator.
They had not, and could not have been: `desc[]` in `core/ui/settings.c` is
indexed by `set_id`, `SET_AUDIO_CORE3` went into the enum above
`SET_AUDIO_CHIP` and into the table below it, and the two swapped in
silence. The machine stayed on reSID whatever the menu said. Fixed, and
`test/uitest` leg 6 now walks every id the frontend acts on against the key
it is supposed to name.

Measured properly (ubuntu-s1, dummy drivers, 300 frames, no gaps in any):

| chip | program | clock | SID render | the machine |
|---|---|---|---|---|
| reSID | SID12 | 30 MHz | 1.226 ms | 5.206 ms |
| FastSID | SID12 | 30 MHz | **0.142 ms** | 4.606 ms |
| OPL2 | OPL2 | 30 MHz | 0.300 ms | 5.358 ms |
| reSID | OPL2 | 30 MHz | 0.055 ms | 5.271 ms (control) |
| reSID | SID12 | 15 MHz | 1.593 ms | 4.781 ms |
| FastSID | SID12 | 15 MHz | **0.188 ms** | 3.794 ms |

**For the handbook agent:** if any figure or number about sound cost was
taken from this session's earlier notes, it was wrong — use these.

## alpha-0.5 'Timbre' is cut and on GitHub — 2026-08-30

Version 0.4 -> 0.5, named for what it adds: the same notes, a choice of what
they sound like. Everything built from scratch (`make -B`), the suite green,
**the Pi kernel built on p15** (1,737,072 bytes) — which caught a real fault:
`core/sidq.c` used `<stdatomic.h>`, which Circle's newlib does not have.
`core/tube_cp.c` says so at its line 6; sidq uses GCC's builtins now.

Pushed to the mirror **and to GitHub** — 45 commits, the whole of alpha-0.4's
development plus this. Tag `alpha-0.5`, a pre-release with the SD-card zip
(laid out on p15) and the handbook PDF, and the repo description and topics
updated. Scanned for session URLs and credentials first: clean.

**I rebuilt the handbook** (`doc/guide/k4510-guide.pdf`, 84 pages) — your
area, so flagging it plainly. Only two things changed: Appendix A picked up
the OPL2's registers by itself (it is generated from `core/*.h`), and I
recaptured `shots/menu.png` because the Audio page has two rows it did not
have. **I did not touch the prose**, and it still describes four reSIDs and
an OPL2 that is planned — Appendix C and the F7 chapter need you.

## How the clock is described — 2026-08-30 / 08-31

Doc has retired the standing form of words the book used for the CPU's
clock. There is no replacement label: **say it plainly, in each sentence's
own terms.** What it always meant, and still means, is that the clock is
whatever the host can hold at 60 fps with clean sound, and that 40.5 is only
the desktop's starting point.

Done across the manual (the cover's spec line, chapter 1, chapter 5's BENCH
passage), `README.md`, the GitHub repo description, `docs/K4510-Design.md`
and `docs/notes/handbook.md`. The sentences were rebuilt around the gap
rather than left with holes in them. 84 pages, the guide's own guards green,
PDF rebuilt and the release asset on GitHub replaced.

**Two things in your area, so flagging them.** I edited three chapter files
and the cover; and I edited `docs/notes/handbook.md`, which I would not
normally touch — Doc asked for it by name, because it recorded the retired
wording as required and would otherwise have had you putting it back. That
edit is confined to the "banner and the clock paradigm" paragraph and its
first bullet, which now asks for no number in the boot banner rather than a
label. Nothing else in the file was touched.

Separately: the cover still reads "1-4 reSIDs", which alpha-0.5 has outgrown
— there are two SID engines and an OPL2 now.

## A second BASIC: Microsoft's, ported — 2026-08-31

The machine now has Microsoft BASIC for 6502 as well as EhBASIC:
`/MSBASIC/msbasic.prg`, started from the shell with `CD /MSBASIC` then
`RUN msbasic`. This is the real 1977 interpreter (Microsoft's 2025 MIT
release, via `mist64/msbasic`), not an MS-alike. Full account in
`docs/BUILD-LOG.md`, 2026-08-31.

**In your area, so flagging it.** Three files you may quote from
changed, all in the licence direction and all from "planned" to "here":

- `LICENSES.md` — the `planned: MS 6502 BASIC 1.1` row is now a real
  `basic/msbasic/` row.
- `THIRD_PARTY_SOURCES.md` — the section headed "MS 6502 BASIC 1.1 --
  planned, not present" is now "MS 6502 BASIC 1.1", with the commit and,
  importantly, the record of what was deliberately *not* vendored.
- `CREDITS.md` — the Michael Steil / Microsoft entry no longer says
  "planned".

**What the handbook can now say, and what it must not.** It runs, it
does 9-digit floating point, `FOR`/`NEXT`, strings, `PEEK`/`POKE`, and
Ctrl-C breaks into the running line. It has **no `LOAD` and no `SAVE`**
yet (the words print a line saying so), and **no way out** — MS BASIC
has no `BYE` and the stack pointer is gone by the time it is up, so
leaving it is the reset chord. Please don't promise either; both are the
next stage rather than a plan for someday.

If you write a chapter or an appendix row for it, the two facts worth a
reader's time are that it is the genuine article rather than a work-alike,
and that it is *separate* from EhBASIC — none of the K4510 extension
words (`GRAPHICS`, `PLOT`, `SPRITE`, the far `PEEK`/`POKE`) exist in it.
A `.BAS` file written for one will not generally run on the other.

No figures were captured. If it wants one, the cold-start screen is the
obvious shot — it prints `MEMORY SIZE?`, `TERMINAL WIDTH?`, `26623 BYTES
FREE` and `COPYRIGHT 1977 BY MICROSOFT CO.`, which is a nice bit of
period furniture.

## MS BASIC: the echo, and a warning about screenshots — 2026-08-31 (b)

Same day, straight after the port: MS BASIC looked like it was ignoring
the keyboard. It was not — it never echoes what you type (that was the
monitor's job in 1977), so the screen stayed blank until the answer
appeared. `MONRDKEY` now echoes, and Backspace is translated to BASIC's
own `_` delete with the erase done on our side.

**If you captured any MS BASIC figure before this, recapture it.** The
cold start now reads

    MEMORY SIZE? 28672
    TERMINAL WIDTH? 80

rather than two bare questions, and any shot of a session now shows the
typed lines as well as the answers. A figure taken this morning is wrong
in a way that looks deliberate.

Also worth a line in the text if you write the chapter: `@` kills the
current line and Backspace deletes a character. There is no cursor-key
editing — this is a 1977 line editor, not the ROM's `readline`.

## A design-ideas note, and one thing in it that touches you — 2026-08-31 (c)

Doc brainstormed a list of directions this evening; the worked-through
version is a new file, `docs/notes/design-ideas.md`. Four items taken up
with steps (a `MOUNT`/VFS layer, JIM as the console, networking + a TNFS
service, a PETSCII mode in JIM), three declined with reasons.

**Nothing is built.** The handbook must not describe any of it — it is a
direction note, not a feature list.

**The one to know about is JIM as the console.** If it happens, the ROM's
own console rendering goes away and everything on screen is drawn by the
terminal at `$DA00` instead. That is invisible in the manual's prose, but
**every figure showing a console screen may shift by a row**, so the
recapture would be wholesale rather than per-chapter. It is second in the
build order, so it is plausibly near. I will tell you the day it lands
rather than leaving you to find it in a diff.

The PETSCII item, if it lands, would also want the fonts appendix to say
that the PETSCII look comes from the vendored open fonts (BESCII,
openroms) and not from Commodore's `chargen.bin` — which the note
explicitly declines to ship or fetch.

## The console is JIM's now (first increment) — 2026-08-31 (d)

Started the JIM work from `docs/notes/design-ideas.md`. **This one is real,
not a plan**, so the handbook needs to know.

Every byte the ROM prints now goes to the terminal at `$DA00`. JIM owns the
wrap, the scroll, the tab stops and the cursor arithmetic; `k_chrout` is a
byte sink. The full-screen UI (RANGER, KOMMANDER, the menu, the bands) still
writes cells directly, as planned — that stays cheaper in ROM than escape
sequences.

**ROM2 went from 547 to 753 bytes free**, which was the argument for doing it.

**For the manual.** Screens should look the same, and the tests say they do,
but the drawing path underneath is completely different — so if a figure ever
looks a pixel off, that is why. Two behaviours are now worth a sentence
somewhere, because a reader can use them:

- A program can print ANSI escape sequences straight through `CHROUT` and
  they work. That is what `ANSIDEMO.PRG` demonstrates.
- The terminal has a **PETSCII mode** (`FLAGS` bit 2 at `$DA0E`): the CBM
  control codes, the sixteen colour codes, RVS, the case sets.
  `PETSCII.PRG` demonstrates it.

Two new demos, `fs/PRG/ansidemo.prg` and `fs/PRG/petscii.prg`, one per mode.
If you want figures they are the obvious ones, and the PETSCII one is now
worth photographing — but read this first, because I got it wrong twice
before getting it right.

The machine's font is **always ASCII/CP437-ordered**: a 4096-byte chargen is
permuted into ASCII order on the way in (`petscii_to_ascii()` in
`sdl/main.c`). So there is no screen-code-ordered font in RAM, and PETSCII
codes are mapped onto CP437 rather than turned into screen codes. Letters,
digits and punctuation are exact; the **line-drawing set is exact too**,
because the same loader lifts those glyphs into their CP437 positions. The
rest of PETSCII's graphics — diagonals, quarter-blocks, card suits — have no
glyph at any code in that font and render as spaces. The demo says so.

Two corrections to what I wrote earlier today, in case either reached the
book: **BESCII is not one of the Screen font entries** (they are kernel8,
unscii, open-roms, PXLfont, C64 chargen, then the ZX set) — it is vendored
as a TTF only. And selecting a chargen is **not** needed for the PETSCII
demo: it looks right in the machine's own font.

`test/jimtest.sh` guards the two things that actually broke on the way: the
column reset after a newline (LNM), and CR being folded onto newline, without
which EhBASIC overprints itself on one row.


## The Pi is an OPL2 machine — 2026-09-01

Doc ran the card on hardware. Verdict: **both SID engines sound bad on the
Pi and the OPL2 sounds good**, so the Pi build now offers the OPL2 and
nothing else. The SIDs are *disabled there, not deleted* — they still build,
still pass their tests, and are still what a desktop starts with.

**For the manual, this is a real difference between the two machines**, and
it is the sort of thing the naming split exists for: the K4510 (desktop) has
1-4 SIDs *or* an OPL2; the BMC-K4510 (the Pi appliance) has an OPL2. The F7
Audio page differs accordingly — on the Pi the Sound chip row has one entry
and the Active SIDs row is not there at all.

Two new things to describe if you want them:

- **`OPLPLAY.PRG`** — three tunes on the nine FM voices, with nine sprites
  moving to them: each voice owns an orb, a note-on lifts it, and how high is
  the note's own pitch, so the picture carries the tune's shape. SPACE cycles
  the tunes, 1-9 mute a voice. **The music is original**, written for the
  program, so it carries the project licence and there is nothing to credit.
- The **USB keyboard layout is fixed** — it was coming up German (y and z
  swapped) because Circle fell back to `DEFAULT_KEYMAP` with no `cmdline.txt`
  on the card. There is one now.

Still broken and *not* to be described as working: **the C64 keyboard on the
GPIO connector**. Doc reports it buggy on hardware and it is not diagnosed.

A note on figures: `OPLPLAY` draws through the sprite engine and its own text
layer, so `test/headless` cannot see its screen — `test/opltest.sh` only
checks that it runs and hands the machine back. Any figure of it has to be a
real screenshot.

## Vertical sync is a setting — 2026-09-01

A Claude session on hdieu proposed adding `SDL_RENDERER_PRESENTVSYNC` back.
That would revert a7c8f19, which was made *because of a measurement on
hdieu*: with vsync and no pacing of our own it ran at 51.8 fps on a 60 Hz
display, and frames are sound here — 17% of it filled in by the SIDs, and Doc
heard it. So the answer is the one a7c8f19 itself named: **F7 → Video →
Vertical sync**, live, off by default, so the default behaviour is unchanged.

**A retraction, recorded because it was briefly in the source.** That session
first reported the emulator loading hdieu's compositor — gnome-shell at
94-95% of a core — and I cited it in f504e81's message and in two comments.
It then profiled gnome-shell per thread and withdrew it: the 95% is the GJS
main thread (twelve shell extensions, two docks at once), while compositing
and KMS are idle, and the 44% iowait was the i915 flip worker being booked as
iowait beside a quiet disk. **The emulator was not loading that compositor.**
The comments no longer say it does. The setting stands on the other reason,
which is the one that was always load-bearing: a7c8f19 said the choice should
be a setting, and neither answer is right for every host.

The hand pacer stays on in both positions; it is a floor, not a cadence, so
it only sleeps when the frame came early and cannot fight the flip.

**For the handbook agent:** the F7 chapter's Video menu has a new row,
*Vertical sync*, off by default. Worth a sentence on what the trade is: off
is the machine keeping its own 60 Hz, on hands pacing to the display, which
costs frames (and therefore sound) if the display is not 60 — and saves a
compositor a great deal of work.

---

## 2026-09-01 — the consolidation, round one

Full account in `docs/BUILD-LOG.md`; the standing inventory Doc rules on is
`docs/CAPABILITIES.md`. What changed under the machine, in one place:

- **The SIDs are OFF on both hosts and the OPL2 is the default.** Not
  deleted — reSID builds, `test/sidtest` exercises it, `audio.chip = reSID`
  in `k4510.cfg` gives it the sound back — but the Sound chip and Active SIDs
  rows are gone from F7 entirely, and `sid_set_mute(1)` is the boot state.
- **FastSID is gone from the tree**, with `SID_ENGINE_*` and
  `sid_set_engine`. `core/sid.cc`'s muted path kept the sample accumulator it
  used (renamed `out_acc`) because the OPL2 and the silence case need it.
- **`core/calib.c` is gone**; `core/hostid.c` holds the host fingerprint that
  was the only live thing in it.
- **`cmd_two` (RENAME/REN/MV, CP) STATs the destination and refuses**, `-f`
  overwrites.
- **`fs_dir_first` no longer stats every entry**; `FS_DIR_NEXT` stats the one
  it serves, and the sort is `qsort`. A TNFS listing still carries its sizes,
  so `fs_list_size` non-NULL means "already known".
- **MS BASIC has star commands and `*BYE`**, caught in `MONRDKEY` so nothing
  under `basic/msbasic/` is touched.

**For the handbook agent, three user-visible changes:**

1. **The Audio menu has two fewer rows.** No Sound chip, no Active SIDs. The
   machine sounds through its OPL2 and that is not a choice offered in the
   interface any more; the config file is the way back to the SIDs. Any figure
   showing that menu needs recapturing, and the sound chapter's "three chips,
   one at a time" framing is no longer what the machine does.
2. **The boot banner changed**, again: `CHIPS: OPL2, 4 SIDs, VICKY, SHEILA,
   FRED, JIM`. It is in a dozen figures.
3. **MS BASIC now has a way out and a shell escape.** `*BYE` returns to K:OS;
   anything else after `*` is run as a K:OS command line. The cold-start banner
   says so in a second line. This is worth documenting beside EhBASIC's `@`,
   and the difference is worth a sentence: `@BYE` cold-starts the machine to
   reach the shell, `*BYE` returns to it.

And one thing that is now wrong in the manual if it says otherwise: `RENAME`
and `CP` refuse to overwrite. `-f` is how you mean it.

**Later the same day — retirements, and the sound audit.**

`retired/` is new: `sids.c`, `sid6.c`, `sid12.c`, `sidorch.h`, the four
`sidplay` files and `romout.c`, out of `fs/PRG` and out of the build on Doc's
instruction. `retired/README.md` says what each was.

**For the handbook agent, and this one is load-bearing:**

- `SIDS`, `SID6`, `SID12`, `SIDPLAY` and `ROMOUT` are no longer on the
  machine. Any chapter, figure or program list naming them is now wrong.
- `INFO`'s SOUND section is rewritten: it leads with the OPL2 as the
  machine's chip and marks the SIDs "off unless chosen". Recapture.
- `HUSH` now silences the OPL2 as well as the SIDs.
- **BBC BASIC's `SOUND` statement is silent as the machine ships**, and so is
  Mad Pascal's `Sound`, and so is any EhBASIC example that POKEs `$D400`.
  Nothing is broken — they drive the SIDs, and the SIDs are muted. Until the
  sequencer learns the OPL2 (`docs/TODO.md`), any sound example in the
  handbook needs `audio.chip = reSID` in `k4510.cfg` stated beside it, or it
  will read as a machine that does not work.
- `fs/SID` is still a symlink to `sidfiles/EC64SC_SID_Files` with nothing left
  to read it. Doc's call whether it goes.

**2026-09-02, the program's half of the bands.**

`$DA0F` BANDTOP, `$DA16` BANDBOT, `FLAGS` bit 3 to claim; apply with VIDEO
(`$FF92`), which now draws K/OS's bands when they are *not* claimed so that
handing them back is one step. `BANDS.PRG` is the worked example.

**For the handbook agent:** there is a new program, `BANDS`, and a new thing a
program can do. If the F7 chapter gained a Terminal section yesterday, this is
its programmer-facing counterpart — worth a short section beside PETSCII mode,
since it is the same claim-and-hand-back discipline and the same failure if a
program forgets.

**A lesson I re-learned twice in one sitting**, and it is already in the
project's own notes: *stale binaries lie*. After changing `core/term.c` I
rebuilt the ROM and `test/headless` but not `sdl/k4510`, and spent a while
chasing a "bug" where the claim was ignored — first in the harness, then again
in the emulator. Both times the code was right and the binary was old. When a
change spans the host and the guest, `make` everything before believing
anything.
