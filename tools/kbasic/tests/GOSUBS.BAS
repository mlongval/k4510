count = 0
Again:
count = count + 1
IF count < 3 THEN GOTO Again
PRINT "D:"; count
GOSUB One
PRINT "C:main"
END
One:
  PRINT "A:one"
  GOSUB Two
  PRINT "B:one again"
  RETURN
Two:
  PRINT "T:two"
  RETURN
