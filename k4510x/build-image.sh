#!/bin/sh
# K4510x: build the disk image.
#
# A RAW DISK IMAGE, not a live ISO.  dd it to a USB stick and the machine
# boots and runs from it, with a writable root that remembers things; dd it to
# the internal drive and it is installed.  That is the "written and run from a
# USB stick or installed to a hard drive" that was asked for, and it is also
# how the Pi appliance already ships, so the two deliveries are the same idea.
#
# It boots on UEFI and on legacy BIOS: GPT with an EFI system partition and a
# BIOS boot partition, GRUB installed both ways.  The t520 can do either.
#
#   sudo ./build-image.sh            # -> k4510x-<date>-amd64.img
#
# Needs, on the build host: debootstrap, parted, dosfstools, e2fsprogs.
set -e

SUITE=${SUITE:-trixie}
MIRROR=${MIRROR:-http://deb.debian.org/debian/}
SIZE=${SIZE:-6G}
# The machine shows the host's clock in its status band, so an image with no
# timezone displays UTC and looks four hours wrong.  Override with TZ=.
TZ_AREA=${TZ_AREA:-America/Toronto}
USER_NAME=k4510
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/.." && pwd)
OUT=${OUT:-$HERE/k4510x-$(date +%Y%m%d)-amd64.img}
MNT=$(mktemp -d)
LOOP=""

[ "$(id -u)" = 0 ] || { echo "build-image.sh: run me with sudo"; exit 1; }

# The build host's TMPDIR does not exist inside the image, and dpkg's
# maintainer scripts call mktemp: grub-efi-amd64, ca-certificates and
# openssh-server all failed on it the first time this ran.  Every chroot below
# is entered through `env -i` for the same reason -- an inherited environment
# is the thing most likely to make an image build differ from a real install.
unset TMPDIR
export LC_ALL=C

cleanup() {
    set +e
    for d in dev/pts dev proc sys; do umount -l "$MNT/$d" 2>/dev/null; done
    umount -l "$MNT/boot/efi" 2>/dev/null
    umount -l "$MNT" 2>/dev/null
    [ -n "$LOOP" ] && losetup -d "$LOOP" 2>/dev/null
    rmdir "$MNT" 2>/dev/null
}
trap cleanup EXIT

echo "== image =="
rm -f "$OUT"; truncate -s "$SIZE" "$OUT"
parted -s "$OUT" mklabel gpt \
    mkpart bios  1MiB   2MiB   set 1 bios_grub on \
    mkpart ESP   fat32 2MiB 514MiB set 2 esp on \
    mkpart root  ext4  514MiB 100%
LOOP=$(losetup --show -f -P "$OUT")
mkfs.vfat -F32 -n K4510X-EFI "${LOOP}p2" >/dev/null
mkfs.ext4 -q -L k4510x "${LOOP}p3"
mount "${LOOP}p3" "$MNT"
mkdir -p "$MNT/boot/efi"

echo "== debootstrap $SUITE =="
debootstrap --arch=amd64 --components=main,contrib,non-free-firmware \
            "$SUITE" "$MNT" "$MIRROR"

mount "${LOOP}p2" "$MNT/boot/efi"
for d in dev dev/pts proc sys; do mount --bind "/$d" "$MNT/$d"; done

cat > "$MNT/etc/apt/sources.list" <<EOF
deb $MIRROR $SUITE main contrib non-free-firmware
deb $MIRROR $SUITE-updates main contrib non-free-firmware
deb http://security.debian.org/debian-security $SUITE-security main contrib non-free-firmware
EOF
echo k4510x > "$MNT/etc/hostname"
ln -sf "/usr/share/zoneinfo/$TZ_AREA" "$MNT/etc/localtime"
echo "$TZ_AREA" > "$MNT/etc/timezone"
printf '127.0.0.1\tlocalhost\n127.0.1.1\tk4510x\n' > "$MNT/etc/hosts"
cat > "$MNT/etc/fstab" <<EOF
LABEL=k4510x      /          ext4  errors=remount-ro  0 1
LABEL=K4510X-EFI  /boot/efi  vfat  umask=0077         0 1
EOF

# The /etc drop-ins: the autologin getty and the profile script that becomes
# the machine on tty1.  Shared with the live-build tree so there is one copy.
cp -a "$HERE/config/includes.chroot/etc/." "$MNT/etc/"

echo "== packages =="
# The package list, minus its comments and blank lines.  One source of truth.
PKGS=$(sed -e 's/#.*//' -e '/^[[:space:]]*$/d' "$HERE/config/package-lists/k4510x.list.chroot" | tr '\n' ' ')
env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin TMPDIR=/tmp LC_ALL=C DEBIAN_FRONTEND=noninteractive chroot "$MNT" /bin/sh -e <<EOF
export DEBIAN_FRONTEND=noninteractive
apt-get update -q
apt-get install -y -q --no-install-recommends \
    linux-image-amd64 systemd-sysv grub-efi-amd64 grub-pc-bin efibootmgr \
    locales sudo openssh-server $PKGS
EOF

echo "== the machine =="
mkdir -p "$MNT/home/$USER_NAME/k4510"
git -C "$REPO" archive --format=tar HEAD | tar -x -C "$MNT/home/$USER_NAME/k4510"
rm -f "$MNT/home/$USER_NAME/k4510/fs/SID"    # a symlink to tunes the repo does not carry
env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin TMPDIR=/tmp LC_ALL=C DEBIAN_FRONTEND=noninteractive chroot "$MNT" /bin/sh -e <<EOF
adduser --disabled-password --gecos "K4510" $USER_NAME
for g in video input audio render sudo; do adduser $USER_NAME \$g 2>/dev/null || true; done
chown -R $USER_NAME:$USER_NAME /home/$USER_NAME
EOF
# NOT 'make all': that includes pascal-prgs, which needs a Mad Pascal checkout
# this image does not carry, and whose .prg files are tracked in the repo
# anyway.  What must be built is what git does not carry.
env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin TMPDIR=/tmp LC_ALL=C DEBIAN_FRONTEND=noninteractive chroot "$MNT" su - $USER_NAME -c \
    'cd ~/k4510 && make ACME=/usr/bin/acme -j"$(nproc)" sdl/k4510 rom/kernal.bin rom/wozmon.bin rom/demo.bin cpm/runcpm' \
    || { echo "build-image.sh: THE MACHINE DID NOT BUILD"; exit 1; }
env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin TMPDIR=/tmp LC_ALL=C DEBIAN_FRONTEND=noninteractive chroot "$MNT" su - $USER_NAME -c 'cd ~/k4510 && make -C tube' \
    || echo "build-image.sh: the Tube (BBC BASIC) did not build; everything else works"
# Two Jaguar cores at 1.2 GHz will not hold a desktop clock; SETUP measures.
env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin TMPDIR=/tmp LC_ALL=C DEBIAN_FRONTEND=noninteractive chroot "$MNT" su - $USER_NAME -c \
    "printf 'version = 2\ncpu.auto = on\ncpu.clock = 20 MHz\nterm.bands = on\n' > ~/k4510/k4510.cfg"

echo "== boot =="
# Both firmwares.  --removable puts the EFI binary at the fallback path
# (/EFI/BOOT/BOOTX64.EFI), which is what a USB stick needs and what a machine
# with no NVRAM entry for us will find; i386-pc writes the BIOS blocks to the
# loop device, from INSIDE the chroot so it is Debian's grub modules and not
# the build host's.
cat >> "$MNT/etc/default/grub" <<'EOF'
GRUB_TIMEOUT=1
GRUB_DISABLE_OS_PROBER=true
EOF
env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin TMPDIR=/tmp LC_ALL=C DEBIAN_FRONTEND=noninteractive chroot "$MNT" /bin/sh -e <<EOF
export DEBIAN_FRONTEND=noninteractive
grub-install --target=x86_64-efi --efi-directory=/boot/efi --removable --no-nvram
grub-install --target=i386-pc $LOOP
update-grub
EOF

echo "== tidy =="
env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin TMPDIR=/tmp LC_ALL=C DEBIAN_FRONTEND=noninteractive chroot "$MNT" apt-get clean
rm -rf "$MNT/var/lib/apt/lists"/* "$MNT/tmp"/*
sync
cleanup; trap - EXIT
echo
echo "build-image.sh: $OUT"
ls -lh "$OUT"
echo "Write it with:  sudo dd if=$OUT of=/dev/sdX bs=4M status=progress conv=fsync"
