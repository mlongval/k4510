#!/bin/sh
# test/remote/zipfixture.sh OUT.ZIP -- the zip test/remote/zip.k4r mounts:
# README.TXT, HELLO.PRG (fs/LANG/C's) and DATA/NOTE.TXT.  Put on the machine
# under test as /HOME/ZIPTEST.ZIP before the script runs.
set -e
cd "$(dirname "$0")/../.."
python3 - "$1" <<'EOF'
import sys, zipfile
with zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("README.TXT", "This file is inside a zip.\n")
    z.write("fs/LANG/C/hello.prg", "HELLO.PRG")
    z.writestr("DATA/NOTE.TXT", "a note in a folder in the zip\n")
EOF
