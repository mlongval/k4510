#!/usr/bin/env python3
"""rec.py -- record what a real program sends a terminal, for JIM's benchmark.

    test/jim/rec.py OUT.jst COLSxROWS SCRIPT -- command args...

Runs the command on a pty of that size, types SCRIPT's keys at their times,
and writes every chunk the program wrote (as the pty handed it over, with the
time it came) and every key typed, so test/jimbench can replay the stream into
JIM chunk by chunk and say what each keystroke's update cost.

It answers the queries JIM answers, as JIM answers them (DA1, DA2, DSR 5 and
the cursor report) and nothing else, so the program takes the same path it
would take talking to a K4510.  --sync answers DECRQM ?2026 as "set/reset
supported", which JIM does not do (yet): what nvim would send if it did.

SCRIPT is a file of steps, one a line:
    wait MS             let the program run
    mark NAME           a phase boundary (the benchmark reports per phase)
    keys MS TEXT        type TEXT, one key every MS ms (\\e \\r \\x04 escapes)
    repeat N MS TEXT    TEXT N times, one every MS ms (a held key)

The .jst file: b"JST1 cols rows\\n", then records of one tag byte
('O' output, 'K' key, 'M' mark), a u32 time in microseconds since the start
and a u32 length, then the bytes.
"""
import os, pty, sys, time, struct, select, fcntl, termios, re, signal

def esc(s):
    return s.encode().decode('unicode_escape').encode('latin-1')

def main():
    a = sys.argv[1:]
    sync = False
    if a and a[0] == '--sync':
        sync = True; a = a[1:]
    out, size, script = a[0], a[1], a[2]
    cmd = a[a.index('--') + 1:]
    cols, rows = map(int, size.split('x'))
    steps = []
    for line in open(script):
        line = line.rstrip('\n')
        if not line or line.startswith('#'):
            continue
        w = line.split(' ', 2)
        if w[0] == 'wait':
            steps.append(('wait', int(w[1])))
        elif w[0] == 'mark':
            steps.append(('mark', w[1]))
        elif w[0] == 'keys':
            for ch in esc(w[2]):
                steps.append(('key', bytes([ch])))
                steps.append(('wait', int(w[1])))
        elif w[0] == 'repeat':
            n, ms, txt = line.split(' ', 3)[1:]
            for _ in range(int(n)):
                steps.append(('key', esc(txt)))
                steps.append(('wait', int(ms)))
    pid, fd = pty.fork()
    if pid == 0:
        os.environ['LANG'] = os.environ.get('LANG', 'C.UTF-8')
        os.execvp(cmd[0], cmd)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', rows, cols, cols * 8, rows * 16))
    os.kill(pid, signal.SIGWINCH)
    f = open(out, 'wb')
    f.write(b'JST1 %d %d\n' % (cols, rows))
    t0 = time.monotonic()
    now_us = lambda: int((time.monotonic() - t0) * 1e6)
    def rec(tag, data):
        f.write(tag + struct.pack('<II', now_us(), len(data)) + data)
    def answer(data):                  # a query split across two reads is missed: rare, and harmless here
        r = b''
        for m in re.finditer(rb'\x1b\[([?>]?)([0-9;]*)(\$?)([a-zA-Z])', data):
            pv, par, it, fin = m.groups()
            if fin == b'c' and pv == b'' and par in (b'', b'0'):
                r += b'\x1b[?62;1;6;22c'
            elif fin == b'c' and pv == b'>':
                r += b'\x1b[>1;10;0c'
            elif fin == b'n' and par == b'5':
                r += b'\x1b[0n'
            elif fin == b'n' and par == b'6':
                r += b'\x1b[1;1R'
            elif fin == b'p' and it == b'$' and pv == b'?' and par == b'2026' and sync:
                r += b'\x1b[?2026;2$y'
        if r:
            os.write(fd, r)
    def pump(ms):
        end = time.monotonic() + ms / 1000.0
        while True:
            left = end - time.monotonic()
            if left <= 0:
                return True
            rd, _, _ = select.select([fd], [], [], left)
            if rd:
                try:
                    d = os.read(fd, 65536)
                except OSError:
                    return False
                if not d:
                    return False
                rec(b'O', d)
                answer(d)
    alive = True
    for st in steps:
        if not alive:
            break
        if st[0] == 'wait':
            alive = pump(st[1])
        elif st[0] == 'mark':
            rec(b'M', st[1].encode())
        elif st[0] == 'key':
            rec(b'K', st[1])
            os.write(fd, st[1])
    if alive:
        pump(1500)
    try:
        os.kill(pid, signal.SIGHUP)
    except OSError:
        pass
    pump(300) if alive else None
    f.close()

main()
