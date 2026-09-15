#!/usr/bin/env python3
"""tools/mkk4510font.py -- build the machine's two fonts from unscii, in the
K4510 code page (core/codepage.h): data/fonts/unscii/font8-unscii.bin (8x8)
and font16-unscii.bin (8x16).  The CP437 layout they had is kept beside them
as font8-cp437.bin / font16-cp437.bin (for a BBS, later).  Stand-ins where
unscii has no glyph: the house (U+2302) is a small triangle, the sun its
asterisk, and the German low quote is its closing quote set on the baseline.

    tools/mkk4510font.py            (from the checkout's root)
"""
import os, re, shutil

D = "data/fonts/unscii"

def codepage():
    src = open("core/codepage.h").read()
    vals = [int(v, 16) for v in re.findall(r"0x([0-9A-F]{4})", src.split("k4510_cp[256]")[1])]
    assert len(vals) == 256
    return vals

def glyphs(fn):
    g = {}
    for line in open(os.path.join(D, fn)):
        k, v = line.strip().split(":")
        g[int(k, 16)] = bytes.fromhex(v)
    return g

def build(fn, out, h):
    g, cp = glyphs(fn), codepage()
    blank = bytes(h)
    star = g[0x2A]
    font = bytearray()
    for b, u in enumerate(cp):
        if b == 0 or u == 0x00A0:
            gl = blank
        elif u in g and len(g[u]) == h:
            gl = g[u]
        elif u == 0x2302:
            gl = g.get(0x25B3, star)
        elif u == 0x263C:
            gl = star
        elif u == 0x201E:                                  # low double quote: the closing one, on the baseline
            src = g[0x201D]
            rows = [i for i in range(h) if src[i]]
            top, bot = rows[0], rows[-1]
            base = h - 2 if h == 8 else h - 3
            gl = bytearray(h)
            for i in range(top, bot + 1):
                gl[base - (bot - i)] = src[i]
            gl = bytes(gl)
        else:
            raise SystemExit("no glyph for U+%04X at $%02X" % (u, b))
        font += gl
    assert len(font) == 256 * h
    open(os.path.join(D, out), "wb").write(font)

for size, h in (("8", 8), ("16", 16)):
    cur, keep = os.path.join(D, "font%s-unscii.bin" % size), os.path.join(D, "font%s-cp437.bin" % size)
    if not os.path.exists(keep):
        shutil.copyfile(cur, keep)                         # the CP437 layout, as it was
    build("unscii-%s.hex" % size, "font%s-unscii.bin" % size, h)
print("fonts built")
