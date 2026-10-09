#!/usr/bin/env python3
"""tools/mkhdfonts.py -- the HD text fonts (core/vicky.h, vicky_hd_font), and the
machine's own wide fonts for 1440x1080 in 16x32 and 16x16 cells.

At 720x540 the panel shows each machine pixel as 2x2, so an 8x16 cell is
16x32 of the panel's own pixels; with an HD font the frame is drawn at that
size and the text is sharp while the graphics stay the machine's.  Each font
is 256 glyphs of 16x32, two bytes a row, MSB first (16384 bytes), in both of
the machine's orders: the K4510 code page (core/codepage.h) and strict CP437.
A glyph a face does not have is unscii-16's, doubled.

    tools/mkhdfonts.py TERMINUS_DIR SRC_DIR
      TERMINUS_DIR  console-setup's fonts: *-Terminus{,Bold}32x16.psf.gz
                    (/usr/share/consolefonts on Debian and Ubuntu)
      SRC_DIR       the rest, as VENDORED-FROM.txt names them: spleen-16x32.bdf,
                    font_8x16.c, AtkinsonHyperlegibleMono.ttf, Go-Mono-Bold.ttf,
                    FiraMono-Bold.ttf, ProggyClean.ttf, Tamzen8x16b.bdf
writes data/fonts/hd/<face>-{k4510,cp437}.bin (16x32, 16384 bytes) and
<face>16-{k4510,cp437}.bin (16x16, 8192 bytes: each pair of the 16x32's rows
ORed into one, so a stroke one row thick survives), for every face
(run from the checkout's root).  Provenance: data/fonts/hd/VENDORED-FROM.txt.

And the same at three times, for 480x360 shown at /3 (2026-10-07):
<face>48-*.bin (24x48, three bytes a row, 36864 bytes) and <face>24-*.bin
(24x24, 18432 bytes).  Terminus and Spleen from their 12x24 cuts, doubled
(console-setup's *-Terminus{Bold,}24x12.psf.gz; SRC_DIR/spleen-12x24.bdf);
the TrueType faces drawn at 24x48; the 8x16 faces tripled.
"""
import glob, gzip, os, re, struct, sys

OUT = "data/fonts/hd"
LOW = [0x0000, 0x263A, 0x263B, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022, 0x25D8, 0x25CB, 0x25D9, 0x2642, 0x2640, 0x266A, 0x266B, 0x263C,
       0x25BA, 0x25C4, 0x2195, 0x203C, 0x00B6, 0x00A7, 0x25AC, 0x21A8, 0x2191, 0x2193, 0x2192, 0x2190, 0x221F, 0x2194, 0x25B2, 0x25BC]
CP437 = LOW + list(range(0x20, 0x7F)) + [0x2302] + [ord(bytes([b]).decode("cp437")) for b in range(0x80, 0x100)]

def k4510_page():
    src = open("core/codepage.h").read()
    v = [int(x, 16) for x in re.findall(r"0x([0-9A-F]{4})", src.split("k4510_cp[256]")[1])][:256]
    assert len(v) == 256
    return v

K = 2                                     # 2: 16x32 glyphs; 3: 24x48 (main() runs both)

def widen(rows, w, f):                    # rows w bits wide (ints) -> f times wider and f times taller
    out = []
    for r in rows:
        v = 0
        for b in range(w):
            if r & (1 << (w - 1 - b)):
                for i in range(f): v |= 1 << (w * f - 1 - (b * f + i))
        out += [v] * f
    return out

def double8(rows8):                       # 8-wide rows (ints) -> the HD size: 16x32 (K 2) or 24x48 (K 3)
    return widen(rows8, 8, K)

def unscii16():
    g = {}
    for line in open("data/fonts/unscii/unscii-16.hex"):
        k, v = line.strip().split(":")
        b = bytes.fromhex(v)
        if len(b) == 16: g[int(k, 16)] = double8(list(b))
    return g

# Doc, 2026-10-09: Terminus's R has its leg start at the stem; ours leaves the
# bowl at about half its width, and bows outward (fast to the right first,
# then down to the foot), not inward.  The leg rows, in the source cuts (16x32 and
# 12x24, before any widening), regular and bold.
R_LEG = {
    ("", 32): (18, ["..##...###......", "..##....###.....", "..##.....###....", "..##......###...",
                    "..##.......###..", "..##.......###..", "..##........##..", "..##........##.."]),
    ("Bold", 32): (18, [".###..####......", ".###...####.....", ".###....####....", ".###.....####...",
                        ".###......####..", ".###......####..", ".###.......###..", ".###.......###.."]),
    ("", 24): (12, [".#...#......", ".#....#.....", ".#.....#....", ".#......#...",
                    ".#......#...", ".#.......#..", ".#.......#.."]),
    ("Bold", 24): (12, [".##..##.....", ".##...##....", ".##....##...", ".##.....##..",
                        ".##.....##..", ".##......##.", ".##......##."]),
}

