' GRAPH3D -- an interactive surface plotter, z=f(x,y) as a wireframe -- K4510 BASIC
' Arrows rotate, +/- zoom, 1-5 pick the surface, Q quits.
CONST N = 40                 ' points a side (Doc's 40; EhBASIC managed 14)
CONST D = 6                  ' x and y run from -D to D
DIM z(N, N), sx(N, N), sy(N, N)
st = 2 * D / N
GRAPHICS 2
PALETTE 16, 255, 255, 255: PALETTE 17, 80, 200, 255: PALETTE 18, 255, 150, 0
a = 0.7: e = 0.5: s = 32: f% = 1
Compute
DO
  Draw
  DO: GET k$: LOOP UNTIL k$ <> ""
  SELECT CASE k$
    CASE CHR$(130): a = a - 0.25
    CASE CHR$(131): a = a + 0.25
    CASE CHR$(128): e = e + 0.15
    CASE CHR$(129): e = e - 0.15
    CASE "+": s = s * 1.3
    CASE "-": s = s / 1.3
    CASE "1" TO "5": f% = VAL(k$): Compute
    CASE "q", "Q": EXIT DO
  END SELECT
LOOP
GRAPHICS 0
END

SUB Status
  SELECT CASE f%
    CASE 1: f$ = "RIPPLE"
    CASE 2: f$ = "SADDLE"
    CASE 3: f$ = "EGGBOX"
    CASE 4: f$ = "HILL"
    CASE ELSE: f$ = "CONE"
  END SELECT
  PRINT "F"; f%; " "; f$; "  ARROWS ROTATE  +/- ZOOM  1-5 SURFACE  Q QUIT"
END SUB

SUB Compute
  CLS: PRINT "COMPUTING ";: Status
  FOR i% = 0 TO N
    x = -D + i% * st
    FOR j% = 0 TO N
      y = -D + j% * st
      r = SQR(x * x + y * y)
      SELECT CASE f%
        CASE 1: z(i%, j%) = 4 * SIN(r) / (1 + r / 4)
        CASE 2: z(i%, j%) = (x * x - y * y) / 8
        CASE 3: z(i%, j%) = 2 * COS(x) * COS(y)
        CASE 4: z(i%, j%) = 6 * EXP(-(x * x + y * y) / 8)
        CASE ELSE: z(i%, j%) = 5 - r
      END SELECT
    NEXT
  NEXT
END SUB

SUB Draw
  GCLS: CLS: Status
  ca = COS(a): sa = SIN(a): ce = COS(e): se = SIN(e)
  FOR i% = 0 TO N
    x = -D + i% * st
    FOR j% = 0 TO N
      y = -D + j% * st
      u = x * ca + y * sa: v = -x * sa + y * ca
      sx(i%, j%) = 320 + s * u: sy(i%, j%) = 240 - s * (z(i%, j%) * ce + v * se)
    NEXT
  NEXT
  FOR i% = 0 TO N
    FOR j% = 1 TO N
      LINE sx(i%, j% - 1), sy(i%, j% - 1), sx(i%, j%), sy(i%, j%), 17
      LINE sx(j% - 1, i%), sy(j% - 1, i%), sx(j%, i%), sy(j%, i%), 18
    NEXT
  NEXT
END SUB
