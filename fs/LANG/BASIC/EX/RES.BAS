' Screen sizes -- a tour of MODE WxH: -i integer, -m sharp-bilinear,
' -a native, -c scanlines (-c N how dark), -p plain -- K4510 BASIC
' Each step: ask for a size (SHELL "MODE ..."), say what came, draw on it
' with GRAPHICS 3 (a bitmap the size of the screen MODE made), wait for a key.
' The steps are DATA: the MODE options, then the lines to say, then "-".
' VICKY says what is: $D0C0 panel w/h, $D0D6 screen w/h, $D0DC/DD text grid.
PALETTE 16, 255, 255, 255: PALETTE 17, 255, 80, 80
PALETTE 18, 80, 255, 120: PALETTE 19, 255, 220, 60

ReadVicky
CLS
PRINT "SCREEN SIZES ON THE K4510": PRINT
PRINT "This panel is "; p$; " pixels."
PRINT "The shell's screen now is "; s$; "."
PRINT
PRINT "A program may keep the screen it is"
PRINT "given, or ask for a size of its own:"
PRINT "      *MODE 640x480"
PRINT "(Pascal: SetResolution(640,480,false))"
PRINT
PRINT "If the panel offers that size, it is"
PRINT "used as it is. If not, the machine"
PRINT "draws exactly 640x480 and shows it at"
PRINT "the largest WHOLE multiple that fits:"
PRINT "every pixel the same size, a frame"
PRINT "round the picture. Integer Best Fit."
PRINT
PRINT "How it is shown: -i integer, -m"
PRINT "sharp-bilinear, -a native (any scale,"
PRINT "hard pixels); none: F12's Smoothing."
PRINT "Effects are asked for, never given:"
PRINT "  -c    scanlines, as on a CRT"
PRINT "  -c N  ...N percent dark"
PRINT "  -p    plain again"
PRINT
PRINT "Too large for the panel is refused."
PRINT "When the program ends, the shell gets"
PRINT "its own screen back."
AnyKey

DO
  READ m$
  IF m$ = "end" THEN EXIT DO
  SHELL "MODE " + m$
  ReadVicky
  PRINT "Asked: MODE "; m$
  PRINT "Got:   "; s$; " pixels, "; STR$(PEEK(53468)); "x"; STR$(PEEK(53469)); " text"
  PRINT "Panel: "; p$; ", whole multiple x"; STR$(k%)
  PRINT
  DO
    READ t$
    IF t$ = "-" THEN EXIT DO
    IF t$ = "#" THEN t$ = "whole multiple: x" + STR$(k%) + " here."
    PRINT t$
  LOOP
  DrawAll
  AnyKey
LOOP

' ---- too large: refused
CLS: PRINT "Asking for more than the panel:"
PRINT "      MODE 4000x3000": PRINT
*MODE 4000x3000
ReadVicky
PRINT: PRINT "The screen is unchanged: "; s$
PRINT: PRINT "That is the tour. When this program"
PRINT "ends, the shell's own screen returns."
PRINT "(Try MODE alone at the prompt.)"
AnyKey
END

DATA "640x480"
DATA "VGA's size. On 1080 lines it fits", "twice (1280x960) with a frame round"
DATA "it; 2.25x would make some pixels two", "lines tall and some three.", "-"
DATA "320x240"
DATA "Half each way: every pixel is a", "block of panel pixels. 40 columns.", "-"
DATA "400x300"
DATA "No panel has this size. The machine", "draws 400x300 and finds the largest", "#", "-"
DATA "400x300 -m"
DATA "The same 400x300, now FITTED: the", "whole multiple first, then smoothed"
DATA "the rest of the way to the panel's", "height. Larger, slightly soft edges.", "-"
DATA "320x240 -c"
DATA "Scanlines: the lower half of every", "row dimmed, as a CRT's beam left a"
DATA "dark gap. Only where a row is two", "panel lines or more.", "-"
DATA "400x300 -m -c"
DATA "Smoothed and scanlined together.", "MODE -p would take both off.", "-"
DATA "400x300 -a -c 100"
DATA "NATIVE: as large as fits, any scale,", "hard pixels -- some rows taller than"
DATA "others. And scanlines at 100: black", "gaps. -c N is 1-100; -c alone is F12's.", "-"
DATA "400x300 -i -p"
DATA "INTEGER by name: the whole multiple,", "whatever F12 > Smoothing says.", "-"
DATA "-p -s 1"
DATA "Scale 1: one machine pixel to one", "panel pixel, on the panel's largest"
DATA "4:3. MODE -l lists every size it offers.", "-"
DATA "end"

SUB ReadVicky
  ' p$ the panel, s$ the screen, w h its size, k% the whole multiple
  pw = PEEK(53440) + 256 * PEEK(53441): ph = PEEK(53442) + 256 * PEEK(53443)
  w = PEEK(53462) + 256 * PEEK(53463): h = PEEK(53464) + 256 * PEEK(53465)
  p$ = STR$(pw) + "x" + STR$(ph)
  s$ = STR$(w) + "x" + STR$(h)
  k% = INT(pw / w): IF INT(ph / h) < k% THEN k% = INT(ph / h)
END SUB

SUB DrawAll
  ' the whole screen: a frame, a ruler every 10 pixels, a circle
  GRAPHICS 3
  LINE 0, 0, w - 1, 0, 16: LINE w - 1, 0, w - 1, h - 1, 16
  LINE w - 1, h - 1, 0, h - 1, 16: LINE 0, h - 1, 0, 0, 16
  FOR x% = 10 TO w - 1 STEP 10
    IF x% MOD 50 = 0 THEN l% = 7 ELSE l% = 3
    LINE x%, h - 1, x%, h - 1 - l%, 19
  NEXT
  FOR y% = 10 TO h - 1 STEP 10
    IF y% MOD 50 = 0 THEN l% = 7 ELSE l% = 3
    LINE w - 1, y%, w - 1 - l%, y%, 19
  NEXT
  CIRCLE w * 3 / 4, h * 2 / 3, h / 5, 18
  LINE w * 3 / 4 - h / 5, h * 2 / 3, w * 3 / 4 + h / 5, h * 2 / 3, 17
  LINE w * 3 / 4, h * 2 / 3 - h / 5, w * 3 / 4, h * 2 / 3 + h / 5, 17
END SUB

SUB AnyKey
  PRINT: PRINT "Any key...";
  DO: GET k$: LOOP UNTIL k$ <> ""
  GRAPHICS 0
END SUB
