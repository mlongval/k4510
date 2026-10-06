#!/bin/sh
# The classic 8-bit BASIC benchmarks (Rugg/Feldman 1977, Byte Sieve 1981,
# Ahl's Creative Computing 1983; sources: github.com/rprouse/8bit-benchmarks)
# on EhBASIC, plus the Byte Sieve in C and the CHROUT benchmark, all run
# headless and timed by the machine's own frame counter (so host speed does
# not matter).  Usage: test/benchmarks.sh [name ...]   (default: all)
# K4510_ROM=file runs them on another ROM (a before-and-after).
cd "$(dirname "$0")/.." || exit 1
run_bas() {   # $1 = file in fs/
    printf '%-10s ' "$1"
    test/headless "${K4510_ROM:-rom/kernal.bin}" "load ehbasic.prg
run

LOAD \"$1\"
RUN
" 36000 "SECONDS|Error" 2>/dev/null | grep -E "THIS MACHINE:|TIME:|ACCURACY|RANDOM|PRIMES|Error" | tr '\n' ' '; echo
}
run_prg() {   # $1 = .prg in fs/
    printf '%-10s ' "$1"
    test/headless "${K4510_ROM:-rom/kernal.bin}" "load $1
run
" 36000 DONE 2>/dev/null | grep -E "TIME:|primes|ch/s" | sed 's/^ *//' | tr '\n' ' '; echo
}
[ $# -eq 0 ] && set -- RF1 RF2 RF3 RF4 RF5 RF6 RF7 RF8 SIEVE AHL sieve.prg chrout.prg
echo "K4510 benchmarks  (1 frame = 1/60 s; 45GS02 at 40.5 MHz)"
for b in "$@"; do
    case $b in *.prg) run_prg "$b" ;; *) run_bas "$b.BAS" ;; esac
done
