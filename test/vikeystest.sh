#!/bin/sh
# VI's normal mode (demo/vikeys.h), in the three editors that run it: VI,
# EDIT -v (2026-10-02) and PROG -v (2026-10-03, demo/dosvi.h's overlays).  The same keys on the same three lines, the file as
# saved compared.  Where the two differ, they differ on purpose: EDIT's
# register holds characters too (x then p swaps two, as in vim), and EDIT's
# : line takes a line number.  Keys: ~ waits 30 frames; octal escapes,
# because sh's printf has no \x (\033 Esc, \r Enter, \022 Ctrl-R, \004 Ctrl-D).
set -e
cd "$(dirname "$0")/.."
F=fs/HOME/ZZVK.TXT
fail() { rm -f $F; echo "vikeystest: FAILED: $1"; exit 1; }
n=0
try() {   # editor, keys, the three lines expected (\n between)
    printf 'one two three\nfour five six\nseven eight nine\n' > $F
    w='~~'; case "$1" in prog*) w='~~~' ;; esac   # PROG takes longer to come up
    ./test/headless rom/kernal.bin "$(printf "$1 ZZVK.TXT\n$w$2~:w\r~:q\r~~")" 900 2>/dev/null >/dev/null
    printf "$3\n" | cmp -s - $F || fail "$1: $2 gave: $(cat $F | tr '\n' '|')"
    n=$((n + 1))
}
for ed in vi 'edit -v' 'prog -v'; do
    try "$ed" 'wdw'          'one three\nfour five six\nseven eight nine'
    try "$ed" 'wcwTWO\033'   'one TWO three\nfour five six\nseven eight nine'
    try "$ed" 'ddp'          'four five six\none two three\nseven eight nine'
    try "$ed" '2dd'          'seven eight nine'
    try "$ed" 'yyjp'         'one two three\nfour five six\none two three\nseven eight nine'
    try "$ed" 'jJ'           'one two three\nfour five six seven eight nine'
    try "$ed" 'jJu'          'one two three\nfour five six\nseven eight nine'
    try "$ed" 'dwu\022'      'two three\nfour five six\nseven eight nine'
    try "$ed" 'd$'           '\nfour five six\nseven eight nine'
    try "$ed" 'jdG'          'one two three'
    try "$ed" '3x'           ' two three\nfour five six\nseven eight nine'
    try "$ed" 'rX'           'Xne two three\nfour five six\nseven eight nine'
    try "$ed" 'Ahi\033'      'one two threehi\nfour five six\nseven eight nine'
    try "$ed" 'Oup\033'      'up\none two three\nfour five six\nseven eight nine'
    try "$ed" 'jjAend\rmore\033' 'one two three\nfour five six\nseven eight nineend\nmore'
    try "$ed" '/eig\rx'      'one two three\nfour five six\nseven ight nine'
    try "$ed" '\rx'          'one two three\nour five six\nseven eight nine'
    try "$ed" '3\004x'       'one two three\nfour five six\neven eight nine'
    try "$ed" ':%%s/e/E/g\r' 'onE two thrEE\nfour fivE six\nsEvEn Eight ninE'
    try "$ed" 'Ahi~jk~x'     'one two threeh\nfour five six\nseven eight nine'          # VI.RC's imap jk <Esc>
    try "$ed" ':map Q dd\r~Q' 'four five six\nseven eight nine'
done
try 'edit -v' 'xp'      'noe two three\nfour five six\nseven eight nine'
try 'edit -v' ':2\rx'   'one two three\nour five six\nseven eight nine'
try 'edit -v' 'ihi \033\023~\021'  'hi one two three\nfour five six\nseven eight nine'   # EDIT's Ctrl keys still EDIT's
try 'edit'    '\317~v~dd' 'four five six\nseven eight nine'                        # Options > VI Keys (Alt+O, V)
try 'prog'    '\317~v~dd' 'four five six\nseven eight nine'
try 'prog -v' 'xp'      'noe two three\nfour five six\nseven eight nine'
rm -f $F
echo "vikeystest: OK ($n edits: motions, d c y, J, undo and redo, insert, search, Enter and Ctrl-D, :%s and maps (VI.RC's jk) -- in VI, EDIT -v and PROG -v; the characters register, :N, Ctrl keys, the Options switch)"
