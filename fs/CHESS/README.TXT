CHESS.PRG keeps its things here.

GAME-<date>-<time>.PGN   what Game -> Export PGN writes.

ENGINE.CFG               one line, tcp://host:port, and Options -> Engine
                         -> Network engine plays against a UCI engine served
                         from that address.  On the serving Linux box:
                             tools/uci-server.sh          (Stockfish on port 4510)
                         so   tcp://192.168.2.234:4510   for instance.
                         This is how the Pi gets Stockfish.
