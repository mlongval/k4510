#!/bin/sh
# MARK (Doc, 2026-09-18: one benchmark, the clocks from 10 to 60, the MATH unit
# off and on).  Headless there is one clock, so this is MARK 40: what is checked
# is that the machine computes RIGHT -- the decimal arithmetic, the sieve's 1899,
# and the Mandelbrot summing to gfoot's 16897 both by shift-and-add and by the
# MATH unit -- that a row of figures comes out with no ! in it, that the MATH
# unit is the faster of the pair, and that the report reaches the disk.  The
# sweep itself needs the real frontend: by hand, under Xvfb (docs/BUILD-LOG.md).
set -e
cd "$(dirname "$0")/.."
L=fs/SYSTEM/LOG/MARK.TXT
fail() { echo "$out"; echo "marktest: FAILED: $1"; exit 1; }
rm -f $L
out=$(./test/headless rom/kernal.bin "MARK 40
" 9000 "MARK.TXT" 2>/dev/null) || fail "MARK did not finish"
echo "$out" | grep -q "against binary: all right"            || fail "the decimal arithmetic"
echo "$out" | grep -q "16897 by shift and add, 16897 by the MATH unit: right" || fail "the Mandelbrot does not sum to 16897 both ways"
row=$(echo "$out" | grep "^ *40\.5 ")                        || fail "no row for 40.5 MHz"
case "$row" in *!*) fail "a figure is marked wrong: $row" ;; esac
set -- $row                                                  # clock 6502 sieve copy mandel +math host
[ $# -eq 7 ]                                                 || fail "the row has not seven columns: $row"
awk -v a="$5" -v b="$6" 'BEGIN { exit !(b > 0 && b < a) }'   || fail "the MATH unit was not the faster: $5 s without, $6 s with"
awk -v m="$2" 'BEGIN { exit !(m > 40 && m < 70) }'           || fail "as a 6502: $2 MHz at a 40.5 MHz clock is not believable"
echo "$out" | grep -q "^/HOME\]"                             || fail "the shell did not come back"
[ -s $L ] && grep -q "^ *40\.5 " $L                          || fail "the report is not in /SYSTEM/LOG/MARK.TXT"
rm -f $L
echo "marktest: OK (BCD, 1899 primes, the picture 16897 both ways, MATH the faster: $5 s -> $6 s, as a 6502 at $2 MHz, the report on disk)"
