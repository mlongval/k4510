#!/bin/sh
# A rehearsal of k4510-split-live on a loop device: a 1.2 GB "disk" with a boot
# partition (a grub.cfg like the Dell's) and a big K4510-like partition with a
# /live and some saved files.  Needs root, so it is run by hand on a K4510
# Linux (sudo sh test/split-live-rehearsal.sh /path/to/k4510-split-live), not
# by `make test`.  Everything is in /run and nothing real is touched: the
# labels are RHK4510 / RHLIVE.
set -eu
S=${1:?the script to rehearse}; W=/run/split-rehearsal; rm -rf $W; mkdir -p $W/medium/live
truncate -s 1200M $W/disk.img
printf 'label: gpt\nstart=2048, size=204800, type=0FC63DAF-8483-4772-8E79-3D69D8477DE4\nstart=206848, type=EBD0A0A2-B9E5-4433-87C0-68B6B72699C7\n' | sfdisk -q $W/disk.img
L=$(losetup -fP --show $W/disk.img); trap 'umount $W/m 2>/dev/null || true; losetup -d $L || true' EXIT
udevadm settle; mkfs.ext4 -q -F ${L}p1; mkfs.ext4 -q -F -L RHK4510 ${L}p2
for f in vmlinuz initrd.img filesystem.squashfs k4510.squashfs; do head -c 3000000 /dev/urandom > $W/medium/live/$f; done
mkdir -p $W/m; mount ${L}p2 $W/m; cp -r $W/medium/live $W/m/; mkdir -p $W/m/home; head -c 50000000 /dev/urandom > $W/m/home/saved.bin; KEEP=$(sha256sum < $W/m/home/saved.bin); umount $W/m
mount ${L}p1 $W/m; mkdir -p $W/m/grub2
cat > $W/m/grub2/grub.cfg <<G
menuentry "K4510 Fantasy Computer" --class k4510 {
    search --no-floppy --set=root --label RHK4510
    linux  /live/vmlinuz boot=live live-media=${L}p2 toram persistence persistence-label=RHK4510 quiet
}
menuentry "K4510 (maintenance)" --class k4510 {
    search --no-floppy --set=root --label RHK4510
    linux  /live/vmlinuz boot=live live-media=${L}p2 toram k4510.maint=split-live
}
G
umount $W/m
K4510_SPLIT_TEST=1 K4510_SPLIT_LABEL=RHK4510 K4510_SPLIT_NEWLABEL=RHLIVE K4510_SPLIT_MB=300 K4510_SPLIT_MEDIUM=$W/medium K4510_SPLIT_LOG=$W/log sh "$S" || true
echo "--- the log"; grep "^==" $W/log
echo "--- the table"; sfdisk -d $L | grep "^/dev"
echo "--- checks"
mount ${L}p2 $W/m; [ "$(sha256sum < $W/m/home/saved.bin)" = "$KEEP" ] && echo "saved file intact" || echo "SAVED FILE DAMAGED"; df -m $W/m | tail -1; umount $W/m
e2fsck -fn ${L}p2 >/dev/null 2>&1 && echo "big partition: fsck clean" || echo "BIG PARTITION NOT CLEAN"
mount ${L}p3 $W/m; ( cd $W/medium/live && sha256sum * ) > $W/sums; ( cd $W/m/live && sha256sum -c --quiet $W/sums ) && echo "new /live matches"; umount $W/m
mount ${L}p1 $W/m; cat $W/m/grub2/grub.cfg; umount $W/m
