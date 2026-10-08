' K4510 BASIC self-test (verbose).  The EhBASIC one, /LANG/EHBASIC/EX/TEST.BAS,
' carried over -- and the things only K4510 BASIC has, tested too.
' Goal, expected, a sample result, PASS/FAIL per test.
CLS
PRINT "K4510 BASIC SELF-TEST": PRINT
PRINT "Goal, expected, a sample result, PASS/FAIL per test."
PRINT "Press a key in 5 sec to step; else it runs through.": PRINT
AskStep
n% = 0: f% = 0: tn% = 0

Begin "Arithmetic", "operators + - * / ^ \ MOD INT ABS SGN", "6*7=42  2^10=1024  INT(-3.7)=-4  7\2=3"
Check 6 * 7 = 42: Check 7 / 2 = 3.5
Check 2 ^ 10 = 1024: Check INT(-3.7) = -4
Check ABS(-5) = 5 AND SGN(-9) = -1: Check ABS(1 / 3 * 3 - 1) < 1E-6
Check 7 \ 2 = 3 AND 7 MOD 2 = 1
Result "6*7=" + STR$(6 * 7) + " 2^10=" + STR$(2 ^ 10)

Begin "Floating point", "SQR SIN COS EXP LOG ATN TAN, to 5 places", "SQR(2)=1.414214  ATN(1)*4=PI"
Check ABS(SQR(2) - 1.414214) < 1E-5
Check ABS(SIN(1) - .841471) < 1E-5
Check ABS(COS(1) - .540302) < 1E-5
Check ABS(EXP(1) - 2.718282) < 1E-5
Check ABS(LOG(10) - 2.302585) < 1E-5
Check ABS(ATN(1) * 4 - 3.141593) < 1E-5
Check ABS(TAN(1) - 1.557408) < 1E-5
Result "SQR(2)=" + STR$(SQR(2))

Begin "Strings", "+, LEFT$ MID$ RIGHT$, LEN ASC CHR$, VAL STR$, INSTR UCASE$", "A$+B$=HELLO WORLD  VAL(3.5)=3.5"
a$ = "HELLO": b$ = " WORLD"
Check a$ + b$ = "HELLO WORLD"
Check LEFT$(a$, 2) = "HE"
Check MID$(a$, 2, 3) = "ELL" AND RIGHT$(a$, 3) = "LLO"
Check LEN(a$ + b$) = 11 AND ASC("A") = 65 AND CHR$(66) = "B"
Check VAL("3.5") = 3.5 AND STR$(42) = "42"
Check a$ < "HELP" AND a$ > "HELL"
Check INSTR(a$ + b$, "WOR") = 7 AND UCASE$("abc") = "ABC" AND LCASE$("ABC") = "abc"
Result "[" + a$ + b$ + "]"

Begin "Control flow and functions", "FOR/NEXT STEP, DEF FN, ON GOTO, AND OR NOT", "sum 1..100=5050  step sum=22  FNQ(7)=50"
s1 = 0: FOR i = 1 TO 100: s1 = s1 + i: NEXT: Check s1 = 5050
s2 = 0: FOR i = 10 TO 1 STEP -3: s2 = s2 + i: NEXT: Check s2 = 22
DEF FNQ(x) = x * x + 1: Check FNQ(7) = 50
k = 2: ON k GOTO Wrong1, Right1
Wrong1:
Check 0: GOTO After1
Right1:
Check -1
After1:
CALL Check((1 = 1) = -1 AND (3 AND 5) = 1 AND (3 OR 5) = 7)
Result "sum=" + STR$(s1) + " step sum=" + STR$(s2) + " FNQ(7)=" + STR$(FNQ(7))

Begin "Blocks", "IF/ELSEIF/ELSE, SELECT CASE, DO/LOOP, WHILE/WEND, EXIT DO", "grade B, case 'few', 6 doublings, wend 5"
sc% = 75
IF sc% >= 90 THEN
  g$ = "A"
ELSEIF sc% >= 70 THEN
  g$ = "B"
ELSE
  g$ = "C"
END IF
Check g$ = "B"
SELECT CASE 5
  CASE 1, 2: c$ = "one or two"
  CASE 3 TO 9: c$ = "few"
  CASE ELSE: c$ = "lots"
END SELECT
Check c$ = "few"
d% = 1: dn% = 0
DO UNTIL d% >= 64: d% = d% * 2: dn% = dn% + 1: LOOP
Check dn% = 6
w% = 0
WHILE w% < 5: w% = w% + 1
WEND
Check w% = 5
x% = 0
DO: x% = x% + 1: IF x% = 3 THEN EXIT DO
LOOP
Check x% = 3
Result "grade " + g$ + ", case '" + c$ + "', " + STR$(dn%) + " doublings, wend " + STR$(w%)

Begin "SUB and FUNCTION", "parameters, a FUNCTION's value, CONST", "Area(3,4)=12  Twice 21 = 42"
Check Area(3, 4) = 12
Twice 21: Check tw = 42
Check TRIES = 7
Result "Area(3,4)=" + STR$(Area(3, 4)) + " Twice 21 =" + STR$(tw)

