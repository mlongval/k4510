#!/bin/sh
# RX, the REXX interpreter (demo/rexx.c): the language, the two kinds of
# shell command, and the port.  Two scripts rather than one, because a
# script's output now survives to the end of the run and a long one scrolls
# off the screen the harness reads.
set -e
cd "$(dirname "$0")/.."
export K4510_NO_STARTUP=1
mkdir -p fs/LANG/RX
cat > fs/LANG/RX/RXTEST.RX <<'EOF'
say 'A' 2+3*4 (7-1)/2 7//3 2**10
say 'B' 'abc'||'def' length('hello') substr('abcdef',2,3) word('one two three',3) words('a b c')
say 'C' upper('mix') reverse('abc') pos('cd','abcde') copies('xy',3) strip('  s  ')
x = 10
if x > 5 then say 'D big'; else say 'D small'
n = 0
do i = 1 to 4; n = n + i; end
say 'E' n
do forever; n = n + 1; if n > 12 then leave; end
say 'F' n
do j = 6 to 1 by -5; say 'G' j; end
select
  when x = 1 then say 'H one'
  when x = 10 then say 'H ten'
  otherwise say 'H other'
end
parse value 'John Smith 42' with first last age
say 'I' last first age+1
parse value 'k=v;rest' with k '=' v ';' r
say 'J' k v r
a.1 = 'one'; i = 1
say 'K' a.i a.9
call greet 'Doc'
say 'L' result
say 'M' square(7) fact(5)
z = 99
call show
say 'N' z
say 'O' datatype(12) datatype('x','N') symbol('QQ')
exit 0
greet: procedure
  parse arg who
  return 'hi' who
square: return arg(1) * arg(1)
fact: procedure
  arg k
  if k <= 1 then return 1
  return k * fact(k - 1)
show: procedure expose z
  z = z + 1
  return
EOF
cat > fs/LANG/RX/RXTEST2.RX <<'EOF'
'DIR /LANG/RX'                           /* a built-in: runs as if typed */
say 'P' rc
'SAY through-a-swap'                /* a program: SWAP carries the script over it, -k keeps what it drew */
say 'P2' rc
'NOSUCHTHING'
say 'Q' rc
signal done
say 'never'
done:
say 'R done'
EOF
out=$(./test/headless rom/kernal.bin 'RX RXTEST
~~~~~~~~~~~~' 1400 2>&1) || true
out2=$(./test/headless rom/kernal.bin 'RX RXTEST2
~~~~~~~~~~~~' 1400 2>&1) || true
rm -f fs/LANG/RX/RXTEST.RX fs/LANG/RX/RXTEST2.RX
fail() { echo "$out"; echo "$out2"; echo "rxtest: FAILED: $1"; exit 1; }
echo "$out" | grep -q 'A 14 3 1 1024'                     || fail "arithmetic"
echo "$out" | grep -q 'B abcdef 5 bcd three 3'            || fail "string functions"
echo "$out" | grep -q 'C MIX cba 3 xyxyxy s'              || fail "more string functions"
echo "$out" | grep -q 'D big'                             || fail "IF/THEN/ELSE"
echo "$out" | grep -q 'E 10'                              || fail "DO n TO m"
echo "$out" | grep -q 'F 13'                              || fail "DO FOREVER / LEAVE"
echo "$out" | grep -q 'G 6'                               || fail "DO ... BY -5"
echo "$out" | grep -q 'H ten'                             || fail "SELECT/WHEN"
echo "$out" | grep -q 'I Smith John 43'                   || fail "PARSE with words"
echo "$out" | grep -q 'J k v rest'                        || fail "PARSE with patterns"
echo "$out" | grep -q 'K one A.9'                         || fail "compound variables"
echo "$out" | grep -q 'L hi Doc'                          || fail "CALL and RESULT"
echo "$out" | grep -q 'M 49 120'                          || fail "functions and recursion"
echo "$out" | grep -q 'N 100'                             || fail "PROCEDURE EXPOSE"
echo "$out" | grep -q 'O NUM 0 LIT'                       || fail "DATATYPE / SYMBOL"
echo "$out2" | grep -q 'file(s)'                          || fail "a built-in command did not reach the shell"
echo "$out2" | grep -q 'P 0'                              || fail "RC after a command that worked"
echo "$out2" | grep -q 'through-a-swap'                   || fail "a program's output did not survive SWAP -k"
echo "$out2" | grep -q 'P2 0'                             || fail "RC after a program run through SWAP"
echo "$out2" | grep -q 'Q 1'                              || fail "RC after a command that failed"
echo "$out2" | grep -q 'R done'                           || fail "SIGNAL"
# the port: CHESS answers @file, keeps its position between calls, and plays
rm -f fs/APPS/CHESS/PORT.GAM fs/LANG/RX/PORT.RPL
printf 'NEW\nMOVE e2e4\nFEN\n' > fs/LANG/RX/PORT.CMD
./test/headless rom/kernal.bin 'RUN CHESS @/LANG/RX/PORT.CMD
~~~~~~' 800 >/dev/null 2>&1 || true
grep -q '4P3' fs/LANG/RX/PORT.RPL 2>/dev/null || { echo "rxtest: FAILED: the chess port did not answer with the position"; exit 1; }
printf 'STATUS\n' > fs/LANG/RX/PORT.CMD
./test/headless rom/kernal.bin 'RUN CHESS @/LANG/RX/PORT.CMD
~~~~~~' 700 >/dev/null 2>&1 || true
grep -q 'black to move' fs/LANG/RX/PORT.RPL 2>/dev/null || { echo "rxtest: FAILED: the chess port forgot the position between calls"; exit 1; }
rm -f fs/LANG/RX/PORT.CMD fs/LANG/RX/PORT.RPL fs/APPS/CHESS/PORT.GAM
echo "rxtest: OK (the language, both kinds of shell command with RC, and the chess port over @file)"
