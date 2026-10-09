#!/usr/bin/env python3
"""The chess piece sets -> VICKY: data/chess/<set>/ -> demo/chess.bin + demo/chess.h

chess.bin, dropped at CHESS_PHYS by the K4SG header, all 4 bpp, two pixels
a byte, high nibble left:
   +0                    the markers: three 32x32 sprites (512 bytes each) --
                         a square frame (2 px), a dot, a thin frame
   +CH_SET0 + n*CH_SET_BYTES   set n: twelve 32x32 pieces (w P N B R Q K, then
                         b), then twelve 16x16 minis (128 bytes each) for the
                         captured trays.  The board is 320x240 since
                         2026-10-09 (the Personality Chooser's look): a
                         square is 22 px, and its sprite sits 5 px up and
                         left of it, the piece inside.
Sets, in order: pixel (data/chess/pixel/pieces.txt, drawn a pixel at a time
for this board), drawn (KoboChess's own), vecteezy (Vecteezy.com, Free
License, credited), lines (unknown licence, present only on Doc's machines:
only with K4510_CHESS_LINES=1, so that the committed chess.prg is what every
checkout builds).  The drawn sets are shrunk to 20 px.

Pixel classes, so a palette bank can colour a side (see classify): 1..5 the
inside from paper to ink in five steps, 6..8 ink edges at three coverages,
9..10 paper edges at two; chess.c's set_scheme computes the fifteen tones
from a scheme's ink and paper.  The markers use 1, 2, 3 of their own bank."""
import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "data", "chess")
OUT = os.path.join(HERE, "..", "demo")
S = 32; P = 20; OFF = (S - P) // 2
SETS = [("pixel", "Pixel"), ("drawn", "Drawn"), ("vecteezy", "Icons"), ("lines", "Line icons")]

def classify(px):
    """Eleven classes for softer edges (Doc, 2026-09-07: "more colors ... better
    antialiasing").  Inside the piece (opaque): five mixes of ink and paper,
    1 = paper .. 5 = ink.  At the edge (part alpha): the same ink/paper mix at
    three coverages, which the palette blends towards the board's mid grey --
    6-8 ink at 75/50/25 %, 9-10 paper at 75/50 %.  0 is clear."""
    r, g, b, a = px
    if a < 32: return 0
    lum = (r * 299 + g * 587 + b * 114) // 1000
    inkness = (255 - lum) / 255.0                 # 0 paper .. 1 ink
    if a >= 224:
        return 1 + int(round(inkness * 4))        # 1..5
    cov = a / 255.0
    if inkness >= 0.5:
        return 6 if cov >= 0.62 else 7 if cov >= 0.35 else 8
    return 9 if cov >= 0.5 else 10
def pack(pix):
    out = bytearray()
    for i in range(0, len(pix), 2): out.append((pix[i] << 4) | pix[i + 1])
    return out
def fit(im, size):
    """the piece at `size` px: the source is square, and the drawn set is
    44 px, a 256 px set more -- shrunk with a proper filter"""
    if im.size != (size, size): im = im.resize((size, size), Image.LANCZOS)
    return im
def frame(th, val):
    pix = [0] * (S * S)
    for y in range(5, 27):
        for x in range(5, 27):
            if x < 5 + th or x >= 27 - th or y < 5 + th or y >= 27 - th: pix[y * S + x] = val
    return pack(pix)
def dot(r, val):
    pix = [0] * (S * S)
    for y in range(S):
        for x in range(S):
            if (x - 15.5) ** 2 + (y - 15.5) ** 2 <= r * r: pix[y * S + x] = val
    return pack(pix)
def pixel_art(path):
    """pieces.txt: a letter on a line of its own, then the rows; a row of 10
    is the left half, mirrored"""
    arts, cur = {}, None
    for line in open(path):
        line = line.rstrip("\n")
        if line.startswith("#") or not line.strip(): continue
        if len(line) == 1: cur = line; arts[cur] = []; continue
        if len(line) == 10: line += line[::-1]
        assert len(line) == 20, (cur, line)
        arts[cur].append(line)
    return arts
