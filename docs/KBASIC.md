# K4510 BASIC

K4510 BASIC is the machine's BASIC for writing programs. It speaks
EhBASIC's words (PRINT, INPUT, FOR, GOSUB, PLOT ...), but it is
*compiled*: `k4510-bas` turns your `.BAS` file into Mad Pascal, and Mad
Pascal turns that into a `.prg`, the same kind of program as everything
in `/APPS`. The result runs many times faster than EhBASIC.

You write a program in PROG (or VI, or any editor), save it as
`NAME.BAS`, and then:

| Where | What to do |
|---|---|
| At the prompt | `BAS NAME` compiles NAME.BAS into `name.prg`; `NAME` runs it |
| In PROG | **F9** compiles; **Ctrl-F9** compiles and runs |

There is no "immediate mode" (typing `PRINT 2+2` and getting 4). For a
quick calculation or a one-line try, use RX: there `PRINT` works like
`SAY`, so `PRINT 6*7` prints 42.

EhBASIC is still on the machine for the old numbered programs in
`/LANG/EHBASIC`. Every one of its examples is in `/LANG/BASIC/EX` too,
written again in K4510 BASIC: `DEMOS` and `BENCH` are the menus.

## A first program

```basic
' guess the number -- the first K4510 BASIC program
CONST TRIES = 7
secret = 42
n% = 0
DO
  INPUT "Your guess"; g
  n% = n% + 1
  IF g < secret THEN
    PRINT "Higher"
  ELSEIF g > secret THEN
    PRINT "Lower"
  END IF
LOOP UNTIL g = secret OR n% = TRIES
GOSUB Bravo
PRINT "Tries:"; n%; "of"; TRIES
END
Bravo:
  IF g = secret THEN PRINT "You got it!" ELSE PRINT "Too bad"
  RETURN
```

No line numbers, keywords in any case, a label (`Bravo:`) where an old
BASIC would have had `GOSUB 1000`.

## Writing it down

- **One statement per line**, or several separated by `:`.
- **Keywords in any case**: `print`, `Print` and `PRINT` are the same.
  So are variable names: `Score` and `SCORE` are one variable.
- **Comments** start with `'` or `REM` and run to the end of the line.
- **Labels**: a name followed by `:` at the start of a line
  (`Again:`). `GOTO Again` and `GOSUB Again` jump there.
- **Line numbers** still work, as labels: `100 PRINT "HI"` and
  `GOTO 100`. You can mix both, and leave most lines without one.

## Variables

| Written | Holds | Starts as |
|---|---|---|
| `score` | a number with decimals (single precision, about 7 figures) | 0 |
| `lives%` | a whole number, -32768 to 32767 | 0 |
| `name$` | text, up to 255 characters | "" |

`a`, `a%` and `a$` are three different variables. Names can be as long
as you like, and every letter counts (EhBASIC only looked at the first
two).

A whole-number variable rounds what it is given: `n% = 3.7` makes 4. A
value outside -32768..32767 stops the program with "Overflow".

**Every variable belongs to the whole program**, wherever it is used,
except the parameters of a SUB or FUNCTION and what a SUB or FUNCTION
DIMs for itself (see below).

### Arrays

```basic
DIM scores(10), board%(7, 7), names$(30)
scores(3) = 9.5
board%(0, 7) = 1
```

- One or two dimensions. `DIM a(10)` has 11 elements, 0 to 10.
- The size is a whole number or a CONST.
- An array used without a DIM is `DIM`med to 10 for you.
- An element of a text array holds up to 80 characters.
- An array may use about 24 KB at most (DIM tells you if it is bigger).
- A wrong index (`scores(11)`, `scores(-1)`) stops the program with
  "Index out of range".

### CONST

```basic
CONST TRIES = 7
CONST GREETING$ = "Hello"
```

A CONST is a variable that may not be changed: `TRIES = 8` is an error
when the program is compiled. A CONST made from a plain number can be
an array's size: `DIM grid(SIZE)`.

## Values and operators

