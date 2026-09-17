#!/bin/sh
# WADCHOOSER, without the network: a WAD of one's own in /DISK/DOOM is listed
# beside the catalogue's, choosing it writes DOOM.CFG, and the shell comes back
# where it was.  The downloads (a URL, and a file out of a zip at a URL) were
# checked by hand against the real servers, 2026-09-17 -- a test suite that
# fetches 28 MB from GitHub on every run is a test suite nobody runs.
cd "$(dirname "$0")/.."
D=fs/DISK/DOOM
mkdir -p "$D"
fail() { echo "$out"; rm -f "$D/ZZMINE.WAD"; [ -n "$keep" ] && printf '%s' "$keep" > "$D/DOOM.CFG" || rm -f "$D/DOOM.CFG"; echo "wadtest: FAILED: $1"; exit 1; }
keep=$(cat "$D/DOOM.CFG" 2>/dev/null)
printf 'IWAD' > "$D/ZZMINE.WAD"

out=$(./test/headless rom/kernal.bin 'CD /HOME
/APPS/DOOM/WADCHOOSER
~~jjjjjjjj~
~~q~~ECHO WADBACK
' 1500 2>&1) || fail "WADCHOOSER did not run"
echo "$out" | grep -q "DOOM plays ZZMINE.WAD" || fail "a WAD of one's own was not listed, or not chosen"
grep -q "^wad = ZZMINE.WAD" "$D/DOOM.CFG" || fail "DOOM.CFG does not name the choice"
echo "$out" | grep -q "^/HOME\] ECHO WADBACK" || fail "the shell's directory was not put back"

rm -f "$D/ZZMINE.WAD"
[ -n "$keep" ] && printf '%s\n' "$keep" > "$D/DOOM.CFG" || rm -f "$D/DOOM.CFG"
echo "wadtest: OK (a WAD of one's own listed and chosen, DOOM.CFG written, the shell back in /HOME)"
