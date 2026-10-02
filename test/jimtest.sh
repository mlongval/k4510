#!/bin/sh
# The console is JIM's (D-11): every byte the ROM prints goes to the terminal
# at $DA00.  These are the things that actually broke while it was being moved
# over, so they are the things worth guarding.
#
# Note what this file does NOT try to check: blank lines.  test/headless prints
# only non-blank rows of the text screen, so a lost blank line is invisible to
# it -- that one needs a screenshot, and it is checked by eye.  Everything here
# is something headless can genuinely see.
cd "$(dirname "$0")/.."

fail() { echo "$out"; echo "jimtest: FAILED: $1"; exit 1; }

cat > fs/SYSTEM/LOG/JIMTEST.TXT <<'EOT'
LINE-ONE

LINE-THREE
EOT

# LNM (ANSI mode 20).  JIM's LF moves down without returning the column -- that
# is correct VT100 -- but the ROM ends its lines with a bare \n and expects
# column 0 back.  video_init sets LNM to make that so.  Without it the console
# staircases: every line starts further right than the last.
out=$(./test/headless rom/kernal.bin 'TYPE /SYSTEM/LOG/JIMTEST.TXT
' 900 2>&1) || fail "TYPE did not run"
echo "$out" | grep -q "^LINE-ONE"   || fail "first line not at column 0"
echo "$out" | grep -q "^LINE-THREE" || fail "line after a newline not at column 0 (LNM)"
rm -f fs/SYSTEM/LOG/JIMTEST.TXT

# CHROUT has always promised that CR makes a whole newline, and guest programs
# rely on it -- EhBASIC's glue, BBC BASIC and CP/M all send a bare CR and mean
# "next line".  JIM's own CR is a carriage return only, so k_chrout folds it
# onto \n.  When this broke, EhBASIC's output overprinted itself on one row.
out=$(./test/headless rom/kernal.bin 'CD /LANG/MSBASIC
RUN msbasic
PRINT "JIMCR-A"
PRINT "JIMCR-B"
' 3000 2>&1) || fail "MS BASIC did not run"
echo "$out" | grep -q "^JIMCR-A" || fail "CR is not folded onto newline (output overprints)"
echo "$out" | grep -q "^JIMCR-B" || fail "second line overprinted the first"

# ANSI mode, driven from a program through CHROUT -- which is the point of it:
# the console IS the terminal, so a program needs no special access to use
# escape sequences.  This was ANSIDEMO until 2026-09-20, when that demo was
# nuked and unlisted (SHIPPING.CFG); EhBASIC does the same job and always
# ships.  The machine folds a typed letter to upper case, so SGR's lower-case
# terminator has to come from CHR$(109): typed, it arrives as ESC[1;37M, which
# is a different command altogether and swallows the title.
out=$(./test/headless rom/kernal.bin 'EHBASIC
10 E$=CHR$(27):M$=CHR$(109)
20 PRINT E$;"[2J";E$;"[1;1H";
30 PRINT E$;"[1;37";M$;"TITLE-AT-ZERO";E$;"[0";M$
40 PRINT "0123456789";E$;"[5D";E$;"[K";"END"
50 PRINT E$;"[3C";"INDENTED"
RUN
' 2500 2>&1) || fail "EhBASIC did not run"
echo "$out" | grep -q "^TITLE-AT-ZERO" || fail "ANSI: SGR took columns -- the title is not at column 0"
echo "$out" | grep -q "^01234END"      || fail "ANSI: cursor back five, then erase to end of line"
echo "$out" | grep -q "^   INDENTED"   || fail "ANSI: cursor forward three"

# PETSCII.PRG writes straight to $DA00 in PETSCII mode, then must put the
# terminal back into ANSI -- leave it in PETSCII and the shell comes back to a
# screen it cannot drive.  The ECHO afterwards is the proof.
# Two runs, because the demo clears the screen as it hands the terminal back:
# one screen cannot show both the demo and the proof that the shell survived it.
out=$(./test/headless rom/kernal.bin 'PETSCII
' 900 2>&1) || fail "PETSCII did not run"
echo "$out" | grep -q "JIM IN PETSCII"     || fail "PETSCII: title missing"
echo "$out" | grep -q "THE SIXTEEN COLOUR" || fail "PETSCII: colour section missing"

out=$(./test/headless rom/kernal.bin 'PETSCII
~~~
ECHO JIMANSIBACK
' 1800 2>&1) || fail "PETSCII did not run (handback)"
echo "$out" | grep -q "JIMANSIBACK"        || fail "PETSCII: the shell was left in PETSCII mode"

# BANDS.PRG takes the status bands (VICKY $D0B0-$D0B2 since 2026-10-01), draws in them,
# and hands them back.  The same discipline as PETSCII mode above, and checked
# the same way, because nothing but the program enforces it.
#
# Note this runs with the bands switched OFF in the host's settings -- the
# headless harness has no k4510.cfg -- which is deliberate: a program claiming
# the bands gets them whether or not the user has them on, and if that ever
# stopped being true this test would go quiet rather than fail.
out=$(./test/headless rom/kernal.bin 'RUN bands
' 300 2>&1) || fail "BANDS did not run"
echo "$out" | grep -q "belong to this program"   || fail "BANDS: the claimed top band was not drawn"
echo "$out" | grep -q "K/OS is not drawing here" || fail "BANDS: the second row of the claimed band is missing"
echo "$out" | grep -q "console text scrolls"     || fail "BANDS: the console did not scroll between the bands"

# Two runs, as with PETSCII: the demo clears the console as it hands back, so
# one screen cannot show both the claim and the proof that the shell survived.
out=$(./test/headless rom/kernal.bin 'RUN bands
~~~~~~
q
ECHO BANDSBACK
' 2000 2>&1) || fail "BANDS did not run (handback)"
echo "$out" | grep -q "handed back"  || fail "BANDS: never reached its hand-back"
echo "$out" | grep -q "BANDSBACK"    || fail "BANDS: the shell did not survive the hand-back"

# The user's band clock carries today's date.  It came up 00.00.0000 for most
# of 2026-10-01: cc65 had compiled the RTC latch before draw_clock to nothing,
# and the band's MHz (gone that morning) had been latching it by accident.
top=$(K4510_SYSOPT=0x0C ./test/headless rom/kernal.bin '' 60 2>/dev/null | head -1)
case "$top" in *"$(date +%Y)"*) ;; *) fail "the band's clock has no date (the RTC was not latched): $top" ;; esac

echo "jimtest: OK (LNM column reset, CR folded onto newline, ANSI from EhBASIC: SGR at column 0, erase to end of line, cursor forward; PETSCII and BANDS hand back; the band clock's date)"
