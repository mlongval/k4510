10 REM SCREEN SIZES - K4510 EHBASIC
20 REM A TOUR OF *MODE WXH: INTEGER BEST FIT, -M SMOOTH, -C SCANLINES, -P PLAIN
30 REM EACH STEP: ASK FOR A SIZE, SAY WHAT CAME, DRAW ON IT WITH GRAPHICS 3
40 REM (A BITMAP THE SIZE OF THE SCREEN *MODE MADE), WAIT FOR A KEY.
50 REM VICKY SAYS WHAT IS: $D0C0 PANEL W/H, $D0D6 SCREEN W/H, $D0DC/DD TEXT GRID
60 PALETTE 16,255,255,255:PALETTE 17,255,80,80:PALETTE 18,80,255,120:PALETTE 19,255,220,60
100 GOSUB 8000:PRINT CHR$(12)
110 PRINT "SCREEN SIZES ON THE K4510":PRINT
120 PRINT "This panel is ";P$;" pixels."
130 PRINT "The shell's screen now is ";S$;"."
140 PRINT
150 PRINT "A program may keep the screen it is"
160 PRINT "given, or ask for a size of its own:"
170 PRINT "      *MODE 640x480"
180 PRINT "(Pascal: SetResolution(640,480,false))"
190 PRINT
200 PRINT "If the panel offers that size, it is"
210 PRINT "used as it is. If not, the machine"
220 PRINT "draws exactly 640x480 and shows it at"
230 PRINT "the largest WHOLE multiple that fits:"
240 PRINT "every pixel the same size, a frame"
250 PRINT "round the picture. Integer Best Fit."
260 PRINT
270 PRINT "Effects are asked for, never given:"
280 PRINT "  -m  smoothed to fill the panel"
290 PRINT "  -c  scanlines, as on a CRT"
300 PRINT "  -p  plain again"
310 PRINT
320 PRINT "Too large for the panel is refused."
330 PRINT "When the program ends, the shell gets"
340 PRINT "its own screen back."
350 GOSUB 8500
1000 REM ---- 640X480: THE CLASSIC SIZE
1010 *MODE 640x480
1020 A$="*MODE 640x480":GOSUB 8000:GOSUB 8200
1030 PRINT "VGA's size. On 1080 lines it fits"
1040 PRINT "twice (1280x960) with a frame round"
1050 PRINT "it; 2.25x would make some pixels two"
1060 PRINT "lines tall and some three."
1070 GOSUB 8100:GOSUB 8500
2000 REM ---- 320X240: CHUNKY
2010 *MODE 320x240
2020 A$="*MODE 320x240":GOSUB 8000:GOSUB 8200
2030 PRINT "Half each way: every pixel is a"
2040 PRINT "block of panel pixels. 40 columns."
2050 GOSUB 8100:GOSUB 8500
3000 REM ---- 400X300: A SIZE NO PANEL OFFERS
3010 *MODE 400x300
3020 A$="*MODE 400x300":GOSUB 8000:GOSUB 8200
3030 PRINT "No panel has this size. The machine"
3040 PRINT "draws 400x300 and finds the largest"
3050 PRINT "whole multiple: x";MID$(STR$(K),2);" here."
3060 GOSUB 8100:GOSUB 8500
4000 REM ---- 400X300 -M: SMOOTHED TO THE PANEL
4010 *MODE 400x300 -m
4020 A$="*MODE 400x300 -m":GOSUB 8000:GOSUB 8200
4030 PRINT "The same 400x300, now FITTED: the"
4040 PRINT "whole multiple first, then smoothed"
4050 PRINT "the rest of the way to the panel's"
4060 PRINT "height. Larger, slightly soft edges."
4070 GOSUB 8100:GOSUB 8500
5000 REM ---- 320X240 -C: SCANLINES
5010 *MODE 320x240 -c
5020 A$="*MODE 320x240 -c":GOSUB 8000:GOSUB 8200
5030 PRINT "Scanlines: the lower half of every"
5040 PRINT "row dimmed, as a CRT's beam left a"
5050 PRINT "dark gap. Only where a row is two"
5060 PRINT "panel lines or more."
5070 GOSUB 8100:GOSUB 8500
6000 REM ---- 400X300 -M -C: BOTH
6010 *MODE 400x300 -m -c
6020 A$="*MODE 400x300 -m -c":GOSUB 8000:GOSUB 8200
6030 PRINT "Smoothed and scanlined together."
6040 PRINT "*MODE -p would take both off."
6050 GOSUB 8100:GOSUB 8500
7000 REM ---- -S 1: THE PANEL ITSELF
7010 *MODE -p -s 1
7020 A$="*MODE -p -s 1":GOSUB 8000:GOSUB 8200
7030 PRINT "Scale 1: one machine pixel to one"
7040 PRINT "panel pixel, on the panel's largest"
7050 PRINT "4:3. MODE -l lists every size it offers."
7060 GOSUB 8100:GOSUB 8500
7500 REM ---- TOO LARGE: REFUSED
7510 PRINT CHR$(12):PRINT "Asking for more than the panel:"
7520 PRINT "      *MODE 4000x3000":PRINT
7530 *MODE 4000x3000
7540 PRINT:PRINT "The screen is unchanged: ";:GOSUB 8000:PRINT S$
7550 PRINT:PRINT "That is the tour. When this program"
7560 PRINT "ends, the shell's own screen returns."
7570 PRINT "(Try MODE alone at the prompt.)"
7580 GOSUB 8500
7590 GOTO 9990
8000 REM ---- READ VICKY: P$ PANEL, S$ SCREEN, W H, K = WHOLE MULTIPLE
8010 PW=PEEK(53440)+256*PEEK(53441):PH=PEEK(53442)+256*PEEK(53443)
8020 W=PEEK(53462)+256*PEEK(53463):H=PEEK(53464)+256*PEEK(53465)
8030 P$=MID$(STR$(PW),2)+"x"+MID$(STR$(PH),2)
8040 S$=MID$(STR$(W),2)+"x"+MID$(STR$(H),2)
8050 K=INT(PW/W):IF INT(PH/H)<K THEN K=INT(PH/H)
8060 RETURN
8100 REM ---- DRAW ON THE WHOLE SCREEN: FRAME, RULER EVERY 10 PIXELS, CIRCLE
8110 GRAPHICS 3
8120 LINE 0,0,W-1,0,16:LINE W-1,0,W-1,H-1,16:LINE W-1,H-1,0,H-1,16:LINE 0,H-1,0,0,16
8130 FOR X=10 TO W-1 STEP 10:L=3:IF X/50=INT(X/50) THEN L=7
8140 LINE X,H-1,X,H-1-L,19:NEXT
8150 FOR Y=10 TO H-1 STEP 10:L=3:IF Y/50=INT(Y/50) THEN L=7
8160 LINE W-1,Y,W-1-L,Y,19:NEXT
8170 CX=W*3/4:CY=H*2/3:R=H/5:X1=CX+R:Y1=CY
8180 FOR A=1 TO 48:X2=CX+R*COS(A*0.1309):Y2=CY+R*SIN(A*0.1309)
8190 LINE X1,Y1,X2,Y2,18:X1=X2:Y1=Y2:NEXT:LINE CX-R,CY,CX+R,CY,17:LINE CX,CY-R,CX,CY+R,17
8195 RETURN
8200 REM ---- WHAT WAS ASKED, WHAT CAME
8210 PRINT "Asked: ";A$
8220 PRINT "Got:   ";S$;" pixels, ";MID$(STR$(PEEK(53468)),2);"x";MID$(STR$(PEEK(53469)),2);" text"
8230 PRINT "Panel: ";P$;", whole multiple x";MID$(STR$(K),2)
8240 PRINT:RETURN
8500 REM ---- ANY KEY, THEN THE BITMAP OFF
8510 PRINT:PRINT "Any key...";
8520 GET K$:IF K$="" THEN 8520
8530 GRAPHICS 0:RETURN
9990 IF PEEK(1039) THEN LOAD "DEMOS.BAS"
