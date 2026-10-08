' SPLIT -- graphics above, four lines of text below -- K4510 BASIC
' SHEILA, VICKY's display list, shows text for the top status band, the
' bitmap below it, and text again for the console's last four rows and
' the bottom band -- every frame, by itself.  Any key leaves.
CONST V = 53248              ' VICKY
CONST DL = 48896             ' the display list, at $BF00
DIM q(15, 3)
GRAPHICS 2
l% = PEEK(V + 16)
' where the parts meet, from JIM: $DA06 the console's rows, $DA08 the rows
' above it (the top band); 8 glass lines a row here
t0 = PEEK(55816) * 8: s = (PEEK(55816) + PEEK(55814) - 4) * 8
yl = t0: ym = s - 1
FOR i% = 0 TO 35: READ b%: POKE DL + i%, b%: NEXT
POKE DL + 6, l%: POKE DL + 30, l%
POKE DL + 9, t0 AND 255: POKE DL + 10, INT(t0 / 256)
POKE DL + 21, s AND 255: POKE DL + 22, INT(s / 256)
DATA 2,32,0,0,2,16,7,0,1,0,0,0,2,16,0,0,2,32,25,0
DATA 1,0,0,0,2,32,0,0,2,16,7,0,0,0,0,0
POKE V + 96, 0: POKE V + 97, 191: POKE V + 98, 0: POKE V + 99, 0: POKE V + 100, 1
x1 = 100: y1 = yl + 40: x2 = 500: y2 = ym - 40
a = 7: b = 3: e = -5: f = 4: k% = 0: h% = 16
' GRAPHICS 2 cleared the screen: the cursor is at the top, where the text
' is hidden.  Sixty empty lines take it down to the four that show.
FOR i% = 1 TO 60: PRINT: NEXT
PRINT "K4510 BASIC: A BITMAP ABOVE, FOUR ROWS OF TEXT BELOW."
PRINT "SHEILA HOLDS THE SPLIT. ANY KEY LEAVES."
DO
  t = PEEK(54541) + 256 * PEEK(54542): n = 0
  DO
    x1 = x1 + a: IF x1 < 0 OR x1 > 639 THEN a = -a: x1 = x1 + 2 * a
    y1 = y1 + b: IF y1 < yl OR y1 > ym THEN b = -b: y1 = y1 + 2 * b
    x2 = x2 + e: IF x2 < 0 OR x2 > 639 THEN e = -e: x2 = x2 + 2 * e
    y2 = y2 + f: IF y2 < yl OR y2 > ym THEN f = -f: y2 = y2 + 2 * f
    LINE q(k%, 0), q(k%, 1), q(k%, 2), q(k%, 3), 0
    h% = h% + 1: IF h% > 255 THEN h% = 16
    LINE x1, y1, x2, y2, h%
    q(k%, 0) = x1: q(k%, 1) = y1: q(k%, 2) = x2: q(k%, 3) = y2
    k% = (k% + 1) AND 15: n = n + 1
    z = PEEK(54541) + 256 * PEEK(54542) - t: IF z < 0 THEN z = z + 65536
    IF PEEK(53505) > 127 THEN EXIT DO
  LOOP UNTIL z >= 120
  IF PEEK(53505) > 127 THEN EXIT DO
  PRINT "K4510 BASIC:"; INT(n * 60 / z); " LINES A SECOND"
LOOP
GET g$
POKE V + 100, 0
GRAPHICS 0
