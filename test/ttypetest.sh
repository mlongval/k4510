#!/bin/sh
# TELNET's terminal type.  It used to answer every TTYPE SEND with "ANSI",
# and Linux's telnetd turns that into TERM=ansi -- the PC/DOS ANSI.SYS
# terminfo, whose sequences are not the ones JIM draws with.  htop over
# telnet came out with its rows misplaced and its F-key bar missing (Doc,
# 2026-09-11).  With TERM=xterm-color, linux or vt220 the same JIM drew
# htop cleanly, so the fix was the name, not the terminal.
#
# TELNET now offers a list, one name per SEND as RFC 1091 has it:
# XTERM-COLOR, VT220, VT100, ANSI, then ANSI again to say that was the
# last.  A Linux host takes the first; a server that does not know it asks
# again and gets an older name (2.11BSD's termcap has vt100, a BBS that
# looks for ANSI finds it at the end).
cd "$(dirname "$0")/.."
command -v python3 >/dev/null || { echo "ttypetest: no python3, skipped"; exit 0; }
fail() { echo "$out"; echo "ttypetest: FAILED: $1"; exit 1; }
freeport() { python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1])'; }

# 1. The list, in order, with the last name repeated.
P=$(freeport)
python3 test/ttyped.py $P list & SP=$!
trap 'kill $SP 2>/dev/null' EXIT
sleep 1
out=$(./test/headless rom/kernal.bin "telnet 127.0.0.1 $P
" 900 "END-TTYPES" 2>&1) || fail "no TTYPE answers came back"
echo "$out" | grep -q "TTYPES: XTERM-COLOR,VT220,VT100,ANSI,ANSI " || fail "terminal types not offered in order"
kill $SP 2>/dev/null

# 2. htop on the TERM a Linux host would pick from that list: the header
#    row and the F-key bar must both be on JIM's screen.  With TERM=ansi
#    the bar never appeared.
if command -v htop >/dev/null; then
    P=$(freeport)
    python3 test/ttyped.py $P pty htop -d 10 & SP=$!
    sleep 1
    out=$(./test/headless rom/kernal.bin "telnet 127.0.0.1 $P
" 3000 "F10Quit" 2>&1) || fail "htop's F-key bar never drew (TERM from TTYPE)"
    echo "$out" | grep -q "PID USER" || fail "htop's column header is missing"
    kill $SP 2>/dev/null
else
    echo "ttypetest: no htop, the htop half skipped"
fi
echo "ttypetest: ok"
