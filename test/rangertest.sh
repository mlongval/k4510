#!/bin/sh
# RANGER, the miller-column file browser: the three columns, the column-count
# option, and every operation that writes to the device.
#
# The trash cases are the point of this file. The device's RENAME and COPY take
# the host's semantics and overwrite an existing name in silence, so "did the
# call fail?" is not a collision test -- the first version of to_trash() used
# it as one, and deleting two files that shared a name destroyed the first.
# Anything here that says "not overwritten" is guarding that.
#
# The fixture sorts last in its directory on purpose: the test walks to it
# with G (bottom) rather than counting j presses, so a stray file in fs/ shifts
# no indices and the test still knows where it landed -- it checks the path in
# RANGER's own header before doing anything destructive.
set -e
cd "$(dirname "$0")/.."
# The fixture lives under /LANG/BBCBASIC: RANGER opens where the shell is, so
# every run starts with CD there (the prompt itself boots in /HOME).
D=fs/LANG/BBCBASIC/ZRTEST
cleanup() { rm -rf "$D" fs/.TRASH; rm -f fs/CPM/A/0/ZZTEST.COM fs/CPM/A/0/K-RUN.SUB 'fs/CPM/A/0/$$$.SUB'; }
trap cleanup EXIT
cleanup
mkdir -p "$D"
printf 'the first one\n' > "$D/DUP.TXT"
printf 'alpha\n'         > "$D/A.TXT"

R() { ./test/headless rom/kernal.bin "CD /LANG/BBCBASIC
$1" "${2:-1200}" 2>/dev/null; }
fail() { echo "$out"; echo "rangertest: FAILED: $1"; exit 1; }
has()   { echo "$out" | grep -q -- "$1" || fail "$2"; }
# Negative assertions need the `if` form: under set -e a bare `grep && fail`
# aborts the script when grep finds nothing, which is the passing case.
hasnt() { if echo "$out" | grep -q -- "$1"; then fail "$2"; fi; }

# 1. it starts, and the header names the directory it is in
out=$(R '~RANGER
~')
has " /"            "no path header"
has "README.BBC"    "no directory listing"
has "hjkl"          "no hint line"

# 2. the column option.  Keyed on the preview (BOUNCE.BBC is inside EX,
# which the bar starts on) and on the listing's indent -- NOT on "^ BBCBASIC",
# which also matches the status line naming the selected entry.
out=$(R '~RANGER 1
~')
hasnt "BOUNCE.BBC"        "RANGER 1 should show no preview column"
out=$(R '~RANGER 2
~')
has   "BOUNCE.BBC"        "RANGER 2 should show a preview column"
hasnt "/ README.BBC"  "RANGER 2 should show no parent column"
out=$(R '~RANGER 3
~')
has   "BOUNCE.BBC"        "RANGER 3 should show a preview column"
has   "/ README.BBC"  "RANGER 3 should show the listing beside the parent column"

# 3. walking in, and the header following
out=$(R '~RANGER
~G~l~')
has "/ZRTEST"       "G then l did not enter the fixture (does something sort after ZRTEST in fs/LANG/BBCBASIC?)"
has "DUP.TXT"       "the fixture's files are not listed"
has "alpha"         "no file preview (the bar starts on A.TXT, which holds it)"

# 4. quitting leaves the shell in the directory we ended in
out=$(R '~RANGER
~G~l~q
~')
has "/ZRTEST\]"     "the shell did not land in the browsed directory"

# 5. Enter on a .prg: RANGER leaves and the shell runs it.  The program's own
# output has to survive -- that is the whole reason the filer gets out of the
# way instead of running it under SWAP, which would restore the screen over it.
cp fs/LANG/PASCAL/hello.prg "$D/ZRUN.PRG"
out=$(R '~RANGER
~G~l~G~
~~' 1800)
has "Hello from Mad Pascal"  "Enter on a .prg did not run it"
hasnt "hjkl"                 "RANGER should have left before the program ran"
rm -f "$D/ZRUN.PRG"

# 6. make a directory
out=$(R '~RANGER
~G~l~mSUB
~q
~DIR
' 1500)
[ -d "$D/SUB" ] || fail "m did not make a directory"

# 7. yank and paste: A.TXT into SUB
out=$(R '~RANGER
~G~l~yy~G~l~pp~' 1600)
[ -f "$D/SUB/A.TXT" ] || fail "yy then pp did not copy the file"
[ -f "$D/A.TXT" ]     || fail "a yank must not remove the original"

# 8. paste must never overwrite
printf 'KEEP ME\n' > "$D/SUB/A.TXT"
out=$(R '~RANGER
~G~l~yy~G~l~pp~' 1600)
has "already here"  "paste over an existing name gave no warning"
grep -q "KEEP ME" "$D/SUB/A.TXT" || fail "paste OVERWROTE an existing file"

# 9. rename
out=$(R "~RANGER
~G~l~r$(printf '\b%.0s' 1 2 3 4 5 6 7)B.TXT
~q
" 1500)
[ -f "$D/B.TXT" ] || fail "r did not rename A.TXT"
if [ -f "$D/A.TXT" ]; then fail "the old name survived the rename"; fi

# 10. delete to the trash: it moves, it does not destroy
out=$(R '~RANGER
~G~l~DD~y~q
' 1400)
[ -f fs/.TRASH/B.TXT ] || fail "DD did not move the file into the trash"
grep -q "alpha" fs/.TRASH/B.TXT || fail "the trashed file lost its contents"

# 11. a second file of the same name must not eat the first
printf 'the second one\n' > "$D/B.TXT"
out=$(R '~RANGER
~G~l~DD~y~q
' 1400)
[ -f fs/.TRASH/B.TXT ]    || fail "the first trashed file was destroyed by the second"
[ -f 'fs/.TRASH/B.TXT~1' ] || fail "the second file was not given a ~1 name"
grep -q "alpha"          fs/.TRASH/B.TXT    || fail "the first trashed file was overwritten"
grep -q "the second one" 'fs/.TRASH/B.TXT~1' || fail "the second trashed file has the wrong contents"

# 12. Enter on a .COM: RANGER leaves, writes the launcher, and the shell types
# CPM K-RUN.  The submit's EXIT is what brings the machine back to the K:OS
# prompt -- without it the round trip would end at CP/M's A0>.  The fixture is
# a copy of STAT.COM under a name that sorts last on A:0, so G lands on it
# however many files the drive has.
cp fs/CPM/A/0/STAT.COM fs/CPM/A/0/ZZTEST.COM
out=$(R 'CD /CPM/A/0
~RANGER
~~G~
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~' 3000)
has "A: R/W, Space:"  "Enter on a .COM did not run it under CP/M"
has 'A0\$'            "the .COM did not run from a submit"
has "/CPM/A/0\]"      "EXIT did not land back at the K:OS prompt"
rm -f fs/CPM/A/0/ZZTEST.COM fs/CPM/A/0/K-RUN.SUB 'fs/CPM/A/0/$$$.SUB'

echo "rangertest: OK (three columns, the column option, mkdir/yank/paste/rename, paste and trash both refuse to overwrite, exit lands in the browsed directory, Enter runs a .prg and a .COM)"
