#!/usr/bin/env python3
"""latency.py FILE.jst...: from a recording, how long the program (and tmux,
mosh) took to answer each key -- first byte and last byte of the update -- per
phase, in ms (median / 95th percentile).  This is the time BEFORE JIM sees a
byte: the far side's share of an echo.  Also, for ?2026 streams, how many
synchronized updates end within one 60 Hz frame (each is a whole repaint)."""
import sys, os, statistics as st
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import jstcat
def q(v, p): v = sorted(v); return v[min(len(v) - 1, int(len(v) * p))] if v else float('nan')
for path in sys.argv[1:]:
    cols, rows, recs = jstcat.read(path)
    print(f'{os.path.basename(path)}  {cols}x{rows}')
    print(f'  {"phase":10s} {"first med":>9s} {"first p95":>9s} {"last med":>9s} {"last p95":>9s}  (ms after the key)')
    ph = 'start'; per = {}; cur = None
    for tag, t, b in recs + [(b'M', 0, b'end')]:
        if tag in (b'K', b'M'):
            if cur and cur[1] is not None: per.setdefault(cur[0], []).append((cur[1] - cur[2], cur[3] - cur[2]))
            cur = None
            if tag == b'M': ph = b.decode()
            else: cur = [ph, None, t, None]
        elif tag == b'O' and cur:
            if cur[1] is None: cur[1] = t
            cur[3] = t
    for k, v in per.items():
        f = [a / 1000 for a, _ in v]; l = [b / 1000 for _, b in v]
        print(f'  {k:10s} {st.median(f):9.1f} {q(f, .95):9.1f} {st.median(l):9.1f} {q(l, .95):9.1f}')
    ends = [t for tag, t, b in recs if tag == b'O' for _ in range(b.count(b'\x1b[?2026l'))]
    if ends:
        frames = {}
        for t in ends: frames[t // 16667] = frames.get(t // 16667, 0) + 1
        print(f'  ?2026l: {len(ends)} in {len(frames)} frames, at most {max(frames.values())} in one frame')
