' Spinning lines -- K4510 BASIC
' Each LINE is one blitter operation; the colours are 16-31 (0-15 are the text's).
GRAPHICS 2
FOR c% = 16 TO 31
  PALETTE c%, (c% - 15) * 16, 255 - (c% - 15) * 16, 128 + (c% - 15) * 8
NEXT
a = 0
FOR n% = 1 TO 400
  x = 320 + 280 * COS(a): y = 240 + 200 * SIN(a)
  x2 = 320 + 280 * COS(a + 2): y2 = 240 + 200 * SIN(a + 2)
  LINE x, y, x2, y2, 16 + (n% AND 15)
  a = a + 0.07
NEXT
PRINT "ANY KEY"
DO: GET k$: LOOP UNTIL k$ <> ""
GRAPHICS 0
