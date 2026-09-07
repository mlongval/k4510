#!/bin/sh
# K4510x as a podman container: the third flavour, and the SANDBOXED one.
#
#   build-image.sh   a stick with a writable root
#   build-live.sh    a stick that loads to RAM (the appliance on the T480)
#   podman.sh        the same Debian as a rootless container on your own
#                    desktop.  The display, sound, keyboard, mouse and pads
#                    come through; `!cmd` at the machine's prompt runs in the
#                    container; `!sudo apt install` works and STICKS (the
#                    container is kept, not thrown away); and the container
#                    sees NOTHING of the host but what is listed below.
#                    Doc, 2026-09-07, after distrobox showed him the host's
#                    root: "giving it access to the whole system seems like it
#                    is not the right path".
#
# What crosses the wall, and nothing else:
#   the display socket (Wayland or X11), the sound socket (PipeWire/Pulse),
#   /dev/dri (a fast renderer), /dev/input (gamepads), and ONE folder:
#   $SHARE on the host (~/k4510x-share) is fs/SHARE inside, which is /SHARE at
#   the machine's prompt -- COPY /SHARE/FOO.BAS /PRG/ brings a file in, and the
#   other way sends one out.  No home, no /tmp, no /run/host.
#
#   k4510x/podman.sh              build the image and create the container
#   k4510x/podman.sh run          start the machine
#   k4510x/podman.sh shell        a shell in the container
#   k4510x/podman.sh update       put this checkout's HEAD into the container and rebuild
#   k4510x/podman.sh rm           delete the container (the image and the share folder stay)
#   k4510x/podman.sh rm --all     the image too
#
# Needs podman (rootless).  Fedora: dnf install podman.  Debian/Ubuntu: apt install podman.
set -e
NAME=${NAME:-k4510x}
IMAGE=${IMAGE:-localhost/k4510x:latest}
SHARE=${SHARE:-$HOME/k4510x-share}
HERE=$(cd "$(dirname "$0")" && pwd); REPO=$(cd "$HERE/.." && pwd)
UIDN=$(id -u)
command -v podman >/dev/null 2>&1 || { echo "podman.sh: no podman on this host (dnf/apt install podman)"; exit 1; }

case "${1:-create}" in
run)
    podman container exists "$NAME" 2>/dev/null || { echo "podman.sh: no container yet; run  $0  first"; exit 1; }
    exec podman start -a "$NAME" ;;
shell)
    podman container exists "$NAME" 2>/dev/null || { echo "podman.sh: no container yet; run  $0  first"; exit 1; }
    podman start "$NAME" >/dev/null 2>&1 || true
    exec podman exec -it "$NAME" /bin/bash ;;
update)
    podman container exists "$NAME" 2>/dev/null || { echo "podman.sh: no container yet; run  $0  first"; exit 1; }
    podman start "$NAME" >/dev/null 2>&1 || true
    echo "== this checkout's HEAD into the container =="
    git -C "$REPO" archive --format=tar HEAD | podman exec -i "$NAME" tar -x -C /home/k4510/k4510
    podman exec "$NAME" sh -c 'cd ~/k4510 && find core sdl -name "*.d" -delete; make -j"$(nproc)" ACME=/usr/bin/acme sdl/k4510 rom/kernal.bin rom/wozmon.bin rom/demo.bin cpm/runcpm && (make -C tube || echo "the Tube did not build; everything else did")'
    podman stop "$NAME" >/dev/null 2>&1 || true
    echo "podman.sh: updated"; exit 0 ;;
rm)
    podman rm -f "$NAME" 2>/dev/null || true
    [ "$2" = "--all" ] && podman rmi -f "$IMAGE" 2>/dev/null || true
    rm -f "$HOME/.local/share/applications/k4510x-box.desktop"
    echo "podman.sh: removed $NAME${2:+ and the image}; $SHARE is untouched"; exit 0 ;;
create) ;;
*) echo "podman.sh [create|run|shell|update|rm [--all]]"; exit 1 ;;
esac

