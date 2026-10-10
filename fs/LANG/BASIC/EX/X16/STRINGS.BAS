' String handling -- K4510 BASIC (compiled)
' Five rounds of: build a 200-character string one CHR$ at a
' time, reverse it with MID$, count its vowels, 200 STR$/VAL
' round trips, 100 LEFT$/RIGHT$ comparisons.  The same work
' as compare/x16/x16/STRINGS.BAS on the Commander X16.
' Timed by FRAMES, the 60 Hz frame counter.
PRINT "STRING HANDLING, K4510 BASIC"
t = FRAMES
FOR r% = 1 TO 5
  a$ = ""
  FOR i% = 1 TO 200: a$ = a$ + CHR$(65 + (i% AND 15)): NEXT
  b$ = ""
  FOR i% = LEN(a$) TO 1 STEP -1: b$ = b$ + MID$(a$, i%, 1): NEXT
  v% = 0
  FOR i% = 1 TO LEN(a$)
    c$ = MID$(a$, i%, 1)
    IF c$ = "A" OR c$ = "E" OR c$ = "I" OR c$ = "O" THEN v% = v% + 1
  NEXT
  s = 0
  FOR i% = 1 TO 200: s = s + VAL(STR$(i% * 3)): NEXT
  w% = 0
  FOR i% = 1 TO 100
    IF LEFT$(a$, i%) < RIGHT$(a$, i%) THEN w% = w% + 1
  NEXT
NEXT r%
t = FRAMES - t
PRINT "CHECK"; LEN(b$); v%; s; w%
PRINT "RESULT STRINGS"; t / 60; "S"
PRINT "DONE"
PRINT "ANY KEY...";
DO: GET k$: LOOP UNTIL k$ <> ""
