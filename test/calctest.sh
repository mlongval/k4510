#!/bin/sh
# test/calctest.sh -- CALC's modern spelling.
#
# Doc, 2026-09-16: "can you rewrite the spreadsheet to use modern conventions
# instead of visicalc ones. noone today remembers visicalc".  So: a formula
# starts with =, functions have no @, a range is A1:B3, and what you type is
# taken for what it looks like -- 3 apples is a note, not a mistake.
#
# The runner types a byte a frame, and octal escapes reach the control keys:
# \023 Ctrl-S, \016 Ctrl-N, \017 Ctrl-O, \231 F10.  It cannot hold a modifier
# down, so Ctrl-Home and Shift-Tab are not tested here.
#
# Every value looked for is an odd one that nothing else on the screen shows.
set -e
cd "$(dirname "$0")/.."
fails=0
check() { if [ "$2" = "$3" ]; then echo "  ok   $1"; else echo "  FAIL $1: got '$2', want '$3'"; fails=$((fails + 1)); fi; }
run() { timeout 120 ./test/headless rom/kernal.bin "$(printf "$1")" "${2:-1200}" 2>/dev/null || true; }
seen() { echo "$1" | grep -c -- "$2" || true; }
rm -f fs/HOME/CT.CAL fs/HOME/CTOLD.CAL

echo "1. what you type is taken for what it looks like"
out=$(run 'CALC\n~~111\n222\n=SUM(A1:A2)\n3 apples\n=1/0\n~')
check "a formula adds the cells above it" "$(seen "$out" '333')" "1"
check "3 apples is a note, not a mistake" "$(seen "$out" '3 apples')" "1"
check "a division by zero says so"        "$(seen "$out" '#ERROR!')" "1"

echo "2. the functions have their modern names and manners"
out=$(run 'CALC\n~~1000\n2000\n=AVERAGE(A1:A2)\n=ROUND(PI(),2)\n=INT(-2.5)*1000\n=COUNT(A1:A2)*1111\n=MAX(A1:A2)+7\n~')
check "AVERAGE over a range"                   "$(seen "$out" '1500')" "1"
check "ROUND takes the places to keep"         "$(seen "$out" '3\.14')" "1"
check "INT goes down, as a spreadsheet's does" "$(seen "$out" '\-3000')" "1"
check "COUNT counts them"                      "$(seen "$out" '2222')" "1"
check "MAX picks the biggest"                  "$(seen "$out" '2007')" "1"

echo "3. a leading ' keeps a thing as text"
out=$(run "CALC\n~~'=A1\n~")
check "'=A1 stays the words =A1" "$(seen "$out" '=A1')" "1"

echo "4. saved and opened again"
# The name is given to CALC, so each prompt comes up already filled in and a
# bare Enter takes it: Ctrl-S save, Ctrl-N Y to empty the sheet, Ctrl-O open.
out=$(run 'CALC CT.CAL\n~~444\n=A1+1\n~\023\n~\016Y~\017\n~~')
check "the sheet comes back with its formula worked out" "$(seen "$out" '445')" "1"
check "and it is written in the modern format" "$(head -1 fs/HOME/CT.CAL 2>/dev/null | cut -d' ' -f1-2)" "K4CALC 2"
check "a formula is saved as one"              "$(grep -c '^A2:F:=A1+1' fs/HOME/CT.CAL 2>/dev/null || true)" "1"
check "a number is saved as one"               "$(grep -c '^A1:N:444' fs/HOME/CT.CAL 2>/dev/null || true)" "1"

echo "5. a sheet from the first CALC is brought over as it loads"
printf 'K4CALC 1 W09 FG\nA1:V:7\nA2:V:8\nA3:V:@SUM(A1...A2)*100\nA4:L:oldsheet\n' > fs/HOME/CTOLD.CAL
out=$(run 'CALC CTOLD.CAL\n~~')
check "@SUM(A1...A2) still adds up" "$(seen "$out" '1500')" "1"
check "and its label is still text" "$(seen "$out" 'oldsheet')" "1"

echo "6. F10 opens the menu the / used to"
out=$(run 'CALC\n~~1\n~\231')
check "the menu offers the commands" "$(seen "$out" 'S save')" "1"

rm -f fs/HOME/CT.CAL fs/HOME/CTOLD.CAL
if [ $fails -eq 0 ]; then echo "calctest: OK (entry rules, the functions, ' for text, a round trip, an old sheet, the menu)"; else echo "calctest: $fails FAILED"; exit 1; fi
