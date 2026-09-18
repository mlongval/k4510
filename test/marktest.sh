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
set -- $row    # clock | SPIN =65C02 | SIEVE s, =65C02 | COPY KB/s, =65C02 | MANDEL s, =65C02 | +MATH s, =65C02 | host
[ $# -eq 11 ]                                                || fail "the row has not eleven columns: $row"
awk -v a="$7" -v b="$9" 'BEGIN { exit !(b > 0 && b < a) }'   || fail "the MATH unit was not the faster: $7 s without, $9 s with"
# a 45GS10 at 40.5 MHz is a 65C02 at 45 to 51 on all four; one far from the rest is a cycle count gone stale
for m in "$2" "$4" "$6" "$8"; do
    awk -v m="$m" 'BEGIN { exit !(m > 40 && m < 56) }'       || fail "=65C02 of $m MHz at a 40.5 MHz clock is not believable: $row"
done
# the counts are of THIS assembly: tools/mark-cycles.py writes the hash it counted
grep -q "sha256 $(sha256sum demo/mark-asm.s | cut -d' ' -f1)" demo/mark-cycles.h \
    || fail "demo/mark-asm.s has changed since its cycles were counted: build, run tools/mark-cycles.py, build again"
echo "$out" | grep -q "written to /SYSTEM/LOG/MARK.TXT"         || fail "MARK did not reach its last line"   # (the screen is read the moment that line lands: the prompt after it may not be drawn yet)
[ -s $L ] && grep -q "^ *40\.5 " $L                          || fail "the report is not in /SYSTEM/LOG/MARK.TXT"
rm -f $L
echo "marktest: OK (BCD, 1899 primes, the picture 16897 both ways, MATH the faster: $7 s -> $9 s; a 65C02 at $2 / $4 / $6 / $8 MHz; the counts are of this assembly; the report on disk)"
