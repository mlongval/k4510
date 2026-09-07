#!/bin/sh
# The two BASICs, tested from inside: fs/EHBASIC/TEST.BAS and
# fs/BBCBASIC/TEST.BBC are self-checking programs (arithmetic, the MATH
# unit's functions, strings, control flow, arrays, the machine's
# registers, graphics and sound escapes, files, the * escape); each
# prints a verdict line the harness reads off the screen. Each waits 5
# seconds for a key at startup (step-by-step mode if pressed); the
# harness presses none, so both run straight through -- but budget the
# frames for that wait (EhBASIC counts frames; BBC INKEY(500) is ~5s of
# the co-processor's own clock).
cd "$(dirname "$0")/.."
rm -f fs/TESTOUT.BAS fs/TESTOUT.TXT
out=$(./test/headless rom/kernal.bin "RUN EHBASIC
~~~RUN \"TEST.BAS\"
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" 3000 2>&1) || { echo "$out"; echo "basictest: FAILED: EhBASIC did not run"; exit 1; }
echo "$out" | grep -q "EHTEST PASSED" || { echo "$out"; echo "basictest: FAILED: EhBASIC"; exit 1; }
echo "$out" | grep -q "STAR OK" || { echo "$out"; echo "basictest: FAILED: EhBASIC * escape"; exit 1; }
[ -s fs/TESTOUT.BAS ] || { echo "basictest: FAILED: EhBASIC SAVE wrote nothing"; exit 1; }
grep -q "EHBASIC SELF-TEST" fs/TESTOUT.BAS || { echo "basictest: FAILED: EhBASIC SAVE content"; exit 1; }
rm -f fs/TESTOUT.BAS
TUBE=./test/tubetest; [ -x $TUBE ] || TUBE=./test/headless   # the in-process Tube when built (make tubetest), else the desktop one (tube/bbcbasic on a pty)
out=$($TUBE rom/kernal.bin "BBC
~~~LOAD \"BBCBASIC/TEST.BBC\"
~~RUN
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*QUIT
~~" 12000 2>&1) || { echo "$out"; echo "basictest: FAILED: BBC BASIC did not run"; exit 1; }
# 240 waits = 7200 frames before *QUIT: TEST.BBC's INKEY(500) is five seconds of the
# co-processor's WALL clock, and this harness runs frames as fast as it can -- on a
# fast host 1500 frames passed in under five seconds and *QUIT's keys landed in the
# INKEY, which read them as "step mode" and then waited for a key forever (2026-09-07).
echo "$out" | grep -q "BBCTEST PASSED" || { echo "$out"; echo "basictest: FAILED: BBC BASIC"; exit 1; }
echo "$out" | grep -q "STAR OK" || { echo "$out"; echo "basictest: FAILED: BBC BASIC * escape"; exit 1; }
rm -f fs/TESTOUT.TXT
echo "basictest: OK (EhBASIC 34 checks + SAVE + *, BBC BASIC 28 checks + files + ULA graphics/sound + *; both verbose, both PASSED)"
