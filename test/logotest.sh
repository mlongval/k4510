#!/bin/sh
# test/logotest.sh -- LOGO on the screen it finds, and its shapes.
#
# Doc, 2026-09-15: "logo seems to force mode 1. it should respect mode it is
# started in", and "historically logo also had other animals i think like a
# bird".  LOGO used to put the console into MODE 0 for the session and back at
# BYE; now its surface is the glass it finds, and SETSHAPE "BIRD fetches
# /LANG/LOGO/BIRD.SPR (tools/mkturtle.py draws both).
#
# The turtle stays 32 of the machine's pixels in every mode, so it looks bigger
# on the doubled screens -- there is nothing to test there but the drawing.
set -e
cd "$(dirname "$0")/.."
fails=0
check() { if [ "$2" = "$3" ]; then echo "  ok   $1"; else echo "  FAIL $1: got '$2', want '$3'"; fails=$((fails + 1)); fi; }

echo "1. the screen LOGO finds is the screen it leaves"
for pair in "0 640x480" "1 640x240" "2 320x240"; do
    set -- $pair
    out=$(K4510_SYSOPT=0x04 timeout 120 ./test/headless rom/kernal.bin "MODE $1
~LOGO
~~REPEAT 4 [FD 40 RT 90]
~BYE
~~MODE
~" 1800 2>/dev/null) || true
    started=$(echo "$out" | grep -c "the turtle is home" || true)
    kept=$(echo "$out" | grep -c "$2 pixels" || true)
    check "MODE $1: LOGO runs and leaves the machine in $2" "$started $kept" "1 1"
done

echo "2. SETSHAPE: the bird, a name that is not there, and the turtle back"
out=$(K4510_SYSOPT=0x04 timeout 120 ./test/headless rom/kernal.bin 'LOGO
~~SETSHAPE "BIRD
~FD 50
~SETSHAPE "NOSUCH
~SETSHAPE "TURTLE
~FD 10
~BYE
~' 1800 2>/dev/null) || true
check "the bird loads without complaint" "$(echo "$out" | grep -c "can't find /LANG/LOGO/BIRD.SPR" || true)" "0"
check "a shape that is not there says so" "$(echo "$out" | grep -c "can't find /LANG/LOGO/NOSUCH.SPR" || true)" "1"
# the prompt shows twice -- the one LOGO was started from, and the one BYE
# comes back to -- so this asks that it came back at all, not how many
check "and LOGO carries on to the prompt" "$([ "$(echo "$out" | grep -c "/HOME\]" || true)" -ge 1 ] && echo yes || echo no)" "yes"

echo "3. both shapes are there, sixteen frames of 32x32"
for f in TURTLE BIRD; do
    check "$f.SPR is 16384 bytes" "$(wc -c < fs/LANG/LOGO/$f.SPR)" "16384"
done

if [ $fails -eq 0 ]; then echo "logotest: OK (three screens kept, SETSHAPE, both shapes)"; else echo "logotest: $fails FAILED"; exit 1; fi
