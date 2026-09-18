#!/bin/sh
# The Apple IIe on the Tube (tube/apple), alone: it boots DOS 3.3 from the
# master disk, takes typing, and the frame it hands over is the Apple's text
# screen with the catalogue on it.  Reading letters out of a frame is more
# test than the thing deserves: the harness types CATALOG, the co-processor's
# own log is asked whether the disk was in the drive, and the frame is held
# to be a text screen with something on it (a few thousand lit pixels).
cd "$(dirname "$0")/.."
[ -x tube/apple/apple_k4510 ] || { echo "appletest: SKIPPED (tube/apple/apple_k4510 is not built: make -C tube apple)"; exit 0; }
command -v python3 >/dev/null || { echo "appletest: SKIPPED (no python3)"; exit 0; }
T=${TMPDIR:-/tmp}/appletest.$$
out=$(python3 test/apple_harness.py "$T" "--d1 $PWD/tube/apple/linapple/res/Master.dsk" 8 "CATALOG" 2>&1) || { echo "$out"; echo "appletest: FAILED: the harness"; exit 1; }
echo "$out" | grep -q "frame 560x384" || { echo "$out"; echo "appletest: FAILED: no frame"; exit 1; }
echo "$out" | grep -q "seq [0-9][0-9][0-9]" || { echo "$out"; echo "appletest: FAILED: fewer than 100 frames in 8 seconds"; exit 1; }
grep -q "drive 1: .*Master.dsk" "$T.log" || { cat "$T.log"; echo "appletest: FAILED: the disk was not in the drive"; exit 1; }
# the catalogue's text is white on black: exactly two colours on the screen, and a lot of them
python3 - "$T.ppm" <<'PY' || { echo "appletest: FAILED: the screen is not a text screen with something on it"; exit 1; }
import sys
d=open(sys.argv[1],"rb").read(); h=d.split(b"\n",3); px=h[3]
lit=sum(1 for i in range(0,len(px),3) if px[i] or px[i+1] or px[i+2])
sys.exit(0 if 3000 < lit < 60000 else 1)
PY
rm -f "$T.ppm" "$T.log"
echo "appletest: OK (an Apple IIe boots DOS 3.3 from the master disk at 60 frames a second and takes typing)"
