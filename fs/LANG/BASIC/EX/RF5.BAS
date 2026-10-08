' Rugg/Feldman benchmark 5 (Kilobaud/PCW, 1977) -- K4510 BASIC
' The 1977 program, compiled.  It is over almost before the frame
' counter moves, so it runs 100 times and the time is divided.
' EhBASIC runs the same program as /LANG/EHBASIC/EX/RF5.BAS.
CONST RUNS = 100
CLS
PRINT "RUGG/FELDMAN BENCHMARK 5 (1977), COMPILED"
PRINT "as 4, plus a GOSUB/RETURN every time round"
PRINT "(run"; RUNS; "times over, the time divided)"
PRINT
t = PEEK(54541) + 256 * PEEK(54542)   ' the 60 Hz frame counter
FOR r% = 1 TO RUNS
  k = 0
Loop5:
  k = k + 1
  a = k / 2 * 3 + 4 - 5
  GOSUB Nothing
  IF k < 1000 THEN GOTO Loop5
NEXT r%
t = PEEK(54541) + 256 * PEEK(54542) - t
IF t < 0 THEN t = t + 65536
s = t / 60 / RUNS
PRINT "THIS MACHINE, ONE RUN: "; STR$(INT(s * 10000 + 0.5) / 10000); " S"
PRINT
PRINT "THE SAME PROGRAM ON (STOPWATCH-ERA FIGURES, APPROXIMATE):"
PRINT "  C64 AND APPLE IIE (1 MHZ, MS BASIC)   21.0 S"
PRINT "  BBC MICRO B (2 MHZ, BBC BASIC)        9.0 S"
PRINT "  EHBASIC ON THIS MACHINE: RUN RF5.BAS IN /LANG/EHBASIC/EX"
PRINT
PRINT "ANY KEY...";
DO: GET k$: LOOP UNTIL k$ <> ""
END
Nothing:
  RETURN
