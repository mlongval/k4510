#!/bin/sh
# test/mounttest.sh -- MOUNT through the shell, the ROM's side (2026-09-15).
# Listing the mounts (MOUNT alone) left 259 bytes on the ROM's C stack each
# time -- a 256-byte buffer in a frame cc65 never gave back -- and after two
# listings the next MOUNT's name buffer was below the stack and overwritten
# (test/remote/sidebar.k4r found it on the Dell).  List them five times, then
# mount a zip and read from it.
set -e
cd "$(dirname "$0")/.."
ROM=${1:-rom/kernal.bin}
Z=fs/HOME/MNTTEST.ZIP
sh test/remote/zipfixture.sh $Z
out=$(K4510_SYSOPT=0x04 ./test/headless "$ROM" 'CD /HOME
~MOUNT MNTTEST.ZIP /MNT/MT
~MOUNT
~MOUNT
~MOUNT
~MOUNT
~MOUNT
~UMOUNT /MNT/MT
~MOUNT /HOME/MNTTEST.ZIP /MNT/MT
~TYPE /MNT/MT/DATA/NOTE.TXT
~UMOUNT /MNT/MT
~' 1500 2>/dev/null) || true
rm -f $Z; rmdir fs/MNT/MT 2>/dev/null || true
if echo "$out" | grep -q 'a note in a folder in the zip'; then
    echo "mounttest: OK (five listings of the mounts, then a mount that reads)"
else
    echo "mounttest: FAILED"; echo "$out" | grep -v '^ *$' | tail -8; exit 1
fi
