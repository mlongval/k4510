#!/usr/bin/env python3
"""LOGO's shapes: a small green turtle -- and now a bird -- in sixteen
headings, as sprite frames.

    tools/mkturtle.py [--shape turtle|bird] [--out FILE] [--preview out.png]

writes fs/LANG/LOGO/TURTLE.SPR: 16 frames of 32x32 pixels, one byte a pixel
(an index into the machine's palette; 0 is transparent), frame k facing
k x 22.5 degrees clockwise from straight up -- LOGO's heading.  LOGO loads
the file into far memory and points sprite 0 at the frame nearest the
turtle's heading, so turning is one register write (Doc, 2026-09-14: "logo
needs a cute turtle sprite -- 22.5 degree variants or a rotation algorithm").

The turtle is drawn once, facing up, eight times larger than it will be
shown, rotated there for each heading, and only then brought down to 32x32:
each 8x8 block becomes one pixel, by vote.  Rotating the small picture
instead would smear it; drawn big, every frame is as clean as the first.
Small parts that a plain vote would lose -- the eyes, the rim of the shell --
win their pixel with less than half of it.

Colours are the machine's own sixteen (/SYSTEM/ETC/PALETTES/C64.PAL), by
index, so PALETTE LOAD GREEN turns the turtle green-on-green with the rest.
"""
import sys, pathlib
from PIL import Image, ImageDraw

REPO = pathlib.Path(__file__).resolve().parent.parent
OUT = REPO / "fs/LANG/LOGO/TURTLE.SPR"          # --out, or SHAPES's own
PAL = REPO / "fs/SYSTEM/ETC/PALETTES/C64.PAL"

SIZE, SS, FRAMES = 32, 8, 16
BIG = SIZE * SS
# the machine's sixteen, by index
TRANSPARENT, BLACK, GREEN, BROWN, DGREY, LGREEN, YELLOW = 0, 0, 5, 9, 11, 13, 7
WHITE, GREY, LGREY, BLUE, LBLUE = 1, 12, 15, 6, 14      # the bird's (index 0 is transparent, so never black)
# a part that wins its pixel with at least this share of the 8x8 block
PRIORITY = {DGREY: 0.20, BROWN: 0.30}


def draw_turtle():
    """The turtle facing up, at 8x, centre at the middle of the canvas."""
    im = Image.new("L", (BIG, BIG), TRANSPARENT)
    d = ImageDraw.Draw(im)
    c = BIG // 2

    def ell(cx, cy, rx, ry, fill, outline=None, width=0):
        d.ellipse((c + cx - rx, c + cy - ry, c + cx + rx, c + cy + ry), fill=fill, outline=outline, width=width)

    # flippers: front pair reach forward and out, back pair smaller
    for sx in (-1, 1):
        ell(sx * 46, -30, 20, 13, LGREEN, GREEN, 5)
        ell(sx * 40, 50, 15, 11, LGREEN, GREEN, 5)
    # tail
    d.polygon([(c - 9, c + 64), (c + 9, c + 64), (c, c + 86)], fill=LGREEN, outline=GREEN)
    # head, poking out of the front of the shell
    ell(0, -76, 21, 23, LGREEN, GREEN, 5)
    # shell: a dome with a brown rim, a lighter plate in the middle, and the
    # lines between the plates
    ell(0, 8, 54, 64, GREEN, BROWN, 9)
    hexr = 20
    hexagon = [(c + hexr * dx, c + 8 + hexr * dy) for dx, dy in
               ((0, -1.25), (1.05, -0.6), (1.05, 0.6), (0, 1.25), (-1.05, 0.6), (-1.05, -0.6))]
    d.polygon(hexagon, fill=LGREEN, outline=GREEN)
    for (x0, y0), (x1, y1) in (((0, -17), (0, -52)), ((0, 33), (0, 66)),
                               ((21, -4), (46, -22)), ((-21, -4), (-46, -22)),
                               ((21, 20), (46, 38)), ((-21, 20), (-46, 38))):
        d.line((c + x0, c + 8 + y0, c + x1, c + 8 + y1), fill=BROWN, width=5)
    # eyes: two dark dots, and a glint of yellow in each
    for sx in (-1, 1):
        ell(sx * 10, -84, 6, 6, DGREY)
    return im


