DIM a(5), b$(3), m%(2, 3)
FOR i = 0 TO 5: a(i) = i * i: NEXT
PRINT "A:"; a(3); a(5)
b$(0) = "zero": b$(3) = "three"
PRINT "B:"; b$(0); "-"; b$(3)
FOR r = 0 TO 2: FOR c = 0 TO 3: m%(r, c) = r * 10 + c: NEXT: NEXT
PRINT "C:"; m%(2, 3)
z(4) = 7: PRINT "D:"; z(4)
CONST SIZE = 4
DIM q(SIZE): q(4) = 1: PRINT "E:"; q(4)
