#!/usr/bin/env python3
"""Generate the register appendix from the machine's own headers.

Chapter 10 promises this: "the authoritative register-level reference is
core/io.h in the source tree; this chapter will be generated from it, the
way the Neo6502 handbook generates its keyword reference from the firmware
source."  This is that generator.

It takes every top-level block comment in the headers below that documents
registers -- the test is a line mentioning a $XX offset -- and turns it
into a reference: an address column the eye can run down, and the
description in the book's own text face.

The first version printed the comments verbatim, on the argument that
anything rewritten could drift from them.  Twelve pages of folded
monospace is not a reference, though, so this one parses instead.  What it
parses is only the shape the comments already have:

    $D720  FOP     write = execute; low 5 bits op, see below
    ^addr  ^name   ^description, continued on any deeper-indented line

Nothing is reworded -- every word printed is a word from the header.  What
the parser decides is only which column a word belongs in, and it is
written to degrade gracefully: a line it cannot read as a register becomes
a note, and a line that is really a little table (two or more runs of
spaces doing the aligning) is kept as one, in monospace, deliberately.

Adding a device to a header still adds it to the book with nothing to edit
here.  Run by make-guide.sh.  Output: generated/registers.tex (never edit).
"""
import re, sys, pathlib

HEADERS = [
    ("core/io.h",    "The I/O page"),
    ("core/digimax.h", "The DigiMAX"),
    ("core/vicky.h", "VICKY, SHEILA and the sprites"),
    ("core/net.h",   "The network"),
    ("core/term.h",  "JIM, the terminal"),
    ("core/mem.h",   "Memory and the far view"),
]

# Nicer titles than the comment's own first line, where the first line is
# a sentence rather than a name.  Matched by prefix.
TITLES = {
    "K4510 I/O page":      "The page map",
    "BANK registers":      "Bank registers and the far gate",
    "MATH unit":           "The MATH unit",
    "SYS registers":       "System registers, and the SIDs",
    "Keyboard":            "The keyboard",
    "Names are NUL":       "Storage: the host filesystem",
    "All addresses phys":  "The DMA engine",
    "The Tube":            "The Tube",
    "VICKY --":            "VICKY",
    "The network, three":  "The N: device",
    "JIM,":                "JIM",
    "The ROM image":       "The ROM window and the stub page",
    "$D000-":              "The page map",
}

BLOCK = re.compile(r"/\*(.*?)\*/", re.S)
REGISTER_LINE = re.compile(r"\$[0-9A-Fa-f]{2,4}")

# An address: $D720, $D724-$D727, $02,03, $0A,0B, +0,1, +11..15, 00, 8A.
ADDR = r"(?:\$[0-9A-Fa-f]+(?:\s*[-,.]+\s*\$?[0-9A-Fa-f]+)*|\+[0-9A-F]+(?:[-,.]+\+?[0-9A-F]+)*|[0-9A-F]{2})"
CODEY = re.compile(r"^(?:%s)$" % ADDR)
# "code<2+ spaces>rest", or "code<one space>rest" when the code is an address.
ROW_WIDE = re.compile(r"^(\S+|bytes?\s+\S+)\s{2,}(\S.*)$")
ROW_ADDR = re.compile(r"^(%s)\s+(\S.*)$" % ADDR)
# a second register starting further along the same line
SPLIT_WIDE = re.compile(r"\s{2,}(?=(?:\$[0-9A-Fa-f]|\+[0-9]))")
SPLIT_TIGHT = re.compile(r"\s+(?=\$[0-9A-Fa-f])")
# access markers that precede a register's name
ACCESS = re.compile(r"^(R/W|RW|R|W)[:.]?\s+(\S.*)$")
NAME_WIDE = re.compile(r"^(\S{1,12}|[A-Z][A-Z0-9_]*(?:\s+[A-Z][A-Z0-9_]*)+)\s{2,}(\S.*)$")
NAME_TIGHT = re.compile(r"^([A-Z][A-Z0-9_]{2,11})([,:.]?)\s+(\S.*)$")
NAME_ALONE = re.compile(r"^([A-Z][A-Z0-9_]{2,11})[,:.]?$")
# words that look like a register name and are not one
NOT_A_NAME = {"CPU", "RAM", "ROM", "SID", "PAL", "NTSC", "IEEE", "ASCII",
              "NUL", "RTS", "JSR", "LSB", "MSB", "CCP", "BIOS", "BDOS"}

# Beyond this many characters an address will not fit the appendix's
# address column, and the entry is set with the text following it instead.
ADDR_FITS = 11


