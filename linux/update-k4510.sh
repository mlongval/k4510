#!/bin/sh
# update-k4510.sh -- refresh the installed K4510 image IN PLACE.  It replaces
# only the live payload (the squashfs, and the kernel/initrd if they changed)
# on the internal K4510 partition.  Your saved settings (persistence) and the
# GRUB entry are left exactly as they are -- no reformat, no bootloader work.
#
#   sudo ./update-k4510.sh              # newest payload: a plugged-in stick if
#                                       # there is one, else pull from p15
#   sudo ./update-k4510.sh --rebuild    # rebuild on p15 FIRST (picks up code
#                                       # changes), then pull and apply
#   sudo ./update-k4510.sh --stick      # force the USB stick as the source
#   sudo SRC_HOST=p15 ./update-k4510.sh # override the build host
#
# After it finishes, reboot and choose K4510 to run the new build.
set -e

DST_LABEL=${DST_LABEL:-K4510}
SRC_HOST=${SRC_HOST:-p15}
# where build-live.sh leaves the freshly-squashed payload on the build host
# The INTERNAL variant (built with NODISK= into .live-work-internal): unlike the
# stick, it can see the NVMe it lives on.  Never point this at the stick's
# .live-work -- that payload bans the internal disk drivers (2026-09-11).
STAGE=${STAGE:-Projects/k4510-pi/k4510/linux/.live-work-internal/stage/live}
CHECKOUT=${CHECKOUT:-Projects/k4510-pi/k4510}
USER=${SUDO_USER:-$(id -un)}

MODE=auto; REBUILD=0
for a in "$@"; do case "$a" in
    --stick) MODE=stick ;;
    --net|--network) MODE=net ;;
    --rebuild) REBUILD=1; MODE=net ;;
    *) echo "usage: sudo $0 [--stick|--rebuild]"; exit 2 ;;
esac; done

say() { printf '\n== %s ==\n' "$*"; }
die() { printf 'update-k4510: %s\n' "$*" >&2; exit 1; }
[ "$(id -u)" = 0 ] || die "run me with sudo"

# --- the target: the installed K4510 partition -----------------------------
DST="/dev/disk/by-label/$DST_LABEL"
[ -b "$DST" ] || die "no '$DST_LABEL' partition here -- run install-k4510.sh first."
DSTDEV=$(readlink -f "$DST")
[ "$(blkid -o value -s TYPE "$DSTDEV")" = ext4 ] || die "$DSTDEV is not the ext4 K4510 install -- run install-k4510.sh first."

# --- pick a source ---------------------------------------------------------
STICK="/dev/disk/by-label/k4510-live"
if [ "$MODE" = auto ]; then [ -b "$STICK" ] && MODE=stick || MODE=net; fi

SMP=""; TMP=""
cleanup() { [ -n "$SMP" ] && umount "$SMP" 2>/dev/null; [ -n "$DMP" ] && umount "$DMP" 2>/dev/null; rm -rf "$SMP" 2>/dev/null; [ -n "$DMP" ] && rmdir "$DMP" 2>/dev/null; return 0; }
trap cleanup EXIT

if [ "$MODE" = stick ]; then
    [ -b "$STICK" ] || die "no K4510 stick plugged in (label k4510-live)."
    SMP=$(mktemp -d); mount -o ro "$STICK" "$SMP"; SRCDIR="$SMP/live"
    say "source: the USB stick ($STICK)"
else
    command -v rsync >/dev/null 2>&1 || die "rsync not installed."
    if [ "$REBUILD" = 1 ]; then
        say "rebuilding the machine on $SRC_HOST (REBUILD=1; a couple of minutes)"
        sudo -u "$USER" ssh -o BatchMode=yes -o ConnectTimeout=8 "$SRC_HOST" \
            "cd ~/$CHECKOUT && git fetch origin -q && git merge --ff-only origin/master >/dev/null 2>&1; sudo env NODISK= WORK=\$PWD/linux/.live-work-internal OUT=\$PWD/linux/k4510-internal-amd64.img REBUILD=1 sh linux/build-live.sh" \
            || die "the remote rebuild failed on $SRC_HOST"
    fi
    # A persistent cache, not a temp dir: rsync then moves only the files that
    # changed on p15 (the 5 MB layer, usually) instead of the whole 810 MB every
    # time (2026-09-11 evening).  The fetch runs as $USER, so it must own it.
    TMP=/var/cache/k4510-live; mkdir -p "$TMP"; chown "$USER" "$TMP"
    say "pulling the payload from $SRC_HOST over the tailnet"
    # --partial: a 750 MB pull over the tailnet takes minutes; if it is cut off,
    # the next run resumes the file instead of starting over (2026-09-11).
    sudo -u "$USER" rsync -a --partial --info=progress2 -e "ssh -o BatchMode=yes -o ConnectTimeout=8" \
        --rsync-path="sudo rsync" "$SRC_HOST:$STAGE/" "$TMP/" \
        || die "could not fetch from $SRC_HOST (ssh key / reachability?)."
    SRCDIR="$TMP"
fi
[ -f "$SRCDIR/filesystem.squashfs" ] || die "source has no filesystem.squashfs."

# --- apply: only /live, nothing else ---------------------------------------
DMP=$(mktemp -d)
umount "$DSTDEV" 2>/dev/null || true
mount "$DSTDEV" "$DMP"
OLD=$(cat "$DMP"/live/*.squashfs 2>/dev/null | sha256sum | cut -c1-12)
say "applying to $DSTDEV:/live  (persistence.conf and GRUB untouched)"
mkdir -p "$DMP/live"
rsync -a --info=progress2 --delete "$SRCDIR"/*.squashfs "$SRCDIR/"vmlinuz "$SRCDIR/"initrd.img "$DMP/live/"   # every layer: base + k4510.squashfs (rsync moves only what changed)
NEW=$(cat "$DMP"/live/*.squashfs | sha256sum | cut -c1-12)
sync

say "done"
if [ "$OLD" = "$NEW" ]; then echo "the payload was already current ($NEW) -- nothing changed."
else echo "payload updated: $OLD -> $NEW.  Reboot and choose K4510 to run it."; fi
