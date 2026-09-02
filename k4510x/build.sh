#!/bin/sh
# K4510x: build the installable image.  live-build produces an ISO that is
# dd-able to a USB stick, boots on BIOS and UEFI, runs the machine live, and
# carries the Debian installer for putting it on internal storage.
#
#   sudo apt install live-build
#   ./build.sh
#
# UNTESTED as of 2026-09-02: written and reviewed, never built, never run on
# a t520.  Until it has been, this is a recipe and not a distribution.
set -e
cd "$(dirname "$0")"
command -v lb >/dev/null || { echo "build.sh: live-build is not installed (apt install live-build)"; exit 1; }

# The machine's own tree, copied in for the chroot hook to build.
mkdir -p config/includes.chroot/home/k4510
if [ ! -d config/includes.chroot/home/k4510/k4510 ]; then
    echo "build.sh: staging the source tree"
    git -C .. archive --format=tar HEAD | \
        (mkdir -p config/includes.chroot/home/k4510/k4510 && \
         tar -x -C config/includes.chroot/home/k4510/k4510)
fi

lb config \
    --architecture amd64 \
    --distribution stable \
    --archive-areas "main contrib non-free-firmware" \
    --debian-installer live \
    --debian-installer-gui false \
    --bootappend-live "boot=live components quiet" \
    --iso-application "K4510x" \
    --iso-volume "K4510x" \
    --memtest none

lb build
echo
echo "build.sh: done.  Write it with:  sudo dd if=live-image-amd64.hybrid.iso of=/dev/sdX bs=4M status=progress conv=fsync"
