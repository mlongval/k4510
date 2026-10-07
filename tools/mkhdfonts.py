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

def double8(rows8):                       # 8-wide rows (ints) -> 16x32 rows (ints)
    out = []
    for r in rows8:
        w = 0
        for b in range(8):
            if r & (0x80 >> b): w |= 0xC000 >> (2 * b)
        out += [w, w]
    return out

def unscii16():
    g = {}
    for line in open("data/fonts/unscii/unscii-16.hex"):
        k, v = line.strip().split(":")
        b = bytes.fromhex(v)
        if len(b) == 16: g[int(k, 16)] = double8(list(b))
    return g

def terminus(d, weight):
    g = {}
    for fn in sorted(glob.glob(os.path.join(d, "*-Terminus%s32x16.psf.gz" % weight)), key=lambda f: (not os.path.basename(f).startswith("Uni"), f)):
        data = gzip.open(fn).read()
        magic, ver, hs, flags, n, bpg, h, w = struct.unpack_from("<8I", data)
        assert magic == 0x864ab572 and (w, h) == (16, 32) and flags & 1, fn
        glyphs = [data[hs + i * bpg: hs + (i + 1) * bpg] for i in range(n)]
        p = hs + n * bpg
        for i in range(n):
            e = data.index(b"\xff", p); seq = data[p:e]; p = e + 1
            rows = [glyphs[i][2 * r] << 8 | glyphs[i][2 * r + 1] for r in range(32)]
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

def write(name, face, fallback, table, tag):
    out, missing = bytearray(), []
    for b, u in enumerate(table):
        if b == 0 or u in (0, 0xA0): rows = [0] * 32
        elif u in face: rows = face[u]
        elif u == 0x201E and 0x201D in face:               # the low double quote: the closing one, on the baseline
            src = face[0x201D]; lit = [i for i in range(32) if src[i]]
            rows = [0] * 32
            for i in lit: rows[min(31, 26 + (i - lit[-1]))] = src[i]
        elif u in fallback: rows = fallback[u]; missing.append("$%02X" % b)
        else: rows = [0] * 32; missing.append("$%02X(blank)" % b)
        for r in rows: out += bytes((r >> 8, r & 0xFF))
    assert len(out) == 16384
    open(os.path.join(OUT, "%s-%s.bin" % (name, tag)), "wb").write(out)
    half = bytearray()
    for b in range(256):
        rows = [out[(b * 32 + r) * 2] << 8 | out[(b * 32 + r) * 2 + 1] for r in range(32)]
        for r in halve(rows): half += bytes((r >> 8, r & 0xFF))
    open(os.path.join(OUT, "%s16-%s.bin" % (name, tag)), "wb").write(half)
    print("%s-%s.bin: %d from unscii %s" % (name, tag, len(missing), " ".join(missing)))

def raster(fn, size=None, wght=None, double=False):
    """A TrueType face drawn without anti-aliasing (FreeType's own hinting) into
    16x32 -- or, double=True, at its design size into 8x16 and doubled."""
    from PIL import Image, ImageDraw, ImageFont
    cw, ch = (8, 16) if double else (16, 32)
    if size is None:                                     # the largest size whose cell fits
        size = 40
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

def halve(rows32):                                       # 16x32 -> 16x16: each pair of rows ORed
    return [rows32[2 * i] | rows32[2 * i + 1] for i in range(16)]

def bdf8(fn):                                            # an 8-wide BDF (Tamzen 8x16), doubled
    g = {}
    for u, rows in bdf(fn, 16, 8).items(): g[u] = double8(rows)
    return g

if __name__ == "__main__":
    tdir, src = sys.argv[1:3]
    S = lambda n: os.path.join(src, n)
    os.makedirs(OUT, exist_ok=True)
    U, K = unscii16(), k4510_page()
    vg = vga(S("font_8x16.c"))
    def boxes(face):                     # the line and block drawings (U+2500-259F) from the VGA, which fill the cell
        for u, r in vg.items():          # and meet their neighbours; a drawn face's own stop short of the edges
            if 0x2500 <= u <= 0x259F: face[u] = r
        return face
    faces = (("zhekov-bold", terminus(tdir, "Bold")), ("zhekov", terminus(tdir, "")),
             ("spleen", bdf(S("spleen-16x32.bdf"))), ("ibm-vga", vg),
             ("atkinson", boxes(raster(S("AtkinsonHyperlegibleMono.ttf"), wght=700))),
             ("go-mono", boxes(raster(S("Go-Mono-Bold.ttf")))), ("fira-mono", boxes(raster(S("FiraMono-Bold.ttf")))),
             ("proggy", boxes(raster(S("ProggyClean.ttf"), size=16, double=True))), ("tamzen-bold", boxes(bdf8(S("Tamzen8x16b.bdf")))))
    for name, face in faces:
        for table, tag in ((K, "k4510"), (CP437, "cp437")):
            write(name, face, U, table, tag)
