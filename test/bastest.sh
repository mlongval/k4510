#!/bin/sh
# K4510 BASIC (tools/k4510-bas): every tools/kbasic/tests/NAME.BAS that has
# a NAME.EXP is compiled into a scratch folder under fs/HOME and run there.
#   NAME.EXP: lines that must each be on the screen afterwards; special:
#     COMPILE-ERROR  (first line) the compile must fail; the other lines
#                    must each be in /SYSTEM/LOG/MAKE.ERR
#     MAKEERR:text   after the run, MAKE.ERR has text
#     NOT:text       text is NOT on the screen
#   NAME.IN: typed, a line per INPUT;  NAME.FRAMES: the frame budget (900)
# Made in K4510-Ed (kbasic/), 2026-10-08.
cd "$(dirname "$0")/.."
set -u
T=tools/kbasic/tests
S=fs/HOME/KBTEST
ERRF=fs/SYSTEM/LOG/MAKE.ERR
pass=0; fail=0; failed=
trap 'rm -rf "$S"' EXIT
[ $# -gt 0 ] || { set --; for f in "$T"/*.EXP; do n=${f##*/}; set -- "$@" "${n%.EXP}"; done; }
for name in "$@"; do
    exp=$T/$name.EXP
    rm -rf "$S"; mkdir -p "$S"; cp "$T/$name.BAS" "$S/"
    out=$(K4510_ROOT=$PWD/fs tools/k4510-bas "/HOME/KBTEST/$name.BAS" 2>&1); st=$?
    miss=
    if head -1 "$exp" | grep -q '^COMPILE-ERROR'; then
        [ $st -ne 0 ] || miss=" [compiled, but should not have]"
        for l in $(tail -n +2 "$exp" | tr ' ' '\001'); do
            l=$(printf '%s' "$l" | tr '\001' ' ')
            grep -qF -- "$l" "$ERRF" || miss="$miss [MAKE.ERR lacks: $l]"
        done
    elif [ $st -ne 0 ]; then
        miss=" [did not compile: $out]"
    else
        keys=$(printf 'CD /HOME/KBTEST\n%s\n~~' "$name")
        if [ -f "$T/$name.IN" ]; then
            while IFS= read -r l; do keys=$(printf '%s~~%s\r' "$keys" "$l"); done < "$T/$name.IN"
            keys="$keys~~"
        fi
        frames=900; [ -f "$T/$name.FRAMES" ] && frames=$(cat "$T/$name.FRAMES")
        screen=$(./test/headless rom/kernal.bin "$keys" "$frames" 2>&1)
        while IFS= read -r l; do
            [ -n "$l" ] || continue
            case $l in
            MAKEERR:*) grep -qF -- "${l#MAKEERR:}" "$ERRF" || miss="$miss [MAKE.ERR lacks: ${l#MAKEERR:}]" ;;
            NOT:*) printf '%s\n' "$screen" | grep -qF -- "${l#NOT:}" && miss="$miss [screen has: ${l#NOT:}]" ;;
            *) printf '%s\n' "$screen" | grep -qF -- "$l" || miss="$miss [screen lacks: $l]" ;;
            esac
        done < "$exp"
    fi
    if [ -n "$miss" ]; then echo "bastest: FAIL $name:$miss"; fail=$((fail+1)); failed="$failed $name"
    else pass=$((pass+1)); fi
done
echo "bastest: $pass passed, $fail failed${failed:+ ($failed )}"
[ $fail -eq 0 ]
