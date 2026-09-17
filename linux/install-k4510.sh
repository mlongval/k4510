#!/bin/sh
# install-k4510.sh -- install the K4510 live system from the USB stick onto an
# internal partition of THIS machine, and add it to GRUB as a second boot
# choice beside the existing OS.  Run under the machine's main Linux, as root.
#
#   sudo ./install-k4510.sh
#
# What it does:
#   1. finds the K4510 USB stick (partition labelled k4510-live)
#   2. finds the internal target partition (labelled K4510) and, the FIRST
#      time, formats it ext4 (a live payload + persistence want ext4, not FAT)
#   3. carves 4 GB off the end of it for a second partition, K4510LIVE, and
#      copies the live payload (kernel, initrd, the two squashfs) onto THAT
#   4. writes persistence.conf on K4510, so the machine SAVES your settings
#      and saved work to the disk
#   5. adds a "K4510" entry to this machine's GRUB (your OS stays the default)
#
# Two partitions, since 2026-09-17, because the machine boots `toram` and
# live-boot's toram copies the WHOLE medium into RAM.  With one partition the
# medium was also the persistence, and everything ever saved -- 2.2 GB on the
# Dell, of which 0.9 GB was the system -- went into memory at every boot
# (docs/STORAGE.md).  K4510LIVE is what is copied; K4510 stays on the disk.
# An install made before that date is converted the next time this is run:
# its filesystem is shrunk, offline, with nothing of yours touched.
# K4510_ONE_PARTITION=1 keeps the old shape; so does a K4510 partition too
# small to split (under 8 GB), with a note saying so.
#
# Re-run it any time to refresh the payload from a newer stick: it will NOT
# reformat once the install is there, so your persisted settings are kept.
#
# Safety: it refuses to touch a USB/removable target, and refuses to write to
# the same disk the stick is on -- it only ever formats the internal K4510
# partition.
set -e

SRC_LABEL=${SRC_LABEL:-k4510-live}     # the stick's live partition
DST_LABEL=${DST_LABEL:-K4510}          # the internal partition: persistence, /DISK, everything saved
LIVE_LABEL=${LIVE_LABEL:-K4510LIVE}    # carved out of it: /live, the only thing toram copies into RAM
LIVE_MB=${LIVE_MB:-4096}               # 0.9 GB of system today; room for a base twice that and a rollback beside it
# The kernel command line for an INTERNAL install.  Unlike the stick's, it does
# NOT blacklist the disk drivers (the machine now lives on an internal disk and
# must be able to read it), and it pins persistence to the K4510 partition by
# label, so the stick's own "persistence" partition is never mistaken for it.
CMDLINE=""   # built once the target device is known (needs live-media=DEV)

say() { printf '\n== %s ==\n' "$*"; }
die() { printf 'install-k4510: %s\n' "$*" >&2; exit 1; }

[ "$(id -u)" = 0 ] || die "run me with sudo"
# For test/install-rehearsal.sh, which runs this against loop devices: write the
# GRUB fragment somewhere harmless and do not regenerate the host's grub.cfg.
GRUB_D=${K4510_GRUB_D:-/etc/grub.d}
MKCFG=$(command -v grub2-mkconfig || command -v grub-mkconfig || true)
[ -n "$MKCFG" ] || [ -n "${K4510_GRUB_D:-}" ] || die "missing tool: grub2-mkconfig or grub-mkconfig"
for t in blkid lsblk mkfs.ext4 rsync sfdisk partx e2fsck resize2fs; do
    command -v "$t" >/dev/null 2>&1 || die "missing tool: $t"
done

# --- source: the USB stick -------------------------------------------------
SRC="/dev/disk/by-label/$SRC_LABEL"
[ -b "$SRC" ] || die "no K4510 stick found (no partition labelled '$SRC_LABEL'). Plug the stick in and try again."
SRCDEV=$(readlink -f "$SRC")
SRCDISK=$(lsblk -no PKNAME "$SRCDEV" | head -1)
say "source stick: $SRCDEV  (on /dev/$SRCDISK)"