# the stick's package list minus what only a bootable machine needs
PKGS=$(sed -e 's/#.*//' -e '/^[[:space:]]*$/d' "$HERE/config/package-lists/k4510x-live.list.chroot" \
       | grep -v -e '^firmware-' -e '^network-manager' -e '^wpasupplicant' -e 'telnetd' -e '^iproute2' -e '^iputils' | tr '\n' ' ')
echo "== building $IMAGE from this checkout (a few minutes the first time) =="
podman build -q -t "$IMAGE" --build-arg UID="$UIDN" --build-arg PKGS="$PKGS" -f "$HERE/Containerfile" "$REPO"

mkdir -p "$SHARE"
podman rm -f "$NAME" >/dev/null 2>&1 || true
RUNDIR=${XDG_RUNTIME_DIR:-/run/user/$UIDN}
set -- --name "$NAME" --userns=keep-id:uid="$UIDN",gid="$(id -g)" --user "$UIDN:$(id -g)" --group-add keep-groups \
       --security-opt label=disable --hostname k4510x \
       -e HOME=/home/k4510 -e SHELL=/bin/bash -e XDG_RUNTIME_DIR=/run/user/"$UIDN" \
       -v "$SHARE:/home/k4510/k4510/fs/SHARE"
# the display: Wayland if there is one, and X11 alongside if there is one
if [ -n "$WAYLAND_DISPLAY" ] && [ -S "$RUNDIR/$WAYLAND_DISPLAY" ]; then
    set -- "$@" -v "$RUNDIR/$WAYLAND_DISPLAY:/run/user/$UIDN/$WAYLAND_DISPLAY" -e WAYLAND_DISPLAY="$WAYLAND_DISPLAY" -e SDL_VIDEODRIVER=wayland
fi
if [ -n "$DISPLAY" ] && [ -d /tmp/.X11-unix ]; then
    set -- "$@" -v /tmp/.X11-unix:/tmp/.X11-unix -e DISPLAY="$DISPLAY" --ipc=host
    [ -n "$XAUTHORITY" ] && [ -f "$XAUTHORITY" ] && set -- "$@" -v "$XAUTHORITY:/home/k4510/.Xauthority:ro" -e XAUTHORITY=/home/k4510/.Xauthority
fi
# the sound: PipeWire's socket, and Pulse's (PipeWire answers on it too)
[ -S "$RUNDIR/pipewire-0" ] && set -- "$@" -v "$RUNDIR/pipewire-0:/run/user/$UIDN/pipewire-0"
[ -S "$RUNDIR/pulse/native" ] && set -- "$@" -v "$RUNDIR/pulse:/run/user/$UIDN/pulse" -e PULSE_SERVER="unix:/run/user/$UIDN/pulse/native"
# the hardware a game wants
[ -d /dev/dri ] && set -- "$@" --device /dev/dri
[ -d /dev/input ] && set -- "$@" --device /dev/input
podman create "$@" "$IMAGE" >/dev/null
# your settings, if this checkout has some (the status bands stay on either way)
if [ -f "$REPO/k4510.cfg" ]; then
    podman cp "$REPO/k4510.cfg" "$NAME:/home/k4510/k4510/k4510.cfg"
    grep -q '^term.bands' "$REPO/k4510.cfg" || podman start "$NAME" >/dev/null && podman exec "$NAME" sh -c 'grep -q "^term.bands" ~/k4510/k4510.cfg || echo "term.bands = on" >> ~/k4510/k4510.cfg' && podman stop "$NAME" >/dev/null
fi
mkdir -p "$HOME/.local/share/applications"
cat > "$HOME/.local/share/applications/k4510x-box.desktop" <<DESK
[Desktop Entry]
Type=Application
Name=K4510x (container)
Comment=The K4510 with a sandboxed Debian beside it
Exec=$HERE/podman.sh run
Terminal=false
Categories=Game;Emulator;
DESK
echo
echo "podman.sh: ready.  Start it with:  $HERE/podman.sh run"
echo "The share folder is $SHARE -- it is /SHARE at the machine's prompt."
echo "Inside the machine, \`!\` is the container's shell; \`!sudo apt install ...\` sticks."
