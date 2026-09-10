#!/usr/bin/env python3
"""Regenerate the curated ZX Origins screen fonts into data/fonts/zx/.

The fonts are from ZX Origins by Damien Guard (https://damieng.com/zx-origins),
Copyright (c) 1988-2023 Damien Guard.  Free to use in a project; the licence
forbids re-hosting the files, so they are NOT committed -- you supply your own
download and this script rebuilds the .bin the emulator loads.

Each ZX Origins zip ships a C64/<Name>.bin: a 4096-byte C64 character ROM,
two 2048-byte charsets.  The K4510's petscii_to_ascii() (sdl/main.c) reads the
lower/upper charset from the SECOND half (offset 2048), the standard C64 order.
ZX Origins puts the two charsets the other way round (lower/upper first), so the
conversion reads the FIRST half (--swap).  Since 2026-09-10 the conversion is
done HERE, once, by tools/mkcp437font.py: the .bin written is a ready 2048-byte
CP437 font (real backslash, braces, box drawing), not a raw C64 chargen -- the
machine no longer converts fonts at run time.

Usage:  tools/mkzxfonts.py [path-to-zx-origins-zips]
  default path: ~/Projects/BMC-K4510/fonts-staging/zx-origins
Run from the repo root; writes data/fonts/zx/<slug>.bin.
"""
import os, sys, zipfile, fnmatch
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# menu slug -> ZX Origins zip name (the FONT_ZX_* order in settings.h).
CURATED = [
    ("bauhaus",   "Bauhaus"),     ("broadway",  "Broadwary"),
    ("computer",  "Computer"),    ("cyberwire", "Cyberwire"),
    ("nlq",       "NLQ"),         ("benguiat",  "ZX Benguiat"),
    ("chicago",   "ZX Chicago"),  ("courier",   "ZX Courier"),
    ("eurostile", "ZX Eurostile"),("ocr-a",     "ZX OCR-A"),
    ("pristine",  "Pristine"),    ("anvil",     "Anvil"),
]

def c64_bin(zf, name):
    """Return the base 4096-byte C64/<name>.bin (a zip may also carry weight
    variants like '<name> Bold.bin' -- prefer the exact base name), or None."""
    fours = [n for n in zf.namelist()
             if fnmatch.fnmatch(n, "*C64/*.bin") and len(zf.read(n)) == 4096]
    exact = [n for n in fours if os.path.basename(n) == name + ".bin"]
    pick = exact or sorted(fours, key=len)      # else the shortest name = the base
    return zf.read(pick[0]) if pick else None

def main():
    zips = sys.argv[1] if len(sys.argv) > 1 else \
        os.path.expanduser("~/Projects/BMC-K4510/fonts-staging/zx-origins")
    out = os.path.join("data", "fonts", "zx")
    if not os.path.isdir(zips):
        sys.exit(f"no ZX Origins zips at {zips} -- pass the folder as an argument")
    os.makedirs(out, exist_ok=True)
    made = missing = 0
    for slug, name in CURATED:
        zp = os.path.join(zips, name + ".zip")
        if not os.path.exists(zp):
            print(f"  MISS {slug}: {name}.zip not found"); missing += 1; continue
        with zipfile.ZipFile(zp) as zf:
            d = c64_bin(zf, name)
        if not d:
            print(f"  MISS {slug}: no 4096-byte C64/*.bin in {name}.zip"); missing += 1; continue
        import mkcp437font
        ref = open(os.path.join("data", "font8.bin"), "rb").read()
        with open(os.path.join(out, slug + ".bin"), "wb") as f:
            f.write(mkcp437font.convert(d, ref, swap=True))    # ZX order -> CP437
        print(f"  OK   {slug}  <- {name}"); made += 1
    print(f"{made} written to {out}/" + (f", {missing} missing" if missing else ""))

if __name__ == "__main__":
    main()
