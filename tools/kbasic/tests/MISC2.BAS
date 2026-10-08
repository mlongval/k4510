' small conveniences
a = 1: b = 2: SWAP a, b: PRINT "S:"; a; b
x$ = "  pad  ": PRINT "T:["; LTRIM$(x$); "]["; RTRIM$(x$); "]"
PRINT "H:"; HEX$(255); " "; STRING$(3, "*"); STRING$(2, 65)
PRINT USING "U:###.## and ##"; 3.14159; 42
PRINT USING "!-&"; "Yes"; "no"
i = 0
REPEAT
  i = i + 1
UNTIL i = 4
PRINT "R:"; i
f = FRAMES: WAITFRAME 6: PRINT "W:"; FRAMES - f > 5; TIMER > 0
SOUND 440, 0.1: PLAY "T200 O4 L8 C D E": PLAY 2, "C": SOUND OFF
PRINT "J:"; JOY(0); KEYDOWN(16)
SPRDEF 0, 1024, 8, 8, 8: MOVSPR 0, 10, 20: SPRVEL 0, 2, 1: WAITFRAME 3
PRINT "P:"; SPRX(0); SPRY(0); HIT(0)
DO: LOOP UNTIL INKEY$ <> ""
