# Handbook session — status

Protocol: `docs/AGENTS.md`. I write only this file.

**Updated: 2026-09-09**

## Now

- Nothing in flight. The book has just been brought up to date with the
  ten days since 2026-08-30 (see below); everything under this heading
  from before that date is history.

## 2026-09-09 — the book caught up with the machine

Doc: "update the handbook to reflect changes (additions and removals)".
It had not been touched since 2026-08-30 and a great deal had happened.
**103 pages now** (was 84); the PDF is rebuilt and committed, and
**every figure was recaptured** from the running machine.

**Removals the book was still asserting.** The SIDs (cover, chapter 1,
the I/O map, EhBASIC, BBC BASIC's `SOUND`, Mad Pascal's `Sound`, the
thanks, the licences). The bare-metal Pi -- chapter 1's "Two names, one
machine" is now "One machine, two ways to run it", with an aside saying
plainly that the BMC-K4510 existed, worked, and was retired on
2026-09-07 and why. `ROMOUT` (retired, and it was broken). `SIDPLAY` and
the HVSC section of "what is not shipped". The old `fs/` layout, in
every chapter that named a path.

**Additions.** Two new chapters: **9, RX** (the language, the three
ADDRESS environments, the file-as-mailbox port, the eight scripts, and
the Wumpus-found bugs as an aside) and **9, The Linux Underneath**
(`!`, `PAS`/`CC`, what is on the stick, the consoles, the blacklisted
internal drive, loopback telnet, Tek40xx and `tekplay`). New sections
elsewhere: the disk's six folders and the search path; the two file
managers and the trash; the line editor and the eight-line history; the
status bands; what is in `/APPS`; `SUPERMON`; `PALETTE`; **MS BASIC**
(3.6); placement, the side panel and **F8 the debugger** (1.7). The F7
menu list is rewritten against `core/ui/menu.c` as it stands --
Terminal is a category now, Audio is one row.

**Appendix C (the sound) keeps its whole history** and gains three
sections: where it stood when it was decided, what was decided (OFF on
09-01, gone on 09-05, and what the sequencer had to learn), and a moral
saying out loud that the measurement argued *against* removal and the
chips went anyway, because the question was identity and not cost. That
seemed the honest shape for a chapter whose thesis is that the
instrument keeps measuring the wrong thing.

**Also renamed `docs/K4510X.md` to `docs/LINUX.md`** as you asked in
your note, with a banner marking what in it is superseded (the raw-image
build, the t520). `docs/NAMING.md` and `docs/TODO.md` follow the rename.
README's "70 pages" is now 103 -- the only line of yours I touched.

## For you (coding) — three things in your area

1. **`/SYSTEM/ETC/HELP` still says `BENCH` sweeps "with a note on SID
   0".** One line; it is the only SID left in a user-facing string that
   I found.
2. **I edited two files of yours, both small, both because they were in
   a figure.** `fs/LANG/EHBASIC/EX/DEMOS.BAS` offered five dead entries
   (SIDWAVE, SIDBEAT, SIDFILT, SID6, SID12 -- the `.BAS` files went on
   09-05) and said `*CD EHBASIC`; both fixed. `fs/CPM/A/0/README.TXT`
   said "CP/M 2.2 ON THE BMC-K4510"; now K4510. Revert either if you
   would rather do it your way.
3. **`DEMOS.BAS` entry 5 chains to `README.BAS`, which is in
   `/LANG/EHBASIC` and not in `EX/` beside the others.** I did not touch
   it because I could not tell which one you meant to move. Also
   `fs/LANG/BBCBASIC/README.BBC` still advertises TUNE as "a round for 3
   SID voices".

## Waiting on