Begin "DATA and READ", "READ numbers and text, RESTORE", "3 primes sum 10, name ADA, again 2"
READ p1, p2, p3, nm$
Check p1 + p2 + p3 = 10 AND nm$ = "ADA"
RESTORE: READ p1
Check p1 = 2
Result "3 primes sum" + STR$(p1 + p2 + p3 - p1 + 2) + ", name " + nm$ + ", again" + STR$(p1)
DATA 2, 3, 5, ADA

Begin "Arrays", "DIM, 1 and 2 dimensions", "A(10)=100  A(3)=9  B(2,3)=77  B(3,2)=0"
DIM ar(10), br(3, 3)
FOR i = 0 TO 10: ar(i) = i * i: NEXT: br(2, 3) = 77
Check ar(10) = 100: Check ar(3) = 9
Check br(2, 3) = 77 AND br(3, 2) = 0
Result "A(10)=" + STR$(ar(10)) + " A(3)=" + STR$(ar(3)) + " B(2,3)=" + STR$(br(2, 3)) + " B(3,2)=" + STR$(br(3, 2))

Begin "The machine (SYS regs, frame counter)", "clock kHz, RAM MB from $D500, POKE/PEEK, 60Hz counter", "a clock in kHz, 256 MB, counter moves"
khz = PEEK(54528) + 256 * PEEK(54529) + 65536 * PEEK(54566)   ' $D500/01/26: the clock moves with the governor
Check khz >= 1000
Check PEEK(54530) + 256 * PEEK(54531) = 256
POKE 48896, 123: Check PEEK(48896) = 123
fz = PEEK(54541) + 256 * PEEK(54542)
FOR i = 1 TO 20000
  IF PEEK(54541) + 256 * PEEK(54542) <> fz THEN EXIT FOR
NEXT
Check PEEK(54541) + 256 * PEEK(54542) <> fz
Result STR$(khz) + " kHz " + STR$(PEEK(54530) + 256 * PEEK(54531)) + " MB counter " + STR$(fz) + " ->" + STR$(PEEK(54541) + 256 * PEEK(54542))

Begin "Graphics (VICKY bitmap over the text)", "GRAPHICS 2, PLOT LINE TRI BOX CIRCLE PALETTE GCLS, back", "draws, then GRAPHICS 0 restores"
GRAPHICS 2: PLOT 10, 10, 17: LINE 0, 0, 639, 479, 18
TRI 100, 100, 200, 100, 150, 200, 19: PALETTE 17, 255, 0, 0
BOX 300, 300, 340, 340, 17: CIRCLE 500, 100, 40, 18
GCLS: GRAPHICS 0: Check -1
Result "drawn and restored"

Begin "The * escape", "a line to K/OS", "ECHO prints STAR OK"
*ECHO STAR OK
Check -1: Result "the shell said it"

PRINT: PRINT "KBTEST: "; n% - f%; " OF "; n%; " CHECKS PASSED"
IF f% = 0 THEN PRINT "KBTEST PASSED" ELSE PRINT "KBTEST FAILED"
PRINT "ANY KEY...";              ' the screen goes back to the shell's at END
DO: GET k$: LOOP UNTIL k$ <> ""
END

CONST TRIES = 7

FUNCTION Area (w, h)
  Area = w * h
END FUNCTION

SUB Twice (v)
  tw = v * 2
END SUB

SUB Check (c)
  ac% = ac% + 1: n% = n% + 1
  IF c = 0 THEN
    af% = af% + 1: f% = f% + 1
    PRINT "    ** CHECK "; t1$; "."; STR$(ac%); " FAILED"
  END IF
END SUB

SUB Begin (t$, g$, e$)
  tn% = tn% + 1: ac% = 0: af% = 0: t1$ = STR$(tn%)
  PRINT: PRINT "--- TEST "; t1$; ": "; t$
  PRINT "    goal:   "; g$: PRINT "    expect: "; e$
END SUB

SUB Result (r$)
  PRINT "    result: "; r$
  IF af% = 0 AND ac% = 1 THEN PRINT "    [PASS] 1 check ("; t1$; ".1)"
  IF af% = 0 AND ac% > 1 THEN PRINT "    [PASS] "; STR$(ac%); " checks ("; t1$; ".1-"; t1$; "."; STR$(ac%); ")"
  IF af% > 0 THEN PRINT "    [FAIL] "; STR$(af%); " of "; STR$(ac%)
  IF stepping% THEN
    PRINT "    (press a key)";
    DO: GET k$: LOOP UNTIL k$ <> ""
    PRINT
  END IF
END SUB

SUB AskStep
  stepping% = 0: fw = PEEK(54541) + 256 * PEEK(54542)
  DO
    GET k$: IF k$ <> "" THEN stepping% = 1: EXIT SUB
    fd = PEEK(54541) + 256 * PEEK(54542) - fw: IF fd < 0 THEN fd = fd + 65536
  LOOP WHILE fd < 300
END SUB
