#!/bin/sh
# DELETE: the shell's safe remove -- move to /.TRASH, list it, restore from it,
# empty it.  And the property that matters more than any single case: DELETE
# and RANGER's DD must agree, because two trashes with different rules would be
# worse than no trash at all.
set -e
cd "$(dirname "$0")/.."
D=fs/ZDTEST
cleanup() { rm -rf "$D" fs/.TRASH; }
trap cleanup EXIT
cleanup
mkdir -p "$D"

R() { ./test/headless rom/kernal.bin "$1" "${2:-1400}" 2>/dev/null; }
fail() { echo "$out"; echo "deletetest: FAILED: $1"; exit 1; }
has()   { echo "$out" | grep -q -- "$1" || fail "$2"; }
hasnt() { if echo "$out" | grep -q -- "$1"; then fail "$2"; fi; }

# 1. it moves rather than destroys
printf 'one\n' > "$D/A.TXT"
printf 'two\n' > "$D/B.TXT"
out=$(R '~CD /ZDTEST
~DELETE A.TXT B.TXT
~DIR
')
has "moved 2 items"     "DELETE did not report moving both"
has "0 file(s)"         "the files are still in the directory"
[ -f fs/.TRASH/A.TXT ] || fail "A.TXT is not in the trash"
grep -q one fs/.TRASH/A.TXT || fail "the trashed file lost its contents"

# 2. a name already in the trash must not be eaten
printf 'again\n' > "$D/A.TXT"
out=$(R '~CD /ZDTEST
~DELETE A.TXT
~DELETE -l
')
[ -f 'fs/.TRASH/A.TXT~1' ]      || fail "the second A.TXT was not given a ~1 name"
grep -q one   fs/.TRASH/A.TXT   || fail "the FIRST A.TXT was overwritten"
grep -q again 'fs/.TRASH/A.TXT~1' || fail "the second A.TXT has the wrong contents"
has "3 items in /.TRASH"        "-l miscounted (DIR1 opens the directory, it is not an entry)"

# 3. restore brings it back, and will not clobber a name in the way
out=$(R '~CD /ZDTEST
~DELETE -r A.TXT
~TYPE A.TXT
')
has "restored A.TXT" "-r did not restore"
has "one"            "-r restored the wrong file"
# B.TXT is still in the trash from step 1; put one of that name in the way.
printf 'in the way\n' > "$D/B.TXT"
out=$(R '~CD /ZDTEST
~DELETE -r B.TXT
')
has "taken here"     "-r overwrote, or did not refuse, an existing name"
grep -q "in the way" "$D/B.TXT" || fail "-r OVERWROTE the file that was in the way"

# 4. empty really does delete
out=$(R '~CD /ZDTEST
~DELETE -e
~DELETE -l
')
has "destroyed"          "-e reported nothing"
has "the trash is empty" "-l after -e should say the trash is empty"
[ -f 'fs/.TRASH/A.TXT~1' ] && fail "-e left files behind"

# 5. DELETE and RANGER's DD must use the same trash, with the same rules.
# The fixture is emptied first so SAME.TXT is the only entry -- otherwise the
# bar lands on whatever sorts first and DD trashes the wrong file, which is
# what the first version of this case actually did.
rm -f "$D"/*
printf 'from delete\n' > "$D/SAME.TXT"
out=$(R '~CD /ZDTEST
~DELETE SAME.TXT
')
printf 'from ranger\n' > "$D/SAME.TXT"
out=$(R '~CD /
~RANGER
~G~l~DD~y~q
' 1600)
# G walks to the LAST entry of the root, so a stray directory sorting after
# ZDTEST would send this into the wrong place and the failure would look like
# a trash bug.  Check where it actually went before blaming the trash.
has "/ZDTEST" "RANGER went somewhere other than the fixture -- is there a stray directory sorting after ZDTEST in fs/?"
[ -f fs/.TRASH/SAME.TXT ]      || fail "the two do not share /.TRASH"
[ -f 'fs/.TRASH/SAME.TXT~1' ]  || fail "RANGER did not apply the same ~1 rule as DELETE"
grep -q "from delete" fs/.TRASH/SAME.TXT      || fail "RANGER's DD overwrote what DELETE had put there"
grep -q "from ranger" 'fs/.TRASH/SAME.TXT~1'  || fail "RANGER's file went to the wrong name"

# 6. the shell's own RM trashes, and RM -f is the way out.  This is the ROM,
# not a .prg -- RM is a built-in and no program or alias can shadow it.
rm -rf "$D" fs/.TRASH; mkdir -p "$D/SUB"
printf 'kept\n'  > "$D/R.TXT"
printf 'burned\n'> "$D/F.TXT"
out=$(R '~CD /ZDTEST
~RM R.TXT
~RM -f F.TXT
~RM SUB
~RM NOPE.TXT
~DIR
' 2000)
[ -f fs/.TRASH/R.TXT ]   || fail "RM did not move the file to the trash"
grep -q kept fs/.TRASH/R.TXT || fail "RM trashed the wrong contents"
if [ -f "$D/F.TXT" ]; then fail "RM -f did not really remove the file"; fi
if [ -f fs/.TRASH/F.TXT ]; then fail "RM -f trashed it instead of removing it"; fi
has "rm: not a file"  "RM on a directory should still refuse"
has "rm: not found"   "RM on a missing name should still say so"

# 7. RM obeys the same ~1 rule as DELETE and RANGER
printf 'later\n' > "$D/R.TXT"
out=$(R '~CD /ZDTEST
~RM R.TXT
' 1600)
[ -f 'fs/.TRASH/R.TXT~1' ]     || fail "RM did not apply the ~1 rule"
grep -q kept  fs/.TRASH/R.TXT  || fail "RM overwrote the file already in the trash"
grep -q later 'fs/.TRASH/R.TXT~1' || fail "RM's second file has the wrong contents"

# 8. no arguments explains itself rather than doing something
out=$(R '~DELETE
')
has "not destroyed" "bare DELETE should say what it does"

echo "deletetest: OK (DELETE move/list/restore/empty, RM trashes and RM -f removes, a taken name gets ~1, and DELETE, RANGER's DD and RM all share one trash)"
