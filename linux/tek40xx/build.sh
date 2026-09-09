#!/bin/sh
# Build Tek40xx from its upstream and install it: /usr/local/bin/tek40xx as
# root (the stick, the container), ~/.local/bin/tek40xx as yourself (a desktop
# checkout -- no sudo needed).  Needs gcc, make, git, patch and the SDL2
# headers (libsdl2-dev / SDL2-devel).
set -e
if [ "$(id -u)" = 0 ]; then DEST=/usr/local/bin; else DEST=$HOME/.local/bin; mkdir -p "$DEST"; fi
command -v sdl2-config >/dev/null 2>&1 || { echo "tek40xx: no SDL2 headers (apt install libsdl2-dev / dnf install SDL2-devel)" >&2; exit 1; }
HERE=$(cd "$(dirname "$0")" && pwd)
W=$(mktemp -d)
git clone -q --depth 1 https://github.com/Isysxp/Tek40xx.git "$W/Tek40xx"
cd "$W/Tek40xx"
patch -p1 < "$HERE/fit-the-panel.patch"
patch -p1 < "$HERE/special-point-plot.patch"
cd Tek40xx && make -s tek4010
install -m 755 tek4010 "$DEST/tek40xx"
rm -rf "$W"
echo "tek40xx: installed in $DEST"
