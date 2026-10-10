#!/bin/sh
# The panel's integer display resolutions, from the prompt (2026-10-07,
# docs/design-video-foundations.md): MODE -l lists what a panel offers,
# MODE -s N or MODE WxH picks one, and K/OS lays its console out on it --
# the larger cells, or the smaller where those leave under 25 rows, 16 wide
# at scale 1.  POSIX options (Doc, 2026-10-07).  K4510_PANEL is the panel
# the frontend would have found.
set -e
cd "$(dirname "$0")/.."
export K4510_NO_STARTUP=1
fail() { echo "$out"; echo "paneltest: FAILED: $1"; exit 1; }
run() { K4510_PANEL=$1 ./test/headless rom/kernal.bin "$2" 1500 2>&1; }
has() { echo "$out" | grep -qF -- "$1" || fail "$2"; }

out=$(run 1920x1080 'MODE -l
')
has "-s 1  1440x1080  90x33, -d 90x67" "1920x1080: scale 1, in both grids"
has "-s 3  480x360  60x45" "1920x1080: 480x360, new"
has "-s 4  360x270  45x33" "1920x1080: 360x270"
out=$(run 1920x1080 'MODE -s 3
MODE
')
has "MODE 5: 60x45 text, 480x360 pixels, scale 3" "MODE -s 3: 480x360 in 8x8 cells (8x16 would leave 22 rows)"
out=$(run 1366x768 'MODE -s 1
MODE -l
')
has "MODE 5: 64x48 text, 1024x768 pixels, scale 1" "1366x768 scale 1: 1024x768, 16x16 cells (16x32 would leave 24 rows)"
has "-s 3  341x256  42x32" "1366x768: three, 341x256 the last"
out=$(run 1366x768 'MODE 341x256
MODE
')
has "MODE 5: 42x32 text, 341x256 pixels" "MODE WxH: 341x256, 42 columns of 8 and five pixels over"
out=$(run 3840x2160 'MODE --scale=1
MODE
')
has "1440x1080 pixels, scale 2" "4K: scale 1 (2880x2160) is over the cap, so it is 2"
out=$(run 1920x1080,full 'MODE -s1
MODE
')
has "MODE 5: 120x33 text, 1920x1080 pixels, scale 1" "the whole panel: 1920x1080, 120x33"
out=$(run 1920x1080 'MODE -s 2 --double
MODE
')
has "MODE 5: 90x67 text, 720x540 pixels, scale 2" "--double picks the smaller cells"
out=$(run 1920x1080 'MODE 720x540 -d
MODE
')
has "MODE 5: 90x67 text, 720x540 pixels, scale 2" "an option after the operand"
out=$(run 1920x1080 'MODE --bogus
MODE 999x1
')
has "mode: not an option of MODE -- MODE -h explains them" "an unknown option points at MODE -h"
has "mode: WxH, 160x100 at least" "a size too small to be one"
# Integer Best Fit (2026-10-08): a size the panel does not offer is the program's own,
# at the largest whole multiple -- smoothed only when asked; larger than the panel refused
out=$(run 1920x1080 'MODE 640x480
MODE
')
has "MODE 5: 80x30 text, 640x480 pixels, the best whole multiple" "MODE 640x480: Integer Best Fit, 80x30 in 8x16 cells"
out=$(run 1920x1080 'MODE 800x600 --smooth
MODE
')
has "800x600 pixels, smoothed to the panel" "--smooth asks for smoothing"
out=$(run 1366x768 'MODE 1600x1200
')
has "mode: larger than this panel" "a size larger than the panel is refused"
# scanlines (2026-10-08): on any mode, kept by a new size, off with -p
out=$(run 1920x1080 'MODE -c
MODE
')
has "640x480 pixels, scanlines" "MODE -c: scanlines on the shell's own mode"
out=$(run 1920x1080 'MODE -c
MODE 640x480
MODE
')
has "640x480 pixels, the best whole multiple, scanlines" "a new size keeps the scanlines"
out=$(run 1920x1080 'MODE -c
MODE -p
MODE
')
echo "$out" | grep -q "pixels.*scanlines" && fail "MODE -p takes them off"
echo "paneltest: OK (the list on 1080p, 768p, 4K and the whole panel; MODE -l, -s, --scale=, WxH, -d; the grids and their fallbacks; Integer Best Fit, --smooth, --scanlines and --plain)"
