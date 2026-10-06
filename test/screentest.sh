#!/bin/sh
# JIM's second screen and its bands (2026-10-05): TERMINAL shows a session on
# the Linux beneath, the keys go to it, ESC ] 4510 ; kos BEL comes back to
# K/OS, the bands name both screens and carry a note sent through JIM.
cd "$(dirname "$0")/.."
fail() { echo "screentest: FAILED: $1"; exit 1; }
export K4510_TERMINAL=/bin/sh
keys=$(python3 -c "import sys; sys.stdout.write('TERMINAL\n~~echo typed-\$((6*7))\n~~')")
out=$(./test/headless rom/kernal.bin "$keys" 400 2>&1)
echo "$out" | grep -q "typed-42" || { echo "$out"; fail "the session did not take the keys or show its answer"; }
keys=$(python3 -c "import sys; sys.stdout.write('TERMINAL\n~~printf \'\\\\033]4510;kos\\\\007\'\n~~ECHO BACK-HOME\n~~')")
out=$(./test/headless rom/kernal.bin "$keys" 600 2>&1)
echo "$out" | grep -q "^BACK-HOME" || { echo "$out"; fail "ESC ] 4510 ; kos did not bring K/OS back with its keys"; }
echo "$out" | grep -q "typed-42" && fail "the session's text showed on K/OS's screen"
top=$(K4510_SYSOPT=0x0C ./test/headless rom/kernal.bin "$(printf 'TERMINAL\n~~')" 300 2>/dev/null | head -1)
case "$top" in *"1 K/OS"*"2 TERMINAL"*"$(date +%Y)"*) ;; *) fail "the top band does not name both screens and the date: $top" ;; esac
keys=$(python3 -c "import sys; sys.stdout.write('TERMINAL\n~~printf \'\\\\033]4510;note;built OK\\\\007\'\n~~')")
bot=$(K4510_SYSOPT=0x0C ./test/headless rom/kernal.bin "$keys" 300 2>/dev/null | tail -1)
case "$bot" in *"built OK"*) ;; *) fail "the note did not reach the bottom band: $bot" ;; esac
out=$(K4510_TERMINAL='exit 0' ./test/headless rom/kernal.bin "$(printf 'TERMINAL\n~~')" 200 2>&1)
echo "$out" | grep -q "the session has ended" || { echo "$out"; fail "an ended session did not say so"; }
echo "screentest: OK (TERMINAL and its keys, back to K/OS by JIM's OSC, the bands' tabs and date, a note in the bottom band, an ended session)"
