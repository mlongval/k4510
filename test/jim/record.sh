#!/bin/sh
# test/jim/record.sh -- record the streams test/jimbench replays (the JIM
# review, 2026-10-09).  Run on a host with nvim, tmux and mosh (ubuntu-s1):
# each is the real program on a pty the Terminal screen's size, TERM as the
# K4510 sets it (xterm-color), the user's own nvim and tmux configuration,
# typed at by nvim.steps / tmux.steps.  Writes test/jim/streams/*.jst.gz.
#
#   nvim-xc-80x28      nvim straight on the pty: what ssh -t brings
#   nvim-xc-100x37     the same, a bigger window
#   nvim-sync-80x28    nvim told ?2026 is supported (rec.py --sync): what
#                      JIM would get if it answered DECRQM
#   tmux-nvim-80x28    nvim in tmux (its status bar, its redraws)
#   mosh-tmux-nvim-80x28  mosh-client's output for nvim in tmux: what the
#                      Terminal screen gets through k4510-connect
# 80x28 is the default Terminal (80x30 less the two bands).
set -e
J=$(cd "$(dirname "$0")" && pwd)
T=$(mktemp -d); trap 'rm -rf "$T"; tmux -L jimrec kill-server 2>/dev/null; tmux -L jimrec2 kill-server 2>/dev/null; true' EXIT
cp "$J/../../core/term.c" "$T/term.c"                 # the file nvim opens: C, syntax on
cd "$T"
unset TMUX
export TERM=xterm-color LANG=${LANG:-C.UTF-8}
TM=$(command -v tmux); [ -x /usr/bin/tmux ] && TM=/usr/bin/tmux   # the real one, not a wrapper
python3 "$J/rec.py" nvim-xc-80x28.jst 80x28 "$J/nvim.steps" -- nvim term.c
python3 "$J/rec.py" nvim-xc-100x37.jst 100x37 "$J/nvim.steps" -- nvim term.c
python3 "$J/rec.py" --sync nvim-sync-80x28.jst 80x28 "$J/nvim.steps" -- nvim term.c
python3 "$J/rec.py" tmux-nvim-80x28.jst 80x28 "$J/tmux.steps" -- "$TM" -L jimrec new -s jimrec nvim term.c
out=$(mosh-server new -i 127.0.0.1 -p 60990:60999 -c 256 -- "$TM" -L jimrec2 new -s m nvim "$T/term.c" 2>&1)
port=$(echo "$out" | sed -n 's/MOSH CONNECT \([0-9]*\) .*/\1/p'); key=$(echo "$out" | sed -n 's/MOSH CONNECT [0-9]* \(.*\)/\1/p')
MOSH_KEY=$key python3 "$J/rec.py" mosh-tmux-nvim-80x28.jst 80x28 "$J/tmux.steps" -- mosh-client 127.0.0.1 "$port"
for f in *.jst; do gzip -9c "$f" > "$J/streams/$f.gz"; done
ls -l "$J/streams"
