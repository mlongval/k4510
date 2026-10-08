SUB Greet(name$, times%)
  FOR i = 1 TO times%
    PRINT "Hi "; name$
  NEXT
END SUB
FUNCTION Area(w, h)
  Area = w * h
END FUNCTION
FUNCTION Shout$(s$)
  Shout$ = UCASE$(s$) + "!"
END FUNCTION
Greet "Ana", 2
CALL Greet("Bo", 1)
PRINT "A:"; Area(3, 4)
PRINT "B:"; Shout$("hey")
x = 10
SUB Bump
  x = x + 1
END SUB
Bump
PRINT "C:"; x
