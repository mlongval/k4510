#!/bin/sh
# the K4510 Linux, LIVE: build the disk image that loads ENTIRELY INTO RAM.
#
# This is the sibling of build-image.sh, not a replacement for it.
#
#   build-image.sh  a writable ext4 root on the stick.  Remembers things.
#                   Can be dd'd to an internal drive and thereby installed.
#   build-live.sh   a squashfs copied wholly into RAM at boot (live-boot's
#                   `toram`).  The stick can be PULLED OUT once the machine is
#                   up.  Nothing persists: every boot is identical.
#
# Doc's requirements, 2026-09-03:
#   - boot, then load entirely to RAM
#   - target is a ThinkPad T480 (i5 or i7), 32 GB
#   - NO access to the internal SSD/NVMe.  Absolute -- one boot entry, no
#     escape hatch in the menu
#   - the k4510 user has passwordless sudo (2026-09-07)
#   - `!cmd` at the machine's prompt runs cmd in this Linux (the emulator is
#     started by profile.d/k4510.sh; a bare `!` is a shell)
#   - telnetd bound to loopback ONLY; from inside the machine that is
#     TELNET 127.0.0.1 23
#   - real network access outbound, so the machine's TELNET can reach BBSes
#   - Mad Pascal toolchain and neovim on the Linux side; PAS name and CC name
#     at the prompt compile a .PAS / .C in the machine's directory (tools/)
#   - Tek40xx, a Tektronix 4010 terminal on SDL2: `tek HOST [PORT]` on tty2
#
#   sudo ./build-live.sh              # -> k4510-live-<date>-amd64.img
#   sudo REBUILD=1 ./build-live.sh    # the same, but keep the rootfs from last
#                                     # time and only rebuild the machine in it
#                                     # (two minutes; use it after a code fix)
#   sudo REUSE=1 ./build-live.sh      # only re-assemble the image
#
# Needs, on the build host: debootstrap, parted, dosfstools, e2fsprogs,
# squashfs-tools.
set -e

