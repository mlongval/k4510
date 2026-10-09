' K4510 BASIC: how to use it -- a program that says so
CLS
PRINT "K4510 BASIC: CLASSIC WORDS, QUICKBASIC'S SHAPE, COMPILED"
PRINT
PRINT "  WRITE  NAME.BAS in PROG (or VI); no line numbers needed"
PRINT "  BUILD  F9 in PROG, or BAS NAME at the prompt -> name.prg"
PRINT "  RUN    Ctrl+F9 in PROG, or NAME at the prompt"
PRINT "  ERRORS land on their line in PROG, the compiler's and the program's own"
PRINT
PRINT "  IF ... ELSEIF ... ELSE ... END IF   SELECT CASE   DO ... LOOP   WHILE ... WEND"
PRINT "  SUB / FUNCTION with parameters; DIM inside one makes it local"
PRINT "  labels (Again:) or line numbers for GOTO and GOSUB; CONST; DATA / READ"
PRINT "  GRAPHICS 0-3  PLOT LINE TRI BOX CIRCLE GCLS  PALETTE  SPRDEF SPRITE SPROFF"
PRINT "  LOCATE COLOR CLS INKEY$   *MODE 640x480 or SHELL ""..."" for K/OS"
PRINT
PRINT "  No immediate mode: RX is the scratchpad (PRINT 6*7 there prints 42)."
PRINT "  Old numbered programs compile too: their line numbers are labels."
PRINT "  The whole language: the handbook's K4510 BASIC chapter."
PRINT
PRINT "TRY: DEMOS (the menu), or open INVADERS.BAS in PROG and press Ctrl+F9"
PRINT
PRINT "ANY KEY...";
DO: GET k$: LOOP UNTIL k$ <> ""
