#!/usr/bin/env python3
# Build the K4510x Linux console font: the IBM VGA ROM glyphs (code page 437)
# at 24x43, so a 1920x1080 console is exactly 80x25 (1920/80 = 24,
# 1080/25 = 43.2).  Source: the Linux kernel's 8x16 console font,
# lib/fonts/font_8x16.c (GPL-2.0), which is the VGA ROM set in CP437 order.
#
# Each glyph is widened to the VGA's 9-dot cell (column 8 repeated into
# column 9 for the line-drawing block C0-DF, as the real card did), then
# area-sampled up to 24x43.  That is 2.67 x 2.69: the 720x400 text mode
# stretched onto the panel with nearly square pixels.  Output is PSF2 with a
# Unicode table, so UTF-8 text finds the CP437 glyphs.
#   python3 data/mkconsolefont.py font_8x16.c \
#       linux/config/includes.chroot/usr/share/consolefonts/K4510-VGA24x43.psf [preview.png]
import re, struct, sys

W, H = 24, 43          # output cell
SW, SH = 9, 16         # the VGA's text cell

# CP437's graphic glyphs in the control range (what the VGA ROM draws there)
LOW = [0x0000, 0x263A, 0x263B, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022,
       0x25D8, 0x25CB, 0x25D9, 0x2642, 0x2640, 0x266A, 0x266B, 0x263C,
       0x25BA, 0x25C4, 0x2195, 0x203C, 0x00B6, 0x00A7, 0x25AC, 0x21A8,
       0x2191, 0x2193, 0x2192, 0x2190, 0x221F, 0x2194, 0x25B2, 0x25BC]


def cp437_unicode(code):
    if code < 0x20:
        return LOW[code]
    if code == 0x7F:
        return 0x2302                                  # the house
    return ord(bytes([code]).decode("cp437"))


src = open(sys.argv[1]).read()
body = src[src.index('FONTDATAMAX, 0 }, {') + 19:]    # after the header struct
vals = [int(v, 16) for v in re.findall(r'^\s*0x([0-9a-fA-F]{2}),', body, re.M)][:4096]
assert len(vals) == 4096, len(vals)

cells = []
for code in range(256):
    src9 = []
    for r in vals[code * SH:(code + 1) * SH]:
        bits = [(r >> (7 - x)) & 1 for x in range(8)]
        bits.append(bits[7] if 0xC0 <= code <= 0xDF else 0)
        src9.append(bits)
    cell = []
    for ty in range(H):                                # on at half coverage or more
        y0, y1 = ty * SH / H, (ty + 1) * SH / H
        row = []
        for tx in range(W):
            x0, x1 = tx * SW / W, (tx + 1) * SW / W
            cov = 0.0
            for sy in range(int(y0), min(SH, int(y1 + 0.999))):
                oy = min(y1, sy + 1) - max(y0, sy)
                for sx in range(int(x0), min(SW, int(x1 + 0.999))):
                    ox = min(x1, sx + 1) - max(x0, sx)
                    if oy > 0 and ox > 0 and src9[sy][sx]:
                        cov += ox * oy
            row.append(cov >= 0.5 * (x1 - x0) * (y1 - y0))
        cell.append(row)
    cells.append(cell)

bpr = (W + 7) // 8
glyphs = bytearray()
for cell in cells:
    for row in cell:
        v = 0
        for x, b in enumerate(row):
            v |= b << (bpr * 8 - 1 - x)
        glyphs += v.to_bytes(bpr, "big")
table = bytearray()
for code in range(256):
    if code:
        table += chr(cp437_unicode(code)).encode("utf-8")
    if code == 0x20:
        table += " ".encode("utf-8")             # no-break space draws as a space
    table += b"\xff"
hdr = struct.pack("<8I", 0x864AB572, 0, 32, 1, 256, bpr * H, H, W)
open(sys.argv[2], "wb").write(hdr + glyphs + table)
print(f"{sys.argv[2]}: {W}x{H} from font_8x16.c (GPL-2.0) -- 1920x1080 is {1920 // W}x{1080 // H}")

if len(sys.argv) > 3:                                  # a sheet of all 256, to look at
    from PIL import Image
    img = Image.new("1", (16 * (W + 1), 16 * (H + 1)), 1)
    for code, cell in enumerate(cells):
        ox, oy = (code % 16) * (W + 1), (code // 16) * (H + 1)
        for y, row in enumerate(cell):
            for x, b in enumerate(row):
                if b:
                    img.putpixel((ox + x, oy + y), 0)
    img.save(sys.argv[3])
