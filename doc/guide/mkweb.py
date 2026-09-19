#!/usr/bin/env python3
"""The handbook as a web site: doc/site/docs/*.md for MkDocs (Read the Docs).

The LaTeX chapters stay the one source.  This reads them in the order
k4510-guide.tex \\inputs them, turns the book's own macros into plain LaTeX
pandoc understands, converts each chapter to Markdown, and repairs what a
single-file book takes for granted and a site of pages does not: the
cross-references (\\ref to a chapter becomes a link to its page), the
screenshots (copied beside the pages, shrunk to palette PNGs -- the machine
draws in a handful of colours, so a 200 KB capture becomes 15), and the
generated lists (commands, menu rows, registers) inlined where the book
\\inputs them.

Run by make-guide.sh after the PDF.  Needs pandoc and PIL.  The output is
committed, so Read the Docs only has to run MkDocs -- it has neither the
emulator that takes the pictures nor the ROM source's generators to hand.
Never edit doc/site/docs by hand: the next build overwrites it.
"""
import re, sys, json, shutil, subprocess, pathlib

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parent.parent
SITE = REPO / "doc/site"
DOCS = SITE / "docs"
IMG = DOCS / "img"
GITHUB = "https://github.com/mlongval/k4510"

# The book's macros, as plain LaTeX pandoc can read.  Environments built on
# Verbatim (type, form, reglit) cannot be macro-expanded and are rewritten by
# regex below.
PREAMBLE = r"""
\newcommand{\pth}[1]{\texttt{#1}}
\newcommand{\rn}[1]{\textbf{\texttt{#1}}}
\newcommand{\racc}[1]{\emph{#1}}
\newcommand{\reg}[2]{\par\noindent\texttt{#1}\quad #2\par}
\newcommand{\reglong}[2]{\par\noindent\texttt{#1}\quad #2\par}
\newcommand{\regnote}[1]{\par #1\par}
\newcommand{\menunote}[1]{\par\emph{#1}\par}
\newcommand{\cmdentry}[3]{\par\noindent\textbf{\texttt{#1}} --- \emph{#2}\\ #3\par}
\newcommand{\shipif}[2]{#2}
"""


def nope_keys():
    """Keys marked `nope' in /DOCUMENTS/SHIPPING.CFG -- the things this build leaves out.

    The web edition and the machine's own pages read the same file the PDF
    does (via mkship.py), so the three editions cannot disagree about what is
    in them.  An absent file hides nothing."""
    cfg = REPO / "fs/DOCUMENTS/SHIPPING.CFG"
    out = set()
    if cfg.is_file():
        for line in cfg.read_text().splitlines():
            line = line.split("#", 1)[0].strip()
            if "=" in line:
                k, v = (x.strip() for x in line.split("=", 1))
                if v in ("nope", "nuke"):      # as mkship.py: a nuked thing is not in the image either
                    out.add(k)
    return out


NOPE = None


def nope():
    """The nope set, read once.  A function rather than a call at import time
    so that prep() cannot depend on chapters() having run first: mkgem.py
    calls both, and an order this file does not control must not decide what
    the pages say."""
    global NOPE
    if NOPE is None:
        NOPE = nope_keys()
    return NOPE


def die(msg):
    print(f"mkweb: {msg}", file=sys.stderr)
    sys.exit(1)


def slug(title):
    """MkDocs' default heading anchor (markdown's toc slugify)."""
    t = re.sub(r"\\[a-zA-Z]+\*?|[{}~]", " ", title)
    t = re.sub(r"[^\w\s-]", "", t).strip().lower()
    return re.sub(r"[-\s]+", "-", t)


def plain(title):
    t = re.sub(r"\\texttt\{([^}]*)\}", r"\1", title)
    t = re.sub(r"\\[a-zA-Z]+\*?", "", t)
    return re.sub(r"[{}]", "", t).replace("~", " ").replace("---", "—").strip()


ALL = []                     # every chapter in the book's order, dropped ones included
PUBLISHED = set()            # the stems this edition publishes