SUITE=${SUITE:-trixie}
MIRROR=${MIRROR:-http://deb.debian.org/debian/}
# The machine shows the host's clock in its status band, so an image with no
# timezone displays UTC and looks four hours wrong.
TZ_AREA=${TZ_AREA:-America/Toronto}
USER_NAME=k4510
# Loopback-only telnet on a RAM-only system with no reachable internal drive.
# The password is weak on purpose and Doc chose it knowingly: the socket does
# not exist off 127.0.0.1, so there is no network path to this door at all.
USER_PASS=${USER_PASS:-k4510}

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/.." && pwd)
BUILD_ID="0.5-$(git -C "$REPO" rev-parse --short=7 HEAD 2>/dev/null || echo nogit)$(git -C "$REPO" diff --quiet HEAD 2>/dev/null || echo +)"   # the commit for K4510_BUILD (the chroot has no .git)
OUT=${OUT:-$HERE/k4510-live-$(date +%Y%m%d)-amd64.img}
WORK=${WORK:-$HERE/.live-work}
REUSE=${REUSE:-0}
ROOT="$WORK/rootfs"
STAGE="$WORK/stage"
MNT="$WORK/mnt"
LOOP=""

# The drives that must not exist.  Blacklisting the MODULES is what makes this
# absolute rather than advisory: the block devices never enumerate, so there is
# nothing for a filesystem probe, an automounter, or a curious user to find.
# nvme/nvme_core is the M.2; ahci/libahci/ata_* is any SATA drive.  sd_mod and
# usb-storage are deliberately NOT here -- the USB stick needs them to boot.
# Overridable: `NODISK= sudo ./build-live.sh` (empty) builds a variant that CAN
# see the internal drives -- for the K4510 installed ON one (the Dell dual-boot,
# 2026-09-11: the ban baked into the initramfs hid the very NVMe it lived on,
# so live-boot could not find its own squashfs).  Unset = the stick's default.
NODISK=${NODISK-nvme,nvme_core,ahci,libahci,ata_piix,ata_generic,pata_acpi}

# No `quiet`.  This is a first bring-up on hardware that has never run it; if
# KMS or live-boot fails, Doc needs to see which one, not a silent black screen.
#
# `persistence` turns on the fourth partition (see PERSIST_MB).  Note what it
# costs: the system still lives in RAM, but SAVING now needs the stick, so
# "pull it out once the banner is up" and "keep my changes" are no longer both
# true at once.  Leave it in, and use F7 -> Shut down.
CMDLINE="boot=live components toram union=overlay persistence"
[ -n "$NODISK" ] && CMDLINE="$CMDLINE modprobe.blacklist=$NODISK"

# The persistence partition.  100 MB is Doc's number and it is a good one: an
# overlay stores only what CHANGED, not the base it sits on, so this holds
# settings, saved programs and wifi credentials many times over.
PERSIST_MB=${PERSIST_MB:-100}

[ "$(id -u)" = 0 ] || { echo "build-live.sh: run me with sudo"; exit 1; }

# The build host's TMPDIR does not exist inside the image, and dpkg's
# maintainer scripts call mktemp.  Every chroot below is entered through
# `env -i` for the same reason.
unset TMPDIR
export LC_ALL=C

CHROOT_ENV="env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin TMPDIR=/tmp LC_ALL=C DEBIAN_FRONTEND=noninteractive"

binds_up()   { for d in dev dev/pts proc sys; do mountpoint -q "$ROOT/$d" || mount --bind "/$d" "$ROOT/$d"; done; }
binds_down() { for d in dev/pts dev proc sys; do umount -l "$ROOT/$d" 2>/dev/null || true; done; }

cleanup() {
    set +e
    binds_down
    umount -l "$ROOT/mnt/live/boot/efi" 2>/dev/null
    umount -l "$ROOT/mnt/live" 2>/dev/null
    umount -l "$MNT/boot/efi" 2>/dev/null
    umount -l "$MNT" 2>/dev/null
    [ -n "$LOOP" ] && losetup -d "$LOOP" 2>/dev/null
}
trap cleanup EXIT

# REUSE=1 skips straight to assembling the image from the rootfs and squashfs
# already in $WORK.  The expensive half of this script is twenty-five minutes
# of debootstrap, apt and three compilers; the image half is thirty seconds.
# Getting the second one wrong should not cost the first one again.
# Two images, and live-boot unions every *.squashfs it finds in /live, in name
# order: filesystem.squashfs is the Debian base with the toolchains (700 MB,
# made by a full build, rarely changes) and k4510.squashfs on top is just the
# machine -- /home/k4510/k4510 and /usr/local/bin (tens of MB, what a code
# change touches).  So a REBUILD squashes only the small one, and an update to
# an installed machine (update-k4510.sh) moves only the small one -- Doc,
# 2026-09-11: "only UPDATE the image on the Dell instead of copying the whole".
LAYER_DIRS="home/k4510/k4510 usr/local/bin"
squash_base() {
    echo "== squashfs: the base =="
    # zstd: decompresses fast, and the whole thing is read into RAM once at boot.
    mksquashfs "$ROOT" "$STAGE/live/filesystem.squashfs" \
        -comp zstd -Xcompression-level 19 -noappend -no-progress \
        -e proc sys dev/pts mnt tmp var/cache/apt/archives $LAYER_DIRS
}
squash_layer() {
    echo "== squashfs: the machine layer =="
    rm -rf "$WORK/layer"; for d in $LAYER_DIRS; do mkdir -p "$WORK/layer/$(dirname "$d")"; cp -a "$ROOT/$d" "$WORK/layer/$d"; done
    mksquashfs "$WORK/layer" "$STAGE/live/k4510.squashfs" \
        -comp zstd -Xcompression-level 19 -noappend -no-progress
    rm -rf "$WORK/layer"
}
squash() { squash_base; squash_layer; }

if [ "$REUSE" = 1 ] && [ -f "$STAGE/live/filesystem.squashfs" ] && [ -f "$STAGE/live/k4510.squashfs" ]; then
    echo "== reusing the rootfs and squashfs already in $WORK =="
    mkdir -p "$MNT"
elif [ "$REBUILD" = 1 ] && [ -d "$ROOT/home/$USER_NAME/k4510" ]; then
    # The middle mode, and the one to reach for after a fix to the emulator:
    # keep the rootfs that debootstrap and three compilers took half an hour to
    # make, put THIS checkout's HEAD into it, rebuild the machine there, and
    # squash it again.  Two minutes instead of thirty.  Anything that changes a
    # PACKAGE still needs the full build.
    echo "== rebuilding the machine inside the rootfs already in $WORK =="
    mkdir -p "$MNT"
    binds_up
    git -C "$REPO" archive --format=tar HEAD | tar -x -C "$ROOT/home/$USER_NAME/k4510"
    $CHROOT_ENV chroot "$ROOT" chown -R "$USER_NAME:$USER_NAME" "/home/$USER_NAME"
    # The .d files name the paths of the last build; a fresh checkout over them
    # is exactly the case where a stale one keeps a changed file from compiling.
    $CHROOT_ENV chroot "$ROOT" su - $USER_NAME -c \
        "cd ~/k4510 && find core sdl -name '*.d' -delete && make ACME=/usr/bin/acme K4510_BUILD='$BUILD_ID' -j\"\$(nproc)\" sdl/k4510 rom/kernal.bin rom/wozmon.bin rom/demo.bin cpm/runcpm" \
        || { echo "build-live.sh: THE MACHINE DID NOT BUILD"; exit 1; }
    $CHROOT_ENV chroot "$ROOT" su - $USER_NAME -c 'cd ~/k4510 && make -C tube' \
        || echo "build-live.sh: the Tube (BBC BASIC) did not build; everything else works"
    # Tek40xx is built from upstream WITH OUR PATCH, so a change to that patch
    # (the seamless-window and ESC/q-quit fixes, 2026-09-11) only reaches the
    # stick if we rebuild it here too -- the emulator rebuild above does not.
    # Same invocation as the full build below; needs the chroot's network.
    # This checkout's /etc overlay too (the lid setting, keymaps...): a config
    # change must reach the fast path, not only a 30-minute full build.
    cp -a "$HERE/config/includes.chroot/etc/." "$ROOT/etc/"
    mkdir -p "$ROOT/usr/local/bin"; cp -a "$HERE/config/includes.chroot/usr/local/bin/." "$ROOT/usr/local/bin/"   # the tek wrapper too (see the full build)
    $CHROOT_ENV chroot "$ROOT" sh /home/$USER_NAME/k4510/linux/tek40xx/build.sh \
        || echo "build-live.sh: Tek40xx did not rebuild; everything else works"
    binds_down
    sync
    # the base is untouched by a code change; if a full build never made the
    # split base (an older WORK dir), make it once now
    [ -f "$STAGE/live/filesystem.squashfs" ] && grep -q k4510layer "$STAGE/.split" 2>/dev/null || { squash_base; echo k4510layer > "$STAGE/.split"; }
    squash_layer
else

rm -rf "$WORK"
mkdir -p "$ROOT" "$STAGE/live" "$MNT"

echo "== debootstrap $SUITE =="
debootstrap --arch=amd64 --components=main,contrib,non-free-firmware \
            "$SUITE" "$ROOT" "$MIRROR"

cat > "$ROOT/etc/apt/sources.list" <<EOF
deb $MIRROR $SUITE main contrib non-free-firmware
deb $MIRROR $SUITE-updates main contrib non-free-firmware
deb http://security.debian.org/debian-security $SUITE-security main contrib non-free-firmware
EOF
echo k4510 > "$ROOT/etc/hostname"
ln -sf "/usr/share/zoneinfo/$TZ_AREA" "$ROOT/etc/localtime"
echo "$TZ_AREA" > "$ROOT/etc/timezone"
printf '127.0.0.1\tlocalhost\n127.0.1.1\tk4510\n' > "$ROOT/etc/hosts"

# The live root is an overlay on tmpfs; there is nothing to mount by label.
# The old fstab's LABEL= lines would hang the boot looking for partitions that
# this image does not have.
cat > "$ROOT/etc/fstab" <<'EOF'
# K4510 live: the root is a tmpfs overlay over a squashfs held in RAM.
# Nothing to mount, and nothing on the internal drives may be mounted.
EOF

# The autologin getty and the profile script that becomes the machine on tty1.
cp -a "$HERE/config/includes.chroot/etc/." "$ROOT/etc/"
# ... and its usr/local/bin: the `tek` wrapper.  Only etc/ was ever copied, so
# the appliance had the tek40xx BINARY but not the wrapper that sets KMSDRM
# and full screen -- `tek HOST` on tty2 and tekplay's wrapper path both
# fell over (Doc, the Dell, 2026-09-11).
mkdir -p "$ROOT/usr/local/bin"; cp -a "$HERE/config/includes.chroot/usr/local/bin/." "$ROOT/usr/local/bin/"

# Second layer under the kernel command line: even if someone boots without the
# modprobe.blacklist=, these keep the drivers out.  `install ... /bin/false` is
# stronger than `blacklist` -- it defeats an explicit `modprobe nvme` too.
mkdir -p "$ROOT/etc/modprobe.d"
if [ -n "$NODISK" ]; then
{
    echo "# K4510: the internal drives do not exist here (Doc, 2026-09-03)."
    echo "# Absolute by choice: there is no boot-menu entry that reveals them."
    for m in $(echo "$NODISK" | tr ',' ' '); do
        echo "blacklist $m"
        echo "install $m /bin/false"
    done
} > "$ROOT/etc/modprobe.d/k4510-no-internal-disks.conf"
else
    rm -f "$ROOT/etc/modprobe.d/k4510-no-internal-disks.conf"   # the internal-install variant must see its disk
fi

echo "== packages =="
PKGS=$(sed -e 's/#.*//' -e '/^[[:space:]]*$/d' "$HERE/packages.list" | tr '\n' ' ')
$CHROOT_ENV chroot "$ROOT" /bin/sh -e <<EOF
export DEBIAN_FRONTEND=noninteractive
apt-get update -q
apt-get install -y -q --no-install-recommends \
    linux-image-amd64 systemd-sysv grub-efi-amd64 grub-pc-bin efibootmgr \
    live-boot live-boot-initramfs-tools \
    locales sudo $PKGS
EOF

echo "== the keyboard picker (k4510.kbd= on the boot line) =="
# A downloader picks a layout in the boot menu; this reads it from the kernel
# command line and applies it BEFORE the emulator's tty1 login, so SDL's
# console keyboard reads the chosen map.  No k4510.kbd= -> the shipped default
# (plain US) stands.  The map codes are the boot menu's.
cat > "$ROOT/usr/local/sbin/k4510-keymap" <<'KBD'
#!/bin/sh
# Apply the keyboard layout chosen in the boot menu (k4510.kbd=<code>).
set -e
code=$(sed -n 's/.*\bk4510\.kbd=\([^ ]*\).*/\1/p' /proc/cmdline)
[ -n "$code" ] || exit 0
case "$code" in
    us)      L=us; V="" ;;
    us-intl) L=us; V=intl ;;
    de)      L=de; V="" ;;
    es)      L=es; V="" ;;
    fr)      L=fr; V="" ;;
    ca|cf)   L=ca; V="" ;;
    gb|uk)   L=gb; V="" ;;
    it)      L=it; V="" ;;
    *)       exit 0 ;;                 # unknown code: leave the shipped default
