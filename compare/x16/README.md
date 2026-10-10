# The K4510 against the Commander X16

The same small programs on two machines that never met: the
K4510 (this repo's emulator, a 45GS02 at 40.5 MHz, VICKY
video, MELODY sound) and the Commander X16 (x16emu r49, a
65C02 at 8 MHz, VERA video and sound, YM2151). Both run
headless on ubuntu-s1 from one script, and every figure is
read off the machine's own clock, never the host's.

The point is not a winner. The point is to see, with real
numbers, what each design choice buys: a compiled BASIC
against an interpreted one, 40 MHz against 8, a blitter
against a software line routine, and where the X16 is
simply faster or simpler.

    compare/x16/run.py              runs everything, writes RESULTS.md
    compare/x16/run.py --only rf1,sieve,csieve
    compare/x16/run.py --x16 DIR    where x16emu and rom.bin are

## What is here

| Path | What |
|---|---|
| `x16/*.BAS` | the X16 BASIC programs (CBM BASIC V2 + X16 words, numbered lines, TI for time) |
| `fs/LANG/BASIC/EX/X16/*.BAS` | their K4510 BASIC twins for the new programs, on the machine's disk where a user finds them (`CD /LANG/BASIC/EX/X16`, then `FILL`) |
| `fs/LANG/BASIC/EX/RF1..RF8, SIEVE, DROGON` | the K4510 twins that already existed: the Rugg/Feldman set, the Byte Sieve, Henderson's Mandelbrot |
| `c/*.c` + `c/bench.h` | the C programs, one source built for both machines |
| `run.py` | builds, runs both machines, writes `RESULTS.md` |
| `RESULTS.md` | the last table written here |
| `build/` | binaries, raw text output, screenshots and the X16's chord as `.wav` (not tracked) |

## Three tracks, and what each one measures

**Track 1, each machine's own BASIC, as a user would write
it.** Rugg/Feldman benchmarks 1-8 (Kilobaud/PCW 1977), the
Byte Sieve (1981), Gordon Henderson's text Mandelbrot, and a
string-handling program (build, reverse, scan, STR$/VAL).
This measures *the BASIC the machine hands you*: on the X16
an interpreter, on the K4510 a compiler. It does not measure
the CPUs, and a ratio of 50x here says nothing about the
silicon. It says what a newcomer typing the same program
gets back.

**Track 2, the same compiled code.** Three C programs (the
sieve, a tight 16-bit integer loop, a memory copy) built by
cc65 for both: `-t cx16` for the X16 and the K4510's own
recipe (`cc65 -t none --cpu 65c02`, what `tools/k4510-cc`
and `CC` run). Same compiler, same 65C02 instruction set,
same library `memcpy`. The K4510 runs twice: at its 40.5 MHz
and at 8 MHz (`K4510_CPU_HZ=8000000` to the headless
harness), so the second column meets the X16 clock for
clock. That column is the nearest thing here to a CPU
comparison. Note that the K4510 run is the 45GS02 executing
65C02 code; nothing uses its 32-bit or Q registers.

**Track 3, graphics and sound, by each machine's natural
means.** Full-screen fills, 500 lines and 5000 points,
sixteen 16x16 sprites moved a hundred times, and a C major
chord. X16 BASIC's `RECT`, `LINE`, `PSET`, `SPRITE`/`MOVSPR`,
`FMCHORD` and `PSGCHORD` against K4510 BASIC's `BOX`, `LINE`,
`PLOT`, `SPRDEF`/`MOVSPR` and `PLAY`. The X16 draws lines in
KERNAL software over VERA; the K4510's `BOX` and `LINE` are
one blitter operation each. Both are driven from BASIC, so
the interpreter's own speed is inside every X16 figure, and
`SPRITES` in particular measures how fast BASIC can issue
1600 `MOVSPR`s, not how fast the hardware can move pixels.
The chord is not timed: a chord is not a race. The X16's is
recorded to a `.wav` (`-wav ...,auto`) and checked for not
being silence; the K4510 harness has no audio device, so its
`CHORD` is checked by running.

## The clocks