def chapters():
    """[(part, file stem)] in the book's order; part is the nav group.
    Also fills ALL and PUBLISHED, which labels() and prep() read."""
    master = (HERE / "k4510-guide.tex").read_text()
    body = master.split(r"\mainmatter", 1)[1]
    part, out = "User's Guide", []
    for tok in re.finditer(r"\\part\{([^}]*)\}|\\appendix|\\backmatter|\\input\{chapters/([\w-]+)\}", body):
        if tok.group(1):
            part = tok.group(1)
        elif tok.group(0) == r"\appendix":
            part = "Appendices"
        elif tok.group(0) == r"\backmatter":
            part = "About the Book"
        else:
            out.append((part, tok.group(2)))
    nope()
    # A chapter marked `nope' is no longer a page here, on the web or on the
    # machine.  labels() still reads every chapter on disk, so a \ref into a
    # dropped chapter does not break the build -- it simply points at a page
    # that is not published, which mkweb turns into plain text below.
    keep = [(p, s) for (p, s) in out if f"chapter:{s}" not in nope()]
    ALL[:] = out
    PUBLISHED.clear()
    PUBLISHED.update(s for _, s in keep)
    return keep


def inline_inputs(tex):
    def sub(m):
        f = HERE / (m.group(1) + ".tex")
        if not f.exists():
            die(f"{f} missing -- run mkregs.py and mkref.py first (make-guide.sh does)")
        return f.read_text()
    return re.sub(r"\\input\{(generated/[\w-]+)\}", sub, tex)


def labels(order):
    """label -> (page, anchor, number, title) for every \\chapter and \\section label."""
    table, n, app = {}, 0, 0
    for part, stem in (ALL or order):
        shown = stem in PUBLISHED or not PUBLISHED
        tex = (HERE / "chapters" / (stem + ".tex")).read_text()
        page = stem + ".md"
        m = re.search(r"\\chapter\{(.*?)\}\\label\{([^}]+)\}", tex)
        if m:
            if not shown:
                num = ""                       # not in this edition: no number to print
            elif part == "Appendices":
                num = "ABCDEFGHIJ"[app]; app += 1
            elif part == "About the Book":
                num = ""
            else:
                n += 1; num = str(n)
            table[m.group(2)] = (page, "", num, plain(m.group(1)))
        for s in re.finditer(r"\\(?:sub)?section\*?\{(.*?)\}\\label\{([^}]+)\}", tex):
            table[s.group(2)] = (page, "#" + slug(plain(s.group(1))), "", plain(s.group(1)))
    return table


def expand_shipif(tex):
    r"""\shipif{key}{content} -> content, or nothing when SHIPPING.CFG says nope.

    Braces are counted rather than matched by a pattern: the content nests as
    deep as it likes (a whole \cmdentry{}{}{} goes inside one), and a regex with
    a fixed nesting limit left the macro sitting in the Markdown.  A backslash
    hides the character after it, so the \\ that ends a table row is two
    characters and not an escape of the brace behind it (which is what the first
    version read it as, 2026-09-19).  \verb is not allowed in here -- LaTeX does
    not allow it in a macro argument either -- so its bars hide nothing.
    """
    out, i = [], 0
    while True:
        j = tex.find("\\shipif{", i)
        if j < 0:
            out.append(tex[i:])
            return "".join(out)
        out.append(tex[i:j])
        k = tex.index("}", j + 8)
        key = tex[j + 8:k]
        if k + 1 >= len(tex) or tex[k + 1] != "{":
            out.append(tex[j:k + 1])                  # not the two-argument form: leave it be
            i = k + 1
            continue
        depth, m = 1, k + 2
        while m < len(tex) and depth:
            c = tex[m]
            if c == "\\":
                m += 2                                # an escaped anything, \\ included
                continue
            depth += (c == "{") - (c == "}")
            m += 1
        out.append("" if key in nope() else expand_shipif(tex[k + 2:m - 1]))
        i = m


def fix_tabular(tex):
    """Column specs with >{...}p{...} and @{} are LaTeX-only; pandoc wants letters."""
    def sub(m):
        spec = m.group(1)
        spec = re.sub(r">\{[^{}]*(?:\{[^{}]*\}[^{}]*)*\}", "", spec)
        spec = re.sub(r"@\{[^}]*\}", "", spec)
        cols = len(re.findall(r"[lcr]|p\{[^}]*\}", spec))
        return r"\begin{tabular}{" + "l" * max(cols, 1) + "}"
    return re.sub(r"\\begin\{tabular\}\{((?:[^{}]|\{(?:[^{}]|\{[^{}]*\})*\})*)\}", sub, tex)