# --- target: the internal partition ----------------------------------------
DST="/dev/disk/by-label/$DST_LABEL"
[ -b "$DST" ] || die "no target partition labelled '$DST_LABEL' on this machine."
DSTDEV=$(readlink -f "$DST")
# live-boot finds a removable stick on its own, but on a FIXED disk it must be
# pointed at the partition, or it dies with "Unable to find a medium containing
# a live file system" (the 2026-09-11 first-boot crash on the Dell). Pin it.
# No live-media-timeout: despite the name it is a MINIMUM wait (live-boot will
# not look before N one-second loops), 10 s lost every boot.  quickusbmodules
# skips the up-to-5 s sleep for USB disks before the persistence search -- an
# internal install never boots from one.  Together ~15 s (2026-09-12).
# (CMDLINE itself is built further down, once it is known which partition /live is on.)
DSTDISK=$(lsblk -no PKNAME "$DSTDEV" | head -1)
[ "$DSTDISK" = "$SRCDISK" ] && die "the '$DST_LABEL' partition is on the stick itself -- refusing. Make the internal partition first."
TRAN=$(lsblk -no TRAN "/dev/$DSTDISK" | head -1)
RM=$(lsblk -no RM "$DSTDEV" | head -1)
[ "$TRAN" = "usb" ] && die "target /dev/$DSTDISK is USB -- refusing (this must be the internal disk)."
[ "$RM" = "1" ] && die "target $DSTDEV is removable -- refusing."
say "target partition: $DSTDEV  (internal disk /dev/$DSTDISK, transport ${TRAN:-none})"

# --- mount points ----------------------------------------------------------
SMP=$(mktemp -d); DMP=$(mktemp -d)
cleanup() { umount "$SMP" 2>/dev/null || true; umount "$DMP" 2>/dev/null || true; rmdir "$SMP" "$DMP" 2>/dev/null || true; }
trap cleanup EXIT

mount -o ro "$SRCDEV" "$SMP"
[ -f "$SMP/live/filesystem.squashfs" ] || die "the stick has no /live/filesystem.squashfs -- is this really the K4510 stick?"

# --- format once (never again, to keep your persisted data) ----------------
umount "$DSTDEV" 2>/dev/null || true
FSTYPE=$(blkid -o value -s TYPE "$DSTDEV" 2>/dev/null || echo "")
FRESH=0
[ "$FSTYPE" = "ext4" ] || FRESH=1

