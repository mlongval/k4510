' Space Invaders on the text screen -- K4510 BASIC
' The EhBASIC game (/LANG/EHBASIC/EX/INVADERS.BAS) poked the screen's
' memory through a bank window.  This one is written the K4510 BASIC
' way: LOCATE, COLOR and PRINT draw, SUBs and FUNCTIONs hold the rules,
' and what is where is worked out, not read back from the screen.
' Keys: A/D or the cursor keys move, space fires, Q quits.
CONST SND = 54752            ' the sound sequencer: channel, amp, pitch, duration
DIM alive%(24)

w% = PEEK(55813): h% = PEEK(55814)          ' JIM's console: columns, rows
IF w% < 60 OR h% < 20 THEN
  PRINT "INVADERS needs 60 x 20 text at least -- try MODE 0"
  END
END IF
POKE SND, 128                                 ' silence, everything flushed
score% = 0: lives% = 3: wave% = 1
prow% = h% - 1                                ' the player's row
PRINT CHR$(27); "[?25l";                      ' the cursor off

DO
  ' ---- a new wave
  CLS
  FOR i% = 1 TO 24: alive%(i%) = 1: NEXT
  left% = 24
  gx% = 8: gy% = 3: dir% = 1
  speed% = 12 - wave% * 2: IF speed% < 3 THEN speed% = 3
  tick% = speed%
  px% = w% \ 2 - 1: by% = 0: ey% = 0
  DrawGrid
  DrawPlayer
  Status
  ' ---- once a frame
  DO
    WAITFRAME
    GET k$
    IF k$ = "q" OR k$ = "Q" THEN lives% = 0: EXIT DO
    IF (k$ = "a" OR k$ = "A" OR k$ = CHR$(130)) AND px% > 1 THEN
      CALL ErasePlayer: px% = px% - 1: DrawPlayer
    END IF
    IF (k$ = "d" OR k$ = "D" OR k$ = CHR$(131)) AND px% < w% - 3 THEN
      CALL ErasePlayer: px% = px% + 1: DrawPlayer
    END IF
    IF k$ = " " AND by% = 0 THEN
      by% = prow% - 1: bx% = px% + 1
      Sfx 16, 120, 2                        ' the shot: noise
    END IF
    IF by% THEN MoveBullet
    IF ey% THEN MoveBomb
    IF lives% = 0 THEN EXIT DO
    IF ey% = 0 AND RND(1) < 0.02 + wave% * 0.005 THEN DropBomb
    tick% = tick% - 1
    IF tick% = 0 THEN tick% = speed%: March
    IF lives% = 0 THEN EXIT DO
    IF left% = 0 THEN EXIT DO
  LOOP
  IF lives% = 0 THEN EXIT DO
  CLS: LOCATE h% \ 2, w% \ 2 - 7: COLOR 7: PRINT "WAVE CLEARED!"
  FOR i% = 1 TO 60: WAITFRAME: NEXT
  wave% = wave% + 1
LOOP

' ---- game over
LOCATE h% \ 2, w% \ 2 - 4: COLOR 2: PRINT "GAME OVER"
FOR i% = 1 TO 180: WAITFRAME: NEXT
POKE SND, 128
COLOR 1: CLS
PRINT CHR$(27); "[?25h";                      ' the cursor back
PRINT "SCORE"; score%
END


SUB Sfx (ch%, pitch%, dur%)
  POKE SND, ch%: POKE SND + 1, 241: POKE SND + 2, pitch%: POKE SND + 3, dur%
END SUB

FUNCTION Invader% (x%, y%)
  ' which live invader is at column x%, row y% (0 if none)
  Invader% = 0
  dy% = y% - gy%: dx% = x% - gx%
  IF dy% < 0 OR dx% < 0 THEN EXIT FUNCTION
  IF dy% MOD 2 <> 0 OR dx% MOD 5 <> 0 THEN EXIT FUNCTION
  r% = dy% \ 2: c% = dx% \ 5
  IF r% > 2 OR c% > 7 THEN EXIT FUNCTION
  n% = r% * 8 + c% + 1
  IF alive%(n%) THEN Invader% = n%
END FUNCTION

SUB DrawGrid
  FOR r% = 0 TO 2
    COLOR 13 - r% * 3
    FOR c% = 0 TO 7
      LOCATE gy% + r% * 2, gx% + c% * 5
      IF alive%(r% * 8 + c% + 1) THEN PRINT "W"; ELSE PRINT " ";
    NEXT
  NEXT
END SUB

SUB EraseGrid
  FOR r% = 0 TO 2
    FOR c% = 0 TO 7
      LOCATE gy% + r% * 2, gx% + c% * 5: PRINT " ";
    NEXT
  NEXT
END SUB

SUB DrawPlayer
  LOCATE prow%, px%: COLOR 1: PRINT "/^\";
END SUB

SUB ErasePlayer
  LOCATE prow%, px%: PRINT "   ";
END SUB

SUB Status
  LOCATE 1, 3: COLOR 1: PRINT "SC:"; RIGHT$("0000" + STR$(score%), 5); "   ";
  LOCATE 1, w% - 10: PRINT "       ";
  LOCATE 1, w% - 10: PRINT LEFT$("^^^^^^", lives%);
END SUB

SUB MoveBullet
  LOCATE by%, bx%: PRINT " ";
  by% = by% - 1
  IF by% < 2 THEN by% = 0: EXIT SUB
  n% = Invader%(bx%, by%)
  IF n% THEN
    alive%(n%) = 0: left% = left% - 1
    score% = score% + (3 - (n% - 1) \ 8) * 10 + wave% * 5
    LOCATE by%, bx%: PRINT " ";
    Sfx 18, 60, 4                         ' a hit
    by% = 0: Status
  ELSE
    LOCATE by%, bx%: COLOR 1: PRINT "!";
  END IF
END SUB

SUB DropBomb
  n% = INT(RND(1) * 24) + 1
  IF alive%(n%) = 0 THEN EXIT SUB
  ex% = gx% + ((n% - 1) MOD 8) * 5
  ey% = gy% + ((n% - 1) \ 8) * 2 + 1
END SUB

SUB MoveBomb
  IF Invader%(ex%, ey%) = 0 THEN LOCATE ey%, ex%: PRINT " ";
  ey% = ey% + 1
  IF ey% > prow% THEN ey% = 0: EXIT SUB
  IF ey% = prow% AND ex% >= px% AND ex% <= px% + 2 THEN
    ' the player is hit
    ey% = 0: lives% = lives% - 1
    Sfx 17, 20, 30
    Status
    FOR i% = 1 TO 30: WAITFRAME: NEXT
    DrawPlayer
    EXIT SUB
  END IF
  IF Invader%(ex%, ey%) = 0 THEN LOCATE ey%, ex%: COLOR 10: PRINT "*";
END SUB

SUB March
  EraseGrid
  gx% = gx% + dir%
  IF gx% < 1 OR gx% > w% - 37 THEN
    dir% = -dir%: gx% = gx% + dir%: gy% = gy% + 1
    speed% = speed% - 1: IF speed% < 2 THEN speed% = 2
  END IF
  IF gy% + 4 >= prow% THEN lives% = 0: EXIT SUB   ' they landed
  DrawGrid
END SUB
