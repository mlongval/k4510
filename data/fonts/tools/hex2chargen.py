# Build a font8.bin-compatible chargen from a Unifont-style .hex file
# (e.g. unscii-8.hex, public domain): 256 glyphs x 8 rows, MSB-first =
# leftmost pixel, CP437 layout ($20-$7E ASCII, rest box drawing/accents),
# glyph 0 blank — the same contract as data/mkfont.py in the k4510 repo.
#   python3 hex2chargen.py unscii-8.hex font8-unscii.bin
# With a third argument of 16 it reads the 8x16 glyphs instead (32 hex
# digits a row) and writes 4096 bytes -- the side panel's font.
#   python3 hex2chargen.py unscii-16.hex font16-unscii.bin 16
import sys

rows = int(sys.argv[3]) if len(sys.argv) > 3 else 8

glyphs = {}
for line in open(sys.argv[1]):
    line = line.strip()
    if not line or ':' not in line:
        continue
    cp, bits = line.split(':', 1)
    if len(bits) == rows * 2:                # 8-wide glyphs of the wanted height (1 byte a row)
        glyphs[int(cp, 16)] = bytes.fromhex(bits)

def hflip(g):                                # mirror each row left<->right
    return bytes(int(f"{b:08b}"[::-1], 2) for b in g)

font = bytearray(256 * rows)
missing = []
for i in range(256):
    if i == 0:
        continue                             # glyph 0 stays blank (text layer's "nothing")
    u = ord(bytes([i]).decode('cp437'))      # CP437 position -> Unicode codepoint
    g = glyphs.get(u)
    if g is None and u == 0x2310 and 0xAC in glyphs:
        g = hflip(glyphs[0xAC])              # reversed-not = mirrored not sign
    if g is None and u == 0x2219:
        g = glyphs.get(0x00B7) or glyphs.get(0x2022)   # bullet operator ~ middle dot
    if g is None:
        missing.append((i, u))
        continue                             # missing glyph stays blank
    font[i*rows:i*rows+rows] = g

open(sys.argv[2], 'wb').write(font)
print(f"{sys.argv[2]}: {256 * rows} bytes, {256 - 1 - len(missing)} glyphs filled")
for i, u in missing:
    print(f"  missing: CP437 {i:#04x} -> U+{u:04X}")
