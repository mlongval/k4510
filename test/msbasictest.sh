#!/bin/sh
# Microsoft BASIC (basic/msbasic/ + basic/k4510msbasic.asm), driven from the
# shell exactly as a user would: RUN msbasic, then type at it.
#
# Unlike basictest.sh there is no self-checking .BAS to load -- LOAD came
# later, and is tested on its own at the end -- so the program is typed in
# and the answers are read off the screen.  Every check is something a port gets wrong: the cold-start
# prompts being answered from the canned input (a port that gets this wrong
# hangs at "MEMORY SIZE?"), the 9-digit floating point, the CR/LF pairing
# through k_chrout, upper-case folding of typed lower case, and Ctrl-C
# reaching ISCNTC through the keyboard queue's break flag at $D103.
cd "$(dirname "$0")/.."

fail() { echo "$out"; echo "msbasictest: FAILED: $1"; exit 1; }

# cold start, a loop, 9-digit FP, strings, and lower case typed at it
out=$(./test/headless rom/kernal.bin 'CD /LANG/MSBASIC
RUN msbasic
10 FOR I=1 TO 3
20 PRINT I;I*I;SQR(I)
30 NEXT
RUN
PRINT 355/113
A$="K4510"
print left$(a$,2);mid$(a$,2,3);len(a$)
' 3000 2>&1) || fail "MS BASIC did not run"

# MS BASIC never echoes what is typed -- INLIN reads through MONRDKEY and
# prints nothing back -- so the echo is the port's job.  Without it the
# machine looks like it is ignoring the keyboard, which is exactly how this
# was first reported.
echo "$out" | grep -q "PRINT 355/113"  || fail "typed command line not echoed (MONRDKEY echo)"
echo "$out" | grep -q "^OK"            || fail "no OK prompt"
echo "$out" | grep -q "3.14159292"     || fail "floating point division"
echo "$out" | grep -q "K4451 5"        || fail "strings, or lower case was not folded up"
# Three consecutive loop lines: if the LF of BASIC's CR/LF pair reached
# k_chrout (which makes a whole newline of CR *and* LF) the output would be
# double spaced.  headless prints only non-blank rows, so the blank lines
# themselves cannot be seen -- but at 60 rows the run would scroll the first
# iteration off the top before the last one printed.
for n in "1  1  1" "2  4  1.41421356" "3  9  1.73205081"; do
    echo "$out" | grep -q "$n" || fail "loop output line '$n' missing"
done

# The cold start, in a session short enough that the top of it is still on
# the 60-row screen.  Both canned answers are echoed, which is what makes
# them visible here at all.
out=$(./test/headless rom/kernal.bin 'CD /LANG/MSBASIC
RUN msbasic
PRINT 1
' 1200 2>&1) || fail "MS BASIC did not run (cold-start test)"
echo "$out" | grep -q "MEMORY SIZE? 28672" || fail "MEMORY SIZE? not answered or not echoed"
echo "$out" | grep -q "TERMINAL WIDTH? 80" || fail "TERMINAL WIDTH? not answered or not echoed"
echo "$out" | grep -q "BYTES FREE"         || fail "no cold-start banner"
echo "$out" | grep -q "COPYRIGHT 1977"     || fail "no Microsoft banner"

# Ctrl-C into a running program: the break flag at $D103, not a queue poll
out=$(./test/headless rom/kernal.bin 'CD /LANG/MSBASIC
RUN msbasic
10 GOTO 10
RUN
~~'"$(printf '\003')"'
' 3000 2>&1) || fail "MS BASIC did not run (break test)"
echo "$out" | grep -q "BREAK IN  10"   || fail "Ctrl-C did not break into line 10"

# ---- star commands --------------------------------------------------------
# A line beginning with "*" at the READY prompt belongs to K:OS, not to
# BASIC.  Three things are worth guarding, and each of them broke once:
#
#  1. the line reaches the shell at all.  The read loop cannot keep its index
#     in Y -- the ROM's jump-table stubs do not preserve it (rom/crt0.s) --
#     and when it did, the shell was handed an empty line and printed
#     nothing, silently, which looked exactly like the feature not existing.
#  2. *BYE returns to the shell, in the same directory, at a working prompt.
#     It gets there by putting back the stack frame saved before COLD_START.
#  3. a "*" typed at an INPUT prompt inside a running program is DATA.  The
#     guard is CURLIN+1 = $FF, MS BASIC's own direct-mode marker.
out=$(./test/headless rom/kernal.bin 'CD /LANG/MSBASIC
RUN msbasic
*ECHO STARWORKS
PRINT 1
' 12000 2>&1) || fail "MS BASIC did not run (star test)"
echo "$out" | grep -q "^STARWORKS" || fail "*ECHO did not reach the K:OS shell"
echo "$out" | grep -q "^ 1"        || fail "BASIC did not get its prompt back after a star command"

out=$(./test/headless rom/kernal.bin 'CD /LANG/MSBASIC
RUN msbasic
*BYE
ECHO BACKINSHELL
' 12000 2>&1) || fail "MS BASIC did not run (BYE test)"
echo "$out" | grep -q "BACKINSHELL"  || fail "*BYE did not return to a working shell"
echo "$out" | grep -q "/LANG/MSBASIC]"    || fail "*BYE lost the shell's working directory"

out=$(./test/headless rom/kernal.bin 'CD /LANG/MSBASIC
RUN msbasic
10 INPUT A$
20 PRINT "GOT ";A$
RUN
*ZZ
' 16000 2>&1) || fail "MS BASIC did not run (INPUT test)"
echo "$out" | grep -q "GOT \*ZZ" || fail "a star at an INPUT prompt was eaten as a command"

# ---- SAVE / LOAD / *VI ------------------------------------------------------
# A program is kept as its LIST, in text.  Worth guarding: SAVE's steering of
# LIST (program lines to the file, WITHOUT FOUT's leading sign space, and the
# OK to the screen, not the file); LOAD typing it back in, quietly, after a
# NEW; a missing file costing nothing; and *VI's round trip, which chains a
# fed SAVE, VI through the shell, and a fed LOAD (Doc, 2026-09-12).
rm -f fs/HOME/MSTEST.BAS fs/LANG/MSBASIC/PROGRAM.BAS
out=$(./test/headless rom/kernal.bin 'CD /LANG/MSBASIC
RUN msbasic
10 PRINT "SAVED";6*7
20 END
SAVE "/HOME/MSTEST"
NEW
LOAD "/HOME/MSTEST"
RUN
LOAD "/HOME/NOSUCHFILE"
LIST
' 16000 2>&1) || fail "MS BASIC did not run (SAVE/LOAD test)"
cleanup() { rm -f fs/HOME/MSTEST.BAS fs/LANG/MSBASIC/PROGRAM.BAS; }
[ -f fs/HOME/MSTEST.BAS ] || { cleanup; fail "SAVE wrote no /HOME/MSTEST.BAS"; }
want=$(printf '10 PRINT "SAVED";6*7\n20 END\n')
[ "$(cat fs/HOME/MSTEST.BAS)" = "$want" ] || { od -c fs/HOME/MSTEST.BAS | head; cleanup; fail "the .BAS is not the listing (a sign space, an OK, or a lost line?)"; }
echo "$out" | grep -q "^SAVED 42"        || { cleanup; fail "LOAD did not bring the program back (RUN)"; }
echo "$out" | grep -q "?FILE NOT FOUND"  || { cleanup; fail "LOAD of a missing file said nothing"; }
echo "$out" | grep -q '^ 10 PRINT "SAVED"' || { cleanup; fail "a missing file cost the program (NEW before the open?)"; }

out=$(./test/headless rom/kernal.bin 'CD /LANG/MSBASIC
RUN msbasic
10 PRINT "OLD"
*VI
~~:s/OLD/NEW/'"$(printf '\r')"':wq'"$(printf '\r')"'~~RUN
' 30000 2>&1) || { cleanup; fail "MS BASIC did not run (*VI test)"; }
echo "$out" | grep -q '^NEW'  || { echo "$out" | tail -20; cleanup; fail "*VI did not bring the edited program back"; }
grep -q 'PRINT "NEW"' fs/LANG/MSBASIC/PROGRAM.BAS 2>/dev/null || { cleanup; fail "*VI did not edit PROGRAM.BAS"; }
cleanup

echo "msbasictest: OK (cold start answered, echo, FOR/NEXT, 9-digit FP, strings, case folding, Ctrl-C break, star commands, *BYE, SAVE/LOAD as text, *VI)"