esac
cat > /etc/default/keyboard <<EOF2
XKBMODEL="pc105"
XKBLAYOUT="$L"
XKBVARIANT="$V"
XKBOPTIONS=""
BACKSPACE="guess"
EOF2
setupcon --force 2>/dev/null || true
KBD
chmod +x "$ROOT/usr/local/sbin/k4510-keymap"
cat > "$ROOT/etc/systemd/system/k4510-keymap.service" <<'EOF'
[Unit]
Description=K4510 keyboard layout from the boot menu (k4510.kbd=)
DefaultDependencies=no
After=systemd-tmpfiles-setup.service
Before=console-setup.service keyboard-setup.service getty@tty1.service
ConditionKernelCommandLine=k4510.kbd

[Service]
Type=oneshot
ExecStart=/usr/local/sbin/k4510-keymap
RemainAfterExit=yes

[Install]
WantedBy=sysinit.target
EOF

echo "== telnet, loopback only =="
# Lifted from the working service on ubuntu-s1 (TELNET-SERVER.md), with the
# one change that matters: ListenStream is 127.0.0.1, so the socket exists
# nowhere else.  No firewall is doing this work -- the binding is.
cat > "$ROOT/etc/systemd/system/k4510-telnet.socket" <<'EOF'
# Telnet for the K4510, reachable from the machine itself and nowhere else.
#
# The binding is the control that matters: ListenStream names the loopback
# address, so the socket never exists on the wifi or the ethernet, whatever a
# firewall happens to say.  From inside the machine:  TELNET 127.0.0.1 23
[Unit]
Description=K4510 telnet (loopback only)
Documentation=man:telnetd(8)
After=network.target

