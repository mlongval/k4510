' Random triangles -- one blitter operation each -- K4510 BASIC
RANDOMIZE
GRAPHICS 2
FOR c% = 16 TO 255
  PALETTE c%, RND(1) * 255, RND(1) * 255, RND(1) * 255
NEXT
FOR n% = 1 TO 300
  TRI RND(1) * 640, RND(1) * 480, RND(1) * 640, RND(1) * 480, RND(1) * 640, RND(1) * 480, 16 + RND(1) * 239
NEXT
PRINT "300 TRIANGLES. ANY KEY"
DO: GET k$: LOOP UNTIL k$ <> ""
GRAPHICS 0
