' Space Invaders with hardware sprites -- K4510 BASIC
' (SPRITE, SPRDEF and SPROFF; INVADERS.BAS is the text-screen version.)
' The shapes are poked 4 bits a pixel straight into far memory at $040000
' (sprite page 1024) -- K4510 BASIC's PEEK and POKE reach it above 65535,
' so EhBASIC's bank window is not needed.  The four bases each own a shape
' page, and hits poke holes into it: erosion.
' A/D or the cursor keys move, space fires, Q quits.
' Speeds come from INVADER2.CFG beside this file; these are the defaults.
CONST SHAPES = 262144          ' $040000: sprite page 1024
CONST PAGE0 = 1024
ps = 8: bs = 10: es = 6: ms = 4: mi = 20: md = 3: mn = 3: fc = 20: fw = 5
RANDOMIZE
ReadConfig
PALETTE 17, 80, 255, 120: PALETTE 18, 255, 255, 255: PALETTE 19, 255, 220, 80
PALETTE 20, 255, 80, 80: PALETTE 21, 120, 255, 180

' ---- the shapes: the arcade originals, 16 rows of 15-bit masks; 6-8 are
' the aliens' second frame
FOR s% = 0 TO 8
  READ c%
  FOR r% = 0 TO 15
    READ m
    b = SHAPES + s% * 256 + r% * 8
    FOR x% = 0 TO 7
      p% = 0
      IF m AND INT(2 ^ (14 - x% * 2)) THEN p% = c% * 16
      IF m AND INT(2 ^ (13 - x% * 2)) THEN p% = p% + c%
      POKE b + x%, p%
    NEXT
  NEXT
NEXT
' squid (top row, 30 points)
DATA 2, 384,960,2016,3504,4080,576,1440,2640,0,0,0,0,0,0,0,0
' crab (middle, 20 points)
DATA 3, 1040,544,2032,3544,8188,6132,5140,864,0,0,0,0,0,0,0,0
' octopodes (bottom row, 10 points -- Greek plural, as the etymology demands)
DATA 1, 960,8184,16380,14748,16380,3696,6552,12300,0,0,0,0,0,0,0,0
' the player's cannon
DATA 5, 128,448,448,8188,16382,16382,16382,16382,0,0,0,0,0,0,0,0
' bullet
DATA 2, 1024,1024,1024,1024,1024,1024,1024,1024,0,0,0,0,0,0,0,0
' bomb
DATA 4, 64,32,64,128,64,32,64,128,0,0,0,0,0,0,0,0
' the second frames: the legs the other way, so the march walks
DATA 2, 384,960,2016,3504,4080,1440,2064,1056,0,0,0,0,0,0,0,0
DATA 3, 1040,4644,6132,7644,8188,4088,1040,2056,0,0,0,0,0,0,0,0
DATA 1, 960,8184,16380,14748,16380,1632,3504,12300,0,0,0,0,0,0,0,0

' ---- the four bases: 32x16, pages 1036-1039, solid with an arch
FOR b% = 0 TO 3
  bb = SHAPES + (12 + b%) * 256
  FOR r% = 0 TO 15
    FOR x% = 0 TO 15
      p% = 85                                         ' colour 5|5: two pixels of 21
      IF r% > 10 AND x% > 3 AND x% < 12 THEN p% = 0   ' the arch
      IF r% < 2 AND (x% < 2 OR x% > 13) THEN p% = 0   ' rounded shoulders
      POKE bb + r% * 16 + x%, p%
    NEXT
  NEXT
NEXT

score% = 0: lives% = 3: wave% = 1
DIM alive%(24)
CLS: PRINT "  INVADERS WITH SPRITES   A/D OR CURSORS, SPACE FIRES, Q QUITS"
SPRDEF 0, PAGE0 + 3, 16, 16, 4
FOR i% = 1 TO 8: SPRDEF i%, PAGE0, 16, 16, 4: NEXT
FOR i% = 9 TO 16: SPRDEF i%, PAGE0 + 1, 16, 16, 4: NEXT
FOR i% = 17 TO 24: SPRDEF i%, PAGE0 + 2, 16, 16, 4: NEXT
SPRDEF 25, PAGE0 + 4, 16, 16, 4: SPRDEF 26, PAGE0 + 5, 16, 16, 4
FOR b% = 0 TO 3: SPRDEF 27 + b%, PAGE0 + 12 + b%, 32, 16, 4: SPRITE 27 + b%, 80 + b% * 140, 192: NEXT

