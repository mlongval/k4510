# K4510 vs Commander X16 -- results

Written by `compare/x16/run.py` on 2026-10-10, K4510 f7858c4, x16emu r49,
cc65 V2.18 - Ubuntu 2.19-1. Seconds of each machine's own clock (60 Hz ticks: TI on
the X16, FRAMES on the K4510). Read README.md before comparing
anything: the two BASICs are not the same kind of thing.

## Track 1: each machine's own BASIC

X16 BASIC is interpreted (CBM BASIC V2 + X16 words, 65C02 at
8 MHz). K4510 BASIC is compiled to Mad Pascal (45GS02 at 40.5 MHz).

| Program | What | X16 (s) | K4510 (s) | X16 / K4510 |
|---|---|---:|---:|---:|
| RF1 | Rugg/Feldman 1: 1000 empty FOR/NEXT | 0.18 | 0.0080 | 22.9x |
| RF2 | RF 2: 1000 turns, K=K+1 / IF ... GOTO | 1.20 | 0.0077 | 155.8x |
| RF3 | RF 3: + A=K/K*K+K-K | 2.22 | 0.0307 | 72.2x |
| RF4 | RF 4: + A=K/2*3+4-5 | 2.43 | 0.0298 | 81.7x |
| RF5 | RF 5: + GOSUB | 2.67 | 0.0317 | 84.1x |
| RF6 | RF 6: + FOR L=1 TO 5 | 4.10 | 0.0758 | 54.1x |
| RF7 | RF 7: + M(L)=A | 6.55 | 0.16 | 41.1x |
| RF8 | RF 8: 100 x K^2, LOG, SIN | 1.00 | 0.0067 | 149.3x |
| SIEVE | Byte Sieve, 8191 flags, once | 39.40 | 0.32 | 124.4x |
| MANDEL | Henderson's text Mandelbrot | 118.08 | 2.25 | 52.5x |
| STRINGS | strings: build, reverse, scan, STR$/VAL | 8.38 | 7.88 | 1.1x |

## Track 2: the same C, built with cc65 for both

One source each (`c/`), `cl65 -t cx16 -O` for the X16 and the
K4510's own recipe (`cc65 -t none --cpu 65c02 -O`, tools/k4510-cc).
The K4510 column at 8 MHz is the harness run with K4510_CPU_HZ=8000000:
the same clock as the X16, so the CPUs meet like for like.

| Program | X16 8 MHz (s) | K4510 40.5 MHz (s) | K4510 8 MHz (s) | X16 / K4510@40.5 | X16 / K4510@8 |
|---|---:|---:|---:|---:|---:|
| CSIEVE | 5.57 | 0.88 | 4.43 | 6.3x | 1.3x |
| CLOOP | 5.83 | 0.87 | 4.35 | 6.7x | 1.3x |
| CBYTECOPY | 4.53 | 0.72 | 3.65 | 6.3x | 1.2x |
| CMEMCPY | 2.00 | 0.32 | 1.65 | 6.2x | 1.2x |

## Track 3: graphics and sound, from BASIC

VERA from X16 BASIC (RECT, LINE, PSET, SPRITE/MOVSPR, FMCHORD,
PSGCHORD) against VICKY and MELODY from K4510 BASIC (BOX, LINE,
PLOT, SPRDEF/MOVSPR, PLAY). The BASIC's own speed is part of
every figure here.

| Program | What | X16 (s) | K4510 (s) | X16 / K4510 |
|---|---|---:|---:|---:|
| FILL | 200 full-screen fills, 320x240 | 11.85 | 0.0167 | 711.0x |
| LINES | 500 lines (LINES) and 5000 points (POINTS) | 6.77 | 0.0333 | 203.0x |
| POINTS | 500 lines (LINES) and 5000 points (POINTS) | 9.97 | 0.35 | 28.5x |
| SPRITES | 16 sprites x 100 MOVSPRs | 4.83 | 0.13 | 36.3x |
| CHORD | a C major chord, not timed | OK | OK | -- |

## Track 4: graphics in C, each machine's own means

`c/gfx.c`, one source, each drawing word written the way that
machine does it best from C (`c/gfx.h`): the X16 KERNAL's GRAPH
routines and VERA's data port; the K4510's blitter, DMA and far
pokes. Milliseconds for one pass; a test repeats its pass for two
seconds of the machine's clock. The K4510's blitter and DMA finish
in the write that starts them, so its figures are the CPU setting
registers: read README.md before quoting CLEAR or SCROLL.

| Test | One pass | X16 8 MHz (ms) | K4510 40.5 MHz (ms) | K4510 8 MHz (ms) | X16 / K4510@40.5 | X16 / K4510@8 |
|---|---|---:|---:|---:|---:|---:|
| CLEAR | the whole 320x240 bitmap filled | 55.0 | 0.0561 | 0.28 | 979.6x | 193.5x |
| RECTS | 64 filled rectangles, 1-128 x 1-96 | 198.5 | 3.21 | 16.1 | 61.9x | 12.3x |
| LINES | 64 lines, ends anywhere | 268.8 | 3.95 | 20.0 | 68.0x | 13.4x |
| PIXELS | 1024 single pixels | 108.8 | 21.3 | 107.9 | 5.1x | 1.0x |
| IMAGE | 16 pictures of 32x32 from memory | 55.4 | 1.13 | 5.71 | 49.0x | 9.7x |
| SCROLL | the whole bitmap up one line | 99.2 | 0.0682 | 0.35 | 1454.6x | 287.2x |
| TEXT | the 40x30 text layer written whole | 23.7 | 12.1 | 61.1 | 2.0x | 0.4x |
| SPRITES | 32 sprites, each moved once | 5.63 | 1.09 | 5.52 | 5.1x | 1.0x |
| PALETTE | 240 palette entries | 5.17 | 1.02 | 5.15 | 5.1x | 1.0x |

Test cards in `build/shots/`: x16-gfx.png, k4510-gfx.png.

Sound check: x16 CHORD: x16-chord.wav, peak 22403 of 32767 (492238 samples). The K4510 harness has no audio device;
its CHORD is checked by running, not by listening.

Screenshots in `build/shots/` (not tracked): fill (x16-fill.png, k4510-fill.png), lines (x16-lines.png, k4510-lines.png), sprites (x16-sprites.png, k4510-sprites.png).
