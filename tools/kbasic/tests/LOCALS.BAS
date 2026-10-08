' local variables: DIM inside a SUB or FUNCTION
x = 100: n$ = "global"
SUB Show(k)
  DIM x, n$, t(3)
  PRINT "L:"; x; n$; t(2)
  x = k: n$ = "local": t(2) = k * 2
  PRINT "L:"; x; n$; t(2)
END SUB
FUNCTION Twice(v)
  DIM r
  r = v * 2
  Twice = r
END FUNCTION
Show 5
Show 6
PRINT "G:"; x; n$; Twice(21)
FOR i = 10 TO 1 STEP -3: PRINT i;: NEXT: PRINT
