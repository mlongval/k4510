#!/bin/sh
# MONITOR, since the monitor and its memory tools left the ROM for
# /SYSTEM/BIN/monitor.prg at $E000 (2026-09-13).  The shell words MON, WOZ,
# FILL and COPY run it; romtest drives mon/fill from C.  Here: store, examine
# and a block in the ROM's exact format; FILL and COPY and what they say; the
# * prompt, a shell command from it (through SWAP -k, from page 3), and X.
cd "$(dirname "$0")/.."
fail() { echo "$out"; echo "montest: FAILED: $1"; exit 1; }

out=$(./test/headless rom/kernal.bin 'MON 1000:41 42 43
MON 1000.1002
FILL 2000.200F 5A
MON 2000.2003
COPY 1000.1002 3000
WOZ 3000.3002
FILL 2000
MON
~~2000.2001
ECHO VIA-PROMPT
X
ECHO BACK
' 4000 2>&1) || fail "the monitor did not run"
echo "$out" | grep -q "^00001000: 41 42 43"          || fail "store then examine (MON 1000:41 42 43, 1000.1002)"
echo "$out" | grep -q "^16 bytes filled"             || fail "FILL did not say what it filled"
echo "$out" | grep -q "^00002000: 5A 5A 5A 5A"       || fail "FILL did not fill"
echo "$out" | grep -q "^3 bytes copied to 00003000"  || fail "COPY did not say what it copied"
echo "$out" | grep -q "^00003000: 41 42 43"          || fail "COPY did not copy (or WOZ is not MON)"
echo "$out" | grep -q "^fill: from.to value"         || fail "a bad FILL did not say how to use it"
echo "$out" | grep -q "^monitor: addr"               || fail "MON alone did not open the * prompt"
echo "$out" | grep -q "^00002000: 5A 5A"             || fail "the * prompt did not examine"
echo "$out" | grep -q "^VIA-PROMPT"                  || fail "a shell command at the * prompt did not run"
echo "$out" | grep -q "^BACK"                        || fail "X did not hand the shell back"

echo "montest: OK (store/examine/block, FILL and COPY, a bad FILL, the * prompt, a shell line from it, X)"
