' Rugg/Feldman benchmark 2 (Kilobaud/PCW, 1977) -- K4510 BASIC
' The 1977 program, compiled.  It is over almost before the frame
' counter moves, so it runs 100 times and the time is divided.
CONST RUNS = 100
CLS
PRINT "RUGG/FELDMAN BENCHMARK 2 (1977), COMPILED"
PRINT "1000 loops built from IF/GOTO"
PRINT "(run"; RUNS; "times over, the time divided)"
PRINT
t = PEEK(54541) + 256 * PEEK(54542)   ' the 60 Hz frame counter
FOR r% = 1 TO RUNS
  k = 0
Loop2:
  k = k + 1
  IF k < 1000 THEN GOTO Loop2
NEXT r%
t = PEEK(54541) + 256 * PEEK(54542) - t
IF t < 0 THEN t = t + 65536
s = t / 60 / RUNS
PRINT "THIS MACHINE, ONE RUN: "; STR$(INT(s * 10000 + 0.5) / 10000); " S"
PRINT
PRINT "THE SAME PROGRAM ON (STOPWATCH-ERA FIGURES, APPROXIMATE):"
PRINT "  C64 AND APPLE IIE (1 MHZ, MS BASIC)   9.3 S"
PRINT "  BBC MICRO B (2 MHZ, BBC BASIC)        3.1 S"
PRINT
PRINT "ANY KEY...";
DO: GET k$: LOOP UNTIL k$ <> ""
END
