#!/bin/sh
# latrun.sh -- JIM's timing measured on the real frontend under Xvfb (the JIM
# timing work, 2026-10-09).  The Terminal screen runs lat_child.py; XTest
# types keys at it; three logs give
#   key -> pty    the child's read time minus the key's send time
#   pty -> glass  K4510_LATLOG's present time minus the child's write time
# in an active phase (keys 60-140 ms apart) and an idle one (2.5-3 s apart,
# so the frame loop is at rest when each key comes); then the emulator's CPU
# over 30 s with nobody at the keys, at the K/OS prompt and on the Terminal.
#   test/jim/latrun.sh [EMULATOR] [N_ACTIVE] [N_IDLE]     (from the repo top)
set -e
EMU=$(realpath "${1:-sdl/k4510}"); NA=${2:-200}; NI=${3:-12}
TOP=$(pwd); HERE=$TOP/test/jim
W=$(mktemp -d "${TMPDIR:-/tmp}/latrun.XXXXXX")
cc -O2 -o "$W/inject" "$HERE/lat_inject.c" -lX11 -lXtst
cp -r fs "$W/fs"; : > "$W/fs/STARTUP.BAT"; ln -s "$TOP/data" "$W/data"   # the fonts are found from the working directory
# the bands on (REMOTE goes there, not in a bar redrawn every frame: a login
# over ssh -- as when this runs on a server -- kept every frame drawn and the
# loop never at rest), the clock fixed (cpu.auto steps it while it measures)
printf 'version = 3\nterm.bands = on\ncpu.auto = off\ncpu.clock = 10 MHz\nvideo.fullscreen = off\n' > "$W/k4510.cfg"
D=:$((70 + $$ % 20))
Xvfb $D -screen 0 1280x1024x24 -nolisten tcp >/dev/null 2>&1 & XP=$!
trap 'rc=$?; set +e; kill $EP $XP 2>/dev/null; wait 2>/dev/null; rm -rf "$W"; exit $rc' EXIT
sleep 1
export DISPLAY=$D
export SDL_RENDER_DRIVER=${SDL_RENDER_DRIVER:-software} SDL_FRAMEBUFFER_ACCELERATION=${SDL_FRAMEBUFFER_ACCELERATION:-0}   # Mesa's llvmpipe under Xvfb spins threads of its own: noise, not the emulator
cpu() {  # utime+stime of $1 over $2 seconds, as % of one core
    a=$(awk '{print $14+$15}' /proc/$1/stat); sleep $2; b=$(awk '{print $14+$15}' /proc/$1/stat)
    echo "scale=2; ($b - $a) * 100 / $(getconf CLK_TCK) / $2" | bc
}
# 1: idle at the K/OS prompt
(cd "$W" && exec env K4510_LATLOG="$W/lat.log" K4510_TERMINAL="python3 $HERE/lat_child.py $W/child.log" \
     "$EMU" "$TOP/rom/kernal.bin" "$W/fs" >/dev/null 2>&1) & EP=$!
sleep 8; echo "cpu idle at the K/OS prompt, 30 s: $(cpu $EP 30)%"
kill $EP; wait $EP 2>/dev/null || true
# 2: the Terminal screen
(cd "$W" && exec env K4510_LATLOG="$W/lat.log" K4510_TERMINAL="python3 $HERE/lat_child.py $W/child.log" \
     K4510_KEYS="$(printf '~~~~TERMINAL\n~')" "$EMU" "$TOP/rom/kernal.bin" "$W/fs" >/dev/null 2>&1) & EP=$!
for i in $(seq 60); do grep -q READY "$W/child.log" 2>/dev/null && break; sleep 0.5; done
grep -q READY "$W/child.log" || { echo "latrun: the Terminal never started"; exit 1; }
sleep 4; echo "cpu idle on the Terminal screen, 30 s: $(cpu $EP 30)%"
"$W/inject" "$NA" 60 140 "$W/keys_active.log"
sleep 1; "$W/inject" "$NI" 2500 3000 "$W/keys_idle.log"
sleep 1
kill $EP; wait $EP 2>/dev/null || true
python3 - "$W" "$NA" <<'PY'
import sys, bisect, statistics as st
w, na = sys.argv[1], int(sys.argv[2])
keys = [int(l) for l in open(f'{w}/keys_active.log')] + [int(l) for l in open(f'{w}/keys_idle.log')]
child = [tuple(map(int, l.split()[1:])) for l in open(f'{w}/child.log') if l.startswith('K ')]
lat = sorted(tuple(map(int, l.split())) for l in open(f'{w}/lat.log'))
reads = [r for r, _ in lat]
if len(child) != len(keys): print(f'warning: {len(keys)} keys sent, {len(child)} read by the child')
def q(v, p): v = sorted(v); return v[min(len(v) - 1, int(len(v) * p))]
def show(name, v): print(f'  {name:28s} n={len(v):4d}  median {st.median(v):6.2f}  p95 {q(v, .95):6.2f}  max {max(v):6.2f} ms')
for phase, sl in (('active', slice(0, na)), ('idle', slice(na, None))):
    k2p, p2g, rd = [], [], []
    for (ks, (cr, cw)) in list(zip(keys, child))[sl]:
        k2p.append((cr - ks) / 1e6)
        i = bisect.bisect_left(reads, cw)
        if i < len(lat): rd.append((lat[i][0] - cw) / 1e6); p2g.append((lat[i][1] - cw) / 1e6)
    print(phase)
    show('key -> pty write', k2p); show('pty byte -> read by s2_pump', rd); show('pty byte -> on the glass', p2g)
    show('key -> echo on the glass', [a + b for a, b in zip(k2p, p2g)])
PY
