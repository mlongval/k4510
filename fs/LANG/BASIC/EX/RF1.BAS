' Rugg/Feldman benchmark 1 (Kilobaud/PCW, 1977) -- K4510 BASIC
' The 1977 program, compiled.  It is over almost before the frame
' counter moves, so it runs 100 times and the time is divided.
' EhBASIC runs the same program as /LANG/EHBASIC/EX/RF1.BAS.
CONST RUNS = 100
CLS
PRINT "RUGG/FELDMAN BENCHMARK 1 (1977), COMPILED"
PRINT "1000 empty FOR/NEXT loops: the bare cost of a loop"
PRINT "(run"; RUNS; "times over, the time divided)"
PRINT
t = PEEK(54541) + 256 * PEEK(54542)   ' the 60 Hz frame counter
FOR r% = 1 TO RUNS
  FOR k = 1 TO 1000
  NEXT k
NEXT r%
t = PEEK(54541) + 256 * PEEK(54542) - t
IF t < 0 THEN t = t + 65536
s = t / 60 / RUNS
PRINT "THIS MACHINE, ONE RUN: "; STR$(INT(s * 10000 + 0.5) / 10000); " S"
PRINT
PRINT "THE SAME PROGRAM ON (STOPWATCH-ERA FIGURES, APPROXIMATE):"
PRINT "  C64 AND APPLE IIE (1 MHZ, MS BASIC)   1.2 S"
PRINT "  BBC MICRO B (2 MHZ, BBC BASIC)        0.8 S"
PRINT "  EHBASIC ON THIS MACHINE: RUN RF1.BAS IN /LANG/EHBASIC/EX"
PRINT
PRINT "ANY KEY...";
DO: GET k$: LOOP UNTIL k$ <> ""
END
