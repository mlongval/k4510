#!/bin/sh
# BOOK, the handbook on the machine (demo/book.c reading fs/SYSTEM/DOC, which
# doc/guide/mkgem.py makes): the contents, a chapter by number and by word,
# a link followed and come back from, a search, a picture shown and put away,
# and leaving for the prompt.  The headless machine prints its text screen
# at the end of each run, so every run ends in the state it checks.
cd "$(dirname "$0")/.."
fail() { echo "$out"; echo "booktest: FAILED: $1"; exit 1; }
run() { out=$(./test/headless rom/kernal.bin "$1" "$2" "$3" 2>&1) || fail "$4 (BOOK did not come up)"; }
[ -s fs/SYSTEM/DOC/INDEX.GMI ] || fail "no fs/SYSTEM/DOC/INDEX.GMI -- doc/guide/make-guide.sh makes it"

run 'BOOK
' 900 'Tab link' "the contents"
echo "$out" | grep -q "The K4510 User's and Programmer's Guide" || fail "the contents' title"
echo "$out" | grep -q "1. The Machine"                           || fail "the contents' first chapter"
echo "$out" | grep -q "BOOK  INDEX.GMI"                          || fail "the status line"

run 'BOOK 2
' 900 '02-SHELL.GMI' "BOOK 2"
echo "$out" | grep -q "Chapter 2. The Shell"                     || fail "BOOK 2 did not open chapter 2"

run 'BOOK SHELL
' 900 '02-SHELL.GMI' "BOOK SHELL"
echo "$out" | grep -q "Chapter 2. The Shell"                     || fail "BOOK SHELL did not find the chapter by its title"

# Tab chooses the first link (1. The Machine), Enter follows it...
run "$(printf 'BOOK\n~\t\r~')" 900 '01-MACHINE.GMI' "a link followed"
echo "$out" | grep -q "Chapter 1. The Machine"                   || fail "Enter did not follow the link"
# ...and Backspace comes back to the contents
run "$(printf 'BOOK\n~\t\r~\b~')" 900 'INDEX.GMI' "back"
echo "$out" | grep -q "1. The Machine"                           || fail "Backspace did not come back"
echo "$out" | grep -q "Chapter 1. The Machine"                   && fail "Backspace left chapter 1 on the screen"

# / finds a word further down: the page scrolls to it
run "$(printf 'BOOK 2\n~/MOUNT\r~')" 900 '02-SHELL.GMI' "a search"
echo "$out" | grep -q "MOUNT"                                    || fail "/ did not find MOUNT"
echo "$out" | grep -q "Chapter 2. The Shell"                     && fail "/ did not scroll to what it found"

# the LOGO chapter's first link is its screenshot: shown, then a key back.
# The heading comes from the page itself: hiding a chapter renumbers the ones
# after it (SHIPPING.CFG, 2026-09-19), and a number typed in here went stale.
LOGOHEAD=$(sed -n 's/^# //p' fs/SYSTEM/DOC/08-LOGO.GMI | head -1)
run "$(printf 'BOOK LOGO\n~\t\r~~ ~')" 1500 '08-LOGO.GMI' "a picture"
echo "$out" | grep -q "$LOGOHEAD"                                || fail "the page did not come back after the picture (looked for \"$LOGOHEAD\")"

run 'BOOK
~q~ECHO BACK
' 900 'BACK' "leaving"
echo "$out" | grep -q "^BACK"                                    || fail "Q did not return to the prompt"
echo "$out" | grep -q "Tab link"                                 && fail "the status line was left behind"

echo "booktest: OK (contents, BOOK 2, BOOK SHELL, a link and back, /, a picture, Q)"
