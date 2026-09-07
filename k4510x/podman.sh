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
#   k4510x/podman.sh update       put this checkout's HEAD into the container and rebuild there
#                                 (the kept container only: apt installs survive; the IMAGE
#                                 is untouched -- run  podman.sh  again to rebuild it too)
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

RUNDIR=${XDG_RUNTIME_DIR:-/run/user/$UIDN}
# The display and sound sockets are found ON DISK, not in this shell's
# environment, so a container created over ssh still gets the desktop's screen:
# the sockets exist whether or not this shell knows their names.
WL=$(ls "$RUNDIR"/wayland-[0-9]* 2>/dev/null | grep -v '\.lock$' | head -1)
WLNAME=${WL##*/}
X11=""; [ -S /tmp/.X11-unix/X0 ] && X11=":0"
XAUTH=$(ls "$RUNDIR"/.mutter-Xwaylandauth.* 2>/dev/null | head -1); [ -z "$XAUTH" ] && [ -n "$XAUTHORITY" ] && [ -f "$XAUTHORITY" ] && XAUTH=$XAUTHORITY
have_display() { [ -n "$WL" ] || [ -n "$X11" ]; }
# what the machine is started with, at exec time (so a newer session's names apply)
machine_env() {
    set -- -e HOME=/home/k4510 -e SHELL=/bin/bash -e XDG_RUNTIME_DIR=/run/user/"$UIDN"
    [ -n "$WL" ] && set -- "$@" -e WAYLAND_DISPLAY="$WLNAME" -e SDL_VIDEODRIVER=wayland
    [ -n "$X11" ] && set -- "$@" -e DISPLAY="$X11"
    [ -n "$XAUTH" ] && set -- "$@" -e XAUTHORITY=/home/k4510/.Xauthority
    [ -S "$RUNDIR/pulse/native" ] && set -- "$@" -e PULSE_SERVER="unix:/run/user/$UIDN/pulse/native"
    printf "%s " "$@"   # not echo: echo eats the leading -e
}
make_container() {
    # the container: idle (sleep) so the machine can be exec'd into it with today's
    # display, and so update/shell work whether or not there is a screen.
    # What it sees of the host is exactly this list of mounts.
    mkdir -p "$SHARE"
    podman rm -f "$NAME" >/dev/null 2>&1 || true
    set -- --name "$NAME" --userns=keep-id:uid="$UIDN",gid="$(id -g)" --user "$UIDN:$(id -g)" --group-add keep-groups \
           --security-opt label=disable --hostname k4510x --entrypoint /bin/sleep \
           -e HOME=/home/k4510 -e SHELL=/bin/bash \
           -v "$SHARE:/home/k4510/k4510/fs/SHARE"
    [ -n "$WL" ] && set -- "$@" -v "$WL:/run/user/$UIDN/$WLNAME"
    [ -n "$X11" ] && set -- "$@" -v /tmp/.X11-unix:/tmp/.X11-unix --ipc=host
    [ -n "$XAUTH" ] && set -- "$@" -v "$XAUTH:/home/k4510/.Xauthority:ro"
    [ -S "$RUNDIR/pipewire-0" ] && set -- "$@" -v "$RUNDIR/pipewire-0:/run/user/$UIDN/pipewire-0"
    [ -S "$RUNDIR/pulse/native" ] && set -- "$@" -v "$RUNDIR/pulse:/run/user/$UIDN/pulse"
    [ -d /dev/dri ] && set -- "$@" --device /dev/dri
    [ -d /dev/input ] && set -- "$@" --device /dev/input
    podman create "$@" "$IMAGE" infinity >/dev/null
    podman start "$NAME" >/dev/null
    # your settings, if this checkout has some (the status bands stay on either way)
    if [ -f "$REPO/k4510.cfg" ]; then podman cp "$REPO/k4510.cfg" "$NAME:/home/k4510/k4510/k4510.cfg"; fi
    podman exec "$NAME" sh -c 'grep -q "^term.bands" ~/k4510/k4510.cfg || echo "term.bands = on" >> ~/k4510/k4510.cfg'
    podman stop -t 1 "$NAME" >/dev/null
    if have_display; then echo "container created with a screen ($([ -n "$WL" ] && echo "Wayland $WLNAME" || echo "X11 $X11"))"
    else echo "container created WITHOUT a screen: no display socket on this host; run  $0  again from a desktop"; fi
}
container_has_display() { podman inspect "$NAME" --format '{{.HostConfig.Binds}}' 2>/dev/null | grep -q -e wayland -e X11-unix; }
up() { podman start "$NAME" >/dev/null 2>&1 || true; }

case "${1:-create}" in
run)
    podman image exists "$IMAGE" 2>/dev/null || { echo "podman.sh: no image yet; run  $0  first"; exit 1; }
    if ! podman container exists "$NAME" 2>/dev/null; then make_container
    elif have_display && ! container_has_display; then echo "podman.sh: this container has no screen; recreating it with this host's display"; make_container
    elif [ "$(podman inspect "$NAME" --format '{{.Config.Entrypoint}}')" != "[/bin/sleep]" ]; then echo "podman.sh: an older container layout; recreating"; make_container
    fi
    have_display || echo "podman.sh: no display socket on this host -- the machine will have no window"
    up
    podman exec -it $(machine_env) "$NAME" sh -c 'cd ~/k4510 && exec ./sdl/k4510 --host-shell'
    podman stop -t 1 "$NAME" >/dev/null 2>&1; exit 0 ;;
shell)
    podman container exists "$NAME" 2>/dev/null || { echo "podman.sh: no container yet; run  $0  first"; exit 1; }
    up; exec podman exec -it $(machine_env) "$NAME" /bin/bash ;;
update)
    podman container exists "$NAME" 2>/dev/null || { echo "podman.sh: no container yet; run  $0  first"; exit 1; }
    up
    echo "== this checkout's HEAD into the container =="
    git -C "$REPO" archive --format=tar HEAD | podman exec -i "$NAME" tar -x -C /home/k4510/k4510
    podman exec "$NAME" sh -c 'cd ~/k4510 && find core sdl -name "*.d" -delete; make -j"$(nproc)" ACME=/usr/bin/acme sdl/k4510 rom/kernal.bin rom/wozmon.bin rom/demo.bin cpm/runcpm && (make -C tube || echo "the Tube did not build; everything else did")'
    podman stop -t 1 "$NAME" >/dev/null 2>&1 || true
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

make_container
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
