#!/usr/bin/env python3
"""tools/mkmanifest.py -- what ships, and what does not.

Doc, 2026-09-16 (a brainshot): "We need to start adding qualifiers to all the
stuff that has been built: Essential (has to go with the shipping version),
Maybe (case by case decision at time of shipping), Nope (stuff that is old, or
not interesting for a newcomer, or some other reason).  Nope does not imply
that we delete it from the repo, just that the build instructions understand
what to pick and what to leave."

So this walks the machine and writes /DOCUMENTS/SHIPPING.CFG: one line per thing, each
marked essential, maybe or nope.  It is the LIST, not the decision -- the file
is Doc's to edit, and running this again keeps every mark he has made.

    tools/mkmanifest.py            update /DOCUMENTS/SHIPPING.CFG, keeping the marks
    tools/mkmanifest.py --check    say what is new or gone; exit 1 if any
    tools/mkmanifest.py --list nope    the keys at one mark, for a build script

**A new thing appears as `maybe`, never as `nope`.**  The default has to be the
one that is wrong in the harmless direction: a `maybe` that should have been
`nope` ships something dull, while a `nope` that should have been `maybe`
silently drops work nobody notices is missing until it is needed.

**What `nope` costs, and why it is not free.**  A thing that stops being built
stops being TESTED, and rots quietly.  The sidebar_names[] bug of 2026-09-16
sat for a day behind exactly that kind of silence.  So `nope` marks what the
build may LEAVE OUT of an image -- not what the repo forgets: the source stays,
the tests still run it, and `make` still compiles it here.
"""
import re, sys, pathlib

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parent
CFG = REPO / "fs/DOCUMENTS/SHIPPING.CFG"   # on the machine's own disk: Doc edits it there, with VI

MARKS = ("essential", "maybe", "nope", "nuke")

# The few that are not a judgement call: without these there is no machine to
# ship, or no handbook that makes sense.  Everything else starts as `maybe`
# and waits for Doc.
ESSENTIAL = {
    "lang:EHBASIC", "lang:LOGO", "lang:BBCBASIC", "lang:FORTH",
    "bin:vi.prg", "bin:type.prg", "bin:monitor.prg", "bin:book.prg",
    "bin:ranger.prg", "bin:setup.prg", "bin:banner.prg", "bin:bug.prg",
    "chapter:01-machine", "chapter:02-shell", "chapter:03-commands",
    "chapter:z1-disclaimer", "chapter:z2-thanks", "chapter:z3-licences",
    "sidebar:border", "sidebar:registers",
}


def scan():
    """[(section, key, note)] -- everything the machine ships, in reading order."""
    out = []

    def add(sec, name, note=""):
        out.append((sec, name, note))

    for d in sorted((REPO / "fs/APPS").iterdir()) if (REPO / "fs/APPS").is_dir() else []:
        if d.is_dir():
            add("apps", f"app:{d.name}", first_line(d))
    for d in sorted((REPO / "fs/LANG").iterdir()) if (REPO / "fs/LANG").is_dir() else []:
        if d.is_dir():
            add("languages", f"lang:{d.name}")
    for f in sorted((REPO / "fs/SYSTEM/BIN").glob("*.prg")):
        add("programs", f"bin:{f.name}")
    m = (REPO / "Makefile").read_text()
    names = re.search(r"^SIDEBAR_NAMES\s*=\s*(.*)$", m, re.M)
    for s in (names.group(1).split() if names else []):
        add("sidebars", f"sidebar:{s}")
    master = (REPO / "doc/guide/k4510-guide.tex").read_text()
    for c in re.finditer(r"\\input\{chapters/([\w-]+)\}", master):
        add("handbook", f"chapter:{c.group(1)}")
    return out


def first_line(d):
    """An app's README first line, if it has one: a reminder of what it is."""
    for n in ("README", "README.TXT", "readme.txt"):
        p = d / n
        if p.is_file():
            for line in p.read_text(errors="replace").splitlines():
                if line.strip():
                    return line.strip()[:58]
    return ""


def read_cfg():
    marks = {}
    if CFG.is_file():
        for line in CFG.read_text().splitlines():
            line = line.split("#", 1)[0].strip()
            if "=" in line:
                k, v = (x.strip() for x in line.split("=", 1))
                if v in MARKS:
                    marks[k] = v
    return marks


def write_cfg(items, marks):
    secs, out = {}, []
    for sec, key, note in items:
        secs.setdefault(sec, []).append((key, note))
    out.append("# K4510 -- what ships.  Written by tools/mkmanifest.py; the MARKS are yours.\n"
               "#\n"
               "#   essential   goes with every shipping image\n"
               "#   maybe       decided case by case, at the time of shipping\n"
               "#   nope        left out of an image -- NOT deleted: the source stays\n"
               "#               in the repo and the tests still run it\n"
               "#   nuke        jettisoned: tools/nuke.py stops tracking it and adds it to\n"
               "#               .gitignore, so it is in no image and is never updated again.\n"
               "#               What is on a disk stays there; git history keeps the rest.\n"
               "#\n"
               "# Run tools/mkmanifest.py again after adding anything: it keeps every mark\n"
               "# below and appends what is new, as `maybe'.  Nothing is ever marked `nope'\n"
               "# for you -- that is a decision, and decisions are not generated.\n")
    width = max((len(k) for _, k, _ in items), default=20) + 1
    for sec in ("apps", "languages", "programs", "sidebars", "handbook"):
        if sec not in secs:
            continue
        out.append(f"\n[{sec}]")
        for key, note in secs[sec]:
            mark = marks.get(key) or ("essential" if key in ESSENTIAL else "maybe")
            line = f"{key:<{width}} = {mark}"
            if note:
                line = f"{line:<{width + 14}} # {note}"
            out.append(line)
    CFG.write_text("\n".join(out) + "\n")


def main():
    args = sys.argv[1:]
    items = scan()
    marks = read_cfg()
    keys = [k for _, k, _ in items]

    if args and args[0] == "--list":
        want = args[1] if len(args) > 1 else "essential"
        for k in keys:
            if (marks.get(k) or ("essential" if k in ESSENTIAL else "maybe")) == want:
                print(k)
        return 0

    new = [k for k in keys if k not in marks]
    gone = [k for k in marks if k not in keys]
    if args and args[0] == "--check":
        for k in new:
            print(f"new, unmarked: {k}")
        for k in gone:
            print(f"in /DOCUMENTS/SHIPPING.CFG but no longer in the machine: {k}")
        return 1 if (new or gone) else 0

    write_cfg(items, marks)
    kept = len(marks) - len(gone)
    print(f"mkmanifest: {len(items)} things, {kept} marks kept, {len(new)} new (as `maybe')"
          + (f", {len(gone)} gone" if gone else ""))
    for k in new:
        print(f"  new: {k}")
    for k in gone:
        print(f"  gone: {k}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