| Operator | Means |
|---|---|
| `+ - * /` | add, subtract, multiply, divide (`/` always gives decimals) |
| `\` | whole-number division: `7 \ 2` is 3 |
| `MOD` | remainder: `7 MOD 2` is 1 |
| `^` | power: `2 ^ 10` is 1024 |
| `+` on text | joins: `"ab" + "cd"` is `"abcd"` |
| `= <> < > <= >=` | compare numbers, or text (alphabetical, by character code) |
| `AND OR XOR NOT` | combine conditions; on numbers, they work bit by bit |

`^` comes first, then a minus sign, then `* /`, `\`, `MOD`, `+ -`, the
comparisons, `NOT`, `AND`, `XOR`, `OR`. So `-2 ^ 2` is -4. Brackets
change the order as usual.

A comparison used as a number is -1 (true) or 0 (false):
`x = (3 > 2)` makes x -1. A number used as a condition is true when it
is not 0: `IF lives% THEN ...`.

Numbers can be written `12`, `3.75`, `.5`, `1.5E+12` or in hexadecimal
`&HFF`.

## Statements

### Printing and asking

| Statement | What it does |
|---|---|
| `PRINT a; b$; c` | prints; `;` keeps going on the same line |
| `PRINT a, b` | `,` moves to the next column of 14 |
| `PRINT TAB(20); "x"` | `TAB(n)` moves to column n, `SPC(n)` prints n spaces |
| `PRINT "no new line";` | a `;` or `,` at the end keeps the cursor on the line |
| `? "hi"` | `?` is short for PRINT |
| `INPUT "Your age"; age` | prints the question and `? `, waits for a line |
| `INPUT "Name: ", n$` | with `,` no `? ` is added |
| `INPUT a, b` | several values, typed separated by commas |
| `GET k$` | the key pressed, or "" if none: does not wait |
| `CLS` | clears the screen |
| `COLOR 7` / `COLOR 7, 6` | text colour, and background (0-15, the palette) |
| `LOCATE 10, 30` | moves the cursor to row 10, column 30 |

A number is printed with a space in front (where a minus sign would
go) and one after: `PRINT 5; 6` shows ` 5  6 `. Text is printed as it
is. Numbers show at most six figures, without useless zeros: `3.75`,
`0.333333`, and very big or very small ones as `1.5E+12`.

### Deciding

```basic
IF x > 10 THEN PRINT "big" ELSE PRINT "small"

IF age < 13 THEN
  PRINT "child"
ELSEIF age < 18 THEN
  PRINT "teenager"
ELSE
  PRINT "adult"
END IF

SELECT CASE n%
  CASE 1, 2
    PRINT "one or two"
  CASE 3 TO 9
    PRINT "a few"
  CASE IS >= 10
    PRINT "lots"
  CASE ELSE
    PRINT "none"
END SELECT
```

- The one-line IF keeps everything after THEN on that line;
  `IF x THEN 100` and `IF x GOTO 100` jump to a label or line number.
- A many-line IF has nothing after THEN and ends with `END IF`.
- `SELECT CASE` works with numbers and with text (`CASE "yes", "y"`).

### Repeating

```basic
FOR i = 1 TO 10 STEP 2 ... NEXT i
WHILE lives% > 0 ... WEND
DO WHILE x < 5 ... LOOP
DO UNTIL done% ... LOOP
DO ... LOOP UNTIL key$ = "q"
DO ... LOOP WHILE x < 5
DO ... LOOP              ' forever, until EXIT DO
```

- `STEP` may be negative (`FOR i = 10 TO 1 STEP -1`) or have decimals.
  Without STEP, the count goes up by 1.
- `NEXT` may name its variable (`NEXT i`) or not.
- `EXIT FOR` and `EXIT DO` leave the loop they are in.

### Jumping

| Statement | What it does |
|---|---|
| `GOTO label` | continues at the label (or line number) |
| `GOSUB label` | runs from the label until RETURN, then comes back |
| `RETURN` | back to the statement after the GOSUB |
| `END` or `STOP` | ends the program |

GOSUBs can be inside each other up to 64 deep.

### SUB and FUNCTION

```basic
SUB Stars (n%)
  FOR i% = 1 TO n%: PRINT "*";: NEXT
  PRINT
