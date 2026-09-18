#!/bin/sh
# RADIO through the shell.  test/headless has no frontend and so no radio: the
# storage device must say so in words, and the shell must come back.  The
# player itself is the frontend's (sdl/sidebars/navidrome.c) and was driven
# against a stand-in Subsonic server by hand, 2026-09-17: SEARCH lists, ALBUM
# queues, NEXT, PAUSE and RESUME heard in the audio output.
cd "$(dirname "$0")/.."
out=$(./test/headless rom/kernal.bin 'RADIO
~RADIO SEARCH nothing
~ECHO RADIOBACK
' 900 2>&1) || { echo "$out"; echo "radiotest: FAILED: did not run"; exit 1; }
echo "$out" | grep -q "no radio on this host" || { echo "$out"; echo "radiotest: FAILED: the device did not say there is no radio"; exit 1; }
echo "$out" | grep -q "RADIOBACK" || { echo "$out"; echo "radiotest: FAILED: the shell did not come back"; exit 1; }
echo "radiotest: OK (RADIO reaches the storage device and says there is no radio here)"
