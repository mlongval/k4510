#!/bin/sh
# test/nvimtest.sh -- the NeoVim Tube's setup (tools/nvim, tools/k4510-nvim),
# on the host with a headless Neovim: each language's filetype and syntax,
# :make's error list from MAKE.ERR, :Run's hand-over to the machine
# (NVIM.BAT, NVIM.RESUME, the wrapper's 42), and coming back where it was.
# The compilers are stand-ins: this tests the setup, not cc65.
set -e
cd "$(dirname "$0")/.."
command -v nvim >/dev/null 2>&1 || { echo "nvimtest: skipped (no nvim here)"; exit 0; }
K=$(pwd)
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
R=$T/fs; mkdir -p "$R/HOME/GAME" "$R/SYSTEM/LOG" "$T/bin"
fails=0
check() { if [ "$2" = "$3" ]; then echo "  ok   $1"; else echo "  FAIL $1: got '$2', want '$3'"; fails=$((fails + 1)); fi; }
res() { sed -n 's/.*@@\(.*\)@@.*/\1/p' | tail -1; }   # the answer, out of whatever else Neovim printed
out() { printf "lua io.stdout:write('@@' .. (%s) .. '@@')" "$1"; }
atstart() { printf "autocmd VimEnter * lua io.stdout:write('@@' .. (%s) .. '@@') vim.cmd('qa!')" "$1"; }   # after start-up, the resume's work included
                                                                        # (:lua takes the rest of the line: the quit is Lua too)

# a stand-in k4510-cc / k4510-pas: files named BAD* get an error on line 3
# and a linker warning, as tools/k4510-errfmt writes them; others compile
for t in k4510-cc k4510-pas; do cat > "$T/bin/$t" <<'EOF'
#!/bin/sh
[ "$1" = -p ] && f=$2 || f=$1
case "$f" in
  BAD*) printf '%s:3:5:E:missing semicolon\n-:0:0:W:ld65 said something\n' "$f" > "$K4510_ROOT/SYSTEM/LOG/MAKE.ERR"; exit 1 ;;
  *)    : > "$K4510_ROOT/SYSTEM/LOG/MAKE.ERR"; echo "$(pwd) $*" > "$K4510_ROOT/SYSTEM/LOG/LASTCOMPILE"; exit 0 ;;
esac
EOF
chmod +x "$T/bin/$t"; done
export K4510_ROOT=$R PATH=$T/bin:$PATH HOME=$T
printf 'void main(void)\n{\n    int x = 1\n}\n' > "$R/HOME/BAD.C"
printf 'void main(void)\n{\n}\n' > "$R/HOME/OK.C"
printf 'say "hello"\nx = 1 +\n' > "$R/HOME/HI.RX"
printf 'NAME=GAME\nLANG=C\nSRC=MAIN.C\n' > "$R/HOME/GAME/PROJECT.K4P"
printf 'void main(void) {}\n' > "$R/HOME/GAME/MAIN.C"
N() { (cd "$R/HOME" && nvim --headless -u "$K/tools/nvim/init.lua" "$@" 2>&1); }
W() { (cd "$R/HOME" && "$K/tools/k4510-nvim" "$@" 2>&1); }

echo "1. the languages, by their extensions in capitals"
for pair in "OK.C c" "X.PAS pascal" "HI.RX rexx" "X.BAS k4510basic" "X.BBC bbcbasic" "X.LGO k4510logo" "x.bas k4510basic" "GAME/PROJECT.K4P dosini"; do
    set -- $pair
    got=$(N -c "$(out "vim.bo.filetype .. ' ' .. (vim.b.current_syntax or '-')")" -c 'qa!' "$1" | res)
    check "$1" "$got" "$2 $2"
done

