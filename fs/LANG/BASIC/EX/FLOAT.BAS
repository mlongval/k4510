' Float benchmark: this machine's own, no historical peer -- K4510 BASIC
' The same sum as /LANG/EHBASIC/EX/FLOAT.BAS, compiled: Mad Pascal's
' single-precision floats instead of EhBASIC's MATH-unit ones.
CLS
PRINT "FLOAT: 5000 ROUNDS OF A=A+SQR(I)*1.5/3+SIN(I/100), COMPILED"
PRINT
t = PEEK(54541) + 256 * PEEK(54542)
a = 0
FOR i = 1 TO 5000
  a = a + SQR(i) * 1.5 / 3 + SIN(i / 100)
NEXT
t = PEEK(54541) + 256 * PEEK(54542) - t
IF t < 0 THEN t = t + 65536
PRINT "A ="; a
PRINT "THIS MACHINE: "; STR$(INT(t / 0.6 + 0.5) / 100); " SECONDS"
PRINT
PRINT "FOR SCALE, THIS SAME PROGRAM ON THIS SAME MACHINE:"
PRINT "  EHBASIC, SOFTWARE FLOATS             4.03 S"
PRINT "  EHBASIC, MATH UNIT                   1.66 S"
PRINT
PRINT "ANY KEY...";
DO: GET k$: LOOP UNTIL k$ <> ""
