#!/bin/sh
# test/errfmttest.sh -- tools/k4510-errfmt on what the compilers really print
# (captured on p15, 2026-09-14): cc65, ld65, and Mad Pascal wrapping its own
# messages at 80 columns.  VI's :make reads only what comes out of it.
set -u
F="$(dirname "$0")/../tools/k4510-errfmt"
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
fails=0
check() {   # name, input file, wanted file
    sh "$F" < "$2" > "$t/got"
    if cmp -s "$t/got" "$3"; then echo "ok   $1"
    else echo "FAIL $1"; diff "$3" "$t/got" | sed 's/^/     /'; fails=$((fails + 1)); fi
}

cat > "$t/cc.in" <<'EOF'
BAD.C(5): Error: ';' expected
BAD.C(5): Error: Undefined symbol: 'y'
/home/doc/k4510/demo/k4510.h(12): Warning: Constant is long
EOF
cat > "$t/cc.want" <<'EOF'
BAD.C:5:0:E:';' expected
BAD.C:5:0:E:Undefined symbol: 'y'
k4510.h:12:0:W:Constant is long
EOF
check "cc65" "$t/cc.in" "$t/cc.want"

cat > "$t/ld.in" <<'EOF'
Unresolved external '_f' referenced in:
  link.s(125)
ld65: Error: 1 unresolved external(s) found - cannot create output file
EOF
cat > "$t/ld.want" <<'EOF'
-:0:0:E:Unresolved external '_f'
-:0:0:E:1 unresolved external(s) found - cannot create output file
EOF
check "ld65" "$t/ld.in" "$t/ld.want"

cat > "$t/mp.in" <<'EOF'
Mad Pascal Compiler version 1.7.8 [2026/08/26] for MOS 6502 CPU
Compiling BAD.PAS
Pass 1
BAD.PAS (line 5, column 3): Error: E40 - SyntaxError: Syntax error, 'END' expect
ed but 'identifier' found.
8 lines compiled, 0.52 sec, 6797 tokens, 494 idents, 115 blocks, 5 types
Program ended with exit code 2
EOF
cat > "$t/mp.want" <<'EOF'
BAD.PAS:5:3:E:Syntax error, 'END' expected but 'identifier' found.
EOF
check "Mad Pascal, one wrap" "$t/mp.in" "$t/mp.want"

cat > "$t/mpu.in" <<'EOF'
Compiling USEIT.PAS
myunit.pas (line 3, column 20): Error: Procedure name expected but  found
9 lines compiled, 0.52 sec, 6822 tokens, 494 idents, 115 blocks, 5 types
EOF
cat > "$t/mpu.want" <<'EOF'
myunit.pas:3:20:E:Procedure name expected but  found
EOF
check "Mad Pascal, in a unit" "$t/mpu.in" "$t/mpu.want"

printf '%s\n' "USEIT.PAS (line 2, column 12): Error: E34 - FileNotFound: Cannot find unit 'myun" \
              "it' used by program 'Program' in path 'unit path '/home/doc/Projects/neo6502_dev" \
              "/Mad-Pascal/lib/''." \
              "2 lines compiled, 0.51 sec, 6 tokens, 0 idents, 0 blocks, 0 types" > "$t/mp2.in"
echo "USEIT.PAS:2:12:E:Cannot find unit 'myunit' used by program 'Program' in path 'unit path '/home/doc/Projects/neo6502_dev/Mad-Pascal/lib/''." > "$t/mp2.want"
check "Mad Pascal, two wraps" "$t/mp2.in" "$t/mp2.want"

[ $fails -eq 0 ] && echo "errfmttest: all passed" || echo "errfmttest: $fails FAILED"
exit $fails