def prep(stem, table, link, img):
    """A chapter's LaTeX with the book's own macros made plain, ready for
    pandoc.  link(page, anchor) and img(name) say what a cross-reference and
    a screenshot point at: the web wants 02-shell.md#swap and img/dir.png,
    the machine's Gemini pages 02-SHELL.GMI and IMG/DIR.PIC (mkgem.py)."""
    tex = (HERE / "chapters" / (stem + ".tex")).read_text()
    tex = inline_inputs(tex)
    tex = re.sub(r"\\markboth\{[^}]*\}\{[^}]*\}|\\setcounter\{[^}]*\}\{[^}]*\}", "", tex)
    # \shipif{key}{...}: the content stays unless /DOCUMENTS/SHIPPING.CFG says nope.
    # Kept by default, exactly as the LaTeX macro is: a misspelt key prints.
    # Braces are counted rather than matched by a pattern: the content nests as
    # deep as it likes (a whole \cmdentry{}{}{} goes inside one of these), and a
    # regex with a fixed nesting limit silently left the macro in the Markdown.
    tex = expand_shipif(tex)
    # cross-references, before pandoc sees them
    def ref(m):
        word, lab = m.group(1), m.group(2)
        if lab not in table:
            die(f"{stem}: \\ref{{{lab}}} has no label")
        page, anchor, num, title = table[lab]
        if page[:-3] not in PUBLISHED:
            return title            # a chapter this edition leaves out: name it, do not link it
        # "Chapter 13, The Linux Underneath"; a section is linked by its own title
        text = f"{word} {num}, {title}" if word and num else title
        return r"\href{" + link(page, anchor) + "}{" + text + "}"
    tex = re.sub(r"(Chapter|Appendix|Section|Chapters)?~?\\ref\{([^}]+)\}", ref, tex)
    tex = re.sub(r"page~?\\pageref\{([^}]+)\}",
                 lambda m: r"\href{" + link(table[m.group(1)][0], table[m.group(1)][1]) + "}{" + table[m.group(1)][3] + "}", tex)
    # verbatim-built environments
    tex = re.sub(r"\\begin\{(type|form)\}", r"\\begin{verbatim}", tex)
    tex = re.sub(r"\\end\{(type|form)\}", r"\\end{verbatim}", tex)
    tex = re.sub(r"\\begin\{reglit\}\{[^}]*\}\{[^}]*\}", r"\\begin{verbatim}", tex)
    tex = tex.replace(r"\end{reglit}", r"\end{verbatim}")
    tex = re.sub(r"\\(begin|end)\{(regblock|cmdblock)\}", "", tex)
    # an aside becomes a quote, marked so the Markdown side can make it a note
    tex = tex.replace(r"\begin{aside}", "\\begin{quote}\nASIDEMARK\n").replace(r"\end{aside}", r"\end{quote}")
    # screenshots
    def shot(m):
        src, cap = m.group(2), m.group(3)
        return r"\begin{figure}\includegraphics{" + img(pathlib.Path(src).name) + r"}\caption{" + cap + r"}\end{figure}"
    tex = re.sub(r"\\(screen|screeninline)\{([^}]+)\}\{((?:[^{}]|\{(?:[^{}]|\{[^{}]*\})*\})*)\}", shot, tex, flags=re.S)
    tex = fix_tabular(tex)
    tex = re.sub(r"\\begin\{description\}\[[^\]]*\]", r"\\begin{description}", tex)
    tex = re.sub(r"\\begin\{itemize\}\[[^\]]*\]", r"\\begin{itemize}", tex)
    tex = tex.replace(r"\small", "").replace(r"\normalsize", "").replace(r"\raggedright", "")
    # the book's maths is typography, not maths: 640$\times$480, F7 $\rightarrow$ Input
    def math(m):
        s = m.group(1)
        for a, b in ((r"\times", "×"), (r"\rightarrow", "→"), (r"\ldots", "…"), (r"\,", " ")):
            s = s.replace(a, b)
        return re.sub(r"\\[a-zA-Z]+|[{}]", "", s).strip()
    tex = re.sub(r"(?<!\\)\$(.+?)(?<!\\)\$", math, tex)
    tex = re.sub(r"\\\((.+?)\\\)", math, tex)
    return tex


