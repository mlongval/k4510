#!/usr/bin/env python3
"""Convert a C64 character ROM to a ready-to-load CP437 screen font.

A C64/ZX chargen is 4096 bytes: two 2048-byte charsets in Commodore
SCREEN-CODE order, not ASCII.  The K4510's screen fonts are 2048 bytes in
CP437 order (kernel8, unscii).  This tool does the rearrangement ONCE, at
import time, and -- unlike the old runtime petscii_to_ascii() it replaces --
bakes in the ASCII characters a Commodore set never had (backslash, { } | ~
^ `) and the CP437 box-drawing/shading/accents, taken from a reference CP437
font.  Nothing converts at run time any more, so nothing can silently draw a
pound where a program wrote a backslash (Doc, 2026-09-10).

  tools/mkcp437font.py IN.bin OUT.bin [--swap] [--ref data/font8.bin]

--swap: the upper/lower charset is the FIRST half (ZX Origins ships it that
way); without it the second half is used (the open-roms and C64 order).
"""
import sys, argparse

def convert(cg, ref, swap=False):
    lo = 0 if swap else 2048                 # offset of the upper/lower charset
    def g(n): return cg[lo + n*8 : lo + n*8 + 8]
    out = bytearray(2048)
    # ASCII $20-$7F from the charset, screen-code -> ASCII, exactly as the old
    # petscii_to_ascii did -- but backslash is left for the reference fill.
    for c in range(0x20, 0x80):
        gi = None
        if c < 0x40: gi = c
        elif c == 0x40: gi = 0
        elif c <= 0x5A: gi = c
        elif c == 0x5B: gi = 0x1B
        elif c == 0x5D: gi = 0x1D
        elif c == 0x5C: gi = None            # backslash: not in a C64 set -> reference
        elif c == 0x5E: gi = 0x1E
        elif c == 0x5F: gi = 0x64
        elif c == 0x60: gi = 0x27
        elif c <= 0x7A: gi = c - 0x60
        elif c == 0x7B: gi = 0x73
        elif c == 0x7C: gi = 0x5D
        elif c == 0x7D: gi = 0x6B
        else: gi = 0x40
        if gi is not None: out[c*8:c*8+8] = g(gi)
    box = [(0xC4,0x40),(0xB3,0x5D),(0xDA,0x70),(0xBF,0x6E),(0xC0,0x6D),(0xD9,0x7D),
           (0xC3,0x6B),(0xB4,0x73),(0xC5,0x5B),(0xC1,0x71),(0xC2,0x72),(0xDB,0xE0),
           (0xB0,0x66),(0xB1,0x66),(0xB2,0x66),(0xCD,0x40),(0xBA,0x5D),(0xC9,0x70),
           (0xBB,0x6E),(0xC8,0x6D),(0xBC,0x7D),(0xFB,0xBA),(0x10,0x3E),(0x1B,0x3C)]
    for a, gi in box: out[a*8:a*8+8] = g(gi)
    # every glyph the charset leaves blank -- backslash, the rest of CP437 --
    # comes from the reference font, so the result is a COMPLETE CP437 page.
    for c in range(256):
        if not any(out[c*8:c*8+8]): out[c*8:c*8+8] = ref[c*8:c*8+8]
    return bytes(out)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("inp"); ap.add_argument("out")
    ap.add_argument("--swap", action="store_true")
    ap.add_argument("--ref", default="data/font8.bin")
    a = ap.parse_args()
    cg = open(a.inp, "rb").read()
    if len(cg) != 4096: sys.exit(f"{a.inp}: expected a 4096-byte C64 chargen, got {len(cg)}")
    ref = open(a.ref, "rb").read()
    if len(ref) < 2048: sys.exit(f"{a.ref}: reference font must be >= 2048 bytes")
    open(a.out, "wb").write(convert(cg, ref, a.swap))
    print(f"{a.out}: 2048 bytes, CP437")

if __name__ == "__main__":
    main()
