#!/usr/bin/env python3
"""mksidebar.py -- pack and check the sidebar zips (docs/SIDEBAR-FORMAT.md).

  mksidebar.py SRC_DIR OUT.ZIP      pack a sidebar: SRC_DIR/SIDEBAR.INF and the rest
  mksidebar.py --check ZIP...       check zips someone else made (make test)

The zip is always the same bytes for the same files: entries sorted, stored
(not deflated -- another zlib would deflate differently and check-artifacts
would call a fresh build stale), dated 1980-01-01, no extra fields.  A sidebar
is a few small text files and a picture; storing them costs nothing.
"""
import io, os, re, sys, zipfile

REQUIRED = ("name", "about", "author", "version", "draw")
KNOWN = set(REQUIRED) | {"season"}
# what `draw = builtin NAME` may name: the sidebars the emulator draws itself
BUILTINS = {"border", "gradient", "knot", "registers", "halloween", "christmas",
            "space", "river", "dreamfall", "tetris", "antfarm"}
NAME_RE = re.compile(r"^[A-Z0-9_-]{1,16}\.ZIP$")   # no 8.3 on this machine (BRAINSHOTS); short enough for a menu row


def parse_cfg(text, what):
    """name = value lines, # comments, blank lines; returns (dict, errors)."""
    d, errs = {}, []
    for n, line in enumerate(text.splitlines(), 1):
        s = line.split("#", 1)[0].strip()
        if not s:
            continue
        if "=" not in s:
            errs.append(f"{what}:{n}: not 'name = value': {line.strip()!r}")
            continue
        k, v = (x.strip() for x in s.split("=", 1))
        if not re.match(r"^[a-z][a-z0-9_]*$", k):
            errs.append(f"{what}:{n}: a name is lower case letters, digits and _: {k!r}")
        elif k in d:
            errs.append(f"{what}:{n}: {k} given twice")
        d[k] = v
    return d, errs


def check_inf(inf, files):
    d, errs = parse_cfg(inf, "SIDEBAR.INF")
    for k in REQUIRED:
        if not d.get(k):
            errs.append(f"SIDEBAR.INF: no {k}")
    for k in d:
        if k not in KNOWN:
            errs.append(f"SIDEBAR.INF: {k} is not a SIDEBAR.INF name")
    if d.get("version") and not d["version"].isdigit():
        errs.append("SIDEBAR.INF: version is a whole number")
    if len(d.get("about", "")) > 60:
        errs.append("SIDEBAR.INF: about is one line of at most 60 characters")
    season = d.get("season", "")
    if season:
        for m in re.split(r"[ ,]+", season):
            if not (m.isdigit() and 1 <= int(m) <= 12):
                errs.append(f"SIDEBAR.INF: season is months, 1 to 12: {season!r}")
                break
    draw = d.get("draw", "").split()
    if draw:
        if draw[0] == "builtin":
            if len(draw) != 2 or draw[1] not in BUILTINS:
                errs.append(f"SIDEBAR.INF: draw = builtin NAME, one of {', '.join(sorted(BUILTINS))}")
        elif draw[0] in ("scene", "program"):
            errs.append(f"SIDEBAR.INF: draw = {draw[0]} is planned, not yet drawn (docs/SIDEBARS-PLAN.md)")
        else:
            errs.append(f"SIDEBAR.INF: draw is 'builtin NAME': {' '.join(draw)!r}")
    return errs


def check_zip(path, name=None):
    """NAME: what the zip is called on the machine (default: its file name)."""
    errs = []
    if not NAME_RE.match(name or os.path.basename(path)):
        errs.append("the name is up to 16 capital letters, digits, _ or -, then .ZIP")
    try:
        z = zipfile.ZipFile(path)
    except zipfile.BadZipFile as e:
        return [f"not a zip: {e}"]
    names = z.namelist()
    if "SIDEBAR.INF" not in names:
        return errs + ["no SIDEBAR.INF"]
    errs += check_inf(z.read("SIDEBAR.INF").decode("utf-8", "replace"), names)
    if "OPTIONS.CFG" in names:
        errs += parse_cfg(z.read("OPTIONS.CFG").decode("utf-8", "replace"), "OPTIONS.CFG")[1]
    for n in names:
        if n.startswith("/") or ".." in n.split("/") or "\\" in n:
            errs.append(f"an entry named {n!r}: it could climb out of the mount")
    return errs


def pack(src, out):
    files = []
    for root, dirs, fs in os.walk(src):
        dirs.sort()
        for f in sorted(fs):
            p = os.path.join(root, f)
            files.append((os.path.relpath(p, src).replace(os.sep, "/"), p))
    files.sort()
    b = io.BytesIO()
    with zipfile.ZipFile(b, "w", zipfile.ZIP_STORED) as z:
        for arc, p in files:
            zi = zipfile.ZipInfo(arc, date_time=(1980, 1, 1, 0, 0, 0))
            zi.create_system = 3
            zi.external_attr = 0o644 << 16
            with open(p, "rb") as fh:
                z.writestr(zi, fh.read())
    tmp = out + ".tmp"
    with open(tmp, "wb") as fh:
        fh.write(b.getvalue())
    errs = check_zip(tmp, os.path.basename(out))
    if errs:
        os.remove(tmp)
        return errs
    os.replace(tmp, out)
    return []


def main(argv):
    if len(argv) >= 2 and argv[0] == "--check":
        bad = 0
        for p in argv[1:]:
            for e in check_zip(p):
                print(f"{p}: {e}")
                bad = 1
        if not bad:
            print(f"mksidebar: {len(argv) - 1} sidebar zips, all well formed")
        return bad
    if len(argv) == 2:
        errs = pack(argv[0], argv[1])
        for e in errs:
            print(f"{argv[1]}: {e}", file=sys.stderr)
        return 1 if errs else 0
    print(__doc__.strip(), file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
