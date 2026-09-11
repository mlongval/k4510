#!/bin/sh
# A current directory longer than the ROM's 64-byte buffer (review 2026-09-05,
# finding 1).  Before 2026-09-11 GETCWD wrote up to 251 bytes into it, so DIR
# in a deep directory overwrote the shell's stack: an empty directory listed as
# "14134 file(s), 892613426 bytes".  Now the device clamps to the caller's CAP
# ($D318, 64 by default) with "..." + the tail, and DIR <dir> finds its way
# home through CHDIR_BACK instead of a copy of the path.
set -e
cd "$(dirname "$0")/.."
export K4510_NO_STARTUP=1
L=LONGDIRECTORYNAMEAAAABBBBCCCCDDDDEEEEFFFFGGGGHHHHIIIIJJJJKKKKLLLLMMMMNNNNOOOOPPPP
D=fs/ZCWDTEST
cleanup() { rm -rf "$D"; }
trap cleanup EXIT
cleanup
mkdir -p "$D/$L/INNER"
printf 'x\n' > "$D/$L/INNER/F.TXT"

R() { ./test/headless rom/kernal.bin "$1" "${2:-1200}" 2>/dev/null; }
fail() { echo "$out"; echo "cwdtest: FAILED: $1"; exit 1; }

# 1. DIR in the deep directory counts what is there, and the shell survives
out=$(R "~CD /ZCWDTEST/$L
~DIR
~ECHO ALIVE
")
echo "$out" | grep -q "1 file(s), 0 bytes\|0 file(s), 0 bytes" || fail "DIR miscounted in a deep directory (stack overwritten?)"
echo "$out" | grep -q "^ALIVE" || fail "the shell did not survive DIR in a deep directory"
echo "$out" | grep -q '\.\.\.' || fail "the long path was not shortened with ..."

# 2. DIR <dir> from the deep directory comes home to it
out=$(R "~CD /ZCWDTEST/$L
~DIR INNER
~DIR
")
echo "$out" | grep -q "F.TXT" || fail "DIR INNER did not list INNER"
n=$(echo "$out" | grep -c "INNER *<DIR>" || true)
[ "$n" -ge 1 ] || fail "after DIR INNER the shell was not back in the deep directory"
echo "cwdtest: OK (a deep cwd is clamped, DIR survives it and comes home)"
