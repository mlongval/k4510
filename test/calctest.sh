#!/bin/sh
# test/calctest.sh -- CALC in Excel's spelling and EDIT's clothes.
#
# Doc, 2026-09-16: "can you rewrite the spreadsheet to use modern conventions
# instead of visicalc ones. noone today remembers visicalc".  And 2026-10-10:
# the look of EDIT and WORD, Excel's and LibreOffice's manners -- $A$1 and F4,
# copy and fill that move references, comparisons and IF, text functions,
# Excel's error values, , and ; between arguments, recalculation in the
# order the formulas need and #CIRC! for a circle, CSV in and out.
#
# The runner types a byte a frame, and octal escapes reach the control keys:
# \003 Ctrl-C  \004 Ctrl-D  \007 Ctrl-G  \016 Ctrl-N  \017 Ctrl-O  \021 Ctrl-Q
# \023 Ctrl-S  \026 Ctrl-V  \033 Esc  \200-\203 the arrows  \221 F2  \223 F4
# \231 F10  \303 Alt+C (the Cells menu).  It cannot hold a modifier down, so
# Shift+arrows (a range), Ctrl-Home and Shift-Tab are not driven here.
#
# Every value looked for is an odd one that nothing else on the screen shows.
set -e
cd "$(dirname "$0")/.."
H=fs/HOME
fails=0
check() { if [ "$2" = "$3" ]; then echo "  ok   $1"; else echo "  FAIL $1: got '$2', want '$3'"; fails=$((fails + 1)); fi; }
run() { timeout 120 ./test/headless rom/kernal.bin "$(printf "$1")" "${2:-1500}" 2>/dev/null || true; }
seen() { echo "$1" | grep -c -- "$2" || true; }
has() { if echo "$1" | grep -q -- "$2"; then echo 1; else echo 0; fi; }
rm -f $H/CT.CAL $H/CTOLD.CAL $H/CT2.CAL $H/SHEET.CSV $H/CTIN.CSV

echo "1. what you type is taken for what it looks like"
out=$(run 'CALC\n~~111\n222\n=SUM(A1:A2)\n3 apples\n=1/0\n~')
check "a formula adds the cells above it" "$(seen "$out" '333')" "1"
check "3 apples is a note, not a mistake" "$(seen "$out" '3 apples')" "1"
check "a division by zero says so, as Excel does" "$(seen "$out" '#DIV/0!')" "1"

echo "2. the functions have their modern names and manners"
out=$(run 'CALC\n~~1000\n2000\n=AVERAGE(A1:A2)\n=ROUND(PI(),2)\n=INT(-2.5)*1000\n=COUNT(A1:A2)*1111\n=MAX(A1:A2)+7\n=MOD(17;5)+9000\n=MIN(A1:A2)+5\n~')
check "AVERAGE over a range"                   "$(seen "$out" '1500')" "1"
check "ROUND takes the places to keep"         "$(seen "$out" '3\.14')" "1"
check "INT goes down, as a spreadsheet's does" "$(seen "$out" '\-3000')" "1"
check "COUNT counts them"                      "$(seen "$out" '2222')" "1"
check "MAX picks the biggest"                  "$(seen "$out" '2007')" "1"
check "MOD is Excel's (the sign of the divisor)" "$(seen "$out" '9002')" "1"
check "MIN picks the smallest"                 "$(seen "$out" '1005')" "1"

echo "3. a leading ' keeps a thing as text"
out=$(run "CALC\n~~'=A1\n~")
check "'=A1 stays the words =A1" "$(has "$out" '=A1')" "1"

