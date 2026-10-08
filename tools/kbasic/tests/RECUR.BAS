' recursion: a FUNCTION calling itself, two SUBs calling each other
FUNCTION Fact(n)
  IF n < 2 THEN Fact = 1 ELSE Fact = n * Fact(n - 1)
END FUNCTION
SUB Ping(n%)
  DIM mine%
  mine% = n%
  IF n% > 0 THEN Pong n% - 1
  PRINT mine%;
END SUB
SUB Pong(n%)
  DIM mine%
  mine% = n%
  IF n% > 0 THEN Ping n% - 1
  PRINT mine%;
END SUB
FUNCTION Rev$(s$)
  IF LEN(s$) < 2 THEN Rev$ = s$ ELSE Rev$ = Rev$(MID$(s$, 2)) + LEFT$(s$, 1)
END FUNCTION
SUB Count(n)
  DIM i
  FOR i = 1 TO 2
    IF n > 0 THEN Count n - 1
  NEXT
  total = total + 1
END SUB
PRINT "R:"; Fact(10)
Ping 5: PRINT
PRINT "R:"; Rev$("hello")
Count 3: PRINT "R:"; total
