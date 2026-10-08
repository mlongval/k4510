' Ahl's Simple Benchmark (Creative Computing, 1983) -- K4510 BASIC
' His lines 10-110, compiled; the clock and the report are ours.
CLS
PRINT "AHL'S SIMPLE BENCHMARK (CREATIVE COMPUTING, 1983), COMPILED"
PRINT "100 ROUNDS OF TEN SQR(A) THEN TEN A^2, PLUS RANDOM SUMS."
PRINT
t = PEEK(54541) + 256 * PEEK(54542)
' --- Ahl's benchmark
FOR n = 1 TO 100: a = n
  FOR i = 1 TO 10
    a = SQR(a): r = r + RND(1)
  NEXT i
  FOR i = 1 TO 10
    a = a ^ 2: r = r + RND(1)
  NEXT i
  s = s + a
NEXT n
ac = ABS(1010 - s / 5)
ra = ABS(1000 - r)
' --- the clock
t = PEEK(54541) + 256 * PEEK(54542) - t
IF t < 0 THEN t = t + 65536
PRINT "THIS MACHINE: "; STR$(INT(t / 0.6 + 0.5) / 100); " SECONDS"
PRINT "ACCURACY "; STR$(ac); "  (HOW FAR TEN SQR THEN TEN ^2 DRIFT: SMALLER IS BETTER)"
PRINT "RANDOM "; STR$(ra); "  (SPREAD OF THE RANDOM SUMS: UNDER 15 IS GOOD)"
PRINT
PRINT "THE SAME PROGRAM ON (FROM THE MARCH 1984 ARTICLE):"
PRINT "  C64 AND APPLE IIE (1 MHZ, MS BASIC)   1:53"
PRINT "  TRS-80 MODEL III                      1:59"
PRINT "  IBM PC / COMPILED                     0:24 / 0:06"
PRINT
PRINT "ANY KEY...";
DO: GET k$: LOOP UNTIL k$ <> ""
