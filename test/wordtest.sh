#!/bin/sh
# WORD, the .DOCX reader (demo/word.c, 2026-10-02), headless.  It opens
# test/word-sample.docx (pandoc's, from test/word-sample.md): the title, a
# heading, bold and italic runs, accents, bulleted and numbered lists, a
# table.  What is checked is the screen, one cell's colour (a bold word is
# white), Save As Text's file, a Find that misses, and a Word 97-2003 .DOC
# refused by name.  Keys in octal: sh's printf has no \x.
set -e
cd "$(dirname "$0")/.."
H=fs/HOME
fail() { echo "wordtest: FAILED: $1"; rm -f $H/ZZWORD.DOCX $H/ZZWORD.TXT $H/ZZOLD.DOC; exit 1; }
run() { ./test/headless rom/kernal.bin "$1" "${2:-400}" 2>/dev/null; }
cp test/word-sample.docx $H/ZZWORD.DOCX
rm -f $H/ZZWORD.TXT

# the document, laid out: row 6 col 13 is "bold" (the title, a blank, the
# heading, a blank, then "This is a bold"), and its colour is white on blue
out=$(K4510_DUMP=0307B4,4 run "$(printf 'word ZZWORD.DOCX\n~~~~')")
for s in "The K4510 Sample" "Chapter One" "This is a bold word, an italic one" "First bullet" \
         "1. One" "3. Three" "A table" "alpha" "Last paragraph, plain." "88 words" "ZZWORD.DOCX"; do
    echo "$out" | grep -q "$s" || fail "not on the screen: $s"
done
case "$(echo "$out" | tail -1)" in "62 00 01 06") ;; *) fail "the bold word is not white on blue: $(echo "$out" | tail -1)" ;; esac

# Save As Text (Ctrl+S, the name offered), then a Find that finds nothing
out=$(run "$(printf 'word ZZWORD.DOCX\n~~~~\023~\r~~\006~zzzz\r~')" 600)
echo "$out" | grep -q "Match not found" || fail "a Find of zzzz did not say so"
[ -f $H/ZZWORD.TXT ] || fail "Save As Text wrote nothing"
for s in "The K4510 Sample" "* First bullet" "  \\* A nested bullet" "2. Two" "alpha | 1 | first"; do
    grep -q "$s" $H/ZZWORD.TXT || fail "the text file lacks: $s"
done

# a Word 97-2003 file (an OLE compound file: D0 CF 11 E0) is named, not garbled
printf '\320\317\021\340rest' > $H/ZZOLD.DOC
run "$(printf 'word ZZOLD.DOC\n~~~~')" | grep -q "Word 97-2003" || fail "a .DOC was not refused by name"

rm -f $H/ZZWORD.DOCX $H/ZZWORD.TXT $H/ZZOLD.DOC
echo "wordtest: OK (a .DOCX laid out: title, heading, runs in colour, lists numbered and bulleted, a table; Save As Text; Find; a .DOC refused)"
