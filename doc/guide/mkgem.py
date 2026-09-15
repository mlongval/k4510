#!/usr/bin/env python3
"""The handbook on the machine itself: fs/SYSTEM/DOC/*.GMI, Gemini text.

The third edition of the same chapters, beside the PDF and the web site:
one Gemtext page per chapter, a contents page, and the screenshots as
pictures the machine can draw.  BOOK reads them; TYPE shows them readably
too, which is why Gemtext -- a line's first characters say what it is
(# a heading, => a link, * an item, > a quote, ``` a block as typed),
and nothing else is markup.

  LaTeX -> mkweb.prep() -> pandoc (Markdown) -> md2gemini -> fixes -> .GMI

The fixes are what the machine needs and Gemini does not say:
  * the machine's character set is IBM's CP437 (core/codepage.h's cp437_cp,
    the default; the K4510 page is an option), so the pages are written in
    it: typographic dashes and quotes become their typewriter
    forms, and a character it cannot draw stops the build;
  * paragraphs are wrapped at 78 columns, so TYPE shows them whole
    (Gemini leaves wrapping to the reader; a wrapped page is still valid);
  * every page ends with links to the contents and the pages either side.

Screenshots become IMG/NAME.PIC: "K4PC", width, height and colour count
(16-bit little-endian each), a format byte, a version byte (3), four
zero bytes, the palette
(3 bytes a colour, 0-255; colour 0 is VICKY's transparent, so a
picture's own colours start at 1), then the pixels: format 0 as runs -- a count
(1-255) and a colour index, which BOOK draws with one DMA fill a run --
or format 1 raw, one byte a pixel, one DMA copy.  mkgem writes whichever
is smaller: a machine screen is mostly flat colour, so most are runs and
a few KB; the Mandelbrot set changes colour nearly every pixel, and is raw.

Run by make-guide.sh after mkweb.py.  Needs pandoc, md2gemini and PIL.
Never edit fs/SYSTEM/DOC by hand: the next build overwrites it.
"""
import re, sys, struct, textwrap, subprocess, pathlib, unicodedata

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import mkweb                                   # noqa: E402  (the shared LaTeX preparation)

REPO = HERE.parent.parent
OUT = REPO / "fs/SYSTEM/DOC"
PICS = OUT / "IMG"
WIDTH = 78

TYPOGRAPHY = {"—": "--", "–": "-", "’": "'", "‘": "'", "“": '"', "”": '"', "…": "...",
              "×": "x", "→": "->", "←": "<-", " ": " ", " ": " ", " ": " ",
              "·": ".", "−": "-", "✓": "v", "★": "*", "©": "(c)",
              # CP437 draws a section sign, but at $15 -- a control code to
              # Python's codec and to JIM alike
              "§": "section ",
              # what only the K4510 page has (Appendix D): a plain spelling in CP437
              # (its accented capitals need none -- the letter without its accent is found)
              "€": "EUR", "œ": "oe", "Œ": "OE", "ø": "o", "Ø": "O", "„": '"', "¶": "(para) ",
              "ẞ": "SS"}


# the book's \, and pandoc's other spaces (no-break, en, em, six-per-em,
# thin, narrow no-break): the machine has one space
TYPOGRAPHY.update({c: " " for c in "      "})


def die(msg):
    print(f"mkgem: {msg}", file=sys.stderr)
    sys.exit(1)


def gem_name(page_md):
    return page_md[:-3].upper() + ".GMI"


def pic_name(png):
    return pathlib.Path(png).stem.upper() + ".PIC"


def cell_text(c):
    """a table cell's Markdown as the plain text a screen shows"""
    c = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", c)          # a link: its words
    c = c.replace("\\|", "|").replace("**", "").replace("`", "")
    c = re.sub(r"(?<!\w)\*(\S[^*]*)\*(?!\w)", r"\1", c)    # *emphasis*
    for a, b in TYPOGRAPHY.items():                         # before measuring: -- is wider than a dash
        c = c.replace(a, b)
    return c.strip()


