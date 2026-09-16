#!/bin/sh
# test/modetest.sh -- every video MODE through the headless runner: it must not
# crash, and MODE must report the size the ROM's tables give (rom/kernal.c,
# pcols_of / prows_of).
#
# 2026-09-15, from Doc's brainshot about the rows: the runner drew an HD mode
# 1440 pixels wide into a 640-wide buffer and died, and it read every screen at
# 80 columns with an 80-cell stride, so MODE 5's 180 columns dumped nothing.
# Both are the runner's, not the machine's -- but nothing tested the HD modes
# at all, which is why neither showed.
set -e
cd "$(dirname "$0")/.."
fails=0
for pair in "0 80x30 640x480" "1 80x30 640x240" "2 40x30 320x240" "5 180x67 1440x1080" "6 90x33 720x540" "7 45x33 360x270"; do
    set -- $pair
    out=$(K4510_SYSOPT=0x04 timeout 90 ./test/headless rom/kernal.bin "MODE $1
~MODE
~" 900 2>/dev/null | grep -i "^MODE $1:" | tail -1) || true
    case "$out" in
        *"$2 text, $3 pixels"*) echo "  ok   MODE $1: $2 text, $3 pixels" ;;
        *) echo "  FAIL MODE $1: got '$out', want '$2 text, $3 pixels'"; fails=$((fails + 1)) ;;
    esac
done
if [ $fails -eq 0 ]; then echo "modetest: OK (six modes, each reporting its own size)"; else echo "modetest: $fails FAILED"; exit 1; fi