DO
  ' ---- a new wave
  left% = 24: FOR i% = 1 TO 24: alive%(i%) = 1: NEXT: frame% = 0
  gx = 80: gy = 40: dir = ms
  speed = mi - (wave% - 1) * md: IF speed < mn THEN speed = mn
  tick = speed: px = 312: oldpx = 0: by = 0: ey = 0
  DrawGrid: SPRITE 0, px, 220
  ' ---- once a frame
  DO
    WaitFrame
    DO                                   ' every queued key, then one draw
      GET k$
      IF k$ = "" THEN EXIT DO
      IF k$ = "q" OR k$ = "Q" THEN lives% = 0: EXIT DO
      IF (k$ = "a" OR k$ = "A" OR k$ = CHR$(130)) AND px > ps THEN px = px - ps
      IF (k$ = "d" OR k$ = "D" OR k$ = CHR$(131)) AND px < 608 - ps THEN px = px + ps
      IF k$ = " " AND by = 0 THEN by = 212: bx = px: SPRITE 25, bx, by
    LOOP
    IF lives% = 0 THEN EXIT DO
    IF px <> oldpx THEN SPRITE 0, px, 220: oldpx = px
    IF by THEN MoveBullet
    IF ey THEN MoveBomb
    IF lives% = 0 THEN EXIT DO
    IF ey = 0 AND RND(1) * 1000 < fc + wave% * fw THEN DropBomb
    tick = tick - 1
    IF tick <= 0 THEN tick = speed: March
    IF lives% = 0 OR left% = 0 THEN EXIT DO
  LOOP
  IF lives% = 0 THEN EXIT DO
  wave% = wave% + 1: PRINT "WAVE CLEARED"
  FOR i% = 1 TO 60: WaitFrame: NEXT
LOOP

PRINT "GAME OVER  SCORE"; score%
FOR i% = 0 TO 30: SPROFF i%: NEXT
PRINT "ANY KEY...";
DO: GET k$: LOOP UNTIL k$ <> ""
END

SUB WaitFrame
  f% = PEEK(54541)
  DO: LOOP WHILE PEEK(54541) = f%
END SUB

SUB DrawGrid
  FOR r% = 0 TO 2
    FOR c% = 0 TO 7
      IF alive%(r% * 8 + c% + 1) THEN SPRITE r% * 8 + c% + 1, gx + c% * 40, gy + r% * 24
    NEXT
  NEXT
END SUB

SUB March
  ' a step, and the other frame -- the legs walk
  frame% = 1 - frame%
  FOR i% = 1 TO 24
    IF alive%(i%) THEN SPRDEF i%, PAGE0 + (i% - 1) \ 8 + frame% * 6, 16, 16, 4
  NEXT
  gx = gx + dir
  IF gx < 8 OR gx > 320 THEN
    dir = -dir: gx = gx + dir: gy = gy + 10
    speed = speed - 1: IF speed < mn THEN speed = mn
  END IF
  IF gy > 170 THEN lives% = 0: EXIT SUB      ' they landed
  DrawGrid
END SUB

SUB MoveBullet
  by = by - bs
  IF by < 20 THEN by = 0: SPROFF 25: EXIT SUB
  SPRITE 25, bx, by
  IF by > 176 AND by < 210 THEN
    IF HitBase(bx + 7, by + 2) THEN by = 0: SPROFF 25: EXIT SUB
  END IF
  c% = INT((bx - gx + 8) / 40): r% = INT((by - gy + 8) / 24)
  IF c% < 0 OR c% > 7 OR r% < 0 OR r% > 2 THEN EXIT SUB
  i% = r% * 8 + c% + 1
  IF alive%(i%) = 0 THEN EXIT SUB
  IF ABS(bx - (gx + c% * 40)) < 12 AND ABS(by - (gy + r% * 24)) < 12 THEN
    alive%(i%) = 0: left% = left% - 1: score% = score% + (3 - r%) * 10
    SPROFF i%: SPROFF 25: by = 0
    PRINT "SCORE"; score%
  END IF
