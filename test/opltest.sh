#!/bin/sh
# The OPL2 programs.  Both draw through the sprite engine and their own text
# layer at $123000, not the console's text32 at $30000 -- which is the only
# thing test/headless can read -- so what is checked here is that they run and
# hand the machine back, not what they put on the glass.  The picture is
# checked by eye against a screenshot; docs/notes/coding.md says so.
cd "$(dirname "$0")/.."

fail() { echo "$out"; echo "opltest: FAILED: $1"; exit 1; }

# OPLPLAY: runs, three tunes, and Q returns to a working shell.
out=$(./test/headless rom/kernal.bin 'OPLPLAY
~~~~ Q
ECHO OPLBACK
' 2400 2>&1) || fail "OPLPLAY did not run"
echo "$out" | grep -q "OPLBACK" || fail "OPLPLAY did not return to the shell"

# and the older nine-voice demo still does too
out=$(./test/headless rom/kernal.bin 'OPL2
~~~Q
ECHO OPL2BACK
' 2400 2>&1) || fail "OPL2 did not run"
echo "$out" | grep -q "OPL2BACK" || fail "OPL2 did not return to the shell"

# SYS+$36 (54582), the wall-clock millisecond counter OPLPLAY paces its music
# against.  It has to be REAL time, not frame time: the frame counter next to
# it stretches whenever the host runs long, which is what made the Pi's
# playback speed wander.  Two reads a few seconds apart must differ, and the
# harness runs flat out, so if this were frame-derived it would race far ahead
# rather than track the clock.
printf 'say "MS" peek(54582)+256*peek(54583)\ncall sleep 300\nsay "MS" peek(54582)+256*peek(54583)\n' > fs/HOME/OPLMS.RX
out=$(./test/headless rom/kernal.bin 'RX /HOME/OPLMS.RX
' 1200 2>&1) || fail "RX did not run"
rm -f fs/HOME/OPLMS.RX
a=$(echo "$out" | grep -E "^MS [0-9]+" | head -1 | cut -d" " -f2)
b=$(echo "$out" | grep -E "^MS [0-9]+" | tail -1 | cut -d" " -f2)
[ -n "$a" ] && [ -n "$b" ] || fail "could not read the millisecond counter at \$D536"
[ "$a" != "$b" ] || fail "the millisecond counter is not advancing ($a twice)"
[ "$a" -ne 65535 ] || fail "\$D536 reads as an unimplemented register"

echo "opltest: OK (OPLPLAY and OPL2 run and hand back; \$D536 keeps real time: $a -> $b)"
