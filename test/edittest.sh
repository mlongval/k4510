#!/bin/sh
# EDIT, MS-DOS EDIT's manner on VI's engine (demo/edit.c, 2026-10-02): driven
# headless, keys only (the mouse is the frontend's; checked under Xvfb by hand,
# docs/BUILD-LOG.md).  Each run types into a file, saves, leaves, and the file
# on the host is what is checked -- plus the screen and two cells' colours.
# Keys: ~ waits 30 frames; $80+ are key codes (Alt+S = $D3, F3 = $92, Home = $84),
# written in octal because sh's printf has no \x.
set -e
cd "$(dirname "$0")/.."
H=fs/HOME
fail() { echo "edittest: FAILED: $1"; rm -f $H/ZZED*.TXT $H/ZZED.BAS $H/ZZED6.BBC; exit 1; }
run() { ./test/headless rom/kernal.bin "$1" "${2:-900}" 2>/dev/null; }
rm -f $H/ZZED*.TXT $H/ZZED.BAS

# 1. type, Enter keeps the indent, save, leave
run "$(printf 'edit ZZED1.TXT\n~~hello world\r  indented\rnext\023~\021~~')" >/dev/null
printf 'hello world\n  indented\n  next\n' | cmp -s - $H/ZZED1.TXT || fail "typed text not saved as typed: $(od -c $H/ZZED1.TXT | head -3)"

# 2. Search > Change (Alt+S, C): every match, through the two-field dialog
printf 'one fish\ntwo fish\nred fish\n' > $H/ZZED2.TXT
run "$(printf 'edit ZZED2.TXT\n~~\323c~fish\tcat\r~\023~\021~~')" >/dev/null
printf 'one cat\ntwo cat\nred cat\n' | cmp -s - $H/ZZED2.TXT || fail "Change did not change all three: $(cat $H/ZZED2.TXT)"

# 3. Find (Ctrl+F) puts the cursor on the match; F3 finds the next one
printf 'aa bb cc bb\n' > $H/ZZED3.TXT
run "$(printf 'edit ZZED3.TXT\n~~\006~bb\r~\222X\023~\021~~')" >/dev/null
printf 'aa bb cc Xbb\n' | cmp -s - $H/ZZED3.TXT || fail "Find then F3: $(cat $H/ZZED3.TXT)"

# 4. Go to line (Ctrl+G); undo (Ctrl+Z) takes back a whole run of typing
printf 'l1\nl2\nl3\n' > $H/ZZED4.TXT
run "$(printf 'edit ZZED4.TXT\n~~\007~3\r~Z\204junk\032\023~\021~~')" >/dev/null
printf 'l1\nl2\nZl3\n' | cmp -s - $H/ZZED4.TXT || fail "Go to line / undo: $(cat $H/ZZED4.TXT)"

# 5. Ctrl+R renumbers a BASIC file, GOTOs and all (the old EDIT's key, kept)
printf '5 GOTO 7\n7 PRINT "X"\n' > $H/ZZED.BAS
run "$(printf 'edit ZZED.BAS\n~~\022~\023~\021~~')" >/dev/null
printf '10 GOTO 20\n20 PRINT "X"\n' | cmp -s - $H/ZZED.BAS || fail "Ctrl+R: $(cat $H/ZZED.BAS)"

# 6. a changed file is not dropped: Ctrl+Q asks, Esc stays, and the text is still there to save
run "$(printf 'edit ZZED5.TXT\n~~kept\021~\033~\023~\021~~')" >/dev/null
printf 'kept\n' | cmp -s - $H/ZZED5.TXT || fail "Exit with changes did not ask, or Esc did not stay"

# 7. the screen: menu bar, window title, status line -- and DOS EDIT's colours
#    (menu bar black on light grey, text on blue), or with -s the console's
out=$(K4510_DUMP=030000,4 run "$(printf 'edit ZZED2.TXT\n~~')" 200)
echo "$out" | grep -q "File  Edit  Search  Options  Help" || fail "no menu bar"
echo "$out" | grep -q "ZZED2.TXT"                         || fail "no title in the frame"
echo "$out" | grep -q "K4510 Editor"                      || fail "no status line"
case "$(echo "$out" | tail -1)" in *" 00 0F") ;; *) fail "menu bar not black on light grey: $(echo "$out" | tail -1)" ;; esac   # glyph, 0, fg, bg
txt=$(K4510_DUMP=030284,4 run "$(printf 'edit ZZED2.TXT\n~~')" 200 | tail -1)
case "$txt" in *" 06") ;; *) fail "text not on blue: $txt" ;; esac
sys=$(K4510_DUMP=030000,4 run "$(printf 'edit -s ZZED2.TXT\n~~')" 200)
echo "$sys" | grep -q "ZZED2.TXT" || fail "-s did not take the name after it"
case "$(echo "$sys" | tail -1)" in *" 0F") fail "-s still DOS's grey menu bar" ;; esac

# 8. BBC BASIC's keywords in capitals: Ctrl+U (and Ctrl+Z takes it back as one), and
#    -u at every save; strings, REM, DATA, a star command and variables stay as typed
B='10 for i%=1 to 3: print "hi ";i%: next
20 rem print me
30 total=5: procshow(total): print left$("abcd",2): *fx 15
40 data print
50 def procshow(x): print "x=";x: endproc'
U='10 FOR i%=1 TO 3: PRINT "hi ";i%: NEXT
20 REM print me
30 total=5: PROCshow(total): PRINT LEFT$("abcd",2): *fx 15
40 DATA print
50 DEF PROCshow(x): PRINT "x=";x: ENDPROC'
echo "$B" > $H/ZZED6.BBC
run "$(printf 'edit ZZED6.BBC\n~~\025~\023~\021~~')" >/dev/null
echo "$U" | cmp -s - $H/ZZED6.BBC || fail "Ctrl+U: $(cat $H/ZZED6.BBC)"
echo "$B" > $H/ZZED6.BBC
run "$(printf 'edit ZZED6.BBC\n~~\025~\032~\023~\021~~')" >/dev/null
echo "$B" | cmp -s - $H/ZZED6.BBC || fail "Ctrl+U then Ctrl+Z did not put it all back: $(cat $H/ZZED6.BBC)"
echo "$B" > $H/ZZED6.BBC
run "$(printf 'edit -u ZZED6.BBC\n~~\023~\021~~')" >/dev/null
echo "$U" | cmp -s - $H/ZZED6.BBC || fail "-u did not capitalise at the save: $(cat $H/ZZED6.BBC)"
rm -f $H/ZZED6.BBC

# 8. Options > Tab Width (Alt+O, T): Tab puts spaces, never a tab character, to the width chosen
run "$(printf 'edit ZZED8.TXT\n~~\317~t~\b2\r~\tx\tyy\tz\023~\021~~')" >/dev/null
printf '  x yy  z\n' | cmp -s - $H/ZZED8.TXT || fail "Tab Width 2: $(od -c $H/ZZED8.TXT | head -3)"

rm -f $H/ZZED*.TXT $H/ZZED.BAS
echo "edittest: OK (typing and indent, Change, Find and F3, Go to line and undo, renumber, Exit asks, the screen in both colour schemes, BBC keywords in capitals by Ctrl+U and -u, Options > Tab Width)"
