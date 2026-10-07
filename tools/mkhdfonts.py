#!/usr/bin/env python3
"""tools/mkhdfonts.py -- the HD text fonts (core/vicky.h, vicky_hd_font).

At 720x540 the panel shows each machine pixel as 2x2, so an 8x16 cell is
16x32 of the panel's own pixels; with an HD font the frame is drawn at that
size and the text is sharp while the graphics stay the machine's.  Each font
is 256 glyphs of 16x32, two bytes a row, MSB first (16384 bytes), in both of
the machine's orders: the K4510 code page (core/codepage.h) and strict CP437.
A glyph a face does not have is unscii-16's, doubled.

    tools/mkhdfonts.py TERMINUS_DIR SPLEEN_BDF FONT_8X16_C
      TERMINUS_DIR  console-setup's fonts: *-TerminusBold32x16.psf.gz
                    (/usr/share/consolefonts on Debian and Ubuntu)
      SPLEEN_BDF    spleen-16x32.bdf (github.com/fcambus/spleen)
      FONT_8X16_C   the Linux kernel's lib/fonts/font_8x16.c: the IBM VGA ROM
                    set, drawn doubled
writes data/fonts/hd/{zhekov-bold,spleen,ibm-vga}-{k4510,cp437}.bin
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

def terminus(d):
    g = {}
    for fn in sorted(glob.glob(os.path.join(d, "*-TerminusBold32x16.psf.gz")), key=lambda f: (not os.path.basename(f).startswith("Uni"), f)):
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

def bdf(fn):
    g, asc, enc, bbx, bm = {}, 26, None, None, None
    for line in open(fn, encoding="latin-1"):
        t = line.split()
        if not t: continue
        if t[0] == "FONT_ASCENT": asc = int(t[1])
        elif t[0] == "ENCODING": enc = int(t[1])
        elif t[0] == "BBX": bbx = list(map(int, t[1:5]))
        elif t[0] == "BITMAP": bm = []
        elif t[0] == "ENDCHAR":
            w, h, xo, yo = bbx; rows = [0] * 32; top = asc - (h + yo)
            nb = (w + 7) // 8
            for i, hx in enumerate(bm):
                y = top + i
                if 0 <= y < 32:
                    v = int(hx, 16) << (8 * (2 - nb)) if nb <= 2 else int(hx, 16) >> (8 * (nb - 2))
                    rows[y] = (v >> xo) & 0xFFFF
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
    print("%s-%s.bin: %d from unscii %s" % (name, tag, len(missing), " ".join(missing)))

if __name__ == "__main__":
    tdir, sbdf, vgac = sys.argv[1:4]
    os.makedirs(OUT, exist_ok=True)
    U, K = unscii16(), k4510_page()
    for name, face in (("zhekov-bold", terminus(tdir)), ("spleen", bdf(sbdf)), ("ibm-vga", vga(vgac))):
        for table, tag in ((K, "k4510"), (CP437, "cp437")): write(name, face, U, table, tag)
