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
make core/build.h >/dev/null 2>&1 || true   # so the Info menu's Build row names this commit
# The preloaded disk, ROM and fonts come from a `git archive` of HEAD, never
# from the working tree: the tree carries the ZX Origins fonts, Doc's OPL
# tunes, STARTUP.BAT and the profiler logs, none of which may be published
# (they were, until 2026-09-12).  Commit before building the page.
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
git archive --format=tar HEAD rom data/font8.bin data/fonts fs > "$T/tree.tar"
tar -x -C "$T" -f "$T/tree.tar"; rm "$T/tree.tar"
SRC="sdl/main.c sdl/panel.c core/xemu/cpu65.c core/mem.c core/io.c core/vicky.c core/net.c core/net_wasm.c core/term.c core/state.c core/hostid.c core/ui/settings.c core/ui/menu.c core/ui/ui_draw.c sdl/host_posix.c core/opl2/fmopl.c core/opl2.c core/vice_clk.c core/sndq.c core/audio.c"
emcc -O2 -Icore -Wno-unused-function -DK4510_NOPROC -DK4510_WASM \
    -sUSE_SDL=2 -sASYNCIFY -sASYNCIFY_STACK_SIZE=65536 \
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=320MB -sMAXIMUM_MEMORY=1GB \
    -sEXIT_RUNTIME=0 -sENVIRONMENT=web \
    --preload-file "$T/rom/kernal.bin@rom/kernal.bin" --preload-file "$T/rom/wozmon.bin@rom/wozmon.bin" --preload-file "$T/rom/demo.bin@rom/demo.bin" \
    --preload-file "$T/data/font8.bin@data/font8.bin" --preload-file "$T/data/fonts@data/fonts" \
    --preload-file "$T/fs@fs" --exclude-file "*/fs/CPM/*" --exclude-file "*/fs/.TRASH/*" --exclude-file "*/fs/HOME/*" \
    -lidbfs.js -sFORCE_FILESYSTEM=1 \
    --shell-file "$HERE/shell.html" \
    -o "$OUT/index.html" $SRC
ls -lh "$OUT"
# what must never be in a public bundle
if strings "$OUT/index.js" | grep -qE 'fonts/zx/[^"]*\.bin|TUNES/|STARTUP\.BAT|SYSTEM/LOG/(PERF|TRACE|BENCH)|\.k4s'; then
    echo "wasm/build.sh: the bundle contains untracked personal files -- not publishing" >&2; exit 1
fi
echo "wasm/build.sh: $OUT  ($BUILD)"
