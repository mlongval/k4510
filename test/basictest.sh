#!/bin/sh
# The two BASICs, tested from inside: fs/LANG/BASIC/EX/TEST.BAS (K4510
# BASIC, compiled by make into test.prg) and fs/LANG/BBCBASIC/EX/TEST.BBC
# are self-checking programs (arithmetic, the MATH unit's functions,
# strings, control flow, arrays, the machine's registers, graphics and
# sound escapes, files, the * escape); each prints a verdict line the
# harness reads off the screen. Each waits 5 seconds for a key at startup
# (step-by-step mode if pressed); the harness presses none, so both run
# straight through -- but budget the frames for that wait (K4510 BASIC
# counts frames; BBC INKEY(500) is ~5s of the co-processor's own clock).
# EhBASIC's half of this test went with EhBASIC (2026-10-09); its TEST.BAS
# lives on as K4510 BASIC's.  The language itself: test/bastest.sh.
cd "$(dirname "$0")/.."
out=$(./test/headless rom/kernal.bin "CD /LANG/BASIC/EX
TEST
" 1500 2>&1) || { echo "$out"; echo "basictest: FAILED: K4510 BASIC's TEST did not run"; exit 1; }
echo "$out" | grep -q "KBTEST PASSED" || { echo "$out"; echo "basictest: FAILED: K4510 BASIC TEST.BAS"; exit 1; }
echo "$out" | grep -q "STAR OK" || { echo "$out"; echo "basictest: FAILED: K4510 BASIC * escape"; exit 1; }
TUBE=./test/tubetest; [ -x $TUBE ] || TUBE=./test/headless   # the in-process Tube when built (make tubetest), else the desktop one (tube/bbcbasic on a pty)
out=$($TUBE rom/kernal.bin "CD /
BBC
~~~LOAD \"LANG/BBCBASIC/EX/TEST.BBC\"
~~RUN
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*QUIT
~~" 12000 2>&1) || { echo "$out"; echo "basictest: FAILED: BBC BASIC did not run"; exit 1; }
# 240 waits = 7200 frames before *QUIT: TEST.BBC's INKEY(500) is five seconds of the
# co-processor's WALL clock, and this harness runs frames as fast as it can -- on a
# fast host 1500 frames passed in under five seconds and *QUIT's keys landed in the
# INKEY, which read them as "step mode" and then waited for a key forever (2026-09-07).
echo "$out" | grep -q "BBCTEST PASSED" || { echo "$out"; echo "basictest: FAILED: BBC BASIC"; exit 1; }
echo "$out" | grep -q "STAR OK" || { echo "$out"; echo "basictest: FAILED: BBC BASIC * escape"; exit 1; }
rm -f fs/TESTOUT.TXT fs/LANG/BBCBASIC/TESTOUT.TXT
# PROG from a program: PROG takes $0800-$CFFF, over the caller, and the
# caller came back to PROG's bytes and ran wild (Doc, the Dell, 2026-10-05:
# *PROG from EhBASIC).  The shell now swaps a program that runs a program
# out of the way first.  RX, at $2000, is the caller now.
printf "'PROG'\nsay 'STILL HERE' 6*7\n" > fs/HOME/PRGTEST.RX
keys=$(python3 -c "import sys; sys.stdout.write('RX /HOME/PRGTEST.RX\n~~~~\x11~~~~')")
out=$(./test/headless rom/kernal.bin "$keys" 1200 2>&1)
rm -f fs/HOME/PRGTEST.RX
echo "$out" | grep -q "^STILL HERE 42" || { echo "$out"; echo "basictest: FAILED: PROG did not come back to RX with its script"; exit 1; }
# GRAPHICS 3 (2026-10-08): a bitmap the size *MODE made -- Integer Best Fit's 400x300
S=fs/HOME/BTGFX3
rm -rf "$S"; mkdir -p "$S"
printf '*MODE 400x300\nGRAPHICS 3\nw%% = GWIDTH: h%% = GHEIGHT\nGRAPHICS 0\nPRINT "G3:"; w%%; h%%\nDO: LOOP UNTIL INKEY$ <> ""\n' > "$S/GFX3.BAS"
K4510_ROOT=$PWD/fs tools/k4510-bas /HOME/BTGFX3/GFX3.BAS >/dev/null 2>&1 || { rm -rf "$S"; echo "basictest: FAILED: GFX3.BAS did not compile"; exit 1; }
out=$(K4510_PANEL=1920x1080 ./test/headless rom/kernal.bin "CD /HOME/BTGFX3
GFX3
" 900 2>&1)
rm -rf "$S"
echo "$out" | grep -q "G3: 400  300" || { echo "$out"; echo "basictest: FAILED: GRAPHICS 3 on *MODE 400x300"; exit 1; }
echo "basictest: OK (K4510 BASIC's TEST.BAS + *, BBC BASIC 28 checks + files + ULA graphics/sound + *, PROG from RX and back; GRAPHICS 3 on *MODE 400x300)"
