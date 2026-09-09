#!/usr/bin/env python3
"""Write k4510.tek: a Tektronix 4010 test page made here (axes, a sine, a
Lissajous, a box, and text in alpha mode), so the terminal can be tried
with a file of known provenance.  The 4010's byte protocol:
  GS ($1D) enters graph mode; a point is up to four bytes -- high Y, low Y,
  high X, low X, each tagged in its top bits (%001, %011, %001, %010); the
  first point after GS is a move, the rest draw; US ($1F) is alpha mode,
  where bytes are text at the beam; ESC FF clears the page.
Coordinates run 0-1023 across and 0-779 up (0 at the bottom)."""
import math, sys
out = bytearray()
def point(x, y):
    x = max(0, min(1023, int(x))); y = max(0, min(779, int(y)))
    out.extend(bytes([0x20 | (y >> 5), 0x60 | (y & 31), 0x20 | (x >> 5), 0x40 | (x & 31)]))
def poly(pts):
    out.append(0x1D)
    for p in pts: point(*p)
def text(x, y, s):
    out.append(0x1D); point(x, y); out.append(0x1F); out.extend(s.encode("ascii"))
out.extend(b"\x1b\x0c")                                    # a clean page
poly([(60, 60), (1000, 60)]); poly([(60, 60), (60, 740)])  # axes
for i in range(0, 11):                                     # ticks
    x = 60 + i * 94; poly([(x, 60), (x, 50)])
poly([(60 + i * 0.94, 400 + 300 * math.sin(i / 940 * 4 * math.pi)) for i in range(0, 941, 4)])   # a sine
poly([(700 + 120 * math.sin(3 * t), 600 + 100 * math.sin(4 * t)) for t in [i * 2 * math.pi / 400 for i in range(401)]])  # Lissajous
poly([(80, 660), (380, 660), (380, 740), (80, 740), (80, 660)])
text(100, 690, "K4510 -- TEKTRONIX 4010 TEST PAGE")
text(100, 20, "made by linux/tek40xx/mkplt.py; HOME clears, END quits")
out.append(0x1F)
open(sys.argv[1] if len(sys.argv) > 1 else "k4510.tek", "wb").write(out)
print(len(out), "bytes")
