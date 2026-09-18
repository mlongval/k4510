#!/bin/sh
# VI's :set wrap (Doc, 2026-09-18: "definable line wrap like nvim").  A file
# with one 200-column line between two short ones, on the 80-column screen:
# folded it is three rows and the line after it starts on row 4; unfolded it
# is one row and scrolls sideways.  Then the cursor (gj gk by a screen row),
# and a line that grows a row under the typist's hands.
set -e
cd "$(dirname "$0")/.."
ESC=$(printf '\033'); CR=$(printf '\r')
F=fs/HOME/VIWRAP.TXT
fail() { echo "$out"; rm -f $F; echo "vitest: FAILED: $1"; exit 1; }
long=$(awk 'BEGIN{for(i=0;i<20;i++) printf "%c123456789", 65+i}')      # A123456789B123456789...T123456789
mk() { printf 'short one\n%s\nafter the long\n' "$long" > $F; }
row() { echo "$out" | sed -n "$1p"; }                                   # headless prints the rows that are not blank, in order

# 1. folded is the default: rows 2-4 are the long line, 80 to a row
mk; out=$(./test/headless rom/kernal.bin "VI VIWRAP.TXT
~~" 400 "VIWRAP" 2>/dev/null) || fail "VI did not open"
[ "$(row 1)" = "short one" ]                                    || fail "row 1 is not the first line"
[ "$(row 2 | sed 's/ *$//')" = "$(echo "$long" | cut -c1-80)" ]    || fail "row 2 is not the long line's first 80"
[ "$(row 3 | sed 's/ *$//')" = "$(echo "$long" | cut -c81-160)" ]  || fail "row 3 is not its second 80"
[ "$(row 4 | sed 's/ *$//')" = "$(echo "$long" | cut -c161-200)" ] || fail "row 4 is not its last 40"
[ "$(row 5 | sed 's/ *$//')" = "after the long" ]               || fail "the line after it is not on row 5"

# 2. :set nowrap -- one row, and the line after it on row 3; $ scrolls it sideways
mk; out=$(./test/headless rom/kernal.bin "VI VIWRAP.TXT
~~:set nowrap${CR}~~" 500 "nowrap" 2>/dev/null) || fail ":set nowrap did not answer"
[ "$(row 3 | sed 's/ *$//')" = "after the long" ]               || fail "nowrap: the line after is not on row 3"
mk; out=$(./test/headless rom/kernal.bin "VI VIWRAP.TXT
~~:set nowrap${CR}~j\$~~" 500 "col 200" 2>/dev/null) || fail "nowrap: \$ did not reach column 200"
# (sideways, the short lines are off the glass: the long one is the first row with anything on it --
#  and its 80th cell is there: an erase-to-end after a full row used to take it, JIM's pending wrap)
[ "$(row 1)" = "$(echo "$long" | cut -c121-200)" ]               || fail "nowrap: the long line did not scroll to its end, all 80 cells of it"

# 3. :set wrap! turns it back, and the fold returns
mk; out=$(./test/headless rom/kernal.bin "VI VIWRAP.TXT
~~:set nowrap${CR}~:set wrap!${CR}~~" 600 "  wrap" 2>/dev/null) || fail ":set wrap! did not answer"
[ "$(row 5 | sed 's/ *$//')" = "after the long" ]               || fail "wrap!: the fold did not come back"

# 4. gj gk move by a screen row; j k by a line
mk; out=$(./test/headless rom/kernal.bin "VI VIWRAP.TXT
~~j5lgjgj~~" 500 "col 166" 2>/dev/null) || fail "gj gj from column 6 did not reach column 166"
mk; out=$(./test/headless rom/kernal.bin "VI VIWRAP.TXT
~~j5lgjgjgk~~" 500 "col 86" 2>/dev/null) || fail "gk did not come back up a row to column 86"
mk; out=$(./test/headless rom/kernal.bin "VI VIWRAP.TXT
~~j5lgjgjgjgj~~" 500 "3/3" 2>/dev/null) || fail "gj off the last row did not go to the next line"
mk; out=$(./test/headless rom/kernal.bin "VI VIWRAP.TXT
~~jj~~" 500 "3/3" 2>/dev/null) || fail "j did not cross the folded line in one"

# 5. a line that grows a row pushes what is under it down; and the file is right
printf 'top\n%s\nunder\n' "$(echo "$long" | cut -c1-80)" > $F
out=$(./test/headless rom/kernal.bin "VI VIWRAP.TXT
~~jAXYZ${ESC}~~" 500 "col 83" 2>/dev/null) || fail "appending to an 80-column line: no column 83"
[ "$(row 3 | sed 's/ *$//')" = "XYZ" ]                          || fail "the 81st column did not fold onto a row of its own"
[ "$(row 4 | sed 's/ *$//')" = "under" ]                        || fail "the line under it did not move down"
out=$(./test/headless rom/kernal.bin "VI VIWRAP.TXT
~~jAXYZ${ESC}:wq${CR}~~ECHO BACK
~" 900 "BACK" 2>/dev/null) || fail "VI did not hand the shell back"
[ "$(sed -n 2p $F)" = "$(echo "$long" | cut -c1-80)XYZ" ]       || fail "the file does not have the appended text"
rm -f $F
echo "vitest: OK (folded by default, :set nowrap / wrap / wrap!, gj gk by a row and j k by a line, a line growing a row)"
