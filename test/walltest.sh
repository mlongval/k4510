#!/bin/sh
# WALL (Doc, 2026-09-18): a message from outside, and the answer to it.  The three
# kinds through the real program -- a choice, a line of text with an accented
# letter, a notice -- oldest first; Esc leaves one for later; and the shell is
# back where it was.  The sending end (tools/k4510-remote wall) writes the same
# files: its half is exercised against this one by hand, it needs an ssh.
set -e
cd "$(dirname "$0")/.."
W=fs/SYSTEM/WALL; CR=$(printf '\r'); ESC=$(printf '\033'); E=$(printf '\037\202')
fail() { echo "$out"; rm -rf $W; echo "walltest: FAILED: $1"; exit 1; }
rm -rf $W
out=$(./test/headless rom/kernal.bin "WALL
~~ECHO DONE
~" 600 "DONE" 2>/dev/null) || fail "WALL with no inbox did not return"
echo "$out" | grep -q "WALL: no messages" || fail "no inbox: it did not say so"

mkdir -p $W/INBOX
printf 'WALL C red|blue|neither\nWhich cable?\n' > $W/INBOX/W-1.TXT
printf 'WALL T\nA name?\n'                         > $W/INBOX/W-2.TXT
printf 'WALL N\nThe layer is built.\n'             > $W/INBOX/W-3.TXT
printf 'no header at all\n'                        > $W/INBOX/W-4.TXT
out=$(./test/headless rom/kernal.bin "CD /DOCUMENTS
WALL
~~7~2~~glac${E}${CR}~~x~~${ESC}~~ECHO DONE
~" 1500 "DONE" 2>/dev/null) || fail "WALL did not hand the shell back"
[ "$(cat $W/OUTBOX/W-1.TXT)" = "2 blue" ]                        || fail "the choice: 7 is not one of three, 2 is"
[ "$(od -An -c $W/OUTBOX/W-2.TXT | tr -d ' \n')" = 'glac202\n' ] || fail "the line of text, with its accented letter"
[ "$(cat $W/OUTBOX/W-3.TXT)" = "seen" ]                          || fail "the notice"
[ -f $W/INBOX/W-4.TXT ] && [ ! -f $W/OUTBOX/W-4.TXT ]            || fail "Esc did not leave the last message for later"
[ "$(ls $W/INBOX)" = "W-4.TXT" ]                                 || fail "the answered messages were not removed"
echo "$out" | grep -q "^/DOCUMENTS\] ECHO DONE"                  || fail "the shell is not back in the directory it was in"
rm -rf $W
echo "walltest: OK (no inbox, a choice, a line of text with an accent, a notice, Esc for later, the directory kept)"