echo "4. comparisons, IF, AND OR NOT, & and the text functions"
out=$(run 'CALC\n~~=IF(1>2;"yes";"no")\n=AND(1;0)\n=OR(0;1)&"!"\n=NOT(1=1)\n=UPPER(LEFT("hello";2))&RIGHT("world";1)\n=MID("spreadsheet";7;5)\n=CONCAT("a";"b";1.5)\n=LEN("abcd")&"x"\n=TRIM("  a   b ")&"|"\n=2<=2\n~')
check "IF takes the branch the test says"  "$(has "$out" '^   1 no')" "1"
check "AND(1;0) is FALSE"                  "$(has "$out" '^   2 *FALSE')" "1"
check "OR(0;1) is TRUE, joined to text"    "$(has "$out" 'TRUE!')" "1"
check "NOT(1=1) is FALSE"                  "$(has "$out" '^   4 *FALSE')" "1"
check "UPPER LEFT RIGHT and &"             "$(has "$out" 'HEd')" "1"
check "MID"                                "$(has "$out" '^   6 sheet')" "1"
check "CONCAT writes a number out"         "$(has "$out" 'ab1.5')" "1"
check "LEN"                                "$(has "$out" '4x')" "1"
check "TRIM"                               "$(has "$out" 'a b|')" "1"
check "<= compares"                        "$(has "$out" '^  10 *TRUE')" "1"

echo "5. , and ; both part the arguments, and a formula is shown as typed"
out=$(run 'CALC\n~~=SUM(1,2,3)*101\n=SUM(1;2;3)*103\n=ROUND(2.567,2)\n=ROUND(2.567;1)\n~\200~')
check "commas, as Excel"                         "$(seen "$out" '606')" "1"
check "semicolons, as LibreOffice in French"     "$(seen "$out" '618')" "1"
check "ROUND(x,2) and ROUND(x;1)"                "$(has "$out" '2\.57')$(has "$out" '2\.6$')" "11"
check "the formula bar shows it as it was typed" "$(has "$out" '=ROUND(2.567;1)')" "1"

echo "6. the error values are Excel's"
out=$(run 'CALC\n~~=ZZ1\n=FOO(1)\n="a"+1\n=NA()\n=SQRT(-1)\n=1+\n=A1\n~')
check "#REF! for a cell off the sheet"      "$(has "$out" '#REF!')" "1"
check "#NAME? for a function it lacks"      "$(has "$out" '#NAME?')" "1"
check "#VALUE! for text in a sum"           "$(has "$out" '#VALUE!')" "1"
check "#N/A from NA()"                      "$(has "$out" '#N/A')" "1"
check "#NUM! for a root of -1"              "$(has "$out" '#NUM!')" "1"
check "#ERROR! for a formula that will not parse" "$(has "$out" '#ERROR!')" "1"
check "an error reaches the cell that uses it" "$(seen "$out" '#REF!')" "2"

echo "7. recalculation in the order the formulas need; a circle is caught"
# A1 =A2*2 is above its 21; A3 =B1+1 and B1 =A3+1 are a circle, and B2 =A3 hangs on it.
out=$(run 'CALC\n~~=A2*2\n21\n=B1+1\n~\203\200\200\200=A3+1\n=A3\n~')
check "a formula above what it uses still comes out" "$(seen "$out" '42')" "1"
check "A3 and B1 are a circle, B2 hangs on it: three #CIRC!" "$(seen "$out" '#CIRC!')" "3"

echo "8. \$A\$1 holds, A1 moves: copy and paste, fill down, F4"
# B1 =A1*2 copied to B3 reads A3; C1 =$A$1*7 copied to C3 still reads A1;
# Ctrl-D in B4 takes B3 down one row.  Then F4 on =A1 in D4 makes it =$A$1.
# (Enter after the Tab goes back to column B: the cursor is at B2 then.)
out=$(run 'CALC\n~~10\n20\n30\n40\n~\200\200\200\200\203=A1*2\t=$A$1*7\n~\200\003\201\201\026\203\200\200\003\201\201\026\202\201\004~\203\203=A1\223\n~\200~')
check "the pasted B3 reads A3: 60"           "$(seen "$out" '60')" "1"
check "the pasted C3 still reads \$A\$1: 70 twice" "$(seen "$out" '70')" "2"
check "B4 filled down from B3 reads A4: 80"  "$(seen "$out" '80')" "1"
check "F4 put the dollars on"                "$(has "$out" '=\$A\$1 ')" "1"

