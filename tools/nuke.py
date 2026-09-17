#!/usr/bin/env python3
"""nuke.py -- jettison what SHIPPING.CFG marks `nuke`.

Doc, 2026-09-17: "nuke is new -> it means that the 'nuked' program is added to
git ignore and no longer updated.  We have to start jettisoning some stuff."

    tools/nuke.py            say what WOULD be done (the default: this is one-way)
    tools/nuke.py --apply    do it

For every key marked nuke, the files that ship it stop being tracked
(`git rm --cached`: nothing is deleted from this disk) and are added to
.gitignore under a marked block.  The image is built from `git archive`, so an
untracked thing is in no image from then on, and nothing anybody does to it is
ever committed again.  History keeps what it was.

What it does NOT do, on purpose: delete source (demo/NAME.c stays, frozen and
tracked, so a nuke can be undone by hand); touch the Makefile's lists (a
checkout that still has the files builds as before; a fresh clone without cc65
will find `make test` wanting them -- unlist them there when that bites); or
touch a sidebar's scene compiled into the emulator (only its zip goes, which
is what takes it off the F12 list).  A chapter marked nuke is hidden from the
handbook exactly as `nope` hides it (doc/guide/mkship.py).
"""
import pathlib, subprocess, sys

REPO = pathlib.Path(__file__).resolve().parent.parent
CFG = REPO / "fs/DOCUMENTS/SHIPPING.CFG"
BEGIN, END = "# >>> nuked (tools/nuke.py; from SHIPPING.CFG) >>>", "# <<< nuked <<<"


def paths_for(key):
    kind, _, name = key.partition(":")
    if kind == "app":     return [f"fs/APPS/{name}/"]
    if kind == "lang":    return [f"fs/LANG/{name}/"]
    if kind == "bin":     return [f"fs/SYSTEM/BIN/{name}"]
    if kind == "sidebar": return [f"fs/SYSTEM/SIDEBARS/{name.upper()}.ZIP"]
    return []                                  # chapter: mkship's business


def main():
    apply = "--apply" in sys.argv[1:]
    keys = []
    for line in CFG.read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if "=" in line:
            k, v = (x.strip() for x in line.split("=", 1))
            if v == "nuke":
                keys.append(k)
    if not keys:
        print("nuke: nothing in SHIPPING.CFG is marked nuke"); return 0
    todo = []
    for k in keys:
        ps = paths_for(k)
        tracked = [p for p in ps if subprocess.run(["git", "-C", str(REPO), "ls-files", "--error-unmatch", p], capture_output=True).returncode == 0]
        print(f"  {k:24s} " + (", ".join(ps) if ps else "(hidden from the handbook; no files)") + ("" if tracked or not ps else "   [already untracked]"))
        todo += tracked
    gi = REPO / ".gitignore"; text = gi.read_text()
    have = text[text.index(BEGIN):text.index(END)].splitlines()[1:] if BEGIN in text else []
    want = sorted(set(have) | {"/" + p for k in keys for p in paths_for(k)})
    if not apply:
        print(f"nuke: {len(todo)} path(s) would stop being tracked and {len(want) - len(have)} line(s) join .gitignore.  --apply to do it."); return 0
    for p in todo:
        subprocess.run(["git", "-C", str(REPO), "rm", "-r", "-q", "--cached", p], check=True)
    block = BEGIN + "\n" + "\n".join(want) + "\n" + END + "\n"
    text = (text[:text.index(BEGIN)] + block + text[text.index(END) + len(END) + 1:]) if BEGIN in text else text.rstrip("\n") + "\n\n" + block
    gi.write_text(text)
    print(f"nuke: done -- {len(todo)} path(s) untracked.  Nothing was deleted from this disk.  Commit to make it so.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
