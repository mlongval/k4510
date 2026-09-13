#!/bin/sh
# TYPE, since it left the ROM for /SYSTEM/BIN/type.prg (2026-09-13).  What the
# ROM's TYPE did, the program must: print a file; say so when there is none;
# page a screenful at a time with "-- more --", Esc/Q stopping it; NOT page
# under a script (the ROM's exec_busy at $022E), or STARTUP.BAT would hang on
# a key nobody presses; and take its prompt back off the screen.  HELP is TYPE
# on /SYSTEM/ETC/HELP, its line copied into the shell's buffer first.
cd "$(dirname "$0")/.."
fail() { echo "$out"; rm -f fs/HOME/TTEST.BAT; echo "typetest: FAILED: $1"; exit 1; }

out=$(./test/headless rom/kernal.bin 'TYPE /SYSTEM/ETC/PALETTES/C64.PAL
TYPE NOSUCHFILE.TXT
' 900 2>&1) || fail "TYPE did not run"
echo "$out" | grep -q "^# C64 -- the VIC-II sixteen" || fail "TYPE did not print the file"
echo "$out" | grep -q "^type: not found"             || fail "TYPE of a missing file said nothing"

printf 'TYPE /SYSTEM/ETC/HELP\nECHO AFTER-EXEC\n' > fs/HOME/TTEST.BAT
out=$(./test/headless rom/kernal.bin 'EXEC TTEST.BAT
' 2400 2>&1) || fail "EXEC did not run"
rm -f fs/HOME/TTEST.BAT
echo "$out" | grep -q -- "-- more --" && fail "TYPE paged under a script (exec_busy at \$022E not seen)"
echo "$out" | grep -q "^AFTER-EXEC"   || fail "the script did not get past TYPE"

out=$(./test/headless rom/kernal.bin 'HELP
~~~~~~q~~ECHO BACK
' 1500 2>&1) || fail "HELP did not run"
echo "$out" | grep -q "^BACK"          || fail "Q at -- more -- did not hand the shell back"
echo "$out" | grep -q -- "-- more --"  && fail "the -- more -- prompt was left on the screen"

echo "typetest: OK (a file, a missing file, no paging under EXEC, HELP pages and Q stops, the prompt taken back)"
