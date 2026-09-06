#!/usr/bin/env python3
"""The Game Boy tileset and Fluffy -> VICKY: data/gbplatformer/ -> demo/fluffy.bin + demo/fluffy.h

fluffy.bin, dropped at FLUFFY_PHYS by the K4SG header:
   +0            tiles: 170 x 16x16 at 4 bpp, 128 bytes each (two pixels a
                 byte, high nibble left): a blank, then the sheet's 13x13 grid
   +FL_SPR       sprites, 16x16 4 bpp, 128 bytes each: Fluffy idle, walk 1-4,
                 jump, fall; the gem (2 frames); the slug (2 frames); a puff
   +FL_MAP       the two levels, FL_MAPW x 15 entries of 2 bytes each
fluffy.h: the four greens, the sizes, tile_kind[] (0 air, 1 solid, 2 spikes,
3 goal, 4 a platform you can stand on but pass through from below), and
the entities (gems, slugs, the start) per level.  The level is the game's
own; the sheet only gave the bricks."""
import os, struct, random
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "data", "gbplatformer")
OUT = os.path.join(HERE, "..", "demo")
sheet = Image.open(os.path.join(SRC, "gameboy_tileset.png")).convert("RGBA")
tones = sorted({px[:3] for px in (sheet.get_flattened_data() if hasattr(sheet, "get_flattened_data") else sheet.getdata()) if px[3] >= 128 and px[:3] != (255, 255, 255)},
               key=lambda c: -(c[0] * 299 + c[1] * 587 + c[2] * 114))          # light .. dark = 1 .. 4
assert len(tones) == 4, tones
tone_idx = {c: i + 1 for i, c in enumerate(tones)}
def idx(px):
    if px[3] < 128 or px[:3] == (255, 255, 255): return 0
    c = px[:3]
    if c in tone_idx: return tone_idx[c]
    return min(tone_idx.items(), key=lambda kv: sum((a - b) ** 2 for a, b in zip(kv[0], c)))[1]
def pack(pixels):                                   # 256 indices -> 128 bytes
    b = bytearray()
    for i in range(0, 256, 2): b.append((pixels[i] << 4) | pixels[i + 1])
    return b
def cell(im, x0, y0):
    return pack([idx(im.getpixel((x0 + x, y0 + y))) for y in range(16) for x in range(16)])

tiles = bytearray(128)                              # tile 0 is air: blank, so a map of zeros is sky
for t in range(169):
    r, c = divmod(t, 13); tiles += cell(sheet, c * 16, r * 16)
NT = 170                                            # sheet cell c is tile c + 1

# sprites
spr = bytearray()
walk = Image.open(os.path.join(SRC, "IdleAndWalk_strip5.png")).convert("RGBA")
for f in range(5): spr += cell(walk, f * 16, 0)
for name in ("Jump.png", "Fall.png"): spr += cell(Image.open(os.path.join(SRC, name)).convert("RGBA"), 0, 0)
def draw(rows):                                     # 16 strings of 16 chars: ' ' 0, '1'..'4' tones
    return pack([0 if ch == ' ' else int(ch) for row in rows for ch in row])
GEM_A = ["                ", "                ", "       44       ", "      4114      ", "     411124     ", "    41111224    ",
         "    41112224    ", "     412224     ", "      4224      ", "       44       ", "                ", "                ",
         "                ", "                ", "                ", "                "]
GEM_B = ["                ", "                ", "       44       ", "      4224      ", "     422214     ", "    42221114    ",
         "    42211114    ", "     421114     ", "      4114      ", "       44       ", "                ", "                ",
         "                ", "                ", "                ", "                "]
SLUG_A = ["                ", "                ", "                ", "                ", "      4444      ", "    44322344    ",
          "   4322222234   ", "   4321122234   ", "  432211222234  ", "  432222222234  ", "  432222222234  ", " 43222222222234 ",
          " 43322223322334 ", " 44433344433444 ", "  4 4 4  4 4 4  ", "                "]
SLUG_B = ["                ", "                ", "                ", "                ", "                ", "      4444      ",
          "    44322344    ", "   4322222234   ", "   4321122234   ", "  432211222234  ", "  432222222234  ", " 43222222222234 ",
          " 43322223322334 ", " 44433344433444 ", "   4 4 4 4 4 4  ", "                "]
PUFF = ["                ", "                ", "                ", "     1    1     ", "    1 1  1 1    ", "     1 11 1     ",
        "      1  1      ", "    11    11    ", "      1  1      ", "     1 11 1     ", "    1 1  1 1    ", "     1    1     ",
        "                ", "                ", "                ", "                "]
