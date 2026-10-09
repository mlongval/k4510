' Expression benchmark: this machine's own, no historical peer -- K4510 BASIC
' The same 20000 evaluations EhBASIC, the old interpreter, ran; compiled.
CLS
PRINT "EXPRESSIONS: 20000 EVALUATIONS OF A+B*C-D/E+I*0.001, COMPILED"
PRINT
t = PEEK(54541) + 256 * PEEK(54542)
a = 0: b = 1.5: c = 2.5: d = 3: e = 7
FOR i = 1 TO 20000
  a = a + b * c - d / e + i * 0.001
NEXT
t = PEEK(54541) + 256 * PEEK(54542) - t
IF t < 0 THEN t = t + 65536
PRINT "A ="; a
PRINT "THIS MACHINE: "; STR$(INT(t / 0.6 + 0.5) / 100); " SECONDS"
PRINT
PRINT "FOR SCALE, THIS PROGRAM IN EHBASIC (THE OLD BASIC), HERE:"
PRINT "  INTERPRETED, MATH-UNIT FLOATS            6.46 S"
PRINT "  WITH ITS EXPRESSION COMPILER             2.11 S"
PRINT
PRINT "ANY KEY...";
DO: GET k$: LOOP UNTIL k$ <> ""
