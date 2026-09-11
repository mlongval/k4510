#!/bin/sh
# The in-process Tube round trip (the interpreter on a thread of the emulator):
# BBCBASIC starts the co-processor, arithmetic comes back through the ULA
# and the console ROM, MODE 2 exercises the graphics escape, *QUIT ends it
# and the ROM sees the alive bit drop; then the whole thing again, because
# the co-processor must be restartable without a process behind it.
set -e
cd "$(dirname "$0")/.."
run() { ./test/tubetest rom/kernal.bin "$1" "$2" "$3" > test/tubetest.out 2>&1 || { cat test/tubetest.out; echo "tubetest: FAILED: $4"; exit 1; }; }
run 'BBCBASIC
PRINT 6*7
' 600 '        42' 'PRINT over the Tube'
run 'BBCBASIC
PRINT "A";
~*QUIT
~BBCBASIC
~PRINT 2^10
~*QUIT
' 1200 'has left' 'restart after *QUIT'
grep -q '1024' test/tubetest.out || { cat test/tubetest.out; echo "tubetest: FAILED: no output after the restart"; exit 1; }
run 'BBCBASIC
MODE 2
GCOL 0,1
PLOT 85,100,100
MODE 7
PRINT "MODEOK"
*QUIT
' 900 'has left' 'MODE 2 + PLOT through the ULA'
grep -q 'MODEOK' test/tubetest.out || { cat test/tubetest.out; echo "tubetest: FAILED: no output after MODE 7"; exit 1; }
run 'CPM
~DIR
~EXIT
' 900 'has left' 'CP/M on the in-process Tube'
grep -q 'A0>' test/tubetest.out || { cat test/tubetest.out; echo "tubetest: FAILED: no CP/M prompt"; exit 1; }
echo "tubetest: OK (PRINT, restart after *QUIT, MODE 2/GCOL/PLOT/MODE 7, CP/M DIR/EXIT)"