for rows in (GEM_A, GEM_B, SLUG_A, SLUG_B, PUFF): spr += draw(rows)
SPR_NAMES = ["SP_IDLE", "SP_WALK1", "SP_WALK2", "SP_WALK3", "SP_WALK4", "SP_JUMP", "SP_FALL", "SP_GEM0", "SP_GEM1", "SP_SLUG0", "SP_SLUG1", "SP_PUFF"]

# tile roles
def C(cell_no): return cell_no + 1                 # sheet cell -> tile number
T_GRASS, T_DIRT, T_BRICK, T_CRATE, T_EYE, T_SMILE, T_DOT = C(43), C(56), C(14), C(12), C(1), C(2), C(3)
T_LEAF_L, T_LEAF_M, T_LEAF_R = C(10), C(11), C(12)
T_SPIKE = C(61)
T_PIPE_TL, T_PIPE_TR, T_PIPE_L, T_PIPE_R = C(136), C(137), C(149), C(150)
T_GRASS_L, T_GRASS_R = C(42), C(44)
kind = [0] * NT
for t in (T_GRASS, T_DIRT, T_BRICK, T_CRATE, T_EYE, T_DOT, T_PIPE_TL, T_PIPE_TR, T_PIPE_L, T_PIPE_R, T_GRASS_L, T_GRASS_R): kind[t] = 1
for t in (T_LEAF_L, T_LEAF_M, T_LEAF_R): kind[t] = 4
kind[T_SPIKE] = 2; kind[T_SMILE] = 3

H = 15
class Level:
    def __init__(self, w):
        self.w = w; self.m = [[0] * w for _ in range(H)]; self.gems = []; self.slugs = []; self.start = (2, 12)
    def ground(self, x0, x1, top):                  # columns x0..x1 filled from row `top` down
        for x in range(x0, x1 + 1):
            for y in range(top, H): self.m[y][x] = T_GRASS if y == top else T_DIRT
    def platform(self, x, y, n):
        for i in range(n): self.m[y][x + i] = T_LEAF_L if i == 0 else T_LEAF_R if i == n - 1 else T_LEAF_M
    def row(self, x, y, n, t):
        for i in range(n): self.m[y][x + i] = t
    def pipe(self, x, top):
        self.m[top][x] = T_PIPE_TL; self.m[top][x + 1] = T_PIPE_TR
        for y in range(top + 1, H):
            if self.m[y][x] == 0: self.m[y][x] = T_PIPE_L
            if self.m[y][x + 1] == 0: self.m[y][x + 1] = T_PIPE_R
    def spikes(self, x0, x1, y):
        for x in range(x0, x1 + 1): self.m[y][x] = T_SPIKE
    def gem(self, x, y): self.gems.append((x, y))
    def gemrow(self, x, y, n, step=1):
        for i in range(n): self.gem(x + i * step, y)
    def slug(self, x, y): self.slugs.append((x, y))
    def goal(self, x, y): self.m[y][x] = T_SMILE

# level 1: by hand, eight screens
L1 = Level(160); L = L1
L.ground(0, 22, 12);   L.gemrow(6, 10, 3); L.slug(14, 11)
L.row(9, 9, 1, T_EYE); L.row(12, 9, 3, T_BRICK); L.gemrow(12, 7, 3)
L.ground(25, 40, 12);  L.spikes(23, 24, 12); L.slug(30, 11); L.slug(36, 11)
L.platform(28, 8, 4);  L.gemrow(28, 6, 4)
L.pipe(38, 9);         L.gem(38, 7); L.gem(39, 7)
L.ground(43, 60, 11);  L.spikes(41, 42, 12); L.ground(41, 42, 13)
L.row(46, 8, 5, T_BRICK); L.row(48, 8, 1, T_EYE); L.gemrow(46, 6, 5); L.slug(52, 10); L.slug(57, 10)
L.platform(62, 9, 3); L.platform(66, 7, 3); L.platform(70, 9, 3); L.gem(63, 7); L.gem(67, 5); L.gem(71, 7)
L.ground(74, 88, 12);  L.pipe(80, 8); L.slug(76, 11); L.slug(86, 11); L.gemrow(74, 10, 6)
L.ground(90, 100, 10); L.spikes(89, 89, 12); L.ground(89, 89, 13); L.row(93, 7, 4, T_CRATE); L.gemrow(93, 5, 4); L.slug(97, 9)
L.ground(103, 118, 12); L.spikes(101, 102, 12); L.ground(101, 102, 13)
L.row(106, 9, 2, T_DOT); L.row(110, 7, 2, T_DOT); L.row(114, 5, 2, T_DOT); L.gemrow(106, 7, 2); L.gemrow(110, 5, 2); L.gemrow(114, 3, 2)
L.slug(108, 11); L.slug(116, 11)
L.ground(121, 135, 11); L.spikes(119, 120, 12); L.ground(119, 120, 13); L.spikes(126, 128, 10); L.platform(125, 7, 5); L.gemrow(125, 5, 5)
L.slug(132, 10)
L.ground(138, 159, 12); L.spikes(136, 137, 12); L.ground(136, 137, 13)
L.row(141, 9, 3, T_BRICK); L.row(146, 7, 3, T_BRICK); L.row(151, 5, 3, T_BRICK); L.gemrow(141, 7, 3); L.gemrow(146, 5, 3); L.gemrow(151, 3, 3)
L.slug(144, 11); L.slug(149, 11); L.slug(154, 11)
L.goal(157, 11)

