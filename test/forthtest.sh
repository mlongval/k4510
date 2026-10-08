#!/bin/sh
# FORTH, Tali Forth 2 native: it starts, it counts, the break stops a loop
# (2026-10-08: ESC or Ctrl-C, at every character it prints and every KEY?),
# and BYE comes back to the shell.
set -e
cd "$(dirname "$0")/.."
export K4510_NO_STARTUP=1
fail() { echo "$out"; echo "forthtest: FAILED: $1"; exit 1; }
out=$(./test/headless rom/kernal.bin "$(printf 'FORTH\n: T 0 BEGIN DUP . 1+ AGAIN ;\nT\n~~~~\033~~~~7 6 * .\nBYE\nECHO BACK\n~~')" 900 2>&1)
echo "$out" | grep -q 'stopped' || fail "ESC did not stop a printing loop"
echo "$out" | grep -q '42  ok' || fail "Forth was not at its prompt after the break"
echo "$out" | grep -q '^BACK' || fail "BYE did not come back to the shell"
echo "forthtest: OK (ESC stops a loop and ABORTs to the prompt, BYE)"