| | X16 | K4510 |
|---|---|---|
| CPU | 65C02, 8 MHz | 45GS02 (via Xemu's core), 40.5 MHz default; 10 MHz is the settings menu's floor; 8 MHz here only through the harness |
| Time | `TI`, jiffies at 60 Hz (cc65's `clock()` reads the same) | `FRAMES` in BASIC, `SYS+$0D` in C: the 60 Hz frame counter |
| Resolution | 1/60 s | 1/60 s |
| Host turbo | never: no `-warp` while timing | the harness runs as fast as it can, but counts frames, not seconds |

The Rugg/Feldman programs finish in a few frames on the
K4510, so the K4510 versions run them a hundred times and
divide (that is why they report `THIS MACHINE, ONE RUN`).
The X16 runs each once. Both report seconds per run.

## Fairness, spelled out

- **Compiled against interpreted (track 1).** K4510 BASIC
  compiles through Mad Pascal; X16 BASIC walks tokens. That
  is the comparison a user meets, and it is not a CPU test.
- **40.5 against 8 (every track).** Five times the clock
  before anything else. Track 2's 8 MHz column removes it.
- **Floating point.** X16 BASIC's floats are the C64's 40-bit
  format in software; K4510 BASIC's are 32-bit singles (also
  software on a 6502-family CPU). RF8 and Mandelbrot lean on
  this; the K4510 shows six figures, the X16 nine.
- **Integer arrays.** The Sieve uses `F%()` on the X16 (a
  float array of 8191 would not fit in 38 KB) and `flags%()`
  on the K4510. CBM BASIC converts integer-array elements
  to float and back on every access; that is the X16's cost
  to bear, and it is what a user would write.
- **String garbage.** The X16 collects CBM BASIC's string
  garbage; the K4510 keeps strings in 256-byte slots in far
  memory with no collector. STRINGS measures both as found.
- **Lines, boxes, points.** VERA has no blitter; the X16
  KERNAL draws lines a pixel at a time through the data
  port. VICKY has one. That is the architecture, not a trick,
  but remember it when reading FILL and LINES.
- **An interpreted BASIC on the K4510?** There is none that
  is a fair match. EhBASIC, the machine's old interpreter,
  was retired on 2026-10-09. BBC BASIC runs on the Tube
  co-processor, which is a host process at host speed, so
  its times mean nothing against a machine clock. RX is an
  interpreter but not BASIC. So track 1 stays as it is:
  each machine's own BASIC, and the caveat above.
- **Emulator against emulator.** x16emu is cycle-counted at
  8 MHz with no VERA wait states; the K4510 steps its CPU a
  scanline's worth of cycles at a time. Both are models.
  Neither is a board.

## What the first run found (2026-10-09)

RESULTS.md has the table; the handbook appendix
(`doc/guide/chapters/a5-x16.tex`) discusses it. In short:

- Track 1: 23x to 156x on the loop benchmarks, 124x on the
  Sieve, 52x on Mandelbrot: a compiler against an
  interpreter. The X16's own figures are a C64's at eight
  times the clock, which is what it is.
- **STRINGS is a near draw** (8.4 s against 7.9 s). K4510
  BASIC keeps every string value in a 256-byte far-memory
  slot copied by DMA, so each `+`, `MID$`, `LEFT$` is a block
  copy and a runtime call. The one place the X16 nearly
  wins, and the place K4510 BASIC could improve.
- Track 2, clock for clock: the K4510 at 8 MHz runs the same
  cc65 code in 1.2-1.3x less time than the X16. That is the
  4510 family's shorter cycle counts, as the two emulators
  model them. The rest of the 40.5 MHz column is the clock.
- Track 3: FILL 711x and LINES 203x are the blitter; POINTS
  28x and SPRITES 36x are back to the BASIC ratio, because a
  point or a sprite move is one register write on both.
- Both machines gave the same answers (1899 primes, the same
  checksums), which is the first thing to check of any
  benchmark.

## Where x16emu comes from

`run.py` looks for a folder holding `x16emu` and `rom.bin`:
`--x16 DIR`, then `X16EMU_DIR`, then
`~/Projects/K4510-Personalities/work/x16emu-r49-official`
(the official r49 Linux release zip, unpacked; this is what
the figures in RESULTS.md used), then the Personalities
project's own build in `work/stage/x16`. That build carries
the K4510's F12-menu and placement patches, which do not
touch timing, but the official binary is the cleaner
reference. The same emulator is the Dell's X16 personality.

x16emu is driven with `-bas FILE -run -echo` (the program is
typed in through the keyboard, then RUN; everything the
KERNAL prints also goes to stdout) on SDL's dummy video and
audio drivers. The script reads stdout until the program
prints `DONE`, then stops the emulator. `-gif FILE,wait` and
`POKE $9FB5,1` take one frame for the record; `-wav
FILE,auto` records the chord. The C programs load with
`-prg FILE -run`.

The K4510 is driven by `test/headless` (types keys, runs
until a marker appears on the text screen, prints the
screen) and `test/capture` (a PNG after N frames), as
`test/benchmarks.sh` and the other tests do.

## Reading the programs

They are meant to be read as teaching examples, side by
side: `x16/SIEVE.BAS` next to `fs/LANG/BASIC/EX/SIEVE.BAS`,
`x16/FILL.BAS` next to `fs/LANG/BASIC/EX/X16/FILL.BAS`. The
algorithm is the same line for line where the dialects
allow; what differs is the dialect (line numbers and `GOTO`
against `DO`/`LOOP` and labels), the clock, and the names of
the drawing words. Mandelbrot is Henderson's listing
unchanged on both machines, as his benchmark asks.