echo "2. :make -- the error list from MAKE.ERR, and to the first error"
Q="#vim.fn.getqflist() .. ' ' .. vim.fn.line('.') .. ' ' .. vim.fn.col('.')"
got=$(N -c 'Make' -c "$(out "$Q .. ' ' .. vim.fn.fnamemodify(vim.fn.bufname(vim.fn.getqflist()[1].bufnr), ':t') .. ' ' .. vim.fn.getqflist()[1].type")" -c 'qa!' BAD.C | res)
check "BAD.C: two messages, at 3:5 of BAD.C, an error" "$got" "2 3 5 BAD.C E"
got=$(N -c 'Make' -c "$(out "#vim.fn.getqflist()")" -c 'qa!' OK.C | res)
check "OK.C: compiles, nothing in the list" "$got" "0"
got=$(N -c 'call feedkeys(":make\<CR>", "tx")' -c "$(out "#vim.fn.getqflist()")" -c 'qa!' BAD.C | res)
check ":make, typed, is the machine's make" "$got" "2"
N -c 'Make' -c 'qa!' GAME/MAIN.C >/dev/null
check "a project builds with -p, in its folder" "$(cat "$R/SYSTEM/LOG/LASTCOMPILE")" "$R/HOME/GAME -p PROJECT.K4P"

echo "3. :Run -- the command for the machine, and the wrapper's 42"
set +e; W --headless -c 'normal 2G' -c 'Run' OK.C >/dev/null; st=$?; set -e
check "the wrapper exits 42" "$st" "42"
check "NVIM.BAT runs the program" "$(cat "$R/SYSTEM/LOG/NVIM.BAT")" "SWAP -k /HOME/OK"
check "NVIM.RESUME: the file and the line" "$(tr '\n' ' ' < "$R/SYSTEM/LOG/NVIM.RESUME")" "$R/HOME/OK.C 2 "
got=$(W --resume --headless -c "$(atstart "vim.fn.expand('%:t') .. ' ' .. vim.fn.line('.')")" | res)
check "--resume: back in the file, on the line" "$got" "OK.C 2"
check "and the hand-over files are gone" "$(ls "$R/SYSTEM/LOG" | grep -c NVIM || true)" "0"
set +e; W --headless -c 'Run' HI.RX >/dev/null; set -e
check "REXX: its interpreter runs the file" "$(cat "$R/SYSTEM/LOG/NVIM.BAT")" "SWAP -k RX /HOME/HI.RX"
set +e; W --headless -c 'Run' GAME/MAIN.C >/dev/null; set -e
check "a project: its program" "$(cat "$R/SYSTEM/LOG/NVIM.BAT")" "SWAP -k /HOME/GAME/GAME"
rm -f "$R/SYSTEM/LOG/NVIM.BAT" "$R/SYSTEM/LOG/NVIM.RESUME"
set +e; W --headless -c 'Run' -c 'qa!' BAD.C >/dev/null; st=$?; set -e
check "a build that fails runs nothing" "$st $(ls "$R/SYSTEM/LOG" | grep -c NVIM.BAT || true)" "0 0"

echo "4. back from a REXX run that stopped: to its line"
printf 'HI.RX:2:1:E:RX: line 2: an expression is missing\n' > "$R/SYSTEM/LOG/MAKE.ERR"
printf '%s\n1\n' "$R/HOME/HI.RX" > "$R/SYSTEM/LOG/NVIM.RESUME"
got=$(W --resume --headless -c "$(atstart "#vim.fn.getqflist() .. ' ' .. vim.fn.line('.')")" | res)
check "the REXX error in the list, and on line 2" "$got" "1 2"

echo "5. the whole round, on the machine: NVIM, :Run, the program, a key, back, :q"
if [ -x "$K/test/headless" ] && [ -f "$K/fs/SYSTEM/BIN/nvim.prg" ]; then
    printf 'say "hello from REXX, run by NVIM"\n' > "$K/fs/HOME/NVTEST.RX"
    log=$(cd "$K" && K4510_ROOT= K4510_SYSOPT=0x04 timeout 150 ./test/headless rom/kernal.bin 'CD /HOME
~NVIM NVTEST.RX
~~~~~~:Run
~~~~~~~~ ~~~~~~~~:q
~~~~' 3600 2>&1) || true
    rm -f "$K/fs/HOME/NVTEST.RX" "$K/fs/SYSTEM/LOG/NVIM.BAT" "$K/fs/SYSTEM/LOG/NVIM.RESUME"
    check "Neovim handed the run to the machine (42)" "$(echo "$log" | grep -c 'cmd: k4510-nvim NVTEST.RX' ) $(echo "$log" | grep -c 'ended: exit 42')" "1 1"
    check "and came back, and ended" "$(echo "$log" | grep -c 'cmd: k4510-nvim --resume') $(echo "$log" | grep -c 'ended: exit 0')" "1 1"
    check "at the prompt" "$(echo "$log" | grep -v '^ *$' | tail -1 | tr -d ' ')" "/HOME]"
else
    echo "  skipped (no test/headless or nvim.prg)"
fi

if [ $fails -eq 0 ]; then echo "nvimtest: OK"; else echo "nvimtest: $fails FAILED"; exit 1; fi
