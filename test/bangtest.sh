#!/bin/sh
# `!` at the prompt: the host's shell on the Tube (program 4).  Gated: without
# K4510_HOST_SHELL the ROM must refuse in words, with it a command runs in the
# machine's current directory and the prompt comes back.
set -e
cd "$(dirname "$0")/.."
export K4510_NO_STARTUP=1
out=$(./test/headless rom/kernal.bin '!echo hi
' 300 'no host shell' 2>&1) || { echo "$out"; echo "bangtest: FAILED: ungated ! did not refuse"; exit 1; }
out=$(K4510_HOST_SHELL=1 ./test/headless rom/kernal.bin '!echo BANG$((6*7)) && pwd
~DIR
' 600 'file(s)' 2>&1) || { echo "$out"; echo "bangtest: FAILED: no prompt back after !"; exit 1; }
echo "$out" | grep -q 'BANG42' || { echo "$out"; echo "bangtest: FAILED: the command's output did not reach the screen"; exit 1; }
echo "$out" | grep -q '/fs$' || { echo "$out"; echo "bangtest: FAILED: the shell did not start in the machine's directory"; exit 1; }
echo "bangtest: OK (refused when not fitted; !cmd runs in fs/, output on screen, prompt back)"
