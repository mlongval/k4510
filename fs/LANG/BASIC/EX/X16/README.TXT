K4510 versus Commander X16: the comparison programs
====================================================

These are the K4510 halves of a set that runs the same work
on this machine and on the Commander X16 (x16emu r49), each
in its own BASIC.  The X16 halves, the C programs, the run
script and the results are in the repo: compare/x16/.

Here:   STRINGS  FILL  LINES  SPRITES  CHORD
Up one level, already part of the examples:
        RF1..RF8 (Rugg/Feldman 1977), SIEVE (Byte 1981),
        DROGON (Gordon Henderson's Mandelbrot)

Each prints a line  RESULT NAME x.xx S  timed by the 60 Hz
frame counter (FRAMES), then waits for a key.  Run one with
RUN: CD /LANG/BASIC/EX/X16 then RUN FILL (FILL alone is the
shell's own word, the memory fill).

Fairness, in one breath: K4510 BASIC is compiled (Mad
Pascal, 45GS02 at 40.5 MHz); X16 BASIC is interpreted
(65C02 at 8 MHz).  The numbers compare machines as a
user meets them, not CPUs.  compare/x16/README.md says more.
