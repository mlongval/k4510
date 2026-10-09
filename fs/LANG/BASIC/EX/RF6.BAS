' Rugg/Feldman benchmark 6 (Kilobaud/PCW, 1977) -- K4510 BASIC
' The 1977 program, compiled.  It is over almost before the frame
' counter moves, so it runs 100 times and the time is divided.
CONST RUNS = 100
DIM m(5)
CLS
PRINT "RUGG/FELDMAN BENCHMARK 6 (1977), COMPILED"
PRINT "as 5, plus an inner FOR l=1 TO 5 loop"
PRINT "(run"; RUNS; "times over, the time divided)"
PRINT
t = PEEK(54541) + 256 * PEEK(54542)   ' the 60 Hz frame counter
FOR r% = 1 TO RUNS
  k = 0
Loop6:
  k = k + 1
  a = k / 2 * 3 + 4 - 5
  GOSUB Nothing
  FOR l = 1 TO 5
  NEXT l
  IF k < 1000 THEN GOTO Loop6
NEXT r%
t = PEEK(54541) + 256 * PEEK(54542) - t
IF t < 0 THEN t = t + 65536
s = t / 60 / RUNS
PRINT "THIS MACHINE, ONE RUN: "; STR$(INT(s * 10000 + 0.5) / 10000); " S"
PRINT
PRINT "THE SAME PROGRAM ON (STOPWATCH-ERA FIGURES, APPROXIMATE):"
PRINT "  C64 AND APPLE IIE (1 MHZ, MS BASIC)   29.5 S"
PRINT "  BBC MICRO B (2 MHZ, BBC BASIC)        13.9 S"
PRINT
PRINT "ANY KEY...";
DO: GET k$: LOOP UNTIL k$ <> ""
END
Nothing:
  RETURN