# --- the second partition ---------------------------------------------------
# Carve LIVE_MB off the END of the K4510 partition.  On a fresh install there
# is no filesystem to mind.  On an install from before 2026-09-17 there is, so
# it is checked, shrunk to well UNDER the new size, the table is rewritten, and
# it is grown back to fill -- the order k4510-split-live uses on a running
# K4510, rehearsed on a loop device (test/install-rehearsal.sh).  The host's
# own partitions are mounted while this runs, so the kernel cannot re-read the
# whole table: sfdisk is told not to ask, and partx tells it about the two
# partitions that changed.
partname() { case "$1" in *[0-9]) echo "$1p$2" ;; *) echo "$1$2" ;; esac; }
split_target() {
    DISK="/dev/$DSTDISK"; BASE=$(basename "$DSTDEV")
    SECT=$(cat "/sys/class/block/$BASE/size"); START=$(cat "/sys/class/block/$BASE/start")
    NEWSECT=$(( (SECT - LIVE_MB * 2048) / 2048 * 2048 ))
    [ $((SECT / 2048)) -ge 8192 ] || { say "$DSTDEV is under 8 GB: keeping ONE partition (saved files will be copied to RAM at boot)"; return 1; }
    sfdisk -d "$DISK" > "$PT.before" || { say "cannot read the partition table: keeping ONE partition"; return 1; }
    NEWNUM=$(( $(sed -nE "s|^$DISK[p]?([0-9]+) :.*|\1|p" "$PT.before" | sort -n | tail -1) + 1 ))
    LIVEDEV=$(partname "$DISK" "$NEWNUM")
    if grep -q '^label: gpt' "$PT.before"; then NEWLINE="$LIVEDEV : start=$((START + NEWSECT)), size=$((LIVE_MB * 2048)), type=0FC63DAF-8483-4772-8E79-3D69D8477DE4, name=\"$LIVE_LABEL\""
    else NEWLINE="$LIVEDEV : start=$((START + NEWSECT)), size=$((LIVE_MB * 2048)), type=83"; fi
    { sed -E "s|^($DSTDEV : start= *$START, size= *)$SECT,|\1$NEWSECT,|" "$PT.before"; echo "$NEWLINE"; } > "$PT.after"
    grep -q "size= *$NEWSECT," "$PT.after" || { say "could not rewrite the partition table: keeping ONE partition"; return 1; }
    if [ "$FRESH" = 0 ]; then
        USED_MB=$(dumpe2fs -h "$DSTDEV" 2>/dev/null | awk -F: '/^Block count/{b=$2}/^Free blocks/{f=$2}/^Block size/{s=$2}END{printf "%d", (b-f)*s/1048576}')
        MARGIN=$(( NEWSECT / 2048 / 4 )); [ "$MARGIN" -gt 2048 ] && MARGIN=2048
        SHRINK_MB=$(( NEWSECT / 2048 - MARGIN ))
        [ "$USED_MB" -gt 0 ] && [ $((USED_MB * 2)) -lt "$SHRINK_MB" ] || { say "$DSTDEV is too full to shrink safely (${USED_MB} MB used): keeping ONE partition"; return 1; }
        say "converting the install: shrinking $DSTDEV (${USED_MB} MB used) to make room for $LIVE_LABEL"
        e2fsck -f -y "$DSTDEV" >/dev/null; [ $? -le 1 ] || die "e2fsck found trouble on $DSTDEV it could not mend; nothing was changed."
        resize2fs "$DSTDEV" "${SHRINK_MB}M" >/dev/null 2>&1 || die "could not shrink the filesystem on $DSTDEV; the partition table is untouched."
    fi
    sfdisk --force --no-reread "$DISK" < "$PT.after" >/dev/null 2>&1 || { sfdisk --force --no-reread "$DISK" < "$PT.before" >/dev/null 2>&1; die "could not write the partition table; the old one was put back."; }
    partx -u --nr "$(cat "/sys/class/block/$BASE/partition")" "$DISK" 2>/dev/null || true
    partx -a --nr "$NEWNUM" "$DISK" 2>/dev/null || true
    command -v udevadm >/dev/null && udevadm settle
    [ -b "$LIVEDEV" ] && [ "$(cat "/sys/class/block/$BASE/size")" = "$NEWSECT" ] \
        || die "the kernel has not taken the new partition table (restart the computer and run me again; nothing of yours was lost)."
    if [ "$FRESH" = 0 ]; then
        resize2fs "$DSTDEV" >/dev/null 2>&1 || say "note: $DSTDEV's filesystem was not grown back to fill its partition (it is merely smaller)"
        e2fsck -f -y "$DSTDEV" >/dev/null || true
    fi
    mkfs.ext4 -q -F -L "$LIVE_LABEL" "$LIVEDEV" || die "could not format $LIVEDEV"
    return 0
}
PT=$(mktemp)
LIVEDEV=$(readlink -f "/dev/disk/by-label/$LIVE_LABEL" 2>/dev/null || true)
if [ -b "$LIVEDEV" ]; then
    [ "$(lsblk -no PKNAME "$LIVEDEV" | head -1)" = "$DSTDISK" ] || die "a '$LIVE_LABEL' partition exists, but not on /dev/$DSTDISK beside '$DST_LABEL' -- refusing to guess."
    say "$LIVE_LABEL is already there ($LIVEDEV): the payload will be refreshed"
elif [ -n "${K4510_ONE_PARTITION:-}" ] || ! split_target; then
    LIVEDEV=$DSTDEV                        # one partition, the old shape
fi
rm -f "$PT" "$PT.before" "$PT.after"

if [ "$FRESH" = 1 ]; then
    say "formatting $DSTDEV as ext4 (label $DST_LABEL) -- first install"
    mkfs.ext4 -q -F -L "$DST_LABEL" "$DSTDEV"
else
    say "$DSTDEV is already ext4 -- keeping it (payload will be refreshed, settings preserved)"
fi
mount "$DSTDEV" "$DMP"
if [ "$LIVEDEV" = "$DSTDEV" ]; then LMP=$DMP; LIVE_SEARCH=$DST_LABEL
else LMP=$(mktemp -d); mount "$LIVEDEV" "$LMP"; LIVE_SEARCH=$LIVE_LABEL
     trap 'umount "$LMP" 2>/dev/null || true; rmdir "$LMP" 2>/dev/null || true; cleanup' EXIT; fi
CMDLINE="boot=live components live-media-path=/live live-media=$LIVEDEV toram union=overlay quickusbmodules persistence persistence-label=$DST_LABEL persistence-storage=filesystem"

