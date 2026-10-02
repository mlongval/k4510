#!/usr/bin/env python3
"""Generate the K4510 launcher icon: data/k4510-icon.png, 256x256.

The boot logo (tools/mkbootlogo.py) shrunk to a desktop tile, so the menu,
the splash and the machine's banner read as one thing:
  - the five colour bars, 4:3:2:3:4 wide, square ends, in the banner's
    VIC-II colours;
  - "K4510" under them, in the machine's 8x8 font (unscii-8), white;
  - on the console's blue, in a rounded tile the size GNOME expects, with a
    one-step lighter rim so it holds its edge on a dark panel.
FANTASY COMPUTER is left off: at 48 px it would be noise.

    python3 tools/mkicon.py [out.png] [size]

Doc, 2026-10-02: "Make a cool Icon for the K4510 launcher that matches the
splash screen".
"""
import sys
from PIL import Image, ImageDraw

BLUE, RIM, WHITE = (0x00, 0x00, 0xAA), (0x00, 0x44, 0xDD), (0xFF, 0xFF, 0xFF)
BARS = [(16, (0x88, 0x00, 0x00)),   # red        -- tools/mkbootlogo.py's, which are
        (12, (0xDD, 0x88, 0x55)),   # orange        the banner's (rom/kernal.c)
        (8,  (0xEE, 0xEE, 0x77)),   # yellow
        (12, (0x00, 0xCC, 0x55)),   # green
        (16, (0x00, 0x88, 0xFF))]   # light blue


def text(draw, font, s, x, y, scale, colour):
    for n, ch in enumerate(s):
        for row, bits in enumerate(font[ord(ch) * 8:ord(ch) * 8 + 8]):
            for col in range(8):
                if bits & (0x80 >> col):
                    px, py = x + (n * 8 + col) * scale, y + row * scale
                    draw.rectangle([px, py, px + scale - 1, py + scale - 1], fill=colour)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "data/k4510-icon.png"
    S = int(sys.argv[2]) if len(sys.argv) > 2 else 256
    font = open("data/fonts/unscii/font8-unscii.bin", "rb").read()
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    # the tile: GNOME's icon grid leaves a margin and rounds the corners
    m, r = S // 16, S // 6
    d.rounded_rectangle([m, m, S - 1 - m, S - 1 - m], radius=r, fill=RIM)
    rim = max(2, S // 64)
    d.rounded_rectangle([m + rim, m + rim, S - 1 - m - rim, S - 1 - m - rim], radius=r - rim, fill=BLUE)

    scale = S * 5 // 256                 # "K4510": 40 font pixels wide -> 200 px at 256
    name_w, name_h = 5 * 8 * scale, 8 * scale
    unit = name_w // 16                  # the widest bar is as wide as the name
    bar_h = S * 16 // 256
    gap = S * 14 // 256
    block_h = len(BARS) * bar_h + gap + name_h
    x0 = (S - name_w) // 2
    y0 = (S - block_h) // 2

    for i, (w, colour) in enumerate(BARS):
        top = y0 + i * bar_h
        d.rectangle([x0, top, x0 + w * unit - 1, top + bar_h - 1], fill=colour)
    text(d, font, "K4510", x0, y0 + len(BARS) * bar_h + gap, scale, WHITE)

    img.save(out, optimize=True)
    print("%s: %dx%d" % (out, S, S))


if __name__ == "__main__":
    main()