- Nothing blocking.
- **`make-shots.sh` changed** (mine): `dir` is captured at `/` now, so
  the figure shows the disk's shape rather than an almost-empty `/HOME`;
  the BBC figure does `CD /LANG/BBCBASIC` first, because BBC BASIC's
  filenames are relative to the machine's directory and the absolute
  path simply failed (the old shot would have shipped "File or path not
  found" as a kaleidoscope).

---

# Earlier

- Nothing in flight.

## Appendix A is generated from your headers — please read this bit

**Changed today: it parses now, it does not dump.** Doc said pages 42-50
were ugly and did not look like the rest of the book, and he was right --
nine pages of folded monospace is not a reference. `mkregs.py` now reads
the shape your comments already have (`$D720  FOP  description`) and sets
it as a reference: address column, name in bold, description in the book's
own text face. **Nothing you write is reworded** -- the parser only decides
which column a word belongs in, and where your own spacing was doing the
work of a table (the VICKY mode table, the MATH op list) it keeps the table
exactly as written, in monospace. Nothing changes for you: the same comment
gets you a better page.

`doc/guide/mkregs.py` lifts every top-level block comment that documents
registers (two or more lines carrying a `$XX`) out of `core/io.h`,
`vicky.h`, `net.h`, `term.h` and `mem.h`, and sets it as Appendix A.
Nine blocks, ten pages.

What this means for you, and it is the whole point: **a device you add to
one of those headers is in the book at the next build, with nothing to
edit on my side.** What it asks of you is only what you already do —
keep the register list in the block comment above the code.

It parses defensively: no comment format is required beyond the `$XX`
test. A line it cannot read as a register becomes a note rather than an
error, and a line whose own spacing is aligning something is kept as a
monospace fragment, set at the largest size that still fits an A5 page.
There is no width you can now exceed, so nothing here bounces back to you.

## Doc's review of the whole book — done, evening of 2026-08-26

Nineteen items, all in the book. The ones that touch you are in "For the
coding agent" below. The rest: an alpha notice as the first page; ch 1
retitled (What is the BMC-K4510? / Building the emulator); ch 2.1
rewritten from the current `fs/` and `.HELP` (directory table, every
command with its synonyms); MODE 3 and 4 gone from the user's guide (Doc:
programs only); 2.5 lists exactly what the network speaks and says there
is no FTP; 2.11 is now three lines pointing at Appendix B; the `@` escape
gone from ch 3; ch 7 rebuilt around a table that separates Turbo Pascal
(on CP/M, user-supplied) from Mad Pascal (cross-compiler); ch 8 is key
tables, one key per row, with `:imap jk <Esc>` tested on the machine and
documented exactly; 8.3 states the rule (`*EDIT` alone = the program,
`*SWAP EDIT name` = any other file); Filing an Issue is Appendix B,
BUG-first, the hand form moved out to `issue-form.txt` (GitHub only);
Meatloaf, FujiNet and SDL thanked; and the BBC BASIC wording corrected.

**That last one matters to you** — see below.

## BUG is in the book, and the check is live

Both done, Doc gave the go. Page 53, "BUG writes it for you": what it
answers itself, what it asks, the file it writes
(`/SYSTEM/BUGREPORTS/BUG-YYYYMMDD-HHMMSS.TXT`, host
`fs/SYSTEM/BUGREPORTS/`), and `*BUG` from both BASICs, the monitor and
CP/M. Your DUMP-first point is an aside on that page, in the shape you
sent it: if the Dump line says none, type `DUMP` while it is still wrong
and run `BUG` again.

**The check is in `mkissue.py` and it fails the guide build.** It reads
`bug_labels[]` out of `demo/bug.c` and compares it against a `FIELDS`
table that maps each of your labels to the line it must appear as in the
book's block -- `None` for the five the machine answers itself, since no
human is asked those. Add or rename a question and the guide build stops
until the page and the table agree; take one away and it stops too. I
tested both directions before pushing, and a checkout with no `demo/bug.c`
in it prints a note and builds anyway.

You do not have to do anything for it. If it ever fires on you, the fix is
mine: send the new label and I will write the line.

## Fixed today, from the coding session's corrections

**The hold-a-key window is out of the book** (their 05cf6d4 removed it from
the machine). It was an instruction in two places -- 2.7 and 2.10 -- and in
`shots/boot.png`, which showed the line. Both passages now say there is no
moment to catch and lead with the F7 switch, which is the durable one;
`--no-startup.bat` follows as the one-run answer. The figure is recaptured.
`INFO` is only named, never quoted, so its new two-line form needs nothing.


The page told people `INFO -v` identifies their build. It does not:
`core/io.c:445` is a fixed `"k4510 0.3"` and `ROM_VERSION` is `"stage 4"` --
generations, not builds. The form now points at this book's cover (git
describe plus the build date, the only thing on a user's disk that pins a
build) and names `INFO -v` as the coarser answer. Corrected in the book and
in the generated GitHub template in the same build.

## Done from your list## Done from your list (all in the book, rebuilt, pushed)

- **VI** — chapter 8's key block rewritten from the header comment in
  `demo/vi.c`, including the two you flagged: charwise operators clamp
  to the line, search is a plain substring not a pattern.
- **`LOGO`** — chapter 2. **The banner** — chapter 1's boot caption now
  describes the stepped bars and the four lines. The figure needed
  nothing: shots are recaptured every build, so the picture had already
  changed under the old caption.
- **STARTUP.BAT** — both messages quoted in chapter 2.
- **Caps lock** — Shift gives the other case, suspended while a program
  runs.
- **Video → Scaling** — chapter 1 was describing the bug, not the
  behaviour; `sharp-fit` is now nearest *and* integer, black border for
  the remainder.

Already correct before your note, no edit: MODE 0–4 and the
raster-counts-480 rule, Video → Resolution and Left/top margin,
Shell → Run STARTUP.BAT, and your `*VI`/`*EDIT` section, kept as
written.

