#!/usr/bin/env python3
"""ttyped.py PORT list | ttyped.py PORT pty CMD...  --  a telnet server on
loopback for test/ttypetest.sh, one connection, then it exits.

  list  asks the client for its terminal type five times (RFC 1091: each
        SEND should get the next name on the client's list, the last one
        repeated when it runs out) and prints what came back:
            TTYPES: XTERM-COLOR,VT220,VT100,ANSI,ANSI END-TTYPES
  pty   asks once, the way Linux telnetd does, and runs CMD in a pty with
        TERM set to the answer in lower case, at the size NAWS reports
        (80x30 until it does).  HOME is a scratch directory, so a program
        that saves settings on exit (htop) writes nothing real.
"""
import os, pty, select, socket, struct, sys, tempfile, fcntl, termios

IAC, SB, SE, DO, WILL = 255, 250, 240, 253, 251
TTYPE, NAWS = 24, 31

port, mode = int(sys.argv[1]), sys.argv[2]
s = socket.socket()
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('127.0.0.1', port))
s.listen(1)
s.settimeout(120)
c, _ = s.accept()
c.settimeout(30)


class Telnet:
    """Splits the client's bytes into data and subnegotiations."""
    def __init__(self):
        self.st, self.sb = 0, bytearray()

    def feed(self, d):
        data, subs = bytearray(), []
        for b in d:
            if self.st == 0:
                if b == IAC: self.st = 1
                else: data.append(b)
            elif self.st == 1:
                if b == SB: self.st, self.sb = 3, bytearray()
                elif b == IAC: data.append(b); self.st = 0
                elif b >= 251: self.st = 2
                else: self.st = 0
            elif self.st == 2: self.st = 0
            elif self.st == 3:
                if b == IAC: self.st = 4
                else: self.sb.append(b)
            elif self.st == 4:
                if b == SE: subs.append(bytes(self.sb)); self.st = 0
                else: self.sb.append(b); self.st = 3
        return bytes(data), subs


tn = Telnet()


def ask_ttype():
    """One TTYPE SEND; the name that comes back (or '' on timeout)."""
    c.sendall(bytes([IAC, SB, TTYPE, 1, IAC, SE]))
    while True:
        try:
            d = c.recv(1024)
        except socket.timeout:
            return ''
        if not d:
            return ''
        for sub in tn.feed(d)[1]:
            if len(sub) >= 2 and sub[0] == TTYPE and sub[1] == 0:
                return sub[2:].decode('ascii', 'replace')


c.sendall(bytes([IAC, DO, TTYPE, IAC, DO, NAWS]))
if mode == 'list':
    names = [ask_ttype() for _ in range(5)]
    c.sendall(('TTYPES: ' + ','.join(names) + ' END-TTYPES\r\n').encode())
    c.settimeout(5)
    try:
        while c.recv(1024):
            pass
    except OSError:
        pass
    sys.exit(0)

term = ask_ttype().lower() or 'dumb'
home = tempfile.mkdtemp(prefix='ttyped.')
pid, fd = pty.fork()
if pid == 0:
    os.environ.update(TERM=term, HOME=home, LANG='C.UTF-8', LC_ALL='C.UTF-8')
    os.execvp(sys.argv[3], sys.argv[3:])
fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', 30, 80, 0, 0))
c.settimeout(None)
while True:
    r, _, _ = select.select([c, fd], [], [], 120)
    if not r:
        break
    if fd in r:
        try:
            d = os.read(fd, 4096)
        except OSError:
            break
        if not d:
            break
        c.sendall(d.replace(bytes([IAC]), bytes([IAC, IAC])))
    if c in r:
        d = c.recv(4096)
        if not d:
            break
        data, subs = tn.feed(d)
        for sub in subs:
            if len(sub) >= 5 and sub[0] == NAWS:
                w, h = sub[1] * 256 + sub[2], sub[3] * 256 + sub[4]
                fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', h, w, 0, 0))
                os.kill(pid, 28)                      # SIGWINCH
        if data:
            os.write(fd, data)
try:
    os.kill(pid, 9)
except OSError:
    pass
