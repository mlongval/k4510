' Lines and points -- K4510 BASIC, VICKY
' 500 LINEs across a 320 x 240 picture, then 5000 PLOTs, each
' timed by FRAMES.  The X16 twin: compare/x16/x16/LINES.BAS
' (LINE and PSET).
PRINT "500 LINES AND 5000 POINTS, 320X240, K4510 BASIC"
GRAPHICS 1
t = FRAMES
FOR i% = 0 TO 499
  y% = i% MOD 240
  LINE 0, y%, 319, 239 - y%, 1 + (i% AND 15)
NEXT
t1 = FRAMES - t
t = FRAMES
FOR y% = 0 TO 49
  FOR x% = 0 TO 99
    PLOT x% * 3, y% * 4, 1 + (x% AND 15)
  NEXT
NEXT
t2 = FRAMES - t
PRINT "RESULT LINES"; t1 / 60; "S"
PRINT "RESULT POINTS"; t2 / 60; "S"
PRINT "DONE"
PRINT "ANY KEY...";
DO: GET k$: LOOP UNTIL k$ <> ""
GRAPHICS 0
