#!/bin/sh
# The keyboard at the prompt: the shell's line editor and the KEY_* codes.
#
# Until 2026-09-08 the arrows printed Ç ü é â at the prompt (Doc, hdieu): the
# arrow codes and those letters share $80-$83, the queue held bare bytes, and
# readline could not edit anyway.  KBDST bit 6 now says which kind the byte
# last read was, and readline edits.  Keys reach the machine here as the
# frontend sends them: a byte of $80+ is a KEY_* code, $1F makes the next
# byte a character (an accented letter), ~ waits.
#
# The screen is read two ways: as text (row()), and as the raw cells for the
# leg that must SEE an $82 -- or prove there is none.
set -e
cd "$(dirname "$0")/.."
L="$(printf '\202')" R="$(printf '\203')" H="$(printf '\204')" E="$(printf '\205')" D="$(printf '\211')" U="$(printf '\200')" N="$(printf '\201')"
EACUTE="$(printf '\037\202')"
run() { K4510_DUMP=30000,4B00 ./test/headless rom/kernal.bin "$1" 600 2>/dev/null; }
fail() { echo "$out"; echo "keytest: FAILED: $1"; exit 1; }
text() { echo "$out" | grep -v '^dump\|^[0-9A-F][0-9A-F] ' ; }
# 1. Left twice (to between a and b), then a character: it goes in at the cursor, the tail moves
out=$(run "ECHO abc${L}${L}X
~~")
text | grep -q '^aXbc$' || fail "insert at the cursor (aXbc)"
# 2. Home and End
out=$(run "ECHO abc${H}${R}${R}${R}${R}${R}Y${E}Z
~~")
text | grep -q '^YabcZ$' || fail "Home, Right x5, End (YabcZ)"
# 3. Delete under the cursor, Backspace before it
out=$(run "ECHO abc${L}${L}${L}${D}
~~")
text | grep -q '^bc$' || fail "Delete (bc)"
out=$(run "ECHO abc${L}$(printf '\010')
~~")
text | grep -q '^ac$' || fail "Backspace mid-line (ac)"
# 4. Esc clears the whole line, cursor anywhere
out=$(run "ECHO abc${L}${L}$(printf '\033')ECHO ok
~~")
text | grep -q '^ok$' || fail "Esc clears the line"
# 5. Up and Down print NOTHING -- the fault in the screenshot
out=$(run "ECHO a${U}${N}b
~~")
text | grep -q '^ab$' || fail "Up/Down typed a glyph"
echo "$out" | python3 -c '
import sys
cells = []
for line in sys.stdin:
    if len(line) >= 3 and line[2] == " " and all(c in "0123456789ABCDEF" for c in line[:2]):
        cells += [int(x, 16) for x in line.split()]
chars = cells[0::4]
sys.exit(1 if [c for c in chars if 0x80 <= c <= 0x83] else 0)' || fail "an arrow left its glyph (Ç ü é â) on the screen"
# 6. An accented letter is a CHARACTER, and is echoed as itself ($82 = é)
out=$(run "ECHO x${EACUTE}y
~~")
echo "$out" | python3 -c '
import sys
cells = []
for line in sys.stdin:
    if len(line) >= 3 and line[2] == " " and all(c in "0123456789ABCDEF" for c in line[:2]):
        cells += [int(x, 16) for x in line.split()]
chars = bytes(cells[0::4])
sys.exit(0 if b"x\x82y" in chars else 1)' || fail "an accented letter did not go in as a character"
# 7. A line longer than the screen is wide: Left walks back up the wrapped row
A=$(printf 'a%.0s' $(seq 1 76))
out=$(run "ECHO ${A}bcd${L}${L}${L}Z
~~")
text | grep -q "^${A}Zbcd\$" || fail "editing across the wrap"

# history: Up recalls the previous lines, Down walks back, past the newest is
# the empty line again.  Kept in bank 3's RAM (rom/kernal.c readline).
out=$(run "ECHO ALPHA
~ECHO BETA
~${U}${U}$(printf '\r')
~${U}${N}${N}$(printf '\r')
~ECHO END
~" 1200)
[ "$(echo "$out" | grep -c ALPHA)" -ge 4 ] || { echo "$out"; echo "keytest: FAILED: Up Up did not recall ECHO ALPHA"; exit 1; }
echo "$out" | grep -q '^/HOME\] *$' || { echo "$out"; echo "keytest: FAILED: Down past the newest line did not clear the line"; exit 1; }
echo "$out" | grep -q '?' && { echo "$out"; echo "keytest: FAILED: a recalled or cleared line ran as an unknown command"; exit 1; }
# VI: an accented letter inserts, a Left (the same byte, KEY kind) moves
rm -f fs/HOME/VITEST.TXT
out=$(run "VI VITEST.TXT
~~ix${EACUTE}y${L}$(printf '\033'):wq$(printf '\r')~~ECHO BACK
~" 900)
echo "$out" | grep -q BACK || { echo "$out"; rm -f fs/HOME/VITEST.TXT; echo "keytest: FAILED: VI did not hand the shell back"; exit 1; }
[ "$(od -An -c fs/HOME/VITEST.TXT | tr -d ' \n')" = 'x202y\n' ] || { od -c fs/HOME/VITEST.TXT; rm -f fs/HOME/VITEST.TXT; echo "keytest: FAILED: VI did not insert the accented letter (or took the Left as one)"; exit 1; }
rm -f fs/HOME/VITEST.TXT
echo "keytest: OK (history Up/Down, VI takes an accented letter, insert at the cursor, Home/End, Delete, Backspace, Esc, Up/Down silent and glyph-free, é is a character, editing across the wrap)"