echo "9. saved and opened again, with a format; the first CALC's sheets"
# A1 444 with Cells > Decimals 2 (Alt+C, D, the field cleared, 2); A2 =A1+1;
# Ctrl-S saves under the name given; Ctrl-N clears; Ctrl-O, the name, Enter.
out=$(run 'CALC CT.CAL\n~~444\n=A1+1\n~\200\200\303d\b2\n~\023~\016~\017~CT.CAL\n~~')
check "the sheet comes back with its formula worked out" "$(seen "$out" '445')" "1"
check "and its format"                                   "$(seen "$out" '444\.00')" "1"
check "it is written as K4CALC 3"       "$(head -1 $H/CT.CAL 2>/dev/null | cut -d' ' -f1-2)" "K4CALC 3"
check "a formula is saved as one"       "$(grep -c '^A2:F:=A1+1' $H/CT.CAL 2>/dev/null || true)" "1"
check "a number is saved with its format" "$(grep -c '^A1:N2:444' $H/CT.CAL 2>/dev/null || true)" "1"
printf 'K4CALC 1 W09 F$\nA1:V:7\nA2:V:8\nA3:V:@SUM(A1...A2)*100\nA4:L:oldsheet\n' > $H/CTOLD.CAL
out=$(run 'CALC CTOLD.CAL\n~~')
check "@SUM(A1...A2) still adds up (F\$: two places)" "$(seen "$out" '1500\.00')" "1"
check "and its label is still text"                   "$(seen "$out" 'oldsheet')" "1"
printf 'K4CALC 2 W09 FG\nA1:N:4440\nA2:F:=A1+1\n' > $H/CT2.CAL
out=$(run 'CALC CT2.CAL\n~~')
check "a K4CALC 2 sheet opens as it is" "$(seen "$out" '4441')" "1"

echo "10. CSV out to LibreOffice and Excel, and back"
# Hello, 12.5, =B1*2, 'x,y; File > Export CSV (F10, p, the name offered);
# Ctrl-N (not saved: Tab Enter is No); File > Import CSV, the name, Enter.
out=$(run "CALC\n~~Hello\t12.5\n=B1*2\t'x,y\n~\231~p~\n~\016~\t\n~\231~i~SHEET.CSV\n~~")
check "the file: a text, a number, a formula's value, a text quoted" "$(cat $H/SHEET.CSV 2>/dev/null | tr '\n' '|')" 'Hello,12.5|25,"x,y"|'
check "and back in: the number and the quoted text" "$(has "$out" '12\.5')$(has "$out" 'x,y')" "11"
printf 'a;b\n1;2\n=A2+B2;"q;x"\n' > $H/CTIN.CSV
out=$(run 'CALC CTIN.CSV\n~~')
check "a semicolon file is told apart, a formula in it works" "$(has "$out" '^   3 *3 *q;x')" "1"

echo "11. the clothes: menus, Tab and Enter, Go To, a date, leaving"
out=$(run 'CALC\n~~1\n~\231')
check "F10 opens the File menu"       "$(has "$out" 'Import CSV')" "1"
out=$(run 'CALC\n~~1\n~\303')
check "Alt+C opens the Cells menu"    "$(has "$out" 'Thousands')" "1"
out=$(run 'CALC\n~~Item\tQty\nApples\n~')
check "Enter after Tabs goes back to the first column" "$(has "$out" '^   2 Apples')" "1"
out=$(run 'CALC\n~~46305\n~\200\303a~\007~C5\n~')
check "Cells > Date shows the days as a date" "$(has "$out" '2026-10-10')" "1"
check "Ctrl-G goes to the cell named"         "$(has "$out" ' C5 *$')" "1"
out=$(run 'CALC\n~~5\n~\021~')
check "Ctrl-Q with the sheet unsaved asks"    "$(has "$out" 'not saved')" "1"
out=$(run 'CALC\n~~5\n~\021~\t\n~')
check "No leaves, and the shell is back"      "$(has "$out" 'HOME\]')" "1"

rm -f $H/CT.CAL $H/CTOLD.CAL $H/CT2.CAL $H/SHEET.CSV $H/CTIN.CSV
if [ $fails -eq 0 ]; then echo "calctest: OK (entry rules, the functions, text and logic, , and ;, Excel's errors, order and #CIRC!, \$A\$1 with copy fill and F4, a round trip and the old sheets, CSV both ways, the clothes)"; else echo "calctest: $fails FAILED"; exit 1; fi
