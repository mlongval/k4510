#!/bin/sh
# DIR -l: one entry a line with the size, the date, the time and the name --
# the bank-3 dir_long behind the ROM's DIR (Doc, 2026-09-11: "like in linux").
set -e
cd "$(dirname "$0")/.."
export K4510_NO_STARTUP=1
out=$(./test/headless rom/kernal.bin 'CD /
~DIR -l
~~' 900 'file(s)' 2>&1) || { echo "$out"; echo "dirtest: FAILED: no listing came back"; exit 1; }
echo "$out" | grep -qE '^ *[0-9]+ +[0-9]{2,4}[./-][0-9]{2}[./-][0-9]{2,4} [0-9]{2}:[0-9]{2} +[A-Za-z]' \
    || { echo "$out"; echo "dirtest: FAILED: no long line with size, date, time and name"; exit 1; }
echo "$out" | grep -qE '<DIR> +[0-9]' || { echo "$out"; echo "dirtest: FAILED: no <DIR> long line (the root has SYSTEM, LANG...)"; exit 1; }
echo "dirtest: OK (DIR -l prints size, date, time and name, one entry a line)"
