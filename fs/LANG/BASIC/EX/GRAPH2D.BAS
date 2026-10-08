' GRAPH2D -- an interactive function plotter (640x480) -- K4510 BASIC
' Arrows pan, +/- zoom, 1-8 pick the function, Q quits.
GRAPHICS 2
PALETTE 16, 255, 255, 255: PALETTE 17, 90, 90, 120: PALETTE 18, 255, 150, 0
cx = 0: cy = 0: w = 13: f% = 1
DO
  Redraw
  DO: GET k$: LOOP UNTIL k$ <> ""
  SELECT CASE k$
    CASE CHR$(130): cx = cx - w / 8
    CASE CHR$(131): cx = cx + w / 8
    CASE CHR$(128): cy = cy + w * 0.75 / 8
    CASE CHR$(129): cy = cy - w * 0.75 / 8
    CASE "+": w = w / 2
    CASE "-": w = w * 2
    CASE "1" TO "8": f% = VAL(k$): cx = 0: cy = 0: w = 13
    CASE "q", "Q": EXIT DO
  END SELECT
LOOP
GRAPHICS 0
END

FUNCTION Fname$ (n%)
  SELECT CASE n%
    CASE 1: Fname$ = "SIN(X)"
    CASE 2: Fname$ = "COS(X)"
    CASE 3: Fname$ = "TAN(X)"
    CASE 4: Fname$ = "X*X/10"
    CASE 5: Fname$ = "SQR(ABS(X))"
    CASE 6: Fname$ = "1/X"
    CASE 7: Fname$ = "EXP(X/3)"
    CASE ELSE: Fname$ = "LOG(ABS(X))"
  END SELECT
END FUNCTION

FUNCTION Fy (n%, x)
  ' y for x; defined% = 0 where the function has no value
  defined% = 1
  SELECT CASE n%
    CASE 1: Fy = SIN(x)
    CASE 2: Fy = COS(x)
    CASE 3: t = TAN(x): Fy = t: IF ABS(t) > 1000 THEN defined% = 0
    CASE 4: Fy = x * x / 10
    CASE 5: Fy = SQR(ABS(x))
    CASE 6: IF ABS(x) < 0.001 THEN defined% = 0 ELSE Fy = 1 / x
    CASE 7: Fy = EXP(x / 3)
    CASE ELSE: IF ABS(x) < 0.001 THEN defined% = 0 ELSE Fy = LOG(ABS(x))
  END SELECT
END FUNCTION

SUB Redraw
  hh = w * 0.75
  GCLS: CLS
  ' the status line, on the text screen under the bitmap
  PRINT "F"; f%; " Y="; Fname$(f%); "  X "; INT((cx - w / 2) * 10) / 10; " TO "; INT((cx + w / 2) * 10) / 10;
  PRINT "  ARROWS PAN  +/- ZOOM  1-8 FN  Q QUIT"
  ' axes and ticks
  ts = 1: IF w > 26 THEN ts = 5
  IF w > 130 THEN ts = 25
  x0 = 640 * (0 - (cx - w / 2)) / w: y0 = 480 - 480 * (0 - (cy - hh / 2)) / hh
  IF x0 >= 0 AND x0 < 640 THEN LINE x0, 0, x0, 479, 16
  IF y0 >= 0 AND y0 < 480 THEN
    LINE 0, y0, 639, y0, 16
    FOR t = ts * INT((cx - w / 2) / ts) TO cx + w / 2 STEP ts
      tx = 640 * (t - (cx - w / 2)) / w
      IF tx >= 0 AND tx < 640 THEN LINE tx, y0 - 3, tx, y0 + 3, 17
    NEXT
  END IF
  IF x0 >= 0 AND x0 < 640 THEN
    FOR t = ts * INT((cy - hh / 2) / ts) TO cy + hh / 2 STEP ts
      ty = 480 - 480 * (t - (cy - hh / 2)) / hh
      IF ty >= 0 AND ty < 480 THEN LINE x0 - 3, ty, x0 + 3, ty, 17
    NEXT
  END IF
  ' the curve, left to right, joined where it is continuous
  joined% = 0
  FOR px% = 0 TO 639 STEP 2
    y = Fy(f%, cx - w / 2 + px% * w / 640)
    py = 480 - 480 * (y - (cy - hh / 2)) / hh
    IF defined% = 0 OR py < 0 OR py > 479 THEN
      joined% = 0
    ELSE
      IF joined% THEN LINE q1, q2, px%, py, 18 ELSE PLOT px%, py, 18
      q1 = px%: q2 = py: joined% = 1
    END IF
  NEXT
END SUB
