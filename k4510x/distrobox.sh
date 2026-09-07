#!/bin/sh
# K4510x as a distrobox: the third flavour.
#
#   build-image.sh   a stick with a writable root
#   build-live.sh    a stick that loads to RAM (the appliance on the T480)
#   distrobox.sh     the same Debian, as a container on your own desktop --
#                    the display, sound, keyboard, mouse and pads come through
#                    from the host, `!cmd` at the machine's prompt runs in the
#                    container, `!sudo apt install` works and STICKS, and none
#                    of it can touch the host.  Doc, 2026-09-07: "that way it
#                    won't bork my system, but will let me add programs".
#
# The container gets ITS OWN HOME ($HOME_DIR), not yours: distrobox shares
# the real home by default, and a `!rm -rf ~` from inside would then reach
# it.  That home is a plain directory on the host, so the repository is
# cloned into it FROM THE HOST SIDE (git on the host, this checkout as its
# origin) and built inside; `update` pulls the same way.  (A --volume bind
# mount was tried first and did not appear inside a rootless podman box.)
#
#   k4510x/distrobox.sh              create (or refresh) the box and build in it
#   k4510x/distrobox.sh run          start the machine
#   k4510x/distrobox.sh shell        a shell in the box
#   k4510x/distrobox.sh update       pull this checkout's HEAD into the box and rebuild
#   k4510x/distrobox.sh rm           delete the box (the home directory stays)
#
# Needs distrobox and podman (or docker) on the host.  distrobox installs
# per-user:  curl -s https://raw.githubusercontent.com/89luca89/distrobox/main/install | sh -s -- --prefix ~/.local
set -e
NAME=${NAME:-k4510x}
IMAGE=${IMAGE:-docker.io/library/debian:trixie}
HOME_DIR=${HOME_DIR:-$HOME/k4510x-home}
HERE=$(cd "$(dirname "$0")" && pwd); REPO=$(cd "$HERE/.." && pwd)
export PATH="$HOME/.local/bin:$PATH"
# distrobox itself, per-user, if this host has none; podman or docker must be there already
if ! command -v distrobox >/dev/null 2>&1; then
    if ! command -v podman >/dev/null 2>&1 && ! command -v docker >/dev/null 2>&1; then
        echo "distrobox.sh: this host has neither podman nor docker; install podman first (dnf/apt install podman)"; exit 1
    fi
    echo "== installing distrobox under ~/.local (no root needed) =="
    curl -sL https://raw.githubusercontent.com/89luca89/distrobox/main/install -o /tmp/distrobox-install.$$ \
        && sh /tmp/distrobox-install.$$ --prefix "$HOME/.local" >/dev/null && rm -f /tmp/distrobox-install.$$ \
        || { echo "distrobox.sh: could not install distrobox"; exit 1; }
    command -v distrobox >/dev/null || { echo "distrobox.sh: distrobox still not on PATH"; exit 1; }
fi
inbox() { distrobox enter "$NAME" -- "$@"; }
case "${1:-create}" in
run)   exec distrobox enter "$NAME" -- sh -c 'cd ~/k4510 && exec ./sdl/k4510 --host-shell' ;;
shell) exec distrobox enter "$NAME" ;;
rm)    exec distrobox rm --force "$NAME" ;;
update)
    git -C "$HOME_DIR/k4510" fetch -q origin && git -C "$HOME_DIR/k4510" merge -q --ff-only origin/master
    inbox sh -c 'cd ~/k4510 && find core sdl -name "*.d" -delete && make -j"$(nproc)" sdl/k4510 rom/kernal.bin rom/wozmon.bin rom/demo.bin cpm/runcpm && (make -C tube || echo "the Tube did not build; everything else did")'
    exit 0 ;;
create) ;;
*) echo "distrobox.sh [create|run|shell|update|rm]"; exit 1 ;;
esac

mkdir -p "$HOME_DIR"
if ! distrobox list 2>/dev/null | grep -q "| $NAME "; then
    echo "== creating $NAME from $IMAGE, home $HOME_DIR =="
    distrobox create --yes --name "$NAME" --image "$IMAGE" --home "$HOME_DIR"
fi
echo "== packages =="
# the stick's list minus what only a bootable machine needs (kernel, firmware, the network stack, telnetd)
PKGS=$(sed -e 's/#.*//' -e '/^[[:space:]]*$/d' "$HERE/config/package-lists/k4510x-live.list.chroot" \
       | grep -v -e '^firmware-' -e '^network-manager' -e '^wpasupplicant' -e 'telnetd' -e '^iproute2' -e '^iputils' | tr '\n' ' ')
inbox sudo -n apt-get update -q
inbox sudo -n env DEBIAN_FRONTEND=noninteractive apt-get install -y -q --no-install-recommends $PKGS
echo "== the machine =="
if [ -d "$HOME_DIR/k4510/.git" ]; then git -C "$HOME_DIR/k4510" fetch -q origin && git -C "$HOME_DIR/k4510" merge -q --ff-only origin/master
else git clone -q "$REPO" "$HOME_DIR/k4510"; fi
inbox sh -c 'cd ~/k4510 && find core sdl -name "*.d" -delete; make -j"$(nproc)" sdl/k4510 rom/kernal.bin rom/wozmon.bin rom/demo.bin cpm/runcpm && (make -C tube || echo "the Tube did not build; everything else did")'
inbox sh -c 'sudo -n true && echo "passwordless sudo inside the box: yes"'
# a launcher on the host desktop
mkdir -p "$HOME/.local/share/applications"
cat > "$HOME/.local/share/applications/k4510x-box.desktop" <<DESK
[Desktop Entry]
Type=Application
Name=K4510x (container)
Comment=The K4510, with a Debian of its own beside it
Exec=$HERE/distrobox.sh run
Terminal=false
Categories=Game;Emulator;
DESK
echo
echo "distrobox.sh: ready.  Start it with:  $HERE/distrobox.sh run"
echo "Inside the machine, \`!\` is the box's shell and \`!sudo apt install ...\` sticks."
