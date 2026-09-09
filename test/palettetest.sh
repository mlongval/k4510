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

# the alias engine shares bank 2 with all of this.  Its table starts at $B400
# and the linker now refuses code past that, but a live alias is the proof.
out=$(./test/headless rom/kernal.bin 'ALIAS PT ECHO alias-intact
PT
' 900 2>&1) || fail "ALIAS did not run"
echo "$out" | grep -q "alias-intact" || fail "the alias engine broke (bank 2 collision?)"

echo "palettetest: OK (VIC-II at boot, .PAL loads with its COLOR line, survives MODE, RESET restores, aliases intact)"
