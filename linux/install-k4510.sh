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
#   3. copies the live payload (kernel, initrd, the ~750 MB squashfs) onto it
#   4. writes persistence.conf so the machine SAVES your settings and saved
#      work back to the free space on that same partition
#   5. adds a "K4510" entry to this machine's GRUB (your OS stays the default)
#
# Re-run it any time to refresh the payload from a newer stick: it will NOT
# reformat once the install is there, so your persisted settings are kept.
#
# Safety: it refuses to touch a USB/removable target, and refuses to write to
# the same disk the stick is on -- it only ever formats the internal K4510
# partition.
set -e

SRC_LABEL=${SRC_LABEL:-k4510-live}     # the stick's live partition
DST_LABEL=${DST_LABEL:-K4510}          # the internal partition to install onto
# The kernel command line for an INTERNAL install.  Unlike the stick's, it does
# NOT blacklist the disk drivers (the machine now lives on an internal disk and
# must be able to read it), and it pins persistence to the K4510 partition by
# label, so the stick's own "persistence" partition is never mistaken for it.
CMDLINE=""   # built once the target device is known (needs live-media=DEV)

say() { printf '\n== %s ==\n' "$*"; }
die() { printf 'install-k4510: %s\n' "$*" >&2; exit 1; }

[ "$(id -u)" = 0 ] || die "run me with sudo"
for t in blkid lsblk mkfs.ext4 rsync grub2-mkconfig; do
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
CMDLINE="boot=live components live-media-path=/live live-media=$DSTDEV live-media-timeout=10 toram union=overlay persistence persistence-label=$DST_LABEL persistence-storage=filesystem"
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
if [ "$FSTYPE" != "ext4" ]; then
    say "formatting $DSTDEV as ext4 (label $DST_LABEL) -- first install"
    mkfs.ext4 -F -L "$DST_LABEL" "$DSTDEV" >/dev/null
    FRESH=1
else
    say "$DSTDEV is already ext4 -- keeping it (payload will be refreshed, settings preserved)"
fi
mount "$DSTDEV" "$DMP"

# --- copy the live payload -------------------------------------------------
say "copying the live payload (this is the ~750 MB squashfs; a minute or two)"
mkdir -p "$DMP/live"
rsync -a --info=progress2 "$SMP/live/" "$DMP/live/"

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
umount "$SMP"; umount "$DMP"
trap - EXIT; rmdir "$SMP" "$DMP" 2>/dev/null || true

# --- GRUB: add a K4510 entry, keep the existing OS the default -------------
say "adding the K4510 entry to GRUB"
cat > /etc/grub.d/42_k4510 <<EOF
#!/bin/sh
# Added by install-k4510.sh -- the K4510 live system on the internal $DST_LABEL
# partition.  Loaded straight from GRUB; nothing about the host OS is changed.
cat <<'MENU'
menuentry "K4510 Fantasy Computer" --class k4510 {
    insmod part_gpt
    insmod ext2
    search --no-floppy --set=root --label $DST_LABEL
    echo   "Loading the K4510 ..."
    linux  /live/vmlinuz $CMDLINE
    initrd /live/initrd.img
}
MENU
EOF
chmod +x /etc/grub.d/42_k4510

GCFG=/boot/grub2/grub.cfg
[ -f "$GCFG" ] || GCFG=/boot/efi/EFI/fedora/grub.cfg
[ -f "$GCFG" ] || die "cannot find grub.cfg (looked in /boot/grub2 and /boot/efi/EFI/fedora)"
cp -a "$GCFG" "$GCFG.pre-k4510.$(date +%Y%m%d%H%M%S)"
grub2-mkconfig -o "$GCFG" >/dev/null 2>&1 || die "grub2-mkconfig failed; your old grub.cfg backup is beside it"

say "done"
echo "The machine now offers 'K4510 Fantasy Computer' in the boot menu; your"
echo "existing OS stays the default.  Pull the USB stick out before booting"
echo "K4510 (it is only needed for this install)."