def clean(block):
    """Strip the comment furniture and the indent it leaves behind."""
    out = []
    for line in block.split("\n"):
        line = re.sub(r"^\s*\*ic? ?", "", line)      # " * " and " *" furniture
        line = re.sub(r"^\s*\* ?", "", line)
        out.append(line.rstrip())
    # a comment that opens on the /* line keeps one space of furniture there
    if out and out[0].startswith(" ") and not out[0].startswith("  "):
        out[0] = out[0][1:]
    while out and not out[0].strip():  out.pop(0)
    while out and not out[-1].strip(): out.pop()
    body = [l for l in out if l.strip()]
    pad = min((len(l) - len(l.lstrip()) for l in body), default=0)
    return [l[pad:] if l.strip() else "" for l in out]


def tex(t):
    """Escape for LaTeX, then set every $XX address in monospace."""
    holes, keep = [], []
    def stash(m):
        holes.append(m.group(0))
        return "\x00%d\x00" % (len(holes) - 1)
    t = re.sub(r"\$[0-9A-Fa-f]+(?:[-,][$]?[0-9A-Fa-f]+)*", stash, t)
    for a, b in [("\\", "/"), ("&", "\\&"), ("%", "\\%"), ("$", "\\$"),
                 ("#", "\\#"), ("_", "\\_"), ("{", "\\{"), ("}", "\\}"),
                 ("~", "\\textasciitilde{}"), ("^", "\\textasciicircum{}")]:
        t = t.replace(a, b)
    for h in holes:
        keep.append("\\texttt{%s}" % h.replace("$", "\\$"))
    t = re.sub(r'"([^"]{1,60})"', r"``\1''", t)
    t = re.sub(r" {2,}", " ", t)
    # long runs of slashes and semicolons -- ANSI sequence lists, mostly --
    # are single unbreakable words to TeX, and a narrow measure cannot take
    # them. Let a line break after the punctuation inside one.
    t = re.sub(r"\S{22,}", lambda m: re.sub(r"([/;,])", r"\1\\allowbreak{}",
                                            m.group(0)), t)
    return re.sub(r"\x00(\d+)\x00", lambda m: keep[int(m.group(1))], t)


def verb(t):
    """Escape for a monospace label (no address wrapping: it is all code)."""
    for a, b in [("\\", "/"), ("&", "\\&"), ("%", "\\%"), ("$", "\\$"),
                 ("#", "\\#"), ("_", "\\_"), ("{", "\\{"), ("}", "\\}"),
                 ("~", "\\textasciitilde{}"), ("^", "\\textasciicircum{}")]:
        t = t.replace(a, b)
    return t


def title_for(first_line):
    first_line = first_line.strip()
    for prefix, name in TITLES.items():
        if first_line.startswith(prefix):
            return name
    return first_line.rstrip(".:").split("--")[0].strip()


def split_registers(rest):
    """One source line may carry two or three registers side by side."""
    parts = [p for p in SPLIT_WIDE.split(rest) if p.strip()]
    if len(parts) == 1:
        tight = [p for p in SPLIT_TIGHT.split(rest) if p.strip()]
        if len(tight) >= 3 and all(len(p) <= 24 for p in tight):
            parts = tight
    return parts


def read_row(line):
    """(code, rest) if this line opens a register entry, else None."""
    m = ROW_WIDE.match(line)
    if m and len(m.group(1)) <= 18:
        return m.group(1), m.group(2)
    m = ROW_ADDR.match(line)
    if m:
        return m.group(1), m.group(2)
    return None


def body_of(rest):
    """Pull an access marker and a register name out of a description."""
    out = ""
    m = ACCESS.match(rest)
    if m:
        out += "\\racc{%s} " % verb(m.group(1))
        rest = m.group(2)
    m = NAME_ALONE.match(rest)
    if m and m.group(1) not in NOT_A_NAME:
        return out + "\\rn{%s}" % verb(m.group(1))
    tail = ""
    m = NAME_WIDE.match(rest)
    if not m:
        m = NAME_TIGHT.match(rest)
        if m:
            tail = m.group(2)
            m = (m.group(1), m.group(3))
    else:
        m = (m.group(1), m.group(2))
    if (m and not CODEY.match(m[0]) and m[0] not in NOT_A_NAME
            and re.search(r"[A-Za-z]", m[0]) and not re.match(r"^[\d|.x]+$", m[0])):
        out += "\\rn{%s}%s " % (verb(m[0]), tail)
        rest = m[1]
    return out + tex(rest)


def fold(lines, width):
    """Last resort: a comment table too wide for the smallest size."""
    out = []
    for line in lines:
        indent = " " * (len(line) - len(line.lstrip()) + 4)
        while len(line) > width:
            cut = line.rfind(" ", 0, width)
            if cut <= len(indent):
                break
            out.append(line[:cut])
            line = indent + line[cut + 1:].lstrip()
        out.append(line)
    return out


def is_table(line):
    """A line whose own spacing is doing the aligning: keep it as it is."""
    return len(re.findall(r"\S {2,}\S", line)) >= 2


