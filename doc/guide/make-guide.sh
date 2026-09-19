#!/bin/sh
# Build the K4510 guide: screenshots first (from the machine itself),
# then XeLaTeX twice, then check the margins. Run from anywhere.
#
# Two things fail this build, both for the same reason -- a book you cannot
# trust is worse than no book:
#   * a screenshot that cannot be produced (make-shots.sh)
#   * a line that runs off the right margin (below)
# A5 is a narrow measure and monospace does not hyphenate, so overfull
# boxes happen. Set a path in prose with \pth{...} (breaks at / and .),
# give a wide table a p{} column at \small, and keep \verb short.
#
# OVERFULL_OK=1 ./make-guide.sh   builds anyway, for work in progress.
#
# Figures are captured from the running machine, which is most of the build
# time. By default an existing shot is kept and only a missing one is taken,
# so a rebuild after an edit is quick and a fresh clone still gets pictures.
#   ./make-guide.sh --shots     recapture every figure (do this before a
#                               release, and after the machine's screens change)
#   ./make-guide.sh --no-shots  never capture; fail if a figure is missing
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SHOTS=missing
for a in "$@"; do
    case "$a" in
        --shots)    SHOTS=all ;;
        --no-shots) SHOTS=none ;;
        *) echo "make-guide.sh: unknown option $a"; exit 2 ;;
    esac
done
if [ "$SHOTS" = none ]; then
    # the list comes from the book itself, so a new figure cannot be forgotten
    for f in $(grep -ohE "shots/[a-z0-9_]+\.png" "$HERE"/chapters/*.tex | sort -u); do
        [ -s "$HERE/$f" ] || { echo "no $f and --no-shots given"; exit 1; }
    done
    echo "shots: kept (--no-shots)"
else
    SHOTS=$SHOTS "$HERE/make-shots.sh"
fi
cd "$HERE"

# The register appendix comes out of the machine's own headers every build.
python3 "$HERE/mkregs.py"

# ...the code page appendix out of core/codepage.h, the one table all the
# machine's text runs through (it stops the build if the page and the book
# disagree about how many places are not CP437's)...
python3 "$HERE/mkcodepage.py"

# ...the command list and the F7 menu's rows out of the ROM and the frontend,
# which fails the build if a command has no description or a described one is
# gone (the descriptions are in mkref.py)...
python3 "$HERE/mkref.py"

# ...and the repository's issue template out of the Filing an Issue page, so
# that the page and the template cannot say different things.
python3 "$HERE/mkissue.py"

# What ships: /DOCUMENTS/SHIPPING.CFG -> generated/shipping.tex, so \shipif{} knows which
# things were marked `nope' and leaves their pages out.  mkweb.py and mkgem.py
# read the same file, so the three editions cannot disagree (tools/mkmanifest.py
# keeps the list itself).
python3 "$HERE/mkship.py"

# The cover carries a version and a build date. GUIDE_VERSION=... overrides
# the git description; the date is always today, written DD.MM.YYYY.
# Short enough to sit on the cover's first line: tag+commits, e.g. alpha-0.2+88.
if [ -z "$GUIDE_VERSION" ]; then
    TAG=$(git -C "$HERE" describe --tags --abbrev=0 2>/dev/null) || TAG=""
    if [ -n "$TAG" ]; then
        N=$(git -C "$HERE" rev-list "$TAG"..HEAD --count 2>/dev/null || echo 0)
        [ "$N" = 0 ] && GUIDE_VERSION="$TAG" || GUIDE_VERSION="$TAG+$N"
    else
        GUIDE_VERSION=unversioned
    fi
fi
VER=$GUIDE_VERSION
printf '%s\n%s\n' \
  "\\renewcommand{\\guideversion}{$VER}" \
  "\\renewcommand{\\guidedate}{$(date +%d.%m.%Y)}" > version.tex

xelatex -interaction=nonstopmode k4510-guide.tex >/dev/null || true   # errors are read from the log below
xelatex -interaction=nonstopmode k4510-guide.tex | tail -2

# Any LaTeX error at all. nonstopmode keeps going and still writes a PDF, so
# without this check a dropped figure or a bad macro ships silently -- which
# is how a screenshot inside an aside went missing for a day.
if grep -q "^! " k4510-guide.log; then
    echo
    echo "LATEX ERRORS:"
    grep -A3 "^! " k4510-guide.log | head -40
    exit 1
fi

# Overfull boxes, with the page each one lands on. TeX prints page numbers
# as [N as it ships them, so counting those up to the complaint locates it.
awk '
  { n = split($0, part, /\[/)
    for (i = 2; i <= n; i++) if (part[i] ~ /^[0-9]/) { page = part[i] + 0 }
  }
  /^Overfull \\hbox/ {
    getline ctx
    gsub(/\\[A-Za-z]+\/[^ ]*/, "", ctx); gsub(/\[\]/, "", ctx)
    gsub(/^ +| +$/, "", ctx)
    amt = $3; gsub(/[()]/, "", amt)
    printf "  p.%-4d %-12s %.60s\n", page + 1, amt, ctx
    bad++
  }
  END { exit (bad > 0) }
' k4510-guide.log > "$HERE/.overfull" || {
    echo
    echo "MARGINS: $(wc -l < "$HERE/.overfull") line(s) run off the right margin:"
    cat "$HERE/.overfull"
    echo
    echo "Fix them (\\pth{} for paths, p{} + \\small for wide tables), or"
    echo "build with OVERFULL_OK=1 to accept them."
    [ -n "$OVERFULL_OK" ] || exit 1
}
rm -f "$HERE/.overfull"

# A reference to something that is not in this edition. It prints as ?? in the
# book, which is worse than a build that stops: a chapter marked `nope' in
# SHIPPING.CFG is no longer input (2026-09-19), so every cross-reference into
# one has to be wrapped in \shipif{chapter:NN-name}{...} as well.
if grep -q "Reference .* undefined" k4510-guide.log; then
    echo
    echo "REFERENCES: this book points at something it does not contain:"
    grep -o "Reference \`[^']*' on page [0-9]*" k4510-guide.log | sort -u | sed 's/^/  /'
    echo
    echo "Wrap each one in \\shipif{<the key that hides it>}{...}, or put the"
    echo "chapter back.  (SHIPPING.CFG hides: $(sed -n 's/^\\shipnope{\(.*\)}$/\1/p' "$HERE/generated/shipping.tex" | tr '\n' ' '))"
    exit 1
fi
echo "-> $HERE/k4510-guide.pdf"

# The same book as a web site (doc/site, for Read the Docs): made from the
# same chapters now, so the PDF and the pages never say different things.
python3 "$HERE/mkweb.py"

# ...and as the machine's own pages (fs/SYSTEM/DOC, Gemini text, for BOOK),
# from the same chapters again.  md2gemini is not packaged by the distro and
# the system Python refuses pip (PEP 668), so it lives in a venv of its own
# here, made the first time; --system-site-packages keeps PIL visible.
if [ ! -x "$HERE/.venv/bin/python" ]; then
    python3 -m venv --system-site-packages "$HERE/.venv" && "$HERE/.venv/bin/pip" install -q md2gemini
fi
"$HERE/.venv/bin/python" "$HERE/mkgem.py"
