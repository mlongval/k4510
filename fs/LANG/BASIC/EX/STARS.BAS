' Starfield: 64 stars flying at you -- K4510 BASIC
' Any key leaves.  ($D101 bit 7: a key is waiting.)
CONST N = 64
DIM x(N), y(N), z(N)
RANDOMIZE
GRAPHICS 1
PALETTE 16, 255, 255, 255: PALETTE 17, 160, 160, 160: PALETTE 18, 90, 90, 90
FOR i% = 1 TO N: NewStar i%: z(i%) = RND(1) * 100 + 1: NEXT
FOR f% = 1 TO 600
  GCLS
  FOR i% = 1 TO N
    z(i%) = z(i%) - 2
    IF z(i%) < 1 THEN NewStar i%
    c% = 18
    IF z(i%) < 60 THEN c% = 17
    IF z(i%) < 30 THEN c% = 16
    PLOT 160 + x(i%) * 40 / z(i%), 120 + y(i%) * 40 / z(i%), c%
  NEXT
  IF PEEK(53505) > 127 THEN EXIT FOR
NEXT
GET k$
GRAPHICS 0

SUB NewStar (s%)
  x(s%) = RND(1) * 320 - 160: y(s%) = RND(1) * 240 - 120: z(s%) = 100
END SUB
