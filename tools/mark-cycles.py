#!/usr/bin/env python3
"""mark-cycles.py -- what MARK's measured loops cost a real 65C02, to the cycle.

    tools/mark-cycles.py          count, and write demo/mark-cycles.h
    tools/mark-cycles.py --show   count, and only say

MARK turns a time into "the work of a 65C02 at N MHz" by dividing these counts
by it.  They come from running the routines of the BUILT fs/SYSTEM/BIN/mark.prg
on py65's cycle-exact 65C02 -- not from reading the source: a page crossed by a
branch or by (zp),Y is a cycle, and only the linked program knows where its
pages are.  (That is also why the Makefile links mark-asm.o FIRST: editing
mark.c then moves nothing that is measured.)

Run it after ANY change to demo/mark-asm.s: build, run this, build again.
test/marktest.sh fails while the header's hash of mark-asm.s is not the file's.

Three things it checks on the way, because a count of the wrong code is worse
than none: the sieve finds 1899, the picture sums to 16897, and SPIN comes to
the figure counted by hand in mark-asm.s (n x 328711 - 1, plus 13 of call).
gfoot's own profile of his Mandelbrot (prof_mandel2.txt in his repository) is
158 961 356 cycles with his printing in it; ours, without, is within 1%.

Needs py65, which is not in the project:  python3 -m venv /tmp/v && /tmp/v/bin/pip install py65
and then run this with /tmp/v/bin/python.  The MATH unit is not simulated --
there is no 65C02 with one -- so +MATH is set against MANDEL's count: the same
picture, as if a 65C02 had done it the long way.
"""
import hashlib, pathlib, re, sys

REPO = pathlib.Path(__file__).resolve().parent.parent
try:
    from py65.devices.mpu65c02 import MPU
except ImportError:
    sys.exit("mark-cycles: no py65 here (see the top of this file)")

sym = dict((m.group(1), int(m.group(2), 16))
           for m in re.finditer(r"(_mk_\w+)\s+([0-9A-F]{6}) R", (REPO / "demo/mark.map").read_text()))
prg = (REPO / "fs/SYSTEM/BIN/mark.prg").read_bytes()
load = prg[0] | (prg[1] << 8)


def run(name, a=0):
    m = MPU(); mem = m.memory
    for i, b in enumerate(prg[4:]):
        mem[load + i] = b
    for n in range(200):
        mem[sym["_mk_tobcd"] + n] = ((n % 100) // 10) << 4 | (n % 100) % 10
    mem[sym["_mk_math"]] = 0
    mem[0x01FF] = 0xEF; mem[0x01FE] = 0xFF; m.sp = 0xFD          # RTS lands on $F000
    m.pc = sym[name]; m.a = a
    while m.pc != 0xF000:
        m.step()
    return m.processorCycles, m.a | (m.x << 8), sum(mem[sym["_mk_img"] + i] for i in range(134 * 80)) & 0xFFFF


spin, _, _ = run("_mk_spin", 50)
bcd, wrong, _ = run("_mk_bcd")
sieve, primes, _ = run("_mk_sieve")
copy, _, _ = run("_mk_copy", 250)
mandel, _, picture = run("_mk_mandel")
for ok, what in ((spin == 328711 * 50 - 1 + 13, f"SPIN is {spin}, not the {328711 * 50 - 1 + 13} counted by hand"),
                 (wrong == 0, f"BCD: {wrong} wrong on a 65C02"), (primes == 1899, f"the sieve found {primes}"),
                 (picture == 16897, f"the picture sums to {picture}")):
    if not ok:
        sys.exit("mark-cycles: " + what)
sha = hashlib.sha256((REPO / "demo/mark-asm.s").read_bytes()).hexdigest()
text = f"""/* Written by tools/mark-cycles.py -- do not edit.  What the measured loops of
 * demo/mark-asm.s cost a 65C02, counted on py65 from the built mark.prg. */
#define MK_CYC_SPIN   {spin}UL   /* mk_spin(50) */
#define MK_CYC_SIEVE  {sieve}UL
#define MK_CYC_COPY   {copy}UL   /* mk_copy(250) */
#define MK_CYC_MANDEL {mandel}UL
/* mark-asm.s sha256 {sha} */
"""
print(f"SPIN {spin}  BCD {bcd}  SIEVE {sieve}  COPY {copy}  MANDEL {mandel}")
if "--show" not in sys.argv[1:]:
    (REPO / "demo/mark-cycles.h").write_text(text)
    print("written: demo/mark-cycles.h")