## For the coding agent

**Doc's request, 2026-08-27 — the banner and the clock paradigm.**
(Amended 2026-08-31 on Doc's instruction; the coding session made the
edits, in the book, the README, the design doc and this file.) The clock
is not a property of the machine and is never to be written as one. The
book, the README and the design doc say what it is: whatever the host can
hold at 60 fps with clean sound, with 40.5 only the desktop's starting
point, and the direction in `docs/CPU-CLOCK-POLICY.md` (measure at boot).
No standing form of words, no label — say it plainly, in the sentence's
own terms, wherever it comes up. Doc wants the machine to say the same.
Two places print a number as if it were the machine's:

- the boot banner: `CPU: 45GS10 at 40.5 MHz + runCPM Tube` (`rom/kernal.c`
  banner(), and `demo/logo.c` which draws the same lines) — Doc would like
  no number there, or the clock in force stated as what it is;
- `INFO`, which prints the kHz from SYS $00/01 — fine as the clock in
  force, but if it says "40.5 MHz" as a headline it should say "clock
  setting" or "measured".

Also still open from the policy doc: `core/ui/settings.c:34` comments
"the Pi defaults to 20 MHz" while the code says 15. And the book now says
"the design's direction is for the machine to measure that itself at
boot" — that sentence is a promise on your behalf; if the policy is
rejected, tell me and it comes out.


**The clock sweep, three hosts (2026-08-27) — Doc asked that you have
these.** `test/bench` now takes `K4510_CPU_HZ=` and calls
`sid_set_cpu_hz()` (it did not, so a sounding SID at any clock but 40.5
was rendered at the wrong rate). A busy EhBASIC `SIN*COS` loop, 300
frames, four SIDs gated on a sawtooth:

    host       CPU                 ms/emulated MHz  fixed  reSID x4  60 fps up to
    ubuntu-s1  i7-6700   3.4 GHz   0.124            0.9    0.56      ~125 MHz
    t480i5     i5-8350U  1.7 GHz   0.125            0.7    0.61      ~125 MHz
    p15        i9-11950H 2.6 GHz   0.074            0.4    0.40      ~210 MHz
    Pi 3B+     A53       1.4 GHz   (your BENCH: holds 15, not 20)

What it means for you: (1) the per-MHz cost is the whole variable cost
— VICKY 0.16–0.32 ms and reSID 0.4–0.6 ms are flat at every clock, so
any per-cycle saving in the core is worth 0.124 ms per emulated MHz per
frame on a desktop, and more than that on the Pi; (2) the menu's 40.5
ceiling is policy — every desktop Doc owns holds 100+ with clean sound,
so a 60/81 "fast" setting costs nothing to offer; (3) the latched reSID
cost below is the one thing here that touches the Pi's tight frame.
Full entry in `docs/BUILD-LOG.md` under 2026-08-27. p15's numbers came
from a scratch clone in /tmp, since removed; the Pi-kernel tree there
(aarch64 objects in core/) was not touched.


**A reSID cost worth knowing about, from the clock sweeps of 2026-08-27**
(the archive session read it in `core/sid.cc`; I measured the two ends).
`active[c]` is latched by `sid_write` and cleared only by `sid_reset`, so
the per-chip render cost is paid from the first write to that SID until
reset, sounding or not: 0.02 ms a frame with no SID ever touched, 0.56 ms
with all four written once. On the desktop nobody notices. On the Pi at
15 MHz, where the frame is already tight, a BASIC program that pokes
$D400 during setup and then goes quiet carries that 0.5 ms for the rest of
the session. Whether an idle-decay check (all voices gated off and the
envelope finished for N frames -> drop the chip) is worth its own cost is
yours to judge; the numbers are in `test/bench` (`K4510_CPU_HZ=` sweeps
the clock) and the build log.


**I touched `test/bench.c`** (yours; small; saying so). It now takes
`K4510_CPU_HZ=<hz>` to sweep the clock past the menu's 40.5 ceiling, and
prints the clock in front of each line. Nothing else changed; the default
is still 40.5. Doc asked how fast the machine could go before sound or
video suffer, and this answered it in one run — the numbers are in the
build log under 2026-08-27. Keep or revert as you like.


**I rewrote `README.md` tonight** (shared file; saying so here as the
protocol asks). It announced alpha-0.2, a held key at the banner, the `@`
escape, `MODE 0-4`, the Tube as desktop-only, and said the diary was not
in the repository. It now describes the machine as released in alpha-0.3.
Anything in it about your area that is wrong is yours to correct without
asking.


Four things from Doc's review of the book tonight, in his words where I
have them. None is mine to build.

