#!/bin/sh
# K4510: one-shot desktop setup -- install the build dependencies,
# build the machine and its tools, run the test suite.
#   git clone https://github.com/mlongval/bmc-k4510 && cd bmc-k4510 && ./setup.sh
# Understands apt (Debian/Ubuntu), dnf (Fedora) and pacman (Arch).
set -e

if command -v apt-get >/dev/null; then
    sudo apt-get update
    sudo apt-get install -y git make gcc g++ libsdl2-dev cc65 64tass nasm unzip
elif command -v dnf >/dev/null; then
    sudo dnf install -y git make gcc gcc-c++ SDL2-devel cc65 64tass nasm unzip || true
elif command -v pacman >/dev/null; then
    sudo pacman -S --needed --noconfirm git make gcc sdl2 cc65 64tass nasm unzip || true
else
    echo "setup: no apt/dnf/pacman -- install by hand: make, gcc, g++, SDL2 dev headers, cc65, 64tass, nasm"
fi

echo
MISSING=""
for t in make gcc g++ sdl2-config cc65 64tass nasm; do
    command -v "$t" >/dev/null || MISSING="$MISSING $t"
done
if [ -n "$MISSING" ]; then
    echo "setup: still missing:$MISSING"
    echo "  cc65   -- required (builds the system ROM);  https://cc65.github.io"
    echo "  64tass -- only to rebuild Forth (fs/LANG/FORTH/forth.prg ships prebuilt);  https://tass64.sourceforge.net"
    echo "  nasm   -- only for the BBC BASIC Tube co-processor"
    echo "  sdl2   -- required (the emulator's window and sound)"
fi

[ -f Makefile ] || { echo "setup: run me from the repo root (git clone https://github.com/mlongval/bmc-k4510)"; exit 1; }
make all
make -C tube 2>/dev/null && echo "setup: Tube (BBC BASIC) built" || echo "setup: Tube skipped (nasm missing?) -- everything else works"
make cpm/runcpm 2>/dev/null && echo "setup: CP/M co-processor built" || true

echo
PASS=0; FAIL=""
for t in mathtest fstest romtest cputest woztest maptest banktest dmatest vickytest sidtest; do
    if ./test/$t >/dev/null 2>&1; then PASS=$((PASS+1)); else FAIL="$FAIL $t"; fi
done
echo "setup: tests: $PASS/10 pass${FAIL:+ (failed:$FAIL)}"
echo
echo "Run the machine:   ./sdl/k4510 rom/kernal.bin fs"
echo "Read the book:     doc/guide/  (or the PDF from the releases page)"
