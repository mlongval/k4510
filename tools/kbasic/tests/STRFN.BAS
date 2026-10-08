a$ = "Hello, World"
PRINT "1:"; LEN(a$)
PRINT "2:"; LEFT$(a$, 5)
PRINT "3:"; RIGHT$(a$, 5)
PRINT "4:"; MID$(a$, 8, 3)
PRINT "5:"; MID$(a$, 8)
PRINT "6:"; INSTR(a$, "World")
PRINT "7:"; UCASE$(a$)
PRINT "8:"; LCASE$(a$)
PRINT "9:"; CHR$(65); ASC("B")
PRINT "10:"; STR$(3.5)
PRINT "11:"; VAL("12.5") + 1
PRINT "12:["; SPACE$(3); "]"
PRINT "13:"; INSTR(a$, "xyz")
b$ = "abc"
IF b$ < "abd" THEN PRINT "14:less"
IF b$ = "abc" THEN PRINT "15:equal"
IF "b" > "abc" THEN PRINT "16:greater"
c$ = a$ + "!": PRINT "17:"; c$