# --- copy the live payload -------------------------------------------------
say "copying the live payload to $LIVEDEV (this is the ~750 MB squashfs; a minute or two)"
mkdir -p "$LMP/live"
# --delete: live-boot unions EVERY *.squashfs in /live, so a stale layer must go
rsync -a --info=progress2 --delete "$SMP/live/" "$LMP/live/"
# a converted install: the old /live on the big partition would be found by
# nothing, and copied nowhere, but it is 0.9 GB of nothing
[ "$LMP" != "$DMP" ] && [ -d "$DMP/live" ] && { sync; rm -rf "$DMP/live"; }

# --- persistence: save settings + saved work to this partition's free space -
# live-boot reads this file from the partition named by persistence-label and
# unions exactly these directories, so each boot is clean except what you chose.
cat > "$DMP/persistence.conf" <<'PCONF'
/home/k4510 union
/etc/NetworkManager/system-connections union
/var/lib/tailscale union
PCONF
# live-boot builds the overlay for /home/k4510 as  home/k4510/rw (upper) +
# home/k4510/work, and makes them as ROOT -- so the merged /home/k4510 came
# out root-owned and the k4510 user (uid 1000 in the image) could not create
# anything at the top of its own home (Doc, 2026-09-11).  Pre-create the
# upper dir owned by that user; live-boot is happy to find it there.
mkdir -p "$DMP/home/k4510/rw"; chown 1000:1000 "$DMP/home/k4510/rw"; chmod 755 "$DMP/home/k4510/rw"
say "persistence: $(printf '%s' "$(df -h --output=avail "$DMP" | tail -1 | tr -d ' ')") free on $DST_LABEL for your settings and saved work"

sync
[ "$LMP" != "$DMP" ] && { umount "$LMP"; rmdir "$LMP" 2>/dev/null || true; }
umount "$SMP"; umount "$DMP"
trap - EXIT; rmdir "$SMP" "$DMP" 2>/dev/null || true

# --- GRUB: add a K4510 entry, keep the existing OS the default -------------
say "adding the K4510 entry to GRUB"
cat > "$GRUB_D/42_k4510" <<EOF
#!/bin/sh
# Added by install-k4510.sh -- the K4510 live system on the internal $DST_LABEL
# partition.  Loaded straight from GRUB; nothing about the host OS is changed.
# savedefault: with GRUB_DEFAULT=saved and GRUB_SAVEDEFAULT=true in the host's
# /etc/default/grub, the OS booted last is the next default -- so the host's
# own update restarts come back to the host (the Dell, Doc, 2026-09-12).
# Without GRUB_SAVEDEFAULT it records the choice and changes nothing else.
# The first entry boots quietly -- no kernel text, no blinking cursor, straight
# to the machine; the second shows Linux starting, for when something needs
# looking at (Doc, 2026-09-12: a startup logo).
cat <<'MENU'
menuentry "K4510 Fantasy Computer" --class k4510 {
    clear
    savedefault
    insmod part_gpt
    insmod ext2
    search --no-floppy --set=root --label $LIVE_SEARCH
    linux  /live/vmlinuz $CMDLINE quiet splash loglevel=3 vt.global_cursor_default=0
    initrd /live/initrd.img
}
menuentry "K4510 (text boot)" --class k4510 {
    savedefault
    insmod part_gpt
    insmod ext2
    search --no-floppy --set=root --label $LIVE_SEARCH
    echo   "Loading the K4510 ..."
    linux  /live/vmlinuz $CMDLINE
    initrd /live/initrd.img
}
MENU
EOF
chmod +x "$GRUB_D/42_k4510"

[ -n "${K4510_GRUB_D:-}" ] && { say "done (rehearsal: $GRUB_D/42_k4510 written, grub.cfg left alone)"; exit 0; }
GCFG=
for c in /boot/grub2/grub.cfg /boot/grub/grub.cfg /boot/efi/EFI/*/grub.cfg; do
    [ -f "$c" ] && { GCFG=$c; break; }
done
[ -n "$GCFG" ] || die "cannot find grub.cfg (looked in /boot/grub2, /boot/grub and /boot/efi/EFI/*)"
cp -a "$GCFG" "$GCFG.pre-k4510.$(date +%Y%m%d%H%M%S)"
"$MKCFG" -o "$GCFG" >/dev/null 2>&1 || die "$MKCFG failed; your old grub.cfg backup is beside it"

say "done"
echo "The machine now offers 'K4510 Fantasy Computer' in the boot menu; your"
echo "existing OS stays the default.  Pull the USB stick out before booting"
echo "K4510 (it is only needed for this install)."
