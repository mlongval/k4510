#!/bin/sh
# PALETTE: the console's colours are VICKY's, and they stay where they are put.
#
# The thing most worth guarding is the last one.  video_init used to reload the
# VIC-II sixteen every time it ran -- at every mode change, every VIDEO call,
# every BBC BASIC text mode -- so a loaded palette looked like it worked and
# then quietly reverted.  It was removed because the host already seeds those
# exact values at reset (core/vicky.c), which made the reload a no-op at boot
# and a wrecking ball afterwards.
cd "$(dirname "$0")/.."

fail() { echo "$out"; echo "palettetest: FAILED: $1"; exit 1; }

# the boot palette is the VIC-II sixteen, and PALETTE reads it back
out=$(./test/headless rom/kernal.bin 'PALETTE
' 900 2>&1) || fail "PALETTE did not run"
echo "$out" | grep -q "0 000000" || fail "entry 0 is not black"
echo "$out" | grep -q "7 EEEE77" || fail "entry 7 is not the VIC-II yellow"
echo "$out" | grep -q "E 0088FF" || fail "entry E is not the VIC-II light blue"

# a .PAL applies, including its COLOR line
out=$(./test/headless rom/kernal.bin 'PALETTE LOAD AMBER
PALETTE
' 1500 2>&1) || fail "PALETTE LOAD did not run"
echo "$out" | grep -q "entries from /SYSTEM/ETC/PALETTES/AMBER.PAL" || fail "AMBER.PAL was not found"
echo "$out" | grep -q "F FFB000" || fail "the amber ramp did not reach entry F"
# entry 1 is the shell's highlight (C_HI: DIR's header, directories): on a
# ramp it must be bright, or they print black on black (Doc, 2026-09-12)
echo "$out" | grep -q "1 FFB000" || fail "AMBER entry 1 (the highlight) is not bright"

# ...and survives a mode change, which is the whole point
out=$(./test/headless rom/kernal.bin 'PALETTE LOAD GREY
MODE 2
PALETTE
' 1800 2>&1) || fail "MODE did not run"
echo "$out" | grep -q "F FFFFFF" || fail "the palette did not survive MODE (video_init reloading it?)"
echo "$out" | grep -q "8 888888" || fail "the grey ramp is not intact after MODE"

# RESET puts the VIC-II sixteen back
out=$(./test/headless rom/kernal.bin 'PALETTE LOAD AMBER
PALETTE RESET
PALETTE
' 1800 2>&1) || fail "PALETTE RESET did not run"
echo "$out" | grep -q "7 EEEE77" || fail "RESET did not restore the VIC-II palette"

# COLOR refuses a pair the palette makes unreadable, says what reads, and ! has
# it anyway; a .PAL with no COLOR line that leaves the text unreadable gets a
# pair that reads (Doc, 2026-09-15, amber)
out=$(./test/headless rom/kernal.bin 'PALETTE LOAD AMBER
COLOR 7 6
' 1500 2>&1) || fail "COLOR did not run"
echo "$out" | grep -q "would be hard to read here; COLOR 01 06 reads" || fail "COLOR 7 6 on amber was not refused with a suggestion"
out=$(./test/headless rom/kernal.bin 'PALETTE LOAD AMBER
COLOR 7 6 !
ECHO forced
' 1500 2>&1) || fail "COLOR ! did not run"
echo "$out" | grep -q "hard to read" && fail "COLOR 7 6 ! was refused"
echo "$out" | grep -q "forced" || fail "the shell did not go on after COLOR !"
printf '6 70 70 70\n7 78 78 78\n' > fs/HOME/DIMTEST.PAL
out=$(./test/headless rom/kernal.bin 'PALETTE LOAD /HOME/DIMTEST.PAL
' 900 2>&1); rm -f fs/HOME/DIMTEST.PAL
echo "$out" | grep -q "to stay readable" || fail "a palette that made 7 on 6 unreadable did not get a readable pair"
out=$(./test/headless rom/kernal.bin 'COLOR E 0
' 900 2>&1)
echo "$out" | grep -q "hard to read" && fail "COLOR E 0 on the VIC-II palette was refused"

# the alias engine shares bank 2 with all of this.  Its table starts at $B400
# and the linker now refuses code past that, but a live alias is the proof.
out=$(./test/headless rom/kernal.bin 'ALIAS PT ECHO alias-intact
PT
' 900 2>&1) || fail "ALIAS did not run"
echo "$out" | grep -q "alias-intact" || fail "the alias engine broke (bank 2 collision?)"

echo "palettetest: OK (VIC-II at boot, .PAL loads with its COLOR line, survives MODE, RESET restores, COLOR refuses the unreadable, a dim .PAL gets a readable pair, aliases intact)"