def draw_bird():
    """A bird seen from above, facing up, at 8x -- LOGO's other traditional
    shape (Atari Logo's turtles took shapes: "cars, planes, human figures,
    animals").  Wings swept back a little, so the heading is unmistakable."""
    im = Image.new("L", (BIG, BIG), TRANSPARENT)
    d = ImageDraw.Draw(im)
    c = BIG // 2

    def ell(cx, cy, rx, ry, fill, outline=None, width=0):
        d.ellipse((c + cx - rx, c + cy - ry, c + cx + rx, c + cy + ry), fill=fill, outline=outline, width=width)

    # wings: swept back from the shoulders, the far tip lower than the root
    for sx in (-1, 1):
        d.polygon([(c + sx * 14, c - 24), (c + sx * 104, c + 6), (c + sx * 96, c + 34), (c + sx * 16, c + 26)],
                  fill=LGREY, outline=GREY)
        for k in range(1, 4):                      # the flight feathers
            d.line((c + sx * (30 + k * 18), c + 12 + k * 4, c + sx * (26 + k * 18), c + 30), fill=GREY, width=4)
    # tail
    d.polygon([(c - 20, c + 46), (c + 20, c + 46), (c + 10, c + 92), (c - 10, c + 92)], fill=LGREY, outline=GREY)
    # body and head
    ell(0, 10, 26, 52, WHITE, GREY, 5)
    ell(0, -54, 22, 22, WHITE, GREY, 5)
    # beak, pointing the way it flies
    d.polygon([(c - 9, c - 72), (c + 9, c - 72), (c, c - 98)], fill=YELLOW, outline=BROWN)
    # eyes
    for sx in (-1, 1):
        ell(sx * 11, -58, 5, 5, DGREY)
    return im


# what each shape draws, what it must not lose in the vote, and where it goes
SHAPES = {
    "turtle": (draw_turtle, {DGREY: 0.20, BROWN: 0.30}, "TURTLE.SPR"),
    "bird":   (draw_bird, {DGREY: 0.18, YELLOW: 0.22, GREY: 0.28}, "BIRD.SPR"),
}


def reduce(big):
    """8x8 blocks to pixels, by vote -- with the small parts favoured."""
    px = big.load()
    out = bytearray(SIZE * SIZE)
    for y in range(SIZE):
        for x in range(SIZE):
            counts = {}
            for yy in range(y * SS, y * SS + SS):
                for xx in range(x * SS, x * SS + SS):
                    v = px[xx, yy]
                    counts[v] = counts.get(v, 0) + 1
            n = SS * SS
            pick = None
            for colour, share in PRIORITY.items():
                if counts.get(colour, 0) >= share * n:
                    pick = colour
                    break
            if pick is None:
                if counts.get(TRANSPARENT, 0) * 2 > n:
                    pick = TRANSPARENT
                else:
                    solid = {k: v for k, v in counts.items() if k != TRANSPARENT}
                    pick = max(solid, key=solid.get)
            out[y * SIZE + x] = pick
    return bytes(out)


def palette():
    rgb = {}
    for line in PAL.read_text().splitlines():
        parts = line.split()
        if len(parts) == 4 and not line.startswith("#"):
            rgb[int(parts[0], 16)] = tuple(int(p, 16) for p in parts[1:])
    return rgb


def preview(frames, path):
    """All sixteen, 4x, on the console's blue, two rows of eight."""
    rgb = palette()
    scale, pad = 4, 8
    w, h = 8 * (SIZE * scale + pad) + pad, 2 * (SIZE * scale + pad) + pad
    im = Image.new("RGB", (w, h), rgb[6])
    for k, f in enumerate(frames):
        ox = pad + (k % 8) * (SIZE * scale + pad)
        oy = pad + (k // 8) * (SIZE * scale + pad)
        for y in range(SIZE):
            for x in range(SIZE):
                v = f[y * SIZE + x]
                if v:
                    for dy in range(scale):
                        for dx in range(scale):
                            im.putpixel((ox + x * scale + dx, oy + y * scale + dy), rgb[v])
    im.save(path)


def main():
    global PRIORITY, OUT
    shape = sys.argv[sys.argv.index("--shape") + 1].lower() if "--shape" in sys.argv else "turtle"
    if shape not in SHAPES:
        raise SystemExit(f"mkturtle: no shape called {shape} (have: {', '.join(sorted(SHAPES))})")
    draw, PRIORITY, name = SHAPES[shape]
    OUT = pathlib.Path(sys.argv[sys.argv.index("--out") + 1]) if "--out" in sys.argv else REPO / "fs/LANG/LOGO" / name
    big = draw()
    frames = []
    for k in range(FRAMES):
        # PIL turns counter-clockwise; LOGO's heading is clockwise
        turned = big.rotate(-k * 360.0 / FRAMES, resample=Image.NEAREST, fillcolor=TRANSPARENT)
        frames.append(reduce(turned))
    OUT.write_bytes(b"".join(frames))
    print(f"mkturtle: {shape}, {FRAMES} frames of {SIZE}x{SIZE} -> {OUT} ({len(frames) * SIZE * SIZE} bytes)")
    if "--preview" in sys.argv:
        p = sys.argv[sys.argv.index("--preview") + 1]
        preview(frames, p)
        print(f"mkturtle: preview -> {p}")


if __name__ == "__main__":
    main()
