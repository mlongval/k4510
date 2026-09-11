#!/bin/sh
# The K4510 in a browser: the same core and SDL2 frontend, compiled to
# WebAssembly with Emscripten, the ROM, the fonts and the machine's own
# filesystem preloaded.  Output: wasm/dist/ -- index.html, k4510.js,
# k4510.wasm, k4510.data -- a static site, served from anywhere.
#
#   wasm/build.sh          (needs emcc on PATH: source ~/opt/emsdk/emsdk_env.sh)
#
# What the page does NOT have, by the nature of a page: the Tube (BBC BASIC,
# CP/M, the host shell, Stockfish -- there are no processes), the N: device
# (no raw sockets).  /HOME survives a reload now (IndexedDB, via the shell);
# the rest of the disk is the build's.  Everything else is the
# machine: VICKY, JIM, the OPL2, the sequencer, the games, CHESS's built-in
# engine, the F7 menu, save states within the session.
set -e
HERE=$(cd "$(dirname "$0")" && pwd); REPO=$(cd "$HERE/.." && pwd); cd "$REPO"
OUT="$HERE/dist"; mkdir -p "$OUT"
BUILD=$(git describe --always --dirty 2>/dev/null || echo dev)
SRC="sdl/main.c sdl/panel.c core/xemu/cpu65.c core/mem.c core/io.c core/vicky.c core/net.c core/net_wasm.c core/term.c core/state.c core/hostid.c core/ui/settings.c core/ui/menu.c core/ui/ui_draw.c sdl/host_posix.c core/opl2/fmopl.c core/opl2.c core/vice_clk.c core/sndq.c core/audio.c"
emcc -O2 -Icore -Wno-unused-function -DK4510_NOPROC -DK4510_WASM \
    -sUSE_SDL=2 -sASYNCIFY -sASYNCIFY_STACK_SIZE=65536 \
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=320MB -sMAXIMUM_MEMORY=1GB \
    -sEXIT_RUNTIME=0 -sENVIRONMENT=web \
    --preload-file rom/kernal.bin --preload-file rom/wozmon.bin --preload-file rom/demo.bin \
    --preload-file data/font8.bin --preload-file data/fonts \
    --preload-file fs --exclude-file "fs/CPM/*" --exclude-file "fs/.TRASH/*" --exclude-file "fs/HOME/*" \
    -lidbfs.js -sFORCE_FILESYSTEM=1 \
    --shell-file "$HERE/shell.html" \
    -o "$OUT/index.html" $SRC
ls -lh "$OUT"
echo "wasm/build.sh: $OUT  ($BUILD)"
