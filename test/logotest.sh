#!/bin/sh
# LOGO: the interpreter runs, does arithmetic on the MATH unit, defines and
# calls a procedure with recursion, draws with the blitter, shows the turtle
# as a sprite, FILLs, and BYE returns.
set -e
cd "$(dirname "$0")/.."
export K4510_NO_STARTUP=1
tmp=$(mktemp); trap 'rm -f "$tmp" "$tmp.err"' EXIT
# the bitmap is at $200000; HOME is (320,240) and FD 60 with heading 0 draws
# straight up: pixels at x=320 for y in 180..240 (row stride 640).  Right
# after the bitmap: the turtle's 16x16 glyph ($24B000) and sprite entry 0
# ($24B100).  The dump covers all three.
K4510_DUMP=200000,$(printf '%x' $((640*480+0x110))) ./test/headless rom/kernal.bin 'CD /LANG/LOGO
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
~PU SETXY 30 30 SETPC 3 FILL
~~BYE
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
W,H=640,480
if len(b) < W*H+0x110: print("logotest: FAILED: short dump", len(b)); sys.exit(1)
col=[b[y*W+320] for y in range(185,236)]
lit=sum(1 for v in col if v)
print("logotest: pixels lit on the square's first edge:", lit, "of", len(col))
ok = lit >= 40
# FILL from (30,30) inside the 60x60 square, pen 3: the inside is 3, the edge is still 1, outside untouched
inside=b[210*W+350]; edge=b[200*W+320]; outside=b[100*W+100]
print("logotest: FILL inside/edge/outside:", inside, edge, outside)
ok = ok and inside == 3 and edge == 1 and outside == 0
# the turtle: sprite 0 enabled, 8 bpp, Z 3; its glyph has pixels; it sits over (350,210)
glyph=b[W*H:W*H+256]; attr=b[W*H+0x100:W*H+0x110]
x=int.from_bytes(attr[0:2],'little',signed=True); y=int.from_bytes(attr[2:4],'little',signed=True)
print("logotest: turtle sprite ctrl=%#x size=%d at (%d,%d), glyph pixels lit: %d" % (attr[8], attr[9], x, y, sum(1 for v in glyph if v)))
ok = ok and attr[8] == 0x33 and attr[9] == 5 and x == 350-8 and y == 210-8 and sum(1 for v in glyph if v) >= 20
sys.exit(0 if ok else 1)
PY
# a runaway REPEAT, stopped by ESC (the $D103 break every interpreter honours)
out2=$(./test/headless rom/kernal.bin "$(printf 'CD /LANG/LOGO\n~LOGO\n~~REPEAT 200000 [FD 1 RT 1]\n~\033~PRINT 77\n~BYE\n~~')" 3000 'LOGO]' 2>/dev/null)
echo "$out2" | grep -q "stopped" || { echo "$out2" | tail -8; echo "logotest: FAILED: ESC did not stop the runaway REPEAT"; exit 1; }
echo "$out2" | grep -q "^77$"    || { echo "$out2" | tail -8; echo "logotest: FAILED: no prompt after the break"; exit 1; }
echo "logotest: OK (MATH-unit arithmetic, TO/END, recursion, blitter lines, the turtle sprite, FILL, ESC stops a run, BYE)"