def tables(md):
    """Pipe tables laid out as text, before md2gemini sees them.  md2gemini
    draws a box and never wraps a cell, and the book's tables are prose in
    their last column -- a line of chapter 2 came out 300 wide.  So: columns
    padded to their widest cell, the earlier ones held to a share of the 78,
    the last one wrapped under itself; two spaces between; no rules.  A row
    of empty cells (pandoc's header for a table that has none) is dropped."""
    out, lines, i = [], md.split("\n"), 0
    while i < len(lines):
        if not lines[i].startswith("|"):
            out.append(lines[i])
            i += 1
            continue
        rows = []
        while i < len(lines) and lines[i].startswith("|"):
            l = lines[i].strip()
            i += 1
            if re.fullmatch(r"\|[-:| ]+\|", l):
                continue
            cells = [cell_text(c) for c in re.split(r"(?<!\\)\|", l.strip("|"))]
            if any(cells):
                rows.append(cells)
        n = max(len(r) for r in rows)
        rows = [r + [""] * (n - len(r)) for r in rows]
        nat = [max(len(r[k]) for r in rows) for k in range(n)]
        cap = max(12, (WIDTH - 2 * (n - 1)) // (n + 1) * 2) if n > 1 else WIDTH
        w = [min(nat[k], cap) for k in range(n - 1)]
        w.append(max(20, WIDTH - sum(w) - 2 * (n - 1)))
        out.append("```")
        for r in rows:
            cols = [textwrap.wrap(r[k], w[k], break_long_words=True) or [""] for k in range(n)]
            for j in range(max(len(c) for c in cols)):
                parts = [(cols[k][j] if j < len(cols[k]) else "").ljust(w[k]) for k in range(n)]
                out.append("  ".join(parts).rstrip())
        out.append("```")
    return "\n".join(out)


def wrap(gem):
    # the typewriter forms first: "--" is a column wider than the dash it
    # replaces, and a line measured before would come out 79 or 80 wide
    for a, b in TYPOGRAPHY.items():
        gem = gem.replace(a, b)
    out, pre = [], False
    for line in gem.split("\n"):
        if line.startswith("```"):
            pre = not pre
            out.append(line)
            continue
        if pre or not line or line.startswith(("=>", "#")) or len(line) <= WIDTH:
            out.append(line)
            continue
        first, rest = "", ""
        if line.startswith("* "):
            first, rest = "* ", "  "
        elif line.startswith(">"):
            first, rest = "> ", "> "
            line = line[1:].lstrip()
        body = line[len(first):] if first == "* " else line
        out += textwrap.wrap(body, WIDTH, initial_indent=first, subsequent_indent=rest,
                             break_long_words=False, break_on_hyphens=False)
    return "\n".join(out)


# The page as text (CP437, the machine's default): $20-$FF but $7F.  $01-$1F and $7F draw pictures, but in a
# file they are controls ($0A ends a line), so they are no place for text.
import mkcodepage
PAGE = {chr(u): b for b, u in enumerate(mkcodepage.table("cp437_cp")) if b >= 0x20 and b != 0x7F}


def machine_char(c):
    """a character the machine cannot draw, as the nearest one it can: the
    letter without its accent (the n of a Polish name), if the page has that"""
    if c in PAGE or c == "\n":
        return c
    base = "".join(ch for ch in unicodedata.normalize("NFKD", c) if not unicodedata.combining(ch))
    return base if base and all(ch in PAGE for ch in base) else None


def machine_text(s, where):
    for a, b in TYPOGRAPHY.items():
        s = s.replace(a, b)
    out = []
    for c in s:
        m = machine_char(c)
        if m is None:
            die(f"{where}: {c!r} (U+{ord(c):04X}) is not in the machine's character set -- add it to TYPOGRAPHY")
        out.append(m)
    return bytes(10 if ch == "\n" else PAGE[ch] for ch in "".join(out))


def convert(stem, table, prev, nxt):
    from md2gemini import md2gemini
    tex = mkweb.prep(stem, table, lambda page, anchor: gem_name(page), lambda name: "IMG/" + pic_name(name))
    tex = tex.replace("ASIDEMARK\n", "")
    # Appendix D's grid shows the pictures $01-$1F draw, which a file can only hold
    # as controls ($0A ends a line).  The printed book and the web have it; the
    # machine is told where to look instead (FONTED and HEXED show the real thing).
    tex = re.sub(r"% not on the machine\n.*?% end not on the machine\n",
                 "(The page as a grid is in the printed book and on the web. On the machine,\n"
                 "FONTED shows every character, and HEXED any byte.)\n\n", tex, flags=re.S)
    r = subprocess.run(["pandoc", "-f", "latex", "-t", "gfm-raw_html", "--wrap=none"],
                       input=mkweb.PREAMBLE + tex, capture_output=True, text=True)
    if r.returncode:
        die(f"pandoc on {stem}: {r.stderr.strip()}")
    md = tables(r.stdout)
    gem = md2gemini(md, links="copy", plain=True, strip_html=True)
    # md2gemini ends some lines CR LF (Gemini allows either); on this console
    # a CR is a whole newline, so TYPE would double-space them.  LF only.
    gem = gem.replace("\r\n", "\n").replace("\r", "\n")
    # md2gemini's rule is 80 dashes: the page is 78
    gem = re.sub(r"^-{%d,}$" % (WIDTH + 1), "-" * WIDTH, gem, flags=re.M)
    # a picture: pandoc leaves the image nameless and the caption as the next
    # paragraph -- the link is labelled with the caption's first clause
    def picture(m):
        cap = re.split(r"(?<=[.:;])\s", m.group(2).strip(), maxsplit=1)[0].rstrip(".:;")
        cap = cap if len(cap) <= 64 else cap[:61].rsplit(" ", 1)[0] + "..."
        return f"=> {m.group(1)} {cap}\n\n{m.group(2)}"
    gem = re.sub(r"^=> (IMG/\S+\.PIC) ?\[IMG\]\n\n(.+)$", picture, gem, flags=re.M)
    gem = re.sub(r"^=> (IMG/\S+\.PIC) (.*?) ?\[IMG\]$", r"=> \1 \2", gem, flags=re.M)
    # the chapter's number in its heading, as in the book
    _, _, num, title = next(v for v in table.values() if v[0] == stem + ".md" and not v[1])
    if num:
        label = ("Appendix " if num.isalpha() else "Chapter ") + num
        gem = re.sub(r"^# .*$", f"# {label}. {title}", gem, count=1, flags=re.M)
    gem = re.sub(r"\n{3,}", "\n\n", gem).strip() + "\n\n"
    gem += "=> INDEX.GMI Contents\n"
    if prev:
        gem += f"=> {prev[0]} Previous: {prev[1]}\n"
    if nxt:
        gem += f"=> {nxt[0]} Next: {nxt[1]}\n"
    for p in re.findall(r"^=> IMG/(\S+\.PIC)", gem, flags=re.M):
        make_pic(p)
    return machine_text(wrap(gem), stem)


def make_pic(pic):
    png = HERE / "shots" / (pic[:-4].lower() + ".png")
    dst = PICS / pic
    if not png.exists():
        die(f"shots/{png.name} missing -- make-guide.sh takes it")
    if dst.exists() and dst.stat().st_mtime >= png.stat().st_mtime:
        old = dst.read_bytes()[:12]
        if old[:4] == b"K4PC" and old[11] == 3:
            return                             # current, and in this format (byte 11 is its version)
    from PIL import Image
    im = Image.open(png).convert("RGB")
    colours = im.getcolors(255)
    if colours is None:
        die(f"{png.name} has more than 255 colours -- not a picture of the machine")
    # index 0 is transparent on VICKY's layers: a colour put there shows what
    # is behind the bitmap (the first LOGO picture drew its white lines green).
    # So 0 is a black nobody uses, and the picture's colours start at 1.
    pal = [(0, 0, 0)] + [c for _, c in colours]
    index = {c: i for i, c in enumerate(pal) if i}
    w, h = im.size
    flat = im.get_flattened_data() if hasattr(im, "get_flattened_data") else im.getdata()
    px = bytes(index[c] for c in flat)
    runs, i = bytearray(), 0
    while i < len(px):
        j = i
        while j < len(px) and px[j] == px[i] and j - i < 255:
            j += 1
        runs += bytes((j - i, px[i]))
        i = j
    # whichever is smaller: a screen of text and flat colour is runs; the
    # Mandelbrot set, whose colour changes nearly every pixel, is raw
    fmt, body = (0, bytes(runs)) if len(runs) < len(px) else (1, px)
    head = b"K4PC" + struct.pack("<HHHBB", w, h, len(pal), fmt, 3) + bytes(4)
    dst.write_bytes(head + bytes(v for c in pal for v in c) + body)


def main():
    try:
        import md2gemini  # noqa: F401
    except ImportError:
        die("md2gemini is not installed (pip install --user md2gemini); the machine's pages were not rebuilt")
    order = mkweb.chapters()
    table = mkweb.labels(order)
    OUT.mkdir(parents=True, exist_ok=True)
    PICS.mkdir(exist_ok=True)
    pages = []
    for part, stem in order:
        _, _, num, title = next(v for v in table.values() if v[0] == stem + ".md" and not v[1])
        pages.append((part, gem_name(stem + ".md"), (f"{num}. " if num else "") + title))
    for k, (part, stem) in enumerate(order):
        prev = pages[k - 1][1:] if k else None
        nxt = pages[k + 1][1:] if k + 1 < len(pages) else None
        (OUT / pages[k][1]).write_bytes(convert(stem, table, prev, nxt))
    # the contents
    idx = ["# The K4510 User's and Programmer's Guide", "",
           "The handbook, on the machine it describes. The same book is a PDF and a web site, "
           "made from the same source at the same time; this is the edition that needs neither "
           "a printer nor a browser.", "",
           "> This is alpha documentation. It describes a machine that is still being built. "
           "Where the book and the machine disagree, the machine is right; Appendix B says how "
           "to tell us.", ""]
    part = None
    for p, name, label in pages:
        if p != part:
            idx += ["", f"## {p}"]
            part = p
        idx.append(f"=> {name} {label}")
    idx += ["", "## Elsewhere",
            "=> https://k4510.readthedocs.io The same book on the web",
            "=> https://github.com/mlongval/k4510 The machine's source", ""]
    (OUT / "INDEX.GMI").write_bytes(machine_text(wrap("\n".join(idx)), "INDEX"))
    keep = {p[1] for p in pages} | {"INDEX.GMI", "IMG"}
    for f in OUT.iterdir():
        if f.name not in keep:
            f.unlink()
    wanted = set()
    for f in OUT.glob("*.GMI"):
        wanted |= set(re.findall(rb"^=> IMG/(\S+\.PIC)", f.read_bytes(), flags=re.M))
    for f in PICS.iterdir():
        if f.name.encode() not in wanted:
            f.unlink()
    size = sum(f.stat().st_size for f in OUT.rglob("*") if f.is_file())
    print(f"mkgem: {len(pages)} pages, {len(wanted)} pictures, {size // 1024} KB -> fs/SYSTEM/DOC")


if __name__ == "__main__":
    main()