def convert(stem, table):
    tex = prep(stem, table, lambda page, anchor: page + anchor, lambda name: "img/" + name)
    # -pipe_tables: the book's tables mostly have no header row, and a pipe
    # table must have one (pandoc gives it an empty one); HTML tables do not.
    r = subprocess.run(["pandoc", "-f", "latex", "-t", "gfm-pipe_tables", "--wrap=none"],
                       input=PREAMBLE + tex, capture_output=True, text=True)
    if r.returncode:
        die(f"pandoc on {stem}: {r.stderr.strip()}")
    md = r.stdout
    # headings: pandoc numbers nothing; the book does, so the site does too
    md = re.sub(r"<span id=\"[^\"]*\" label=\"[^\"]*\"></span>", "", md)
    md = re.sub(r"\n> ASIDEMARK\n>\n", "\n!!! note \"\"\n", md)
    md = re.sub(r"(!!! note \"\"\n)((?:>.*\n?)+)",
                lambda m: m.group(1) + re.sub(r"^> ?", "    ", m.group(2), flags=re.M), md)
    # figures: pandoc writes <figure><img src=...><figcaption>, and MkDocs
    # rewrites only MARKDOWN image paths for a page's own folder -- a raw
    # <img src="img/x.png"> on 02-shell/index.html looks in 02-shell/img.
    # So each figure becomes a Markdown image with its caption under it.
    def fig(m):
        body = m.group(1)
        src = re.search(r'src="(img/[\w-]+\.png)"', body)
        cap = re.search(r"<figcaption>(.*?)</figcaption>", body, re.S)
        if not src:
            return m.group(0)
        c = re.sub(r"\s+", " ", cap.group(1)).strip() if cap else ""
        c = re.sub(r"<(?!/?(?:code|em)>)[^>]+>", "", c)   # the caption is HTML: Markdown is not read inside it
        return f"![]({src.group(1)})\n\n<p class=\"caption\">{c}</p>\n" if c else f"![]({src.group(1)})\n"
    md = re.sub(r"<figure>(.*?)</figure>", fig, md, flags=re.S)
    for p in re.findall(r'img/([\w-]+\.png)', md):
        copy_shot(p)
    return md


def copy_shot(name):
    src, dst = HERE / "shots" / name, IMG / name
    if not src.exists():
        die(f"shots/{name} missing -- make-guide.sh takes it")
    if dst.exists() and dst.stat().st_mtime >= src.stat().st_mtime:
        return
    from PIL import Image
    im = Image.open(src).convert("RGB")
    im.quantize(colors=64, method=Image.Quantize.MEDIANCUT).save(dst, optimize=True)


def main():
    if not shutil.which("pandoc"):
        die("pandoc is not installed; the web pages were not rebuilt")
    order = chapters()
    table = labels(order)
    DOCS.mkdir(parents=True, exist_ok=True)
    IMG.mkdir(exist_ok=True)
    # Convert the lot BEFORE removing anything: until 2026-09-19 the old pages
    # went first, so a build that died half way (one bad \ref did) left the web
    # edition deleted and `git checkout doc/site` was the way back.
    nav, pages = {}, {}
    for part, stem in order:
        pages[stem + ".md"] = convert(stem, table)
    keep = {"index.md", "extra.css", "img"}
    for f in DOCS.iterdir():
        if f.name not in keep:
            f.unlink()
    for name, md in pages.items():
        (DOCS / name).write_text(md)
    for part, stem in order:
        page, _, num, title = next(v for k, v in table.items() if v[0] == stem + ".md" and not v[1])
        label = f"{num}. {title}" if num and part != "Appendices" else (f"{num}. {title}" if num else title)
        nav.setdefault(part, []).append(f"      - {json.dumps(label)}: {stem}.md")   # JSON quoting is valid YAML: "The Machine's REXX"
    used = {p.name for p in IMG.iterdir()}
    wanted = set()
    for f in DOCS.glob("*.md"):
        wanted |= set(re.findall(r'img/([\w-]+\.png)', f.read_text()))
    for stale in used - wanted:
        (IMG / stale).unlink()
    # the nav, into mkdocs.yml between its markers
    cfg = SITE / "mkdocs.yml"
    text = cfg.read_text()
    block = "\n".join(["  - Home: index.md"] + [f"  - \"{p}\":\n" + "\n".join(v) for p, v in nav.items()])
    text = re.sub(r"(# nav: generated by mkweb.py.*?\n)(.*?)(\n# end nav)", lambda m: m.group(1) + "nav:\n" + block + m.group(3), text, flags=re.S)
    cfg.write_text(text)
    print(f"mkweb: {len(order)} pages, {len(wanted)} pictures -> doc/site")


if __name__ == "__main__":
    main()
