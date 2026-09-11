#!/bin/sh
# LOGO: the interpreter runs, does arithmetic on the MATH unit, defines and
# calls a procedure with recursion, draws with the blitter, and BYE returns.
set -e
cd "$(dirname "$0")/.."
export K4510_NO_STARTUP=1
tmp=$(mktemp); trap 'rm -f "$tmp" "$tmp.err"' EXIT
# the bitmap is at $200000; HOME is (320,240) and FD 60 with heading 0 draws
# straight up: pixels at x=320 for y in 180..240 (row stride 640)
K4510_DUMP=200000,$(printf '%x' $((640*480))) ./test/headless rom/kernal.bin 'CD /LANG/LOGO
~LOGO
~~PRINT 2 + 3 * 4
~TO SQ :N
~REPEAT 4 [FD :N RT 90]
~END
~TO COUNT :N
~IF :N < 1 [STOP]
~COUNT :N - 1
~END
~COUNT 12
~SQ 60
~PRINT XCOR
~PRINT HEADING
~BYE
~~' 3000 'LOGO]' > "$tmp" 2>"$tmp.err" || { grep -v "^[0-9A-F][0-9A-F] " "$tmp"; echo "logotest: FAILED: no prompt back after BYE"; exit 1; }
txt=$(grep -v "^dump\|^[0-9A-F][0-9A-F] " "$tmp")
echo "$txt" | grep -q "^14$"          || { echo "$txt"; echo "logotest: FAILED: PRINT 2 + 3 * 4 is not 14"; exit 1; }
echo "$txt" | grep -q "SQ defined"    || { echo "$txt"; echo "logotest: FAILED: TO SQ did not define"; exit 1; }
echo "$txt" | grep -q "COUNT defined" || { echo "$txt"; echo "logotest: FAILED: TO COUNT did not define"; exit 1; }
echo "$txt" | grep -qi "too deep\|don't know" && { echo "$txt"; echo "logotest: FAILED: recursion or a word went wrong"; exit 1; }
echo "$txt" | grep -q "^0$"           || { echo "$txt"; echo "logotest: FAILED: XCOR is not back at 0 after the square"; exit 1; }
python3 - "$tmp" <<'PY' || exit 1
import sys,re
t=open(sys.argv[1]).read(); m=re.search(r"dump \$200000:(.*)", t, re.S)
b=bytes.fromhex((m.group(1) if m else "").replace("\n"," "))
col=[b[y*640+320] for y in range(185,236)] if len(b)>=640*480 else []
lit=sum(1 for v in col if v)
print("logotest: pixels lit on the square's first edge:", lit, "of", len(col))
sys.exit(0 if lit >= 40 else 1)
PY
echo "logotest: OK (MATH-unit arithmetic, TO/END, recursion, blitter lines, BYE)"
