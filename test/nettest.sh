#!/bin/sh
# The network, offline: a local HTTP server and a TCP echo server on
# loopback, then the Meatloaf rule through the ROM (TYPE a URL, CP a URL to
# a file) and the N: device through the telnet demo (a line typed, echoed).
set -e
cd "$(dirname "$0")/.."
command -v python3 >/dev/null || { echo "nettest: no python3, skipped"; exit 0; }
command -v curl >/dev/null || { echo "nettest: no curl, skipped"; exit 0; }
W=$(mktemp -d); trap 'kill $HP $EP 2>/dev/null; rm -rf "$W" fs/NETCOPY.TXT fs/HOME/NETCOPY.TXT' EXIT
printf 'HELLO FROM HTTP\n' > "$W/HELLO.TXT"
HPORT=$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1])')
EPORT=$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1])')
(cd "$W" && python3 -m http.server --bind 127.0.0.1 $HPORT >/dev/null 2>&1) & HP=$!
python3 - $EPORT <<'PY' & EP=$!
import socket,sys
s=socket.socket(); s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1); s.bind(("127.0.0.1",int(sys.argv[1]))); s.listen(1)
while True:
    c,_=s.accept(); c.sendall(b"ECHO READY\r\n")
    while True:
        d=c.recv(256)
        if not d: break
        c.sendall(d.upper())
    c.close()
PY
sleep 1
out=$(./test/headless rom/kernal.bin "TYPE http://127.0.0.1:$HPORT/HELLO.TXT
CP http://127.0.0.1:$HPORT/HELLO.TXT NETCOPY.TXT
TYPE NETCOPY.TXT
" 400 "1 file" 2>&1) || true
echo "$out" | grep -q "HELLO FROM HTTP" || { echo "$out"; echo "nettest: FAILED: TYPE of a URL"; exit 1; }
[ "$(echo "$out" | grep -c 'HELLO FROM HTTP')" -ge 2 ] || { echo "$out"; echo "nettest: FAILED: CP of a URL"; exit 1; }
out=$(./test/headless rom/kernal.bin "telnet 127.0.0.1 $EPORT
~hello there
~~" 600 "HELLO THERE" 2>&1) || { echo "$out"; echo "nettest: FAILED: telnet echo"; exit 1; }
echo "$out" | grep -q "ECHO READY" || { echo "$out"; echo "nettest: FAILED: telnet banner"; exit 1; }
# TNFS: a server on loopback, the machine's current directory on it
TPORT=$(python3 -c 'import socket;s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.bind(("127.0.0.1",0));print(s.getsockname()[1])')
mkdir -p "$W/tnfs/SUB" "$W/tnfs/4] SPACED"; printf 'HELLO FROM TNFS\n' > "$W/tnfs/HELLO.TXT"; printf 'DEEPER\n' > "$W/tnfs/SUB/DEEP.TXT"; printf 'IN A SPACED DIR\n' > "$W/tnfs/4] SPACED/S.TXT"; cp fs/SYSTEM/BIN/say.prg "$W/tnfs/say.prg"
python3 test/tnfsd.py $TPORT "$W/tnfs" & TP=$!
trap 'kill $HP $EP $TP 2>/dev/null; rm -rf "$W" fs/NETCOPY.TXT fs/HOME/NETCOPY.TXT' EXIT
sleep 1
out=$(./test/headless rom/kernal.bin "CD tnfs://127.0.0.1:$TPORT/
DIR
TYPE HELLO.TXT
SAY FROM A TNFS SERVER
CD SUB
TYPE DEEP.TXT
CD ..
CD -
DIR
" 900 "file(s)" 2>&1) || true
for m in "HELLO.TXT" "HELLO FROM TNFS" "FROM A TNFS SERVER" "DEEPER" "tnfs://127.0.0.1:$TPORT/SUB]" "README.TXT"; do
  echo "$out" | grep -q -- "$m" || { echo "$out"; echo "nettest: FAILED: TNFS, expected '$m'"; exit 1; }
done
# Mount: a server as a local subtree.  A spaced name is entered, a remote file
# read, and a LOCAL program (SAY) runs on the mount (RANGER's requirement).
out=$(./test/headless rom/kernal.bin "MOUNT tnfs://127.0.0.1:$TPORT/ /MNT/NET
CD /MNT/NET
DIR
CD \"4] SPACED\"
TYPE S.TXT
SAY MOUNTED OK
UMOUNT /MNT/NET
" 1500 "IN A SPACED DIR" 2>&1) || true
for m in "/MNT/NET/4] SPACED]" "IN A SPACED DIR" "MOUNTED OK"; do
  echo "$out" | grep -q -- "$m" || { echo "$out"; echo "nettest: FAILED: mount, expected '$m'"; exit 1; }
done
echo "nettest: OK (TYPE/CP of a URL, telnet echo on the N: device, TNFS: CD/DIR/TYPE/RUN/CD -, MOUNT a server + spaced name + a local program on it)"
