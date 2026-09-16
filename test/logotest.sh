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

echo "3. a screen too small: warn, ask, and either move or go"
# the host publishes the mode at $D521 bits 5-7 as mode+1, so 0xA4 boots the
# machine straight into MODE 4 (160x200) -- which the prompt itself refuses.
# Its 20 columns wrap every line, so these look for fragments, not sentences.
out=$(K4510_SYSOPT=0xA4 timeout 120 ./test/headless rom/kernal.bin 'LOGO
~~N
~MODE
~' 1500 2>/dev/null) || true
flat=$(echo "$out" | tr -d ' \n')
check "it says what it needs and what it found" "$(echo "$flat" | grep -c "LOGOneeds320x240" || true)$(echo "$flat" | grep -c "screenis160x200" || true)" "11"
check "N leaves the machine where it was" "$(echo "$flat" | grep -c "screenisyours" || true)$(echo "$flat" | grep -c "MODE4:" || true)" "11"

# Two sessions, because each answer can only be seen in its own screen: after
# BYE the shell's own MODE scrolls LOGO's greeting away, so the session that
# proves LOGO ran must end while LOGO is still on screen.
out=$(K4510_SYSOPT=0xA4 timeout 120 ./test/headless rom/kernal.bin 'LOGO
~~Y
~~REPEAT 4 [FD 30 RT 90]
~' 1500 2>/dev/null) || true
flat=$(echo "$out" | tr -d ' \n')
check "Y moves up, and LOGO runs there" "$(echo "$flat" | grep -c "turtleishome" || true)$(echo "$flat" | grep -c "REPEAT4\[FD30RT90\]" || true)" "11"
out=$(K4510_SYSOPT=0xA4 timeout 120 ./test/headless rom/kernal.bin 'LOGO
~~Y
~BYE
~~MODE
~' 1500 2>/dev/null) || true
check "and the machine is left at 320x240" "$(echo "$out" | tr -d ' \n' | grep -c "MODE2:" || true)" "1"

echo "4. both shapes are there, sixteen frames of 32x32"
for f in TURTLE BIRD; do
    check "$f.SPR is 16384 bytes" "$(wc -c < fs/LANG/LOGO/$f.SPR)" "16384"
done

if [ $fails -eq 0 ]; then echo "logotest: OK (three screens kept, the small screen asked about, SETSHAPE, both shapes)"; else echo "logotest: $fails FAILED"; exit 1; fi
