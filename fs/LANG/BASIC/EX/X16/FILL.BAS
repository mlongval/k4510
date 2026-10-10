' Fill the screen -- K4510 BASIC, VICKY
' GRAPHICS 1 is 320 x 240 over the text; BOX fills a rectangle
' (one blitter operation).  200 full-screen fills, timed by
' FRAMES; twenty took no frame at all.  The X16 twin: compare/x16/x16/FILL.BAS (RECT).
PRINT "FILL THE SCREEN 200 TIMES, 320X240, K4510 BASIC"
GRAPHICS 1
t = FRAMES
FOR i% = 1 TO 200
  c% = 1 + (i% AND 15)
  BOX 0, 0, 319, 239, c%
NEXT
t = FRAMES - t
PRINT "RESULT FILL"; t / 60; "S"
PRINT "DONE"
PRINT "ANY KEY...";
DO: GET k$: LOOP UNTIL k$ <> ""
GRAPHICS 0
