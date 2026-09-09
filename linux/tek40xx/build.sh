#!/bin/sh
# Build Tek40xx from its upstream and install it as /usr/local/bin/tek40xx.
# Run inside the K4510 Linux (chroot or container) as root or with sudo.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
W=$(mktemp -d)
git clone -q --depth 1 https://github.com/Isysxp/Tek40xx.git "$W/Tek40xx"
cd "$W/Tek40xx"
patch -p1 < "$HERE/fit-the-panel.patch"
cd Tek40xx && make -s tek4010
install -m 755 tek4010 /usr/local/bin/tek40xx
rm -rf "$W"
echo "tek40xx: installed"