# level 2: longer, generated from a seed; the same pieces, more of everything
random.seed(4510)
L2 = Level(200); L = L2
x = 0; top = 12
L.ground(0, 14, 12); L.gemrow(4, 10, 4)
x = 15
while x < 190:
    gap = random.choice([2, 2, 3, 3, 3])
    if random.random() < 0.4: L.spikes(x, x + gap - 1, 12); L.ground(x, x + gap - 1, 13)
    x += gap
    run = random.randint(8, 16); top = random.choice([10, 11, 12, 12])
    if x + run > 196: run = 196 - x
    L.ground(x, x + run - 1, top)
    piece = random.randint(0, 4)
    if piece == 0 and run >= 8: L.platform(x + 2, top - 4, 4); L.gemrow(x + 2, top - 6, 4)
    elif piece == 1 and run >= 6: L.row(x + 2, top - 3, 3, T_BRICK); L.gemrow(x + 2, top - 5, 3)
    elif piece == 2 and run >= 6: L.pipe(x + run // 2, top - 2); L.gem(x + run // 2, top - 4); L.gem(x + run // 2 + 1, top - 4)
    elif piece == 3 and run >= 10: L.spikes(x + 4, x + 5, top); L.platform(x + 3, top - 3, 4); L.gemrow(x + 3, top - 5, 4)
    else: L.gemrow(x + 1, top - 2, min(run - 2, 5))
    for _ in range(random.randint(1, 3)):
        sx = x + random.randint(1, run - 2)
        if L.m[top][sx] == T_GRASS and L.m[top - 1][sx] == 0: L.slug(sx, top - 1)
    x += run
L.ground(196, 199, 12); L.goal(198, 11)

mp = bytearray(); ents = []
for lv in (L1, L2):
    for y in range(H):
        for x in range(lv.w): mp += struct.pack("<H", lv.m[y][x])
bin_ = bytes(tiles) + bytes(spr) + bytes(mp)
open(os.path.join(OUT, "fluffy.bin"), "wb").write(bin_)
with open(os.path.join(OUT, "fluffy.h"), "w") as f:
    f.write("/* generated by tools/mkfluffy.py from data/gbplatformer -- do not edit.\n * Game Boy Platformer Tileset & Character by Chloe Wolfe, CC0. */\n")
    f.write(f"#define FL_NTILES {NT}\n#define FL_SPR {len(tiles)}UL\n#define FL_MAP {len(tiles) + len(spr)}UL\n#define FL_MAP2 {len(tiles) + len(spr) + L1.w * H * 2}UL\n")
    f.write(f"#define FL_MAPW1 {L1.w}\n#define FL_MAPW2 {L2.w}\n#define FL_MAPH {H}\n")
    for i, n in enumerate(SPR_NAMES): f.write(f"#define {n} {i}\n")
    f.write("static const uint8_t fl_tone[4][3] = {" + ",".join("{%d,%d,%d}" % c for c in tones) + "};\n")
    f.write("static const uint8_t tile_kind[FL_NTILES] = {" + ",".join(str(k) for k in kind) + "};\n")
    for n, lv in (("1", L1), ("2", L2)):
        f.write(f"#define FL_NGEM{n} {len(lv.gems)}\n#define FL_NSLUG{n} {len(lv.slugs)}\n")
        f.write(f"static const uint8_t gems{n}[][2] = {{" + ",".join("{%d,%d}" % g for g in lv.gems) + "};\n")
        f.write(f"static const uint8_t slugs{n}[][2] = {{" + ",".join("{%d,%d}" % g for g in lv.slugs) + "};\n")
print(f"fluffy.bin: {len(tiles)} tile + {len(spr)} sprite + {len(mp)} map = {len(bin_)}; level 1: {len(L1.gems)} gems {len(L1.slugs)} slugs; level 2: {len(L2.gems)} gems {len(L2.slugs)} slugs")