# white: a light body, dark lines; black: a dark body, light lines
PIX_CLASS = {"w": {"#": 5, "o": 1, "*": 5}, "b": {"#": 5, "o": 5, "*": 1}}
PIX_RGBA = {1: (255, 255, 255, 255), 5: (0, 0, 0, 255)}
def pixel_piece(rows, side):
    """the 32x32 sprite: 20 wide from x 6, its last row on y 24 (a square's row 19 of 22)"""
    pix = [0] * (S * S)
    top = 25 - len(rows)
    for y, row in enumerate(rows):
        for x, c in enumerate(row):
            if c != ".": pix[(top + y) * S + 6 + x] = PIX_CLASS[side][c]
    im = Image.new("RGBA", (24, 24), (0, 0, 0, 0))   # for the mini: the piece as a picture
    for y, row in enumerate(rows):
        for x, c in enumerate(row):
            if c != ".": im.putpixel((2 + x, 22 - len(rows) + y), PIX_RGBA[PIX_CLASS[side][c]])
    return pix, im

data = frame(2, 1) + dot(3, 2) + frame(1, 3)
SET0 = len(data)
names = []
def mini(im):
    im = im.resize((12, 12), Image.LANCZOS)
    pix = [0] * 256
    for y in range(12):
        for x in range(12): pix[(y + 2) * 16 + x + 2] = classify(im.getpixel((x, y)))
    return pack(pix)
for folder, label in SETS:
    d = os.path.join(SRC, folder)
    if folder == "pixel":
        arts = pixel_art(os.path.join(d, "pieces.txt"))
        start = len(data); minis = b""
        for side in "wb":
            for kind in "pnbrqk":
                pix, im = pixel_piece(arts[kind], side)
                data += pack(pix); minis += mini(im)
        data += minis
        names.append(label)
        assert len(data) - start == 12 * 512 + 12 * 128
        continue
    if folder == "lines" and os.environ.get("K4510_CHESS_LINES") != "1":
        continue
    if not all(os.path.exists(os.path.join(d, f"{s}{k}.png")) for s in "wb" for k in "pnbrqk"):
        print(f"mkchess: no {folder} set here, skipped"); continue
    start = len(data)
    for side in "wb":
        for kind in "pnbrqk":
            im = fit(Image.open(os.path.join(d, f"{side}{kind}.png")).convert("RGBA"), P)
            pix = [0] * (S * S)
            for y in range(P):
                for x in range(P): pix[(y + OFF) * S + x + OFF] = classify(im.getpixel((x, y)))
            data += pack(pix)
    for side in "wb":
        for kind in "pnbrqk":
            data += mini(Image.open(os.path.join(d, f"{side}{kind}.png")).convert("RGBA"))
    names.append(label)
    assert len(data) - start == 12 * 512 + 12 * 128
open(os.path.join(OUT, "chess.bin"), "wb").write(bytes(data))
with open(os.path.join(OUT, "chess.h"), "w") as f:
    f.write("/* generated by tools/mkchess.py from data/chess -- do not edit. */\n")
    f.write("#define CH_SPR_BYTES 512UL\n#define CH_MARK_FRAME 0\n#define CH_MARK_DOT 1\n#define CH_MARK_THIN 2\n")
    f.write(f"#define CH_SET0 {SET0}UL\n#define CH_SET_BYTES {12 * 512 + 12 * 128}UL\n#define CH_MINI_OFF {12 * 512}UL\n#define CH_MINI_BYTES 128UL\n")
    f.write(f"#define CH_NSETS {len(names)}\n")
    f.write("static const char *const CH_SET_NAME[] = {" + ",".join('"%s"' % n for n in names) + "};\n")
print(f"chess.bin: {len(data)} bytes, sets: {', '.join(names)}")
