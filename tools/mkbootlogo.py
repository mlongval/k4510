#!/usr/bin/env python3
"""Generate the K4510 boot logo: data/bootlogo.png, 1920x1080 (the Dell's panel).

Made of the machine's own parts, so the boot screen and the machine's face
(rom/kernal.c, banner()) read as one thing:
  - the five colour bars, 4:3:2:3:4 wide -- Doc's proportions -- in the
    banner's colours (VIC-II 2, 8, 7, 5, 14), with square ends, as banner()
    draws them on the screen (a first draft cut them to points; Doc: "the
    bars are not pointy on the real screen, please keep it uniform");
  - "K4510" in the machine's 8x8 font (data/font8.bin), blown up so its
    height is the bars' height;
  - FANTASY COMPUTER in the console's yellow, on the console's blue.

    python3 tools/mkbootlogo.py [out.png] [width height]

Doc, 2026-09-12: "can you make a cool startup logo".
"""
import sys
from PIL import Image, ImageDraw

# the VIC-II sixteen as this machine boots them (fs/SYSTEM/ETC/PALETTES/C64.PAL)
BLUE, WHITE, YELLOW = (0x00, 0x00, 0xAA), (0xFF, 0xFF, 0xFF), (0xEE, 0xEE, 0x77)
BARS = [(16, (0x88, 0x00, 0x00)),   # red
        (12, (0xDD, 0x88, 0x55)),   # orange
        (8,  (0xEE, 0xEE, 0x77)),   # yellow
        (12, (0x00, 0xCC, 0x55)),   # green
        (16, (0x00, 0x88, 0xFF))]   # light blue


def glyph(font, ch):
    return font[ord(ch) * 8:ord(ch) * 8 + 8]


def text(draw, font, s, x, y, scale, colour):
    """the machine's font, each pixel a scale x scale square; returns the width"""
    for n, ch in enumerate(s):
        for row, bits in enumerate(glyph(font, ch)):
            for col in range(8):
                if bits & (0x80 >> col):
                    px, py = x + (n * 8 + col) * scale, y + row * scale
                    draw.rectangle([px, py, px + scale - 1, py + scale - 1], fill=colour)
    return len(s) * 8 * scale


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "data/bootlogo.png"
    W, H = (int(sys.argv[2]), int(sys.argv[3])) if len(sys.argv) > 3 else (1920, 1080)
    font = open("data/font8.bin", "rb").read()
    img = Image.new("RGB", (W, H), BLUE)
    d = ImageDraw.Draw(img)

    cell = W // 48                       # one character cell of the banner, 40 px at 1920
    rows = len(BARS) * cell              # the bar block's height
    big = rows // 8                      # "K4510" as tall as the bars
    name_w = 5 * 8 * big
    gap = 2 * cell
    bars_w = 16 * cell                   # the widest bar
    x0 = (W - (bars_w + gap + name_w)) // 2
    y0 = (H - rows) // 2 - cell

    for r, (w, colour) in enumerate(BARS):         # square ends, as the banner draws them
        top, bot = y0 + r * cell, y0 + (r + 1) * cell - 1
        d.rectangle([x0, top, x0 + w * cell - 1, bot], fill=colour)

    xn = x0 + bars_w + gap
    text(d, font, "K4510", xn, y0, big, WHITE)
    small = cell // 8 * 2                # FANTASY COMPUTER: 16 px letters' pixels at 1920
    sub = "FANTASY COMPUTER"
    sw = len(sub) * 8 * small
    text(d, font, sub, xn + name_w - sw, y0 + rows + cell, small, YELLOW)

    img.save(out, optimize=True)
    print("%s: %dx%d" % (out, W, H))


if __name__ == "__main__":
    main()
