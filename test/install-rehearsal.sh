#!/bin/sh
# install-k4510.sh, rehearsed on loop devices -- three times over:
#   1. a FRESH install: an unformatted K4510 partition is split and both formatted
#   2. run AGAIN: K4510LIVE is found and refreshed, K4510 left alone
#   4. a fresh install on an MBR disk (see the end)
#   3. a CONVERSION: a one-partition install from before 2026-09-17, with a
#      /live and a saved file on it, is shrunk and split, the file intact
# The target "disk" has another partition MOUNTED throughout, as a host's own
# root would be, so the kernel cannot simply re-read the table.  Needs root;
# run by hand (sudo sh test/install-rehearsal.sh linux/install-k4510.sh), not
# by `make test`.  Labels are RHSTICK / RHK / RHL: nothing real is touched.
set -eu
S=$(readlink -f "${1:?the installer}"); W=/run/install-rehearsal; rm -rf $W; mkdir -p $W/m $W/host $W/grub; B=/var/tmp/install-rehearsal-bin; rm -rf $B; mkdir -p $B   # not under /run: that is noexec
# a machine without rsync (the K4510 Linux): the two lines of it this needs
command -v rsync >/dev/null || { printf '#!/bin/sh\nfor a; do s=$d; d=$a; done\nrm -rf "$d"/*; cp -a "$s". "$d"\n' > $B/rsync; chmod +x $B/rsync; PATH=$B:$PATH; }
LOOPS=
cleanup() { umount $W/m 2>/dev/null || true; umount $W/host 2>/dev/null || true; for l in $LOOPS; do losetup -d $l 2>/dev/null || true; done; }
trap cleanup EXIT
mkdisk() { truncate -s "$2" "$1"; printf "$3" | sfdisk -q "$1"; l=$(losetup -fP --show "$1"); LOOPS="$LOOPS $l"; udevadm settle; echo $l; }
STICK=$(mkdisk $W/stick.img 100M 'label: gpt\nstart=2048\n'); mkfs.ext4 -q -F -L RHSTICK ${STICK}p1
mount ${STICK}p1 $W/m; mkdir $W/m/live; for f in vmlinuz initrd.img filesystem.squashfs k4510.squashfs; do head -c 2000000 /dev/urandom > $W/m/live/$f; done
( cd $W/m/live && sha256sum * ) > $W/sums; umount $W/m
run() { SRC_LABEL=RHSTICK DST_LABEL=RHK LIVE_LABEL=RHL LIVE_MB=${MB:-2048} K4510_GRUB_D=$W/grub sh "$S" 2>&1 | grep -E "^==|install-k4510:" | sed 's/^/    /'; }
check() {  # $1 disk, $2 what is expected
    echo "  table:"; sfdisk -d $1 | grep "^/dev" | sed -E 's/, uuid=[^,]*//; s/^/    /'
    LIVE=$(readlink -f /dev/disk/by-label/RHL); mount -o ro $LIVE $W/m; ( cd $W/m/live && sha256sum -c --quiet $W/sums ) && echo "  payload on $LIVE: matches"; umount $W/m
    BIG=$(readlink -f /dev/disk/by-label/RHK); e2fsck -fn $BIG >/dev/null 2>&1 && echo "  $BIG: fsck clean"
    mount -o ro $BIG $W/m; [ -f $W/m/persistence.conf ] && echo "  persistence.conf: there"; [ -d $W/m/live ] && echo "  STALE /live LEFT ON THE BIG PARTITION" || echo "  no /live on the big partition"
    [ -z "${KEEP:-}" ] || { [ "$(sha256sum < $W/m/home/saved.bin)" = "$KEEP" ] && echo "  the saved file: intact" || echo "  THE SAVED FILE IS DAMAGED"; }; df -m $W/m | tail -1 | awk '{print "  big partition: " $2 " MB, " $3 " used"}'; umount $W/m
    grep -h "label\|live-media" $W/grub/42_k4510 | sed -n '1,2p' | sed -E 's/^ +/  grub: /; s/(toram).*/\1 .../'
}
echo "== 1. fresh"
D=$(mkdisk $W/d1.img 10G 'label: gpt\nstart=2048, size=204800\nstart=206848, name="RHK"\n'); mkfs.ext4 -q -F ${D}p1; mount ${D}p1 $W/host
mkfs.vfat -n RHK ${D}p2 >/dev/null 2>&1 || mkswap -L RHK ${D}p2 >/dev/null; udevadm settle      # labelled, and NOT ext4: a first install
run; check $D
echo "== 2. again"
run; check $D
umount $W/host; losetup -d $D; LOOPS=$(echo $LOOPS | sed "s|$D||")
echo "== 3. converting a one-partition install"
D=$(mkdisk $W/d3.img 10G 'label: gpt\nstart=2048, size=204800\nstart=206848\n'); mkfs.ext4 -q -F ${D}p1; mount ${D}p1 $W/host; mkfs.ext4 -q -F -L RHK ${D}p2; udevadm settle
mount ${D}p2 $W/m; mkdir -p $W/m/live $W/m/home; cp /run/install-rehearsal/sums $W/m/live/old.squashfs; head -c 80000000 /dev/urandom > $W/m/home/saved.bin; KEEP=$(sha256sum < $W/m/home/saved.bin); umount $W/m
run; check $D
umount $W/host; losetup -d $D; LOOPS=$(echo $LOOPS | sed "s|$D||"); KEEP=
echo "== 4. fresh, on an MBR disk (old hardware)"
D=$(mkdisk $W/d4.img 10G 'label: dos\nstart=2048, size=204800, type=83\nstart=206848, type=83\n'); mkfs.ext4 -q -F ${D}p1; mount ${D}p1 $W/host
mkfs.vfat -n RHK ${D}p2 >/dev/null 2>&1 || mkswap -L RHK ${D}p2 >/dev/null; udevadm settle
run; check $D
