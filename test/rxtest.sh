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
# INTERPRET: simple, with variables, inside a loop, nested blocks, nested
# INTERPRET, a stem, LEAVE inside its own loop, RETURN from inside one in a
# procedure, and SIGNAL refused
cat > fs/LANG/RX/RXTEST3.RX <<'EOF'
interpret 'say "S" 1+2'
x = 5; interpret 'y = x * 2'; say 'V' y
s = ''
do i = 1 to 3
  interpret 's = s || i'
  if i = 2 then interpret 'do j = 1 to 2; s = s"j"j; end'
end
say 'W' s
interpret 'select; when y = 10 then do; say "X ten"; end; otherwise say "X other"; end'
a.1 = 'one'; k = 1
interpret 'say "Y" a.k; a.2 = "two"'
say 'Y2' a.2
interpret 'interpret "say ''Z nested''"'
say 'ZF' f(3)
interpret 'do n = 1 to 5; if n = 3 then leave; end'; say 'ZL' n
interpret 'signal there'
there:
exit
f: procedure
  arg k
  interpret 'r = k * 7'
  if k > 0 then interpret 'return r'
  return 0
EOF
out3=$(./test/headless rom/kernal.bin 'RX RXTEST3
~~~~~~~~~~~~' 1400 2>&1) || true
# Immediate mode: RX alone, a line at a time from the keyboard -- a DO block
# read on at ..>, an error and the prompt again, variables and stems across
# lines, Up recalling a line, a command, PULL, a block dropped with Esc, a runaway loop broken
# with Esc, then EXIT.  Three sessions: the screen the harness reads holds
# about 30 lines.
esc=$(printf '\033')
up=$(printf '\200')                                 # KEY_UP: headless sends $80+ as a key code
out4=$(./test/headless rom/kernal.bin "RX
~x = 4
say 'P' x*x
do i = 1 to 3
say 'Q' i
end
say nosuch(1)
a.1 = 'stem'; k = 1; say 'R' a.k
x = 5
$up$up$up$up$up$up$up
~~exit
say 'after'
~~" 1800 2>&1) || true
out6=$(./test/headless rom/kernal.bin "RX
~x = 4
if x > 3 then
say 'S big'; else say 'S small'
select
when x = 4 then say 'T four'
otherwise nop
end
say 'U' 1 +
say 'U2' x
~~exit
say 'after'
~~" 1800 2>&1) || true
out5=$(./test/headless rom/kernal.bin "RX
~x = 7
do 2
$esc
'DIR /LANG/RX'
say 'V' rc x
pull w
hello there
say 'W' w
do forever
end
\`\`\`\`$esc~
interpret 'say ''X'' x+1'
~$esc~~
say 'after'
~~" 2400 2>&1) || true
rm -f fs/LANG/RX/RXTEST.RX fs/LANG/RX/RXTEST2.RX fs/LANG/RX/RXTEST3.RX
fail() { echo "$out"; echo "$out2"; echo "$out3"; echo "$out4"; echo "$out5"; echo "$out6"; echo "rxtest: FAILED: $1"; exit 1; }
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
echo "$out3" | grep -q 'S 3'                             || fail "INTERPRET"
echo "$out3" | grep -q 'V 10'                            || fail "INTERPRET sharing the variables"
echo "$out3" | grep -q 'W 12j1j23'                       || fail "INTERPRET inside a loop, a loop inside INTERPRET"
echo "$out3" | grep -q 'X ten'                           || fail "INTERPRET of SELECT with a DO inside"
echo "$out3" | grep -q 'Y one'                           || fail "INTERPRET and a stem"
echo "$out3" | grep -q 'Y2 two'                          || fail "a stem assigned inside INTERPRET"
echo "$out3" | grep -q 'Z nested'                        || fail "INTERPRET inside INTERPRET"
echo "$out3" | grep -q 'ZF 21'                           || fail "RETURN from inside INTERPRET in a procedure"
echo "$out3" | grep -q 'ZL 3'                            || fail "LEAVE inside INTERPRET"
echo "$out3" | grep -q 'line 16: SIGNAL inside INTERPRET' || fail "SIGNAL inside INTERPRET is refused, at the INTERPRET's line"
echo "$out4" | grep -q 'RX immediate mode'               || fail "RX alone: immediate mode"
echo "$out4" | grep -q '^P 16'                           || fail "immediate mode: a line runs as typed"
echo "$out4" | grep -q '^\.\.> end'                      || fail "immediate mode: a DO reads on at ..>"
echo "$out4" | grep -q '^Q 3'                            || fail "immediate mode: the DO block ran"
echo "$out4" | grep -q '^RX: unknown function'           || fail "immediate mode: the error"
echo "$out4" | grep -q '^R stem'                         || fail "immediate mode: stems across lines"
echo "$out4" | grep -q '^P 25'                           || fail "immediate mode: Up recalls a line"
echo "$out6" | grep -q '^S big'                          || fail "immediate mode: IF ... THEN, the rest on the next line"
echo "$out6" | grep -q '^T four'                         || fail "immediate mode: SELECT"
echo "$out6" | grep -q '^U2 4'                           || fail "immediate mode: the prompt again after an error"
echo "$out4" | grep -q "^/HOME\] say 'after"             || fail "immediate mode: EXIT returns to K/OS"
echo "$out6" | grep -q "^/HOME\] say 'after"             || fail "immediate mode: EXIT after an error"
echo "$out5" | grep -q 'file(s)'                         || fail "immediate mode: a command"
echo "$out5" | grep -q '^V 0 7'                          || fail "immediate mode: RC, and Esc at ..> drops the block"
echo "$out5" | grep -q '^W HELLO THERE'                  || fail "immediate mode: PULL"
echo "$out5" | grep -q '^RX: interrupted'                || fail "immediate mode: Esc breaks a runaway loop"
echo "$out5" | grep -q '^X 8'                            || fail "immediate mode: INTERPRET, after the break"
echo "$out5" | grep -q "^/HOME\] say 'after"             || fail "immediate mode: Esc at an empty prompt returns to K/OS"
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
echo "rxtest: OK (the language, INTERPRET, immediate mode, both kinds of shell command with RC, and the chess port over @file)"