END SUB

SUB DropBomb
  ' the lowest invader of a random column fires
  c% = INT(RND(1) * 8)
  FOR r% = 2 TO 0 STEP -1
    i% = r% * 8 + c% + 1
    IF alive%(i%) THEN
      ex = gx + c% * 40: ey = gy + r% * 24 + 16: SPRITE 26, ex, ey
      EXIT FOR
    END IF
  NEXT
END SUB

SUB MoveBomb
  ey = ey + es
  IF ey > 236 THEN ey = 0: SPROFF 26: EXIT SUB
  SPRITE 26, ex, ey
  IF ey > 176 AND ey < 208 THEN
    IF HitBase(ex + 7, ey + 13) THEN ey = 0: SPROFF 26: EXIT SUB
  END IF
  IF ey > 208 AND ABS(ex - px) < 12 THEN
    lives% = lives% - 1: ey = 0: SPROFF 26
    PRINT "OUCH! LIVES"; lives%
  END IF
END SUB

FUNCTION HitBase (hx, hy)
  ' is (hx,hy) on a base?  If so, erode it: a hole 4 rows by 5 bytes
  HitBase = 0
  IF hy < 192 OR hy > 207 THEN EXIT FUNCTION
  FOR b% = 0 TO 3
    lx = hx - (80 + b% * 140)
    IF lx >= 0 AND lx <= 31 THEN
      ly = hy - 192
      page = SHAPES + (12 + b%) * 256
      IF PEEK(page + ly * 16 + INT(lx / 2)) THEN
        HitBase = 1
        FOR yy = ly - 1 TO ly + 2
          FOR xx = INT(lx / 2) - 2 TO INT(lx / 2) + 2
            IF yy >= 0 AND yy <= 15 AND xx >= 0 AND xx <= 15 THEN POKE page + yy * 16 + xx, 0
          NEXT
        NEXT
        EXIT FUNCTION
      END IF
    END IF
  NEXT
END FUNCTION

SUB ReadConfig
  ' INVADER2.CFG: "NAME VALUE" lines, anything else ignored.  The file
  ' device at $D300 loads it whole into far memory at $041100.
  n$ = "/LANG/BASIC/EX/INVADER2.CFG"
  FOR j% = 1 TO LEN(n$): POKE 266239 + j%, ASC(MID$(n$, j%, 1)): NEXT
  POKE 266240 + LEN(n$), 0                                         ' the name at $041000
  POKE 54020, 0: POKE 54021, 16: POKE 54022, 4: POKE 54023, 0
  POKE 54024, 0: POKE 54025, 17: POKE 54026, 4: POKE 54027, 0      ' the buffer at $041100
  POKE 54016, 9
  IF PEEK(54017) THEN EXIT SUB                                     ' no file: the defaults stand
  l = PEEK(54028) + 256 * PEEK(54029): IF l > 3000 THEN l = 3000
  j = 0
  DO WHILE j < l
    c% = PEEK(266496 + j)
    IF c% >= 65 AND c% <= 90 THEN
      w$ = ""
      DO WHILE c% >= 65 AND c% <= 90: w$ = w$ + CHR$(c%): j = j + 1: c% = PEEK(266496 + j): LOOP
      DO WHILE c% = 32: j = j + 1: c% = PEEK(266496 + j): LOOP
      v = 0
      DO WHILE c% >= 48 AND c% <= 57: v = v * 10 + c% - 48: j = j + 1: c% = PEEK(266496 + j): LOOP
      SELECT CASE w$
        CASE "PLAYER": ps = v
        CASE "BULLET": bs = v
        CASE "BOMB": es = v
        CASE "STEP": ms = v
        CASE "MARCH": mi = v
        CASE "FASTER": md = v
        CASE "FASTEST": mn = v
        CASE "FIRE": fc = v
        CASE "FIREWAVE": fw = v
      END SELECT
    END IF
    DO WHILE j < l AND PEEK(266496 + j) <> 10: j = j + 1: LOOP   ' to the next line
    j = j + 1
  LOOP
END SUB