END SUB

FUNCTION Area (w, h)
  Area = w * h
END FUNCTION

Stars 5
CALL Stars(3)
PRINT Area(3, 4)
```

- A SUB is called by its name (with or without brackets round the
  values) or with `CALL`.
- A FUNCTION gives back the value assigned to its own name.
- They can be written anywhere in the file, before or after they are
  used.
- `EXIT SUB` and `EXIT FUNCTION` leave early.
- **Local variables:** `DIM x, n%, s$` or `DIM t(3)` inside a SUB or
  FUNCTION makes them its own, 0 or "" at every call, hiding the
  program's variables of the same name. Its parameters are its own too.
  Every other variable inside it is the program's variable of that name.
- A SUB or FUNCTION cannot call itself (no recursion): its own
  variables are kept in one place, not one set per call.
- `Name: next statement` is a call of the SUB Name followed by another
  statement; for any other name, `Name:` at the start of a line is a
  label.
- Labels, GOTO and GOSUB stay outside SUBs and FUNCTIONs.

### DATA and READ

```basic
FOR i = 1 TO 3: READ name$, age: PRINT name$; age: NEXT
DATA Ada, 36, "Grace", 85
DATA Alan, 41
RESTORE           ' READ starts again from the first DATA
RESTORE Colours   ' ... or from the DATA after a label
```

- DATA can be anywhere in the program; READ takes the items in order.
- An item is a number, a "quoted" text, or text written as it is (the
  spaces round it trimmed).
- READ past the last item stops the program with "Out of DATA".

### ON ... GOTO, DEF FN

```basic
ON choice% GOTO One, Two, Three     ' 1 to One, 2 to Two ...
ON choice% GOSUB One, Two, Three
DEF FNarea(w, h) = w * h            ' a one-line function
DEF FNfull$(a$, b$) = a$ + " " + b$
PRINT FNarea(3, 4)
```

A value outside 1 to the number of labels goes on to the next statement.

### The shell: * and SHELL

```basic
*MODE 640x480               ' the rest of the line goes to K/OS, : and all
SHELL "MODE " + size$       ' a command worked out by the program
SHELL "INVADERS"            ' runs another program; this one goes on after it
```

A program that changes the screen gets the shell's own screen back when
it ends (which clears it): wait for a key before END if the last words
matter.

### Other statements

| Statement | What it does |
|---|---|
| `LET x = 5` | the same as `x = 5` |
| `SLEEP 1.5` | waits that many seconds |
| `RANDOMIZE` | new random numbers (every program already starts with fresh ones) |
| `POKE address, value` | writes a byte into memory: 0-65535 is what the CPU sees, I/O included; 65536 and up is the machine's far memory (to 16 MB), as `PEEK` reads it |

## Functions

| Function | Gives |
|---|---|
| `ABS(x)` | x without its sign |
| `INT(x)` | x rounded down: `INT(-2.5)` is -3 |
| `FIX(x)` | x with its decimals cut off: `FIX(-2.5)` is -2 |
| `SGN(x)` | -1, 0 or 1 |
| `SQR(x)` | square root |
| `SIN COS TAN ATN (x)` | trigonometry, in radians |
| `EXP(x)` `LOG(x)` | e to the power x; natural logarithm |
| `RND` or `RND(1)` | a random number from 0 up to (not including) 1 |
| `LEN(a$)` | how many characters |
| `ASC(a$)` | the code of the first character (0 if empty) |
| `CHR$(n)` | the character with code n |
| `VAL(a$)` | the number written in a$ (`VAL("3.5")` is 3.5) |
| `STR$(x)` | the number as text, without the leading space |
| `LEFT$(a$, n)` `RIGHT$(a$, n)` | the first / last n characters |
| `MID$(a$, start)` `MID$(a$, start, n)` | n characters from position start (from 1) |
| `INSTR(a$, b$)` | where b$ is in a$ (1 = first character), 0 if it is not |
| `UCASE$(a$)` `LCASE$(a$)` | in capitals / small letters |
| `SPACE$(n)` | n spaces |
| `INKEY$` | the key pressed, or "": does not wait |
| `PEEK(address)` | a byte of memory |

To make a whole number from 1 to 6: `INT(RND * 6) + 1`.

## Graphics

The graphics words draw on a picture laid over the text, as EhBASIC's
do. Colour 0 is see-through: the text shows through it.

| Statement | What it does |
|---|---|
| `GRAPHICS 1` | the picture on, 320 x 240 (MODE 2), as in EhBASIC |
| `GRAPHICS 2` | the picture on, 640 x 480 (MODE 0) |
| `GRAPHICS 3` | the picture on, at the size of the screen as it is (after `*MODE 400x300`, say) |
| `GRAPHICS 0` | the picture off, and the text mode GRAPHICS found back |
| `PALETTE i, r, g, b` | colour i (0-255) as red, green, blue (0-255); 0-15 are the text's |
| `SPRDEF n, page, w, h, bpp` | sprite n (0-127) has its shape at page*256 in far memory, w x h (8, 16, 32, 64), 4 or 8 bits a pixel |
| `SPRITE n, x, y` | puts sprite n there and shows it |
| `SPROFF n` | hides it |
| `GCLS` | clears the picture |
| `PLOT x, y, c` | one dot |
| `LINE x1, y1, x2, y2, c` | a line |
| `BOX x1, y1, x2, y2, c` | a filled rectangle |
| `TRI x1, y1, x2, y2, x3, y3, c` | a filled triangle |
| `CIRCLE x, y, r, c` | a circle |
| `GWIDTH` `GHEIGHT` | the picture's width and height, in dots |

The first drawing word turns the picture on (`GRAPHICS 3`) if the
program has not. A program that ends with the picture on waits for a
key, then takes it away. Coordinates and colours are whole numbers;
(0, 0) is the top left.

## Errors

**When compiling.** A mistake stops the compile with a message and the
line it is on: "IF (END IF) is never closed", "there is no label
Bravo", "TRIES is a CONST: it cannot change", "a number was expected
here, not a string". In PROG, F9 puts the cursor on that line.

**When running.** Some mistakes can only be seen while the program
runs. The program stops, prints the line and the reason, and goes back
to K/OS; PROG then puts the cursor on that line:

| Message | Because |
|---|---|
| `Line N: Division by zero` | `/`, `\` or `MOD` by 0 |
| `Line N: Index out of range (DIM it bigger?)` | an array index outside its DIM |
| `Line N: Overflow: a whole number (%) holds -32768 to 32767` | too big for a `%` variable |
| `Line N: RETURN without GOSUB` | a RETURN reached without a GOSUB |
| `Line N: Too many GOSUBs inside each other` | more than 64 GOSUBs not yet RETURNed |

## Compared with EhBASIC and QuickBASIC

**Like EhBASIC:** the words, the graphics statements, the variable
types, PRINT's look.

**Unlike EhBASIC:** compiled, not interpreted; no line numbers needed;
keywords in any case; every letter of a name counts; multi-line IF,
SELECT CASE, SUB and FUNCTION, labels, LOCATE, COLOR, CLS, INKEY$.

**Like QuickBASIC:** the block statements (IF ... END IF, DO ... LOOP,
SELECT CASE), labels, SUB and FUNCTION, CONST.

**Not there (yet):**

- `PRINT USING`, `LINE INPUT`, `WRITE`
- `SWAP`, `ERASE`, `REDIM`
- recursion; `SHARED`, `STATIC`
- labels, `GOTO` and `GOSUB` inside a SUB or FUNCTION
- files (`OPEN`, `LOAD`, `SAVE`), `SOUND`, `TIMER`, `DATE$`
- arrays of more than two dimensions; text array elements longer than 80
- immediate mode (use RX)
