#!/bin/sh
# Serve a UCI chess engine over TCP for CHESS.PRG's "Network engine": one
# engine process per connection, stdin/stdout on the socket.  Any Linux box
# on the LAN or the tailnet; the machine's /APPS/CHESS/ENGINE.CFG names it as
# tcp://host:port.  socat if present, else nc.
#   tools/uci-server.sh [engine] [port]
ENGINE=${1:-${K4510_UCI:-$(command -v stockfish || echo "$HOME/opt/stockfish-bin")}}
PORT=${2:-4510}
[ -x "$ENGINE" ] || { echo "uci-server: no engine at $ENGINE"; exit 1; }
echo "uci-server: $ENGINE on port $PORT (Ctrl-C stops)"
if command -v socat >/dev/null; then
    exec socat "TCP-LISTEN:$PORT,reuseaddr,fork" "EXEC:$ENGINE"
else
    while :; do nc -l -p "$PORT" -e "$ENGINE" 2>/dev/null || nc -l "$PORT" -e "$ENGINE" || { echo "uci-server: this nc cannot -e; install socat"; exit 1; }; done
fi
