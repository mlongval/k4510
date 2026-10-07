#!/bin/sh
# The panel's integer display resolutions, from the prompt (2026-10-07,
# docs/design-video-foundations.md): MODE alone lists what a panel offers,
# MODE /n picks one, and K/OS lays its console out on it -- the larger cells,
# or the smaller where those leave under 25 rows, 16 wide at scale 1.
# K4510_PANEL is the panel the frontend would have found.
set -e
cd "$(dirname "$0")/.."
export K4510_NO_STARTUP=1
fail() { echo "$out"; echo "paneltest: FAILED: $1"; exit 1; }
run() { K4510_PANEL=$1 ./test/headless rom/kernal.bin "$2" 1500 2>&1; }
has() { echo "$out" | grep -qF "$1" || fail "$2"; }

out=$(run 1920x1080 'MODE
')
has "offers  /1 1440x1080  /2 720x540  /3 480x360  /4 360x270" "1920x1080: the four the HD family had, and 480x360"
out=$(run 1920x1080 'MODE /3
MODE
')
has "MODE 5: 60x45 text, 480x360 pixels, /3" "MODE /3: 480x360 in 8x8 cells (8x16 would leave 22 rows)"
out=$(run 1366x768 'MODE /1
MODE
')
has "MODE 5: 64x48 text, 1024x768 pixels, /1" "1366x768 /1: 1024x768, 16x16 cells (16x32 would leave 24 rows)"
has "offers  /1 1024x768  /2 512x384  /3" "1366x768: three (the line wraps at 64 columns)"
out=$(run 1366x768 'MODE /3
MODE
')
has "MODE 5: 42x32 text, 341x256 pixels, /3" "341x256: 42 columns of 8 and five pixels over"
out=$(run 3840x2160 'MODE /1
MODE
')
has "1440x1080 pixels, /2" "4K: /1 (2880x2160) is over the cap, so it is /2"
out=$(run 1920x1080,full 'MODE /1
MODE
')
has "MODE 5: 120x33 text, 1920x1080 pixels, /1" "the whole panel: 1920x1080, 120x33"
out=$(run 1920x1080 'MODE /2 67
MODE
')
has "MODE 5: 90x67 text, 720x540 pixels, /2" "a row count over 40 picks the smaller cells"
echo "paneltest: OK (the list on 1080p, 768p, 4K and the whole panel; MODE /n, the grids and their fallbacks)"
