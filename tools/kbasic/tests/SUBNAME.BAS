' a SUB named like a CONST: inside it, the name is the CONST (only a FUNCTION's name is its result)
CONST DRUM = 7
Drum 3
FUNCTION Twice (n)
  Twice = n * 2
END FUNCTION
PRINT "T:"; Twice(21)
SUB Drum (k%)
  PRINT "D:"; DRUM + k%
END SUB
