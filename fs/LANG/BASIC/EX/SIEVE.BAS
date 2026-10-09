' The BYTE sieve (1981), full size -- K4510 BASIC
' EhBASIC could not hold DIM FLAGS(8191) (8192 six-byte floats) and
' poked bytes through a bank window instead.
' Compiled, a whole-number array is two bytes an element: 16 KB fits.
CONST SIZE = 8190
DIM flags%(SIZE)
CLS
PRINT "BYTE SIEVE, 8191 FLAGS IN A WHOLE-NUMBER ARRAY, COMPILED"
t = PEEK(54541) + 256 * PEEK(54542)
count% = 0
FOR i% = 0 TO SIZE: flags%(i%) = 1: NEXT
FOR i% = 0 TO SIZE
  IF flags%(i%) THEN
    prime% = i% + i% + 3
    k% = i% + prime%
    WHILE k% <= SIZE
      flags%(k%) = 0
      k% = k% + prime%
    WEND
    count% = count% + 1
  END IF
NEXT
t = PEEK(54541) + 256 * PEEK(54542) - t
IF t < 0 THEN t = t + 65536
PRINT count%; " PRIMES"
PRINT "TIME:"; t; "FRAMES ="; INT(t / 0.6) / 100; "SECONDS  (ONE ITERATION)"
PRINT "FOR SCALE: A STOCK C64 NEEDS SEVERAL MINUTES FOR ONE ITERATION;"
PRINT "THE C VERSION IS SIEVE.C IN /LANG/C: CC SIEVE THERE, THEN SIEVE."
PRINT
PRINT "ANY KEY...";
DO: GET k$: LOOP UNTIL k$ <> ""
