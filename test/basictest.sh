#!/bin/sh
# The two BASICs, tested from inside: fs/LANG/EHBASIC/EX/TEST.BAS and
# fs/LANG/BBCBASIC/EX/TEST.BBC are self-checking programs (arithmetic, the MATH
# unit's functions, strings, control flow, arrays, the machine's
# registers, graphics and sound escapes, files, the * escape); each
# prints a verdict line the harness reads off the screen. Each waits 5
# seconds for a key at startup (step-by-step mode if pressed); the
# harness presses none, so both run straight through -- but budget the
# frames for that wait (EhBASIC counts frames; BBC INKEY(500) is ~5s of
# the co-processor's own clock).
cd "$(dirname "$0")/.."
rm -f fs/HOME/TESTOUT.BAS fs/HOME/TESTOUT.TXT
out=$(./test/headless rom/kernal.bin "RUN EHBASIC
~~~RUN \"TEST.BAS\"
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" 3000 2>&1) || { echo "$out"; echo "basictest: FAILED: EhBASIC did not run"; exit 1; }
echo "$out" | grep -q "EHTEST PASSED" || { echo "$out"; echo "basictest: FAILED: EhBASIC"; exit 1; }
echo "$out" | grep -q "STAR OK" || { echo "$out"; echo "basictest: FAILED: EhBASIC * escape"; exit 1; }
[ -s fs/HOME/TESTOUT.BAS ] || { echo "basictest: FAILED: EhBASIC SAVE wrote nothing"; exit 1; }
grep -q "EHBASIC SELF-TEST" fs/HOME/TESTOUT.BAS || { echo "basictest: FAILED: EhBASIC SAVE content"; exit 1; }
rm -f fs/HOME/TESTOUT.BAS
TUBE=./test/tubetest; [ -x $TUBE ] || TUBE=./test/headless   # the in-process Tube when built (make tubetest), else the desktop one (tube/bbcbasic on a pty)
out=$($TUBE rom/kernal.bin "CD /
BBC
~~~LOAD \"LANG/BBCBASIC/EX/TEST.BBC\"
~~RUN
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*QUIT
~~" 12000 2>&1) || { echo "$out"; echo "basictest: FAILED: BBC BASIC did not run"; exit 1; }
# 240 waits = 7200 frames before *QUIT: TEST.BBC's INKEY(500) is five seconds of the
# co-processor's WALL clock, and this harness runs frames as fast as it can -- on a
# fast host 1500 frames passed in under five seconds and *QUIT's keys landed in the
# INKEY, which read them as "step mode" and then waited for a key forever (2026-09-07).
echo "$out" | grep -q "BBCTEST PASSED" || { echo "$out"; echo "basictest: FAILED: BBC BASIC"; exit 1; }
echo "$out" | grep -q "STAR OK" || { echo "$out"; echo "basictest: FAILED: BBC BASIC * escape"; exit 1; }
rm -f fs/TESTOUT.TXT fs/LANG/BBCBASIC/TESTOUT.TXT
# *PROG from EhBASIC: PROG takes $0800-$CFFF, over the interpreter, and the
# BASIC came back to PROG's bytes and ran wild (Doc, the Dell, 2026-10-05).
# The shell now swaps a program that runs a program out of the way first.
keys=$(python3 -c "import sys; sys.stdout.write('EHBASIC\n~~10 PRINT \"STILL HERE\"\n~*PROG\n~~~~\x11~~~~PRINT 6*7\n~~RUN\n~~')")
out=$(./test/headless rom/kernal.bin "$keys" 1200 2>&1)
echo "$out" | grep -q "^ 42" && echo "$out" | grep -q "^STILL HERE" || { echo "$out"; echo "basictest: FAILED: *PROG did not come back to EhBASIC with its program"; exit 1; }
# GRAPHICS 3 (2026-10-08): a bitmap the size *MODE made -- Integer Best Fit's 400x300
keys=$(python3 -c "import sys; sys.stdout.write('EHBASIC\n~~*MODE 400x300\n~~GRAPHICS 3\n~PRINT PEEK(949)+256*PEEK(950);PEEK(951)+256*PEEK(952)\n~~GRAPHICS 0\n~*MODE\n~~')")
out=$(K4510_PANEL=1920x1080 ./test/headless rom/kernal.bin "$keys" 900 2>&1)
echo "$out" | grep -q "^ 400 300" && echo "$out" | grep -q "MODE 5: .*400x300 pixels, the best whole" || { echo "$out"; echo "basictest: FAILED: GRAPHICS 3 on *MODE 400x300"; exit 1; }
echo "basictest: OK (EhBASIC 34 checks + SAVE + *, BBC BASIC 28 checks + files + ULA graphics/sound + *, *PROG and back; GRAPHICS 3 on *MODE 400x300; both verbose, both PASSED)"
