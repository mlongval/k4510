x = 5
IF x > 3 THEN
  PRINT "A:big"
ELSE
  PRINT "A:small"
END IF
FOR x = 1 TO 3
  IF x = 1 THEN
    PRINT "B1"
  ELSEIF x = 2 THEN
    PRINT "B2"
  ELSE
    PRINT "B3"
  END IF
NEXT
IF x > 100 THEN PRINT "C:yes" ELSE PRINT "C:no"
IF 1 = 2 THEN PRINT "D:bad1": PRINT "D:bad2"
PRINT "D:done"
IF x THEN PRINT "E:nonzero"
IF x > 1 AND x < 10 THEN PRINT "F:range"
IF x > 1 THEN
  IF x < 3 THEN
    PRINT "G:inner"
  ELSE
    PRINT "G:outer"
  END IF
END IF
