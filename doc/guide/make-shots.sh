#!/bin/sh
# Every screenshot in the guide is captured from the running machine, here.
# Needs test/capture built and rom/ + fs/ present (built or copied from the
# machine that builds them). A shot that cannot be produced fails the build.
set -e
HERE=$(cd "$(dirname "$0")" && pwd); REPO=$(cd "$HERE/../.." && pwd)
cd "$REPO"
mkdir -p "$HERE/shots"
# the Tube chapters photograph the co-processors: they must exist, or the
# shots quietly show the wrong machine (it happened)
make -s cpm/runcpm
[ -x tube/bbcbasic ] || { echo "tube/bbcbasic missing: make -C tube (needs nasm) or copy the binary"; exit 1; }
# SHOTS=all recaptures everything; anything else (the default) keeps a shot
# that is already there and takes only the missing ones. The pictures are the
# slow part of the book's build, and they only change when the machine's
# screens do -- so make-guide.sh --shots is the deliberate way to refresh them.
kept=0
shot() { # name frames keys
    if [ "${SHOTS:-missing}" != all ] && [ -s "$HERE/shots/$1.png" ]; then
        kept=$((kept + 1)); return
    fi
    test/capture rom/kernal.bin "$2" "$HERE/shots/$1.png" "$3" >/dev/null
    [ -s "$HERE/shots/$1.png" ] || { echo "shot $1 FAILED"; exit 1; }
}
shot boot 40 ""
# A ~ before each command waits 30 frames, so the shell has time to run the
# line before the next key arrives.  Without them the keys go one per frame
# and the picture catches the machine mid-word: "dir" typed, Enter not yet
# processed, no listing at all (2026-09-16, three captures wasted on it --
# test/dirtest.sh had the right form all along, and test/headless shows the
# text screen in a second where a PNG has to be squinted at).
shot dir 200 "cd /
~dir
~~"
shot mon 120 "mon
ff80.ff8f
"
shot demos 900 "run ehbasic

RUN \"DEMOS.BAS\"
"
shot invaders 1400 "run ehbasic

RUN \"INVADER2.BAS\"
"
shot split 300 "SPLIT
"
shot msbasic 500 "msbasic
10 FOR I=1 TO 6
20 PRINT I, I*I, SQR(I)
30 NEXT
RUN
"
shot logo 1500 "logo
REPEAT 36 [RT 10 REPEAT 4 [FD 90 RT 90]]
"
shot forth 240 "forth
2 3 + .
: cube dup dup * * ;
7 cube .
' dup 10 disasm
"
shot bbc 800 "cd /LANG/BBCBASIC
bbc
LOAD \"EX/KALEID.BBC\"
RUN
"
shot cpm 500 "cpm
dir
type readme.txt
"
shot turbo 1500 "CPM
~~~H:
~~USER 3
~~TURBO
~~~~Y
~~~~"
shot wordstar 4200 "CPM
~~~E:
~~USER 3
~~WS
~~~~~~~~~~~~~~D~~~~READ.ME
~~~~~~~~~~~~~~~~"
shot pmandel 1500 "PMANDEL
"
shot pgraph 700 "PGRAPH
"
# the F7 menu is the frontend's, not the machine's: the SDL build with the dummy driver, a PPM converted
# octal, not \xNN: this script is /bin/sh, and dash's printf prints "\x96" literally
# (that is how the menu shot became a picture of the shell rejecting \x96 as a command)
K="$(printf "~~~\226\n\201\201\n~")"   # F7, Down, Down, Enter
# -k: SDL turns SIGTERM into an SDL_QUIT *event*, so a wedged main loop never
# acts on it; plain `timeout` then waits for a process that will never die
timeout -k 5 120 env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy K4510_KEYS="$K" K4510_SHOT="$HERE/shots/menu.ppm:200" sdl/k4510 >/dev/null 2>&1 || true
[ -s "$HERE/shots/menu.ppm" ] && python3 -c "from PIL import Image; Image.open('$HERE/shots/menu.ppm').save('$HERE/shots/menu.png')" && rm -f "$HERE/shots/menu.ppm"
[ -s "$HERE/shots/menu.png" ] || { echo "shot menu FAILED"; exit 1; }
if [ "$kept" -gt 0 ]; then
    echo "shots: $kept kept, the rest captured -- make-guide.sh --shots to recapture all"
    # A picture older than the machine it shows is the one failure mode of
    # keeping shots, so say when that is true rather than let it pass.
    for f in "$HERE"/shots/*.png; do
        if [ "$REPO/rom/kernal.bin" -nt "$f" ]; then
            echo "shots: NOTE -- rom/kernal.bin is newer than $(basename "$f"): the machine has been rebuilt since that picture"
            break
        fi
    done
    true
else
    echo "shots: done"
fi