def fix_r(rows, weight, h, w):           # the leg of R, from the bowl (R_LEG); rows as the cut has them, w bits wide
    top, leg = R_LEG[(weight, h)]
    rows = list(rows)
    for i, t in enumerate(leg):
        rows[top + i] = sum(1 << (15 - b) for b, c in enumerate(t) if c == "#")   # psf rows: MSB first in 16 bits
    return rows

def terminus(d, weight):
    g = {}
    size = "32x16" if K == 2 else "24x12"            # at three times, the 12x24 cut doubled
    for fn in sorted(glob.glob(os.path.join(d, "*-Terminus%s%s.psf.gz" % (weight, size))), key=lambda f: (not os.path.basename(f).startswith("Uni"), f)):
        data = gzip.open(fn).read()
        magic, ver, hs, flags, n, bpg, h, w = struct.unpack_from("<8I", data)
        assert magic == 0x864ab572 and (w, h) == ((16, 32) if K == 2 else (12, 24)) and flags & 1, fn
        glyphs = [data[hs + i * bpg: hs + (i + 1) * bpg] for i in range(n)]
        p = hs + n * bpg
        for i in range(n):
            e = data.index(b"\xff", p); seq = data[p:e]; p = e + 1
            rows = [glyphs[i][2 * r] << 8 | glyphs[i][2 * r + 1] for r in range(h)]
            if "R" in seq.split(b"\xfe")[0].decode("utf-8", "ignore"): rows = fix_r(rows, weight, h, w)
            if K == 3: rows = widen([r >> 4 for r in rows], 12, 2)
            for ch in seq.split(b"\xfe")[0].decode("utf-8", "ignore"):
                g.setdefault(ord(ch), rows)
    return g

def bdf(fn, ch=32, cw=16):
    g, asc, enc, bbx, bm = {}, ch - 6, None, None, None
    for line in open(fn, encoding="latin-1"):
        t = line.split()
        if not t: continue
        if t[0] == "FONT_ASCENT": asc = int(t[1])
        elif t[0] == "ENCODING": enc = int(t[1])
        elif t[0] == "BBX": bbx = list(map(int, t[1:5]))
        elif t[0] == "BITMAP": bm = []
        elif t[0] == "ENDCHAR":
            w, h, xo, yo = bbx; rows = [0] * ch; top = asc - (h + yo)
            nb, cb = (w + 7) // 8, cw // 8
            for i, hx in enumerate(bm):
                y = top + i
                if 0 <= y < ch:
                    v = int(hx, 16) << (8 * (cb - nb)) if nb <= cb else int(hx, 16) >> (8 * (nb - cb))
                    rows[y] = (v >> xo) & ((1 << cw) - 1)
            if enc is not None and enc >= 0: g.setdefault(enc, rows)
            bm = None
        elif bm is not None: bm.append(t[0])
    return g

def vga(fn):
    src = open(fn).read()
    body = src[src.index("FONTDATAMAX, 0 }, {") + 19:]
    vals = [int(v, 16) for v in re.findall(r"^\s*0x([0-9a-fA-F]{2}),", body, re.M)][:4096]
    assert len(vals) == 4096
    g = {}
    for c in range(256): g.setdefault(CP437[c], double8(vals[c * 16:(c + 1) * 16]))
    return g

def put_row(out, r):                                     # a glyph row, K bytes, MSB first
    out += bytes((r >> (8 * (K - 1 - i))) & 0xFF for i in range(K))

