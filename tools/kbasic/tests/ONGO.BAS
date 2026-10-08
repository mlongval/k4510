FOR i = 1 TO 4
  ON i GOSUB One, Two, Three
  ON i GOTO 100, 200
  PRINT "O:none"; i
  GOTO 300
100 PRINT "O:g1": GOTO 300
200 PRINT "O:g2"
300 NEXT
DEF FNsq(x) = x * x
DEF FNtag$(a$) = "<" + a$ + ">"
PRINT "F:"; FNsq(7); FNtag$("ok")
END
One: PRINT "O:s1": RETURN
Two: PRINT "O:s2": RETURN
Three: PRINT "O:s3": RETURN
