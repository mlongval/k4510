#!/bin/sh
# tools/get-freedoom.sh -- fetch the game data DOOM on the Tube needs.
#
# No WAD is vendored in this repository.  freedoom1.wad is 28.8 MB and the
# machine layer a deploy carries is 5.9 MB, so shipping it inside every image
# would make every update six times larger for a game that is not why anyone
# turns the machine on.  It is one command away instead, and .gitignore keeps
# the download out of git.
#
# Freedoom (https://freedoom.github.io/) is BSD-3-Clause: it may be
# redistributed, which matters for a machine handed out as an image, and
# unlike the shareware DOOM1.WAD its terms are plain.  Any IWAD the engine
# accepts will do -- put your own in fs/APPS/DOOM/ and name it to DOOM.
#
#   tools/get-freedoom.sh          fetch into fs/APPS/DOOM/
#   tools/get-freedoom.sh --both   freedoom2.wad as well
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
DEST="$HERE/fs/APPS/DOOM"
VER=0.13.0
URL="https://github.com/freedoom/freedoom/releases/download/v$VER/freedoom-$VER.zip"

mkdir -p "$DEST"
if [ -s "$DEST/freedoom1.wad" ] && [ "$1" != "--force" ]; then
    echo "get-freedoom: $DEST/freedoom1.wad is already here (--force to fetch again)"
    exit 0
fi

command -v curl >/dev/null || { echo "get-freedoom: needs curl"; exit 1; }
command -v unzip >/dev/null || { echo "get-freedoom: needs unzip"; exit 1; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
echo "== fetching Freedoom $VER (24 MB) =="
curl -fsSL -o "$TMP/freedoom.zip" "$URL"

echo "== unpacking =="
unzip -q -o "$TMP/freedoom.zip" -d "$TMP"
cp "$TMP/freedoom-$VER/freedoom1.wad" "$DEST/"
[ "$1" = "--both" ] && cp "$TMP/freedoom-$VER/freedoom2.wad" "$DEST/"
# the licence travels with the data, as BSD-3 asks
cp "$TMP/freedoom-$VER/COPYING.txt" "$DEST/COPYING.TXT"
cp "$TMP/freedoom-$VER/CREDITS.txt" "$DEST/CREDITS.TXT" 2>/dev/null || true

ls -l "$DEST"
echo
echo "get-freedoom: done.  Type DOOM at the machine's prompt."