def write(name, face, fallback, table, tag):
    ch = 16 * K
    out, missing = bytearray(), []
    for b, u in enumerate(table):
        if b == 0 or u in (0, 0xA0): rows = [0] * ch
        elif u in face: rows = face[u]
        elif u == 0x201E and 0x201D in face:               # the low double quote: the closing one, on the baseline
            src = face[0x201D]; lit = [i for i in range(ch) if src[i]]
            rows = [0] * ch
            for i in lit: rows[min(ch - 1, ch * 13 // 16 + (i - lit[-1]))] = src[i]
        elif u in fallback: rows = fallback[u]; missing.append("$%02X" % b)
        else: rows = [0] * ch; missing.append("$%02X(blank)" % b)
        for r in rows: put_row(out, r)
    assert len(out) == 256 * ch * K
    big, small = ("%s-%s.bin", "%s16-%s.bin") if K == 2 else ("%s48-%s.bin", "%s24-%s.bin")
    open(os.path.join(OUT, big % (name, tag)), "wb").write(out)
    half = bytearray()
    for b in range(256):
        rows = [int.from_bytes(out[(b * ch + r) * K:(b * ch + r + 1) * K], "big") for r in range(ch)]
        for r in halve(rows): put_row(half, r)
    open(os.path.join(OUT, small % (name, tag)), "wb").write(half)
    print("%s: %d from unscii %s" % (big % (name, tag), len(missing), " ".join(missing)))

def raster(fn, size=None, wght=None, double=False):
    """A TrueType face drawn without anti-aliasing (FreeType's own hinting) into
    16x32 -- or, double=True, at its design size into 8x16 and doubled."""
    from PIL import Image, ImageDraw, ImageFont
    cw, ch = (8, 16) if double else (8 * K, 16 * K)
    if size is None:                                     # the largest size whose cell fits
        size = 20 * K
        while True:
            f = ImageFont.truetype(fn, size)
            if wght: f.set_variation_by_axes([wght])
            a, d = f.getmetrics()
            if f.getlength("M") <= cw and a + d <= ch: break
            size -= 1
    f = ImageFont.truetype(fn, size)
    if wght: f.set_variation_by_axes([wght])
    a, d = f.getmetrics(); base = (ch - (a + d)) // 2 + a; adv = f.getlength("M")
    def draw(c):
        im = Image.new("1", (cw, ch), 0); dr = ImageDraw.Draw(im); dr.fontmode = "1"
        dr.text(((cw - adv) / 2, base), c, font=f, fill=1, anchor="ls")
        return im
    def rows_of(im):
        px = im.load(); out = []
        for y in range(ch):
            w = 0
            for x in range(cw):
                if px[x, y]: w |= 1 << (cw - 1 - x)
            out.append(w)
        return out
    notdef = rows_of(draw("\uE000"))
    g = {}
    for u in set(CP437) | set(k4510_page()):
        if u < 0x20: continue
        r = rows_of(draw(chr(u)))
        if r == notdef and u != 0x20: continue           # not in the face
        g[u] = double8(r) if double else r
    return g

def halve(rows):                                         # 16x32 -> 16x16 (24x48 -> 24x24): each pair of rows ORed
    return [rows[2 * i] | rows[2 * i + 1] for i in range(len(rows) // 2)]

def bdf8(fn):                                            # an 8-wide BDF (Tamzen 8x16), doubled
    g = {}
    for u, rows in bdf(fn, 16, 8).items(): g[u] = double8(rows)
    return g

def spleen():                                           # 16x32, or at three times the 12x24 cut doubled
    if K == 2: return bdf(S("spleen-16x32.bdf"))
    return {u: widen([r >> 4 for r in rows], 12, 2) for u, rows in bdf(S("spleen-12x24.bdf"), 24, 16).items()}

def build():
    U, KP = unscii16(), k4510_page()
    vg = vga(S("font_8x16.c"))
    def boxes(face):                     # the line and block drawings (U+2500-259F) from the VGA, which fill the cell
        for u, r in vg.items():          # and meet their neighbours; a drawn face's own stop short of the edges
            if 0x2500 <= u <= 0x259F: face[u] = r
        return face
    faces = (("zhekov-bold", terminus(tdir, "Bold")), ("zhekov", terminus(tdir, "")),
             ("spleen", spleen()), ("ibm-vga", vg),
             ("atkinson", boxes(raster(S("AtkinsonHyperlegibleMono.ttf"), wght=700))),
             ("go-mono", boxes(raster(S("Go-Mono-Bold.ttf")))), ("fira-mono", boxes(raster(S("FiraMono-Bold.ttf")))),
             ("proggy", boxes(raster(S("ProggyClean.ttf"), size=16, double=True))), ("tamzen-bold", boxes(bdf8(S("Tamzen8x16b.bdf")))))
    for name, face in faces:
        for table, tag in ((KP, "k4510"), (CP437, "cp437")):
            write(name, face, U, table, tag)

if __name__ == "__main__":
    tdir, src = sys.argv[1:3]
    S = lambda n: os.path.join(src, n)
    os.makedirs(OUT, exist_ok=True)
    for K in (2, 3):
        build()
