#!/bin/sh
# test/ziptest.sh -- MOUNT a zip (2026-09-15).  The zips are made here, by
# Python's zipfile and by Info-ZIP's zip, good ones and ones made wrong on
# purpose; test/ziptest mounts them through the machine's own file registers.
set -e
cd "$(dirname "$0")/.."
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
mkdir -p "$T/fs/HOME" "$T/src"
python3 - "$T" <<'EOF'
import sys, os, io, zipfile, random
t = sys.argv[1]; src = t + "/src"; home = t + "/fs/HOME"
random.seed(4510)
files = {
    "README.TXT":     b"Hello from inside a zip.\n",                                   # short: fixed Huffman codes
    "BIN/HELLO.PRG":  bytes(random.getrandbits(8) for _ in range(5000)),              # stored
    "DATA/BIG.TXT":   b"".join(b"line %d of the big file, and text that repeats\n" % (i % 97) for i in range(4000)),   # dynamic codes, long matches
    "DATA/RAND.BIN":  bytes(random.getrandbits(8) for _ in range(70000)),             # deflate that chooses stored blocks
    "EMPTY.TXT":      b"",
    "Mixed.Case.txt": b"case\n",
}
for n, d in files.items():
    p = os.path.join(src, n); os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, "wb").write(d)

def make(entries, comment=b""):
    b = io.BytesIO()
    with zipfile.ZipFile(b, "w") as z:
        for n, d, m in entries: z.writestr(n, d, compress_type=m, compresslevel=9 if m == zipfile.ZIP_DEFLATED else None)
        z.comment = comment
    return b.getvalue()
D, S = zipfile.ZIP_DEFLATED, zipfile.ZIP_STORED
good = make([(n, d, S if n.startswith("BIN/") else D) for n, d in files.items()] + [("DOCS/", b"", S)], b"a comment, which the end record must be found behind " * 3)
open(home + "/GOOD.ZIP", "wb").write(good)
open(home + "/NEST.ZIP", "wb").write(make([("INNER.ZIP", good, S)]))
open(home + "/EMPTY.ZIP", "wb").write(make([]))
open(home + "/TRUNC.ZIP", "wb").write(good[:len(good) // 2])
open(home + "/NOTZIP.ZIP", "wb").write(b"not a zip at all, only text\n" * 4)
# names that could climb out of the mount: made with a harmless name of the same
# length, then patched (local header and central directory both)
for fn, safe, bad in (("EVIL1.ZIP", b"XX/EVIL.TXT", b"../EVIL.TXT"), ("EVIL2.ZIP", b"XABS.TXT", b"/ABS.TXT"),
                      ("EVIL3.ZIP", b"AXB.TXT", b"A\\B.TXT"), ("EVIL4.ZIP", b"A/X/B.TXT", b"A/../B.TXT")):
    open(home + "/" + fn, "wb").write(make([(safe.decode(), b"x", D)]).replace(safe, bad))
# an entry marked encrypted (the flag only: this must refuse it, not try)
e = bytearray(make([("SECRET.TXT", b"secret", D)]))
c = e.find(b"PK\x01\x02"); e[c + 8] |= 1
open(home + "/ENC.ZIP", "wb").write(bytes(e))
# a damaged entry: the last byte of its deflated data changed
bc = bytearray(make([("DATA.TXT", files["DATA/BIG.TXT"], D)]))
c = bc.find(b"PK\x01\x02"); bc[c - 1] ^= 0x55
open(home + "/BADCRC.ZIP", "wb").write(bytes(bc))
EOF
# Info-ZIP's own: folders as entries, extra fields; and one streamed (a data descriptor)
(cd "$T/src" && zip -qrX "$T/fs/HOME/INFO.ZIP" .)
printf 'streamed through a pipe\n' | zip -q "$T/fs/HOME/STREAM.ZIP" -
./test/ziptest "$T"
