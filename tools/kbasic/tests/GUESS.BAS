' guess the number -- the first K4510 BASIC program
CONST TRIES = 7
secret = 42
n% = 0
DO
  INPUT "Your guess"; g
  n% = n% + 1
  IF g < secret THEN
    PRINT "Higher"
  ELSEIF g > secret THEN
    PRINT "Lower"
  END IF
LOOP UNTIL g = secret OR n% = TRIES
GOSUB Bravo
PRINT "Tries:"; n%; "of"; TRIES
END
Bravo:
  IF g = secret THEN PRINT "You got it!" ELSE PRINT "Too bad"
  RETURN
