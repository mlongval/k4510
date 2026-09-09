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
echo "$out" | grep -q '/fs/HOME$' || { echo "$out"; echo "bangtest: FAILED: the shell did not start in the machine's directory"; exit 1; }
# PAS / CC: the compilers beside the machine (tools/k4510-pas, tools/k4510-cc)
# compile a source in the machine's directory into a .prg that then RUNs.
# Only where the toolchain is: mp+mads at the Makefile's defaults, cc65 on PATH.
MP_DIR=${MP_DIR:-$HOME/Projects/neo6502_dev/Mad-Pascal}
[ -x "$HOME/opt/cc65/bin/cc65" ] && PATH="$HOME/opt/cc65/bin:$PATH"
export PATH="$PWD/tools:$PATH"
if [ -x "$MP_DIR/bin/mp" ] && command -v cc65 >/dev/null 2>&1; then
    rm -rf fs/SELFHOST; mkdir fs/SELFHOST
    cp fs/LANG/PASCAL/HELLO.PAS fs/SELFHOST/HELLO.PAS; cp fs/LANG/C/SIEVE.C fs/SELFHOST/SIEVE.C
    printf 'program bad;\nbegin\n  x := ;\nend.\n' > fs/SELFHOST/BAD.PAS
    out=$(K4510_HOST_SHELL=1 ./test/headless rom/kernal.bin 'CD /SELFHOST
~PAS HELLO
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~HELLO from the machine
~~~~~~~~CC SIEVE
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~DIR
~~~~PAS BAD
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ECHO DONE
' 8000 'DONE' 2>&1) || { echo "$out"; rm -rf fs/SELFHOST; echo "bangtest: FAILED: PAS/CC leg did not finish"; exit 1; }
    rm -rf fs/SELFHOST
    echo "$out" | grep -q -i 'HELLO.prg: [0-9]* bytes'   || { echo "$out"; echo "bangtest: FAILED: PAS HELLO did not compile"; exit 1; }
    echo "$out" | grep -q 'You said: from the machine' || { echo "$out"; echo "bangtest: FAILED: the compiled HELLO did not run"; exit 1; }
    echo "$out" | grep -q -i 'SIEVE.prg: [0-9]* bytes'   || { echo "$out"; echo "bangtest: FAILED: CC SIEVE did not compile"; exit 1; }
    echo "$out" | grep -q -i 'sieve.prg'                || { echo "$out"; echo "bangtest: FAILED: SIEVE.prg not in DIR"; exit 1; }
    echo "$out" | grep -q "Identifier not found 'X'"   || { echo "$out"; echo "bangtest: FAILED: PAS BAD did not report the error"; exit 1; }
    extra=", PAS and CC compile here and the program runs"
else
    extra=", PAS/CC leg skipped (no mp or cc65 here)"
fi
echo "bangtest: OK (refused when not fitted; !cmd runs in fs/, output on screen, prompt back$extra)"
