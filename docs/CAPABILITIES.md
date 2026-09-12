# Capabilities — the consolidation ballot

Everything the machine currently claims to do, one line each, so Doc can
say what happens to it.  Edit the token at the start of each line:

    KEEP   it stays as it is
    WORK   it stays, but it needs work before it is honest (say what, in the margin)
    OFF    the code stays, the machine stops offering it (menu entry, command, or
           #ifdef'd out on one host)
    CUT    it comes out of the tree
    ?      undecided (the default -- everything ships as ? until Doc rules)

Nothing here is lost by CUT: the history keeps it and it can be lifted back.
What CUT actually buys is ROM bytes, test-battery time and one less thing to
be true in the handbook.  Lines marked (no ROM) cost the ROM nothing, so
cutting them buys only attention.

ROM after the first round: ROM1A 627 free, ROM1C 690, ROM2 694, SW1 964,
SW2 454, SW2T 3071.  ZP 0 free and BSSR 1 free -- both still fail-loud.

WHAT DOC RULED, 2026-09-01 (first round), and what was done:
  OFF  the SIDs, the Sound chip row, the Active SIDs row.  OPL2 is the
       default on both hosts.  Nothing SID is deleted.
  CUT  the boot speed test.  CUT FastSID.
  WORK MS BASIC's exit -- done, as star commands and *BYE.
  WORK RENAME and COPY overwriting in silence -- done, with -f.
  WORK DIR on long directories -- done; needs hearing on the Pi.
Everything still marked ? is for the second round, after the testing.

---

## 1. The shell (K:OS command line)

Resident ROM unless noted.  This is where ROM pressure actually is.

KEEP  DIR / LS       list a directory.  FIXED 2026-09-01: the listing streams (the
                     per-entry stat moved to DIR_NEXT) and the sort is qsort, not
                     insertion.  NEEDS HEARING ON THE PI.
?  CD / CHDIR        change directory, CD - for previous, CD tnfs://... for remote
?  MKDIR             make a directory
?  RM / ERASE / DEL  delete; RM trashes to /.TRASH, RM -f removes
?  RMDIR             remove a directory
KEEP  RENAME/REN/MV  rename.  FIXED 2026-09-01: refuses an existing destination;
                     -f overwrites, as RM spells it
KEEP  CP             copy a file.  FIXED 2026-09-01, same guard as RENAME
?  COPY               copy MEMORY (DMA), not files -- unrelated to CP despite the name
?  LOAD / SAVE       load/save a memory image
?  RUN               load and run a .prg
?  EXEC              run a .BAT script
?  SWAP              replace the running program in place (how EDIT/VI are reached)
?  TYPE              print a text file, with paging
?  XD / HEX / DUMP   hex dump a file / memory
?  FILL              fill memory
?  INFO              machine identity and configuration
?  TIME              the clock
?  COLOR / COLOUR    set foreground/background
?  PALETTE           list/set/LOAD/SAVE/RESET the 256-entry palette (bank 2)
?  MODE              set the video mode; saved and restored across boots
?  CLS / CLG         clear text screen / graphics screen
?  LOGO              draw the boot logo
?  ECHO              print arguments
KEEP  HUSH           silence.  FIXED 2026-09-01: it zeroed the SIDs and the
                     sequencer only, so it did not hush the chip the machine
                     actually sounds through.  Keys off all nine FM voices now
?  ON / OFF          toggles
?  ALIAS             user-defined command aliases (bank 2, table at $B400)
?  CAPSLOCK / CAPS   caps lock
?  HELP              the command list
?  RESET             warm reset
?  MON / WOZ         resident Wozmon (~1.1K).  Ruled KEEP 2026-08-29: its whole
                     value is being there when the machine cannot load from disk
?  BBCBASIC / BBC    launch BBC BASIC on the Tube co-processor
?  CPM               launch RunCPM, optionally with a command

## 2. Filesystem and host bridge

?  Host filesystem at $D300   the machine's disk is a host directory (fs/)
?  DIR1 / DIR_NEXT            directory walk API.  Note: DIR1 *opens*, it does
                              not return entry 1 (a repeated source of bugs)
?  /.TRASH                    shared trash for RM / DELETE / DD
?  STARTUP.BAT                boot script; keyboard layout, colours, autostart
?  Settings saved on exit     F7 settings persist to a file
?  MOUNT / a real VFS         PROPOSED, not built (docs/notes/design-ideas.md)

## 3. Editors and file managers  (all .prg -- no ROM)

?  EDIT        the simple full-screen editor
?  VI          the vi-alike
?  KOMMANDER   two-pane Norton-style file manager
?  RANGER      miller-columns file manager, vi keys, exits in place
?  DELETE      trash-aware delete tool

## 4. Debug and monitor tools

?  Wozmon (resident)   see MON above
?  SUPERMON.prg        full 45GS02 disassembler, assembler, hunt, transfer (no ROM)
?  WATCH               $D530 write watchpoint
?  DUMP                memory dump
?  PERF.TXT / io_prof  profiling counters.  NOTE: profiling at the shell prompt
                       lies; the workload must be in a script
?  KEYTEST.prg         keyboard diagnostic (no ROM)
?  BENCH.prg           the CPU clock ladder (no ROM)
CUT   Boot speed test  GONE 2026-09-01 (core/calib.c deleted).  It had been compiled
                       but unreachable since the boot was made instantaneous.  The
                       host fingerprint it also held lives on in core/hostid.c;
                       SETUP.prg is how a machine gets measured

## 5. Languages and guest systems

?  EhBASIC             the primary BASIC.  Has the K4510 words: GRAPHICS, PLOT,
                       LINE, TRI, PALETTE, SPRITE, far PEEK/POKE, shell escape
                       (~1,850 lines, basic/k4510*.asm)
WORK  MS BASIC        /MSBASIC/msbasic.prg at $7000.  2026-09-01: star commands --
                       a line starting with * at the READY prompt goes to K:OS, and
                       *BYE returns to the shell cleanly, same directory, no reset.
                       STILL OWED: LOAD/SAVE, and the K4510 words
?  BBC BASIC           on the Tube co-processor
?  CP/M (RunCPM)       K:/P:/D: drives, .SUB launchers, CPM [command]
?  Tali Forth          forth/
?  Mad Pascal          pascal/ -- PFLOAT, PGRAPH, PMANDEL, PSIEVE
?  TINY                the small C-ish thing (demo/tiny.c)

## 6. Video -- VICKY

?  640x480 / 640x240 / 320x240 modes (+ two smaller fields)
?  Four layers: bitmap / tile / text
?  128 sprites, no per-line limit
?  Blitter: copy / fill / logic / line / triangle
?  SHEILA display-list coprocessor (copper-style)
?  256 colours from 24-bit; 16 per sprite/tile, all 256 for text32 and 8bpp
?  Four predefined palettes: C64, PEPTO, GREY, AMBER (fs/SYSTEM/PALETTES)
?  Scanline effect, borders included (fixed 2026-09-01)
?  Chargen loading    a 4096-byte C64 chargen is permuted to ASCII/CP437 order.
                      A 2048-byte PET chargen is NOT supported (renders wrong)
?  Screen fonts       openroms (LGPL), unscii (PD).  BESCII is vendored as TTF
                      only -- not selectable

## 7. JIM, the console terminal ($DA00)

?  CHROUT is a byte sink to JIM   every ROM byte goes through the terminal
?  ANSI / VT100 mode              escapes, colours, attributes, DEC line drawing
?  LNM (mode 20)                  newline returns the column; the ROM needs it
?  PETSCII mode (FLAGS bit 2)     colour codes, reverse, cursor codes.  PARTIAL:
                                  diagonals, quarter-blocks and card suits render
                                  as spaces -- no glyph exists in an ASCII font
?  JIM's own cursor (FLAGS bit 0) exists but UNUSED; the ROM still draws one, and
                                  the k_getin workaround for the two of them is
                                  still in the tree
?  ANSIDEMO.prg / PETSCII.prg     one demo per mode (no ROM)

## 8. Sound

OFF   reSID         Dag Lem's, cycle-accurate.  1-4 chips.  ~1.50 ms/frame at 4.
                  Still built, still tested, no longer what the machine boots with;
                  `audio.chip = reSID` in k4510.cfg brings it back
CUT   FastSID       REMOVED 2026-09-01, whole engine (core/fastsid/, core/fsid.*)
KEEP  OPL2 (YM3812) nine FM voices, AdLib register map at $D480.  THE DEFAULT on
                  both hosts since 2026-09-01
OFF   Sound chip row    no menu row on either host now; the setting remains
OFF   Active SIDs row   likewise gone from the menu (the OPL2 is one chip)
?  OPLPLAY.prg     .OPL player, 3 original tunes + a library (no ROM)
?  vgm2opl.py      VGM/VGZ -> .OPL converter.  Refuses OPL3 rather than downmix
?  /OPL library    826 converted tunes.  gitignored -- not ours to distribute
CUT   SID playback  SIDPLAY.prg retired (see 13).  `fs/SID` is a SYMLINK to
                  sidfiles/EC64SC_SID_Files and is STILL THERE with nothing
                  left to read it -- Doc's call whether it goes too
?  Audio/video lag  KNOWN, unfixed: sound trails the on-screen VU meters by the
                    audio ring's lead

## 9. CPU and memory

?  45GS10 core       Xemu's 45GS02 instruction core, byte for byte
?  256 MB flat, 28-bit
?  Bank registers ($D600) and the far-call gate ($DF00)
?  Sideways ROM banks, Beeb-style, $A000-$BFFF.  Banks 0-2 are the OS;
   banks 3-15 are PROPOSED as user RAM banks and are unclaimed
?  DMA
?  MAP (28-bit)
?  CPU clock as a setting   F7 -> Machine.  Desktop starts at 40.5, Pi at 15.
                            No standing phrase for it anywhere -- deliberate

## 10. Networking

?  N: device at $D900, core/net.c   built, desktop only
?  TNFS client (CD tnfs://, CD -)   remote working directory
?  TELNET.prg                        (no ROM)
?  Meatloaf-style URL-as-filename
?  Pi networking                     UNTESTED.  Needs Ethernet.  No TLS
?  TNFS *server*, Docker, FTP mount  PROPOSED only (docs/notes/design-ideas.md)

## 11. Host frontend and UI

?  SDL2 desktop frontend
?  F7 settings menu    Machine / Video / Audio / ...
?  Vsync               OFF EVERYWHERE, deliberately (a7c8f19).  Measured at
                       51.8 fps with audible artefacts on hdieu.  Re-proposed
                       and withdrawn 2026-09-01
?  Screenshot capture (K4510_SHOT)
?  State save/load (core/state.c)
?  Reset chord

## 12. The Pi appliance (BMC-K4510) -- retired 2026-09-07, its name 2026-09-12

?  Bare-metal Circle boot on a 3B+
?  USB keyboard        layout via cmdline.txt keymap=; FIXED to us 2026-09-01,
                        needs confirming on hardware
?  C64 keyboard on GPIO  Doc: "buggy".  No symptom written down yet
?  make-sd.sh / install-sd.sh
?  Boot report (SYSTEM/BOOT.TXT)
?  Core 3 audio handover   fixed for OPL2 2026-09-01
?  Everything since alpha-0.3-105 is UNVERIFIED on hardware:
   the games, SUPERMON, KOMMANDER, the video-mode rebuild

## 13. Demos and games  (all .prg -- no ROM, but they are the test surface)

Smoke-tested on the desktop 2026-09-01: every program below starts, runs and
returns the shell EXCEPT ROMOUT (broken, see below) and KEYTEST (which asks
for named keys, so a scripted harness cannot finish it -- it is not faulty).
"Returns the shell" is not "is correct": correctness still needs eyes.

?  BOMBER      Bomb Party, CC-BY art
?  LODE        procedural art
?  GRAPH2D / GRAPH3D  EhBASIC
?  BALLS, CUBE, MANDEL, SIEVE, BENCH, LOGO
?  SEGDEMO, CHROUT, SAY, BUG, SETUP    all start and hand the shell back
CUT   SIDS, SID6, SID12, SIDPLAY  moved to retired/ 2026-09-01 on Doc's
                  instruction: out of fs/, out of the build, sources kept.
                  They played into a muted chip, so they demonstrated nothing
CUT   ROMOUT      retired/ too, and **it was BROKEN before today.**  Found by the smoke pass of
                  2026-09-01: it prints three lines and hangs, on this ROM and
                  on the committed one alike.  One cause is certain -- it does
                  fill_check($A000,$D000), and the top kilobyte of that is its
                  OWN C stack (demo/prg.cfg: PRG $6000+$7000, __STACKSIZE__
                  $0400), which is the same RAM banking exposes.  Stopping the
                  fill at $CC00 gets past that and it then dies differently
                  (blank screen), so there is a second fault behind the first.
                  Not a banking fault as far as anything shows -- the machine
                  is fine; this demo eats itself.  Attempted fix reverted
                  rather than half-landed
?  OPL2.prg, OPLPLAY.prg
?  ANSIDEMO, PETSCII

## 14. Development tooling

?  test/ battery      ~20 tests; `make test`
?  tools/romfree.py   the ROM budget.  Run before touching the ROM
?  headless harness   BLIND SPOTS: prints only non-blank rows; K4510_SHOT
                      disables scanlines and grabs the texture not the window;
                      sprite demos are invisible to it
?  Xvfb + xdotool + import   screenshot capture
?  The handbook build (ubuntu-s1 only)

---

## Known debt that is not a capability

These are not on the ballot -- they are owed regardless.

- core/io.c split into per-chip files
- ZP is 32/32 and BSSR 447/448.  Both fail loud.  New state has to go in
  VICKY or a bank until one is relieved
- Bank 2's code ceiling is $B400; the linker enforces it now
- docs/HOSTS.md does not exist; per-host build knowledge lives in session memory
- EDITTMP.BAS and other scratch files in the machine filesystem root
- fs/SYSTEM/PERF.TXT is generated and not gitignored