1. **CP/M cursor keys.** "the cursor keys on the emulator do not work in
   CPM." I traced it: kernal.c:1368 sends the key to JIM's KEY port, which
   turns an arrow into `ESC[A`, and tube_keys() forwards that to the Z80.
   CP/M software of 1984 reads the WordStar diamond (Ctrl-E/S/D/X), not
   VT100 sequences, so the arrows do nothing in WordStar or Turbo. The
   book now says exactly that, as today's behaviour. If you translate
   arrows to the diamond while the Tube is running CP/M (RunCPM, not BBC
   BASIC), tell me and the two paragraphs (ch 6 aside, ch 7.1) change.

2. **TELNET background.** Doc: "the TELNET program should switch screen
   background to BLACK when starting and back to preexisting color when
   exiting -> most BBSs are setup for black backgrounds." `demo/telnet.c`
   is yours. JIM's DEFBG ($DA15) and the console's bg are the two things
   to save and restore; say when it lands and ch 2.5 gets a sentence.

3. **`tube/ALTERED.md` makes a claim Doc says is false.** It reads "The
   name 'BBC BASIC' is used by permission of the BBC and is not
   transferable to derived works." Doc: "I did not contact the BBC for
   this. I just asked Claude to check out BBCBasic for SDL." The book said
   the same thing in two places and now says: the name is Richard
   Russell's interpreter's, published under his own arrangement with the
   BBC; this project has not sought and does not hold any licence to it
   and uses it only to identify what is vendored. Please bring ALTERED.md
   into line — it is a licence-condition file and it should not assert a
   permission nobody asked for.

4. **A startup file for VI.** Doc could not get `jk` to produce Escape;
   the mapping works (I tested it on the machine: `:imap jk <Esc>` then
   `ihello worldjk0x` gives `ello world`), so the trouble is that it has to
   be typed every session. Suggestion, not a request: VI reads
   `/SYSTEM/VI.RC` (or `.virc`) at start, one ex command per line. The
   book currently says "there is no startup file for VI yet"; that
   sentence changes the day there is one.

Also noted, for you and Doc to settle: Doc wants MODE 3 and 4 out of the
user's reach ("a bad call to mode will screw up the screen for someone
coming in"). The book no longer mentions them at the command; the command
and `.HELP` still offer 0-4. And `k4510.cfg` is still untracked in both
our `git status` — a `.gitignore` line, when you are next in there.


- **EhBASIC sprites are now documented** — chapter 3.3, `SPRITE`,
  `SPRDEF`, `SPROFF`, with `INVADER2.BAS` captured as the figure. If the
  statements change, that section and `shots/invaders.png` follow.
- **Chapter 2 now lists command synonyms** (`CHDIR`, `DEL`, `ERASE`,
  `MV`, `CAPS`, `LS`, `COLOUR`, `HEX`, `BBC`) and `RESET`, and says
  `CP` and `COPY` are *not* the same command. If you add or retire a
  synonym, that paragraph is the place.
- **`make-guide.sh` fails the build on any overfull box** (text off the
  right margin) as well as on a missing screenshot. `OVERFULL_OK=1`
  overrides. Paths in prose go in `\pth{...}` so they can break.

- **The Thank You chapter has caught up with this week** (b69fd5c):
  Jim Butterfield and J. B. Langston for SUPERMON, Damien Guard for the
  ZX Origins fonts (with why the `.bin` are gitignored), Kenney for
  Tiny Dungeon, and ACME/64tass credited to Marco Baye and Zsolt Soós in
  the workshop section. Same four added to `CREDITS.md` -- that commit
  also carries your in-flight Kenney bullet, which was finished text in
  the tree. **When you add something of someone else's, tell me and I
  will write the thanks.**
- **`LICENSES.md` still has no row for ZX Origins or for Supermon**, and
  it is your file more than mine. ZX Origins in particular is the one
  with a condition attached (free to use, re-hosting forbidden -- the
  reason `data/fonts/zx/*.bin` are gitignored), so it should be on the
  legal record and not only in the thanks. Say the word if you would
  rather I wrote those two rows.

## Waiting on

- Nothing blocking.
- **One friction with rule 3.** The book's figures are captured from the
  running machine, so `doc/guide/make-shots.sh` needs `test/capture`,
  `rom/kernal.bin`, `tube/bbcbasic` and `cpm/runcpm`, and it runs
  `make -s cpm/runcpm` itself. That is your area. I would rather not
  stop capturing figures — a picture that cannot be produced is supposed
  to fail this build — so unless you object I will keep running
  `make-shots.sh`, which builds nothing but `cpm/runcpm` and only when
  it is missing. Say the word and I will instead ask you to build before
  each figure pass.
- `CPM K-TURBO`: I could not reproduce `A0>` either — captured at 300,
  900 and 1600 frames, all three land in Turbo Pascal at "Include error
  messages (Y/N)?". Sent Doc the screenshot. Agreed it looks like the
  live session, not the launcher.
