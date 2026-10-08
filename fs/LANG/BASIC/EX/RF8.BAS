' Rugg/Feldman benchmark 8 (Kilobaud/PCW, 1977) -- K4510 BASIC
' The 1977 program, compiled.  It is over almost before the frame
' counter moves, so it runs 10 times and the time is divided.
' EhBASIC runs the same program as /LANG/EHBASIC/EX/RF8.BAS.
CONST RUNS = 10
CLS
PRINT "RUGG/FELDMAN BENCHMARK 8 (1977), COMPILED"
PRINT "100 loops of k^2, LOG(k), SIN(k): transcendental functions"
PRINT "(run"; RUNS; "times over, the time divided)"
PRINT
t = PEEK(54541) + 256 * PEEK(54542)   ' the 60 Hz frame counter
FOR r% = 1 TO RUNS
  k = 0
Loop8:
  k = k + 1
  a = k ^ 2
  b = LOG(k)
  c = SIN(k)
  IF k < 100 THEN GOTO Loop8
NEXT r%
t = PEEK(54541) + 256 * PEEK(54542) - t
IF t < 0 THEN t = t + 65536
s = t / 60 / RUNS
PRINT "THIS MACHINE, ONE RUN: "; STR$(INT(s * 10000 + 0.5) / 10000); " S"
PRINT
PRINT "THE SAME PROGRAM ON (STOPWATCH-ERA FIGURES, APPROXIMATE):"
PRINT "  C64 AND APPLE IIE (1 MHZ, MS BASIC)   11.9 S"
PRINT "  BBC MICRO B (2 MHZ, BBC BASIC)        5.1 S"
PRINT "  EHBASIC ON THIS MACHINE: RUN RF8.BAS IN /LANG/EHBASIC/EX"
PRINT
PRINT "ANY KEY...";
DO: GET k$: LOOP UNTIL k$ <> ""
END