def emit(lines):
    """Turn one cleaned block comment into the appendix's own markup."""
    out, prose, lit = [], [], []
    row_col = 0            # column an open entry's description starts in
    lit_col = None         # indent of an open monospace fragment
    open_entry = False
    note_indent = None     # indent of an open note, so its lines join up

    def flush_prose():
        if prose:
            out.append(tex(" ".join(prose)))
            out.append("")
            prose.clear()

    def flush_lit():
        """A monospace fragment, set at the largest size that still fits.

        These are the little tables written inside the comments, where the
        spacing is the meaning. They are kept exactly as written, so the
        only thing to choose is how wide the page can afford them to be:
        indented under the address column if narrow, full measure if not,
        a size down if still not. Nothing here folds a line."""
        nonlocal lit_col
        if lit:
            pad = min(len(l) - len(l.lstrip()) for l in lit)
            body = [l[pad:] for l in lit]
            wide = max(len(l) for l in body)
            for limit, size, margin in [(60, "\\small", "\\regcol"),
                                        (74, "\\small", "0pt"),
                                        (83, "\\footnotesize", "0pt"),
                                        (94, "\\scriptsize", "0pt")]:
                if wide <= limit:
                    break
            else:
                size, margin = "\\scriptsize", "0pt"
                body = fold(body, 94)
            out.append("\\begin{reglit}{%s}{%s}" % (size, margin))
            out.extend(body)
            out.append("\\end{reglit}")
            lit.clear()
        lit_col = None

    # a numbered sub-item inside a description ("6 LINE: ...") is one of a
    # list, and reads as one only if it starts a line, as it did in the header
    SUBITEM = re.compile(r"^\d{1,2} [A-Z][A-Z0-9]{2,}")

    def append_to_last(text):
        joiner = "\\\\ " if SUBITEM.match(text) else " "
        out[-1] = out[-1][:-1] + joiner + tex(text) + "}"

    def add_row(code, rest, indent):
        nonlocal row_col, open_entry, note_indent
        flush_lit()
        macro = "reg" if len(code) <= ADDR_FITS else "reglong"
        out.append("\\%s{%s}{%s}" % (macro, verb(code), body_of(rest)))
        row_col = indent + len(code) + 1
        open_entry, note_indent = True, None

    for line in lines:
        if not line.strip():
            flush_lit()
            flush_prose()
            open_entry, note_indent = False, None
            continue
        indent = len(line) - len(line.lstrip())
        stripped = line.strip()

        if indent == 0:
            flush_lit()
            open_entry, note_indent = False, None
            prose.append(stripped)
            continue

        flush_prose()

        # inside an open monospace fragment: anything further in belongs to
        # it, and so does the rest of the table it started
        if lit_col is not None and (indent > lit_col or
                                    (indent == lit_col and is_table(stripped))):
            lit.append(line)
            continue

        # deeper than an open entry's text column, and not itself a
        # register: this is that entry's description, continued
        continuation = open_entry and indent >= row_col
        row = None if continuation else read_row(stripped)

        if row:
            parts = split_registers(row[0] + "  " + row[1])
            first = True
            for part in parts:
                sub = read_row(part.strip())
                if sub and (first or CODEY.match(sub[0])):
                    add_row(sub[0], sub[1], indent if first else row_col)
                elif open_entry:
                    append_to_last(part.strip())
                first = False
        elif is_table(stripped):
            if lit_col is None:
                flush_lit()
                lit_col = indent
                open_entry, note_indent = False, None
            lit.append(line)
        elif continuation:
            flush_lit()
            append_to_last(stripped)
        elif note_indent == indent:
            append_to_last(stripped)      # a note carried onto a second line
        else:
            flush_lit()
            out.append("\\regnote{%s}" % tex(stripped))
            open_entry, note_indent = False, indent

    flush_lit()
    flush_prose()
    return out


def main():
    root = pathlib.Path(__file__).resolve().parents[2]
    out = ["% GENERATED by doc/guide/mkregs.py -- do not edit.",
           "% Source: the register comments in the machine's own headers.", ""]
    count = 0
    for rel, part in HEADERS:
        src = (root / rel).read_text()
        blocks = [clean(b) for b in BLOCK.findall(src)]
        blocks = [b for b in blocks if b and sum(bool(REGISTER_LINE.search(l)) for l in b) >= 2]
        if not blocks:
            continue
        out.append("\\section{%s}" % part)
        out.append("\\noindent Generated from \\pth{%s}.\\par\\medskip" % rel)
        for b in blocks:
            count += 1
            out.append("\\subsection*{%s}" % tex(title_for(b[0])))
            out.append("\\begin{regblock}")
            out.extend(emit(b))
            out.append("\\end{regblock}")
        out.append("")
    (pathlib.Path(__file__).parent / "generated").mkdir(exist_ok=True)
    (pathlib.Path(__file__).parent / "generated" / "registers.tex").write_text("\n".join(out) + "\n")
    print("registers: %d blocks" % count)
    return 0

sys.exit(main())