[Socket]
ListenStream=127.0.0.1:23
Accept=yes

[Install]
WantedBy=sockets.target
EOF
cat > "$ROOT/etc/systemd/system/k4510-telnet@.service" <<'EOF'
[Unit]
Description=K4510 telnet session
After=k4510-telnet.socket
Requires=k4510-telnet.socket

[Service]
# -h: no host banner before the login prompt.
ExecStart=-/usr/sbin/telnetd -h -E /usr/local/sbin/k4510-telnet-login
StandardInput=socket
StandardError=journal
EOF
mkdir -p "$ROOT/usr/local/sbin"
cat > "$ROOT/usr/local/sbin/k4510-telnet-login" <<'EOF'
#!/bin/sh
# The login program telnetd runs (telnetd -E).  It forces the account: the only
# name this door will ever offer is k4510.  Same reasoning as the service on
# ubuntu-s1, and it costs nothing to keep the habit even on loopback.
exec /bin/login "$@" k4510
EOF
chmod 755 "$ROOT/usr/local/sbin/k4510-telnet-login"

echo "== shutting the computer down from the F7 menu =="
# The marker the emulator looks for (sdl/main.c): its presence is what reveals
# the "Shut down the computer" row, so a desktop build never offers to power
# off Doc's workstation.
: > "$ROOT/etc/k4510-linux"
# Two scripts, because only the second may run as root.  The emulator execs
# the first after SDL has given the console back and the settings are written.
cat > "$ROOT/usr/local/sbin/k4510-poweroff" <<'EOF'
#!/bin/sh
# Run by the emulator (F7 -> Shut down) as the k4510 user.  All it may do is
# ask for the real one, which sudoers permits by name and by name only.
exec sudo -n /usr/local/sbin/k4510-halt
EOF
cat > "$ROOT/usr/local/sbin/k4510-halt" <<'EOF'
#!/bin/sh
# The clean stop.  The persistence partition is the only thing on the stick
# that is ever written, so flush it and take it read-only BEFORE halting: after
# this returns, pulling the stick out cannot lose anything.  It is mounted
# `sync` anyway (k4510-persistence-sync.service), so this is belt and braces.
sync
for m in /run/live/persistence/*; do
    [ -d "$m" ] || continue
    mountpoint -q "$m" && mount -o remount,ro "$m" 2>/dev/null
done
sync
exec systemctl poweroff
EOF
chmod 755 "$ROOT/usr/local/sbin/k4510-poweroff" "$ROOT/usr/local/sbin/k4510-halt"
mkdir -p "$ROOT/etc/sudoers.d"
echo "$USER_NAME ALL=(root) NOPASSWD: /usr/local/sbin/k4510-halt" > "$ROOT/etc/sudoers.d/k4510-halt"
chmod 440 "$ROOT/etc/sudoers.d/k4510-halt"
# Passwordless sudo for the user, Doc's call 2026-09-07: the stick is a RAM-only
# system with the internal drives locked out, and `!` already gives the shell.
# Note what it does NOT buy: persistence keeps /home only, so `!sudo apt install`
# here lasts until the next boot.  The distrobox flavour (distrobox.sh) is the
# one where installs stick.
echo "$USER_NAME ALL=(ALL) NOPASSWD: ALL" > "$ROOT/etc/sudoers.d/k4510-user"
chmod 440 "$ROOT/etc/sudoers.d/k4510-user"

# Doc asked whether the save partition could be "mounted and automatically
# unmounted after write".  It cannot: an overlay's upper directory has to stay
# mounted for the whole session, or the files it is holding vanish underneath
# you.  `sync` is the honest version of the same wish -- every write reaches
# the flash as it happens, instead of sitting in the page cache waiting for a
# clean unmount that an unplugged stick never gets.  On 100 MB of small files
# the cost of this is not measurable.
cat > "$ROOT/etc/systemd/system/k4510-persistence-sync.service" <<'EOF'
[Unit]
Description=Make the K4510 save partition write straight through
DefaultDependencies=no
After=local-fs.target
Before=getty@tty1.service

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/bin/sh -c 'for m in /run/live/persistence/*; do mountpoint -q "$m" && mount -o remount,sync "$m"; done; exit 0'

[Install]
WantedBy=multi-user.target
EOF

echo "== the machine =="
mkdir -p "$ROOT/home/$USER_NAME/k4510"
git -C "$REPO" archive --format=tar HEAD | tar -x -C "$ROOT/home/$USER_NAME/k4510"
$CHROOT_ENV chroot "$ROOT" /bin/sh -e <<EOF
systemctl enable k4510-telnet.socket
systemctl enable k4510-persistence-sync.service
systemctl enable keyboard-setup.service console-setup.service 2>/dev/null || true   # the console keymap machinery (setupcon/ckbcomp)
systemctl enable k4510-keymap.service 2>/dev/null || true                           # apply the boot menu's k4510.kbd= before tty1
setupcon --save-only 2>/dev/null || true   # bake the default (US) keymap cache (chroot-safe)
adduser --disabled-password --gecos "K4510" $USER_NAME
echo '$USER_NAME:$USER_PASS' | chpasswd
for g in video input audio render sudo netdev plugdev; do adduser $USER_NAME \$g 2>/dev/null || true; done
chown -R $USER_NAME:$USER_NAME /home/$USER_NAME
EOF

# NOT 'make all': that includes pascal-prgs, whose .prg files are tracked in
# the repo anyway.  What must be built is what git does not carry.
$CHROOT_ENV chroot "$ROOT" su - $USER_NAME -c \
    "cd ~/k4510 && make ACME=/usr/bin/acme K4510_BUILD='$BUILD_ID' -j\"\$(nproc)\" sdl/k4510 rom/kernal.bin rom/wozmon.bin rom/demo.bin cpm/runcpm" \
    || { echo "build-live.sh: THE MACHINE DID NOT BUILD"; exit 1; }
$CHROOT_ENV chroot "$ROOT" su - $USER_NAME -c 'cd ~/k4510 && make -C tube' \
    || echo "build-live.sh: the Tube (BBC BASIC) did not build; everything else works"

echo "== Mad Pascal =="
# Two checkouts, not one: mp compiles Pascal to 6502 assembly and MADS
# assembles it (Makefile:187-188).  They go at exactly the paths the Makefile
# already defaults to, so `make pascal` works with no overrides at all.
$CHROOT_ENV chroot "$ROOT" su - $USER_NAME -c '
set -e
mkdir -p ~/Projects/neo6502_dev && cd ~/Projects/neo6502_dev
git clone --depth 1 https://github.com/tebe6502/Mad-Pascal.git
git clone --depth 1 https://github.com/tebe6502/Mad-Assembler.git
cd Mad-Assembler && fpc -Mdelphi -O2 mads.pas
' || { echo "build-live.sh: MAD PASCAL CHECKOUTS FAILED"; exit 1; }
# install.py grafts the K4510 target in and rebuilds mp with FPC.
$CHROOT_ENV chroot "$ROOT" su - $USER_NAME -c \
    'cd ~/k4510 && python3 pascal/install.py ~/Projects/neo6502_dev/Mad-Pascal' \
    || { echo "build-live.sh: THE K4510 MAD PASCAL TARGET DID NOT INSTALL"; exit 1; }
# Prove the whole chain works here rather than discovering it on the laptop.
$CHROOT_ENV chroot "$ROOT" su - $USER_NAME -c 'cd ~/k4510 && make pascal' \
    || { echo "build-live.sh: MAD PASCAL BUILT BUT DID NOT COMPILE THE DEMOS"; exit 1; }
echo "== Tek40xx =="
# A Tektronix 4010 on SDL2, a telnet client (linux/tek40xx/README.md): the
# second terminal for a PiDP-11, on tty2.  Built from upstream with one patch.
$CHROOT_ENV chroot "$ROOT" sh /home/$USER_NAME/k4510/linux/tek40xx/build.sh \
    || echo "build-live.sh: Tek40xx did not build; everything else works"
# The C side of the same thing: k4510-cc links every program with these two.
$CHROOT_ENV chroot "$ROOT" su - $USER_NAME -c 'cd ~/k4510 && make demo/prg0.o demo/romcalls.o' \
    || { echo "build-live.sh: THE C STARTUP OBJECTS DID NOT BUILD"; exit 1; }

# Two Jaguar cores at 1.2 GHz will not hold a desktop clock; a T480 is a very
# different machine, but SETUP measures either way.
$CHROOT_ENV chroot "$ROOT" su - $USER_NAME -c \
    "printf 'version = 2\ncpu.auto = on\ncpu.clock = 20 MHz\nterm.bands = on\n' > ~/k4510/k4510.cfg"
$CHROOT_ENV chroot "$ROOT" chown -R $USER_NAME:$USER_NAME "/home/$USER_NAME"

echo "== initramfs =="
# live-boot's hooks have to be in the initramfs or `boot=live` means nothing.
$CHROOT_ENV chroot "$ROOT" update-initramfs -u -k all
KVER=$(basename "$(ls -1 "$ROOT"/boot/vmlinuz-* | tail -1)" | sed 's/^vmlinuz-//')
cp "$ROOT/boot/vmlinuz-$KVER"    "$STAGE/live/vmlinuz"
cp "$ROOT/boot/initrd.img-$KVER" "$STAGE/live/initrd.img"

echo "== tidy =="
$CHROOT_ENV chroot "$ROOT" apt-get clean
rm -rf "$ROOT/var/lib/apt/lists"/* "$ROOT/tmp"/*
binds_down
sync

squash
fi

SQ=$(( $(stat -c %s "$STAGE/live/filesystem.squashfs") + $(stat -c %s "$STAGE/live/k4510.squashfs") ))
echo "   squashfs: $(numfmt --to=iec "$SQ") (base + machine layer)"; echo k4510layer > "$STAGE/.split"

echo "== image =="
# Sized to fit, not to a round number: this gets written to a USB stick that
# measured about 4 MB/s, so every gigabyte of empty image is four wasted
# minutes.  ESP 512 MiB + squashfs + kernel + 512 MiB of slack.
LIVE_MB=$(( (SQ / 1048576) + 128 ))                       # squashfs + kernel + initrd
LIVE_END=$(( 514 + LIVE_MB ))
MIB=$(( LIVE_END + PERSIST_MB + 8 ))
rm -f "$OUT"; truncate -s "${MIB}M" "$OUT"
# Four partitions now.  The fourth MUST be labelled exactly `persistence` and
# MUST contain persistence.conf -- that pair is how live-boot finds it; a
# different label is simply not looked at, with no error anywhere.
parted -s "$OUT" mklabel gpt \
    mkpart bios  1MiB   2MiB   set 1 bios_grub on \
    mkpart ESP   fat32 2MiB 514MiB set 2 esp on \
    mkpart live  ext4  514MiB "${LIVE_END}MiB" \
    mkpart save  ext4  "${LIVE_END}MiB" 100%
LOOP=$(losetup --show -f -P "$OUT")
mkfs.vfat -F32 -n K4510-EFI "${LOOP}p2" >/dev/null
mkfs.ext4 -q -L k4510-live "${LOOP}p3"
mkfs.ext4 -q -L persistence  "${LOOP}p4"

# What survives a reboot.  Custom mounts, not `/ union`: a whole-root overlay
# would also persist every log, every apt lock and every bit of /var churn,
# and the point of this machine is that each boot is clean except for the
# things Doc actually chose.
#
#   /home/k4510   the machine's own filesystem (fs/, saved programs), k4510.cfg
#                 and the Mad Pascal checkouts -- an overlay stores only what
#                 changed, so the 200 MB of checkouts cost nothing here.
#   the wifi      because otherwise nmtui has to be redone at every boot, which
#                 was the one real annoyance of the first build.
PSAVE=$(mktemp -d)
mount "${LOOP}p4" "$PSAVE"
cat > "$PSAVE/persistence.conf" <<'EOF'
/home/k4510 union
/etc/NetworkManager/system-connections union
EOF
umount "$PSAVE"; rmdir "$PSAVE"

mount "${LOOP}p3" "$MNT"
mkdir -p "$MNT/boot/efi"
mount "${LOOP}p2" "$MNT/boot/efi"
cp -a "$STAGE/live" "$MNT/live"

echo "== boot =="
# grub-install from INSIDE the chroot, so the modules are Debian's and not the
# Ubuntu build host's.  --removable puts the EFI binary at the fallback path,
# which is what a USB stick needs on a machine with no NVRAM entry for us.
mkdir -p "$ROOT/mnt/live"
# --rbind, NOT --bind: a plain bind does not carry submounts, so the ESP
# mounted at $MNT/boot/efi would appear inside the chroot as an empty
# directory and grub-install would say "doesn't look like an EFI partition".
mount --rbind "$MNT" "$ROOT/mnt/live"
binds_up
$CHROOT_ENV chroot "$ROOT" /bin/sh -e <<EOF
grub-install --target=x86_64-efi --efi-directory=/mnt/live/boot/efi \
             --boot-directory=/mnt/live/boot --removable --no-nvram
grub-install --target=i386-pc --boot-directory=/mnt/live/boot $LOOP
EOF

# Our own grub.cfg, not update-grub's: there is no installed root here for it
# to find, and ONE entry is the point.  No recovery line, no second entry with
# the drives visible -- Doc asked for absolute.
# The boot menu IS the keyboard picker: the emulator owns the screen once it
# starts, and SDL reads the console keymap only at startup, so a downloader's
# one chance to choose a layout with no Linux knowledge is here, at power-on.
# Each entry passes k4510.kbd=<code>; k4510-keymap.service applies it before
# tty1's login.  Default (entry 0) is plain US (Doc, 2026-09-10).
{
    echo "set default=0"
    echo "set timeout=10"
    echo 'set timeout_style=menu'
    echo
    # label  ->  k4510.kbd code
    for pair in \
        "US (default)|us" \
        "US-International (dead keys: accents)|us-intl" \
        "Deutsch (German)|de" \
        "Espanol (Spanish)|es" \
        "Francais (French)|fr" \
        "Canadien-francais (Canadian French)|ca" \
        "UK (British)|gb" \
        "Italiano (Italian)|it"
    do
        label=${pair%|*}; code=${pair#*|}
        echo "menuentry \"K4510  --  keyboard: $label\" {"
        echo "    search --no-floppy --set=root --label k4510-live"
        echo "    linux  /live/vmlinuz $CMDLINE k4510.kbd=$code"
        echo "    initrd /live/initrd.img"
        echo "}"
    done
} > "$MNT/boot/grub/grub.cfg"

binds_down
# -R and then a guard: the chroot view is an rbind of $MNT, and unmounting
# through it propagates, so $MNT/boot/efi is usually already gone by the time
# we get here.  Unguarded, that umount fails, `set -e` takes the script out
# before the final sync, and the build reports failure having done everything
# right.  (It did exactly that twice.)
umount -R "$ROOT/mnt/live"
mountpoint -q "$MNT/boot/efi" && umount "$MNT/boot/efi"
umount "$MNT"
losetup -d "$LOOP"; LOOP=""
sync
trap - EXIT

echo
echo "build-live.sh: $OUT"
ls -lh "$OUT"
echo "Write it with:  sudo dd if=$OUT of=/dev/sdX bs=4M status=progress conv=fsync"
