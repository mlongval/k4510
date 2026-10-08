' array reads mixed with function calls
DIM a(5), n$(3), g%(2, 2)
FOR i = 0 TO 5: a(i) = i * 10: NEXT
n$(1) = "ab": n$(2) = "cd"
g%(1, 2) = 5
PRINT "A:"; a(2) + SQR(16); a(1) * a(3)
PRINT "B:"; n$(1) + n$(2) + LEFT$(n$(2), 1)
PRINT "C:"; LEN(n$(1) + n$(2)); g%(1, 2) * INT(2.5)
PRINT "D:"; a(INT(1.5)); n$(INT(2.2))
x$ = n$(0): PRINT "E:["; x$; "]"
