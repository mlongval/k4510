' Moving sprites -- K4510 BASIC, VICKY
' One 16 x 16 shape, 8 bits a pixel, poked into far memory at
' $040000 (sprite page 1024); 16 sprites share it.  100 moves
' of all 16 (1600 MOVSPRs), timed by FRAMES.  The X16 twin:
' compare/x16/x16/SPRITES.BAS (VPOKE, SPRMEM, SPRITE, MOVSPR).
' SPRVEL would let VICKY move them by itself; this is the
' fair, BASIC-driven way.
CONST SHAPE = 262144
CONST PAGE = 1024
PRINT "16 SPRITES, 100 MOVES EACH, K4510 BASIC"
FOR c% = 16 TO 31
  PALETTE c%, (c% - 15) * 16, 255 - (c% - 15) * 16, 128
NEXT
FOR i% = 0 TO 255: POKE SHAPE + i%, 16 + (i% AND 15): NEXT
FOR n% = 0 TO 15
  SPRDEF n%, PAGE, 16, 16, 8
  SPRITE n%, 20 + n% * 8, 100 + n% * 4
NEXT
t = FRAMES
FOR f% = 0 TO 99
  FOR n% = 0 TO 15
    MOVSPR n%, 20 + f% * 2 + n% * 8, 100 + n% * 4
  NEXT
NEXT
t = FRAMES - t
PRINT "RESULT SPRITES"; t / 60; "S"
PRINT "DONE"
PRINT "ANY KEY...";
DO: GET k$: LOOP UNTIL k$ <> ""
FOR n% = 0 TO 15: SPROFF n%: NEXT
