RX -- REXX on the K4510
=======================

RX name [arguments]     runs name, name.RX, or /RX/name.RX
name arguments          the same, when name.RX exists (the shell looks)

A script is a text file of REXX clauses.  A clause that is not one of the
words below is worked out as an expression and sent to the current
environment as a command; RC is what came back.

  SAY  PULL  PARSE (ARG PULL VAR VALUE SOURCE VERSION, UPPER)
  IF ... THEN ... ELSE       SELECT / WHEN / OTHERWISE / END
  DO n | var = a TO b BY c | FOREVER | WHILE | UNTIL ... END
  LEAVE  ITERATE  CALL  RETURN  PROCEDURE [EXPOSE]  DROP  UPPER
  PUSH  QUEUE  SIGNAL  ADDRESS  EXIT  NOP  TRACE

Values are strings; arithmetic is whole numbers (32-bit).  A value holds up
to 199 characters.  Variables with a dot are compound: A.I uses I's value.

Functions
  LENGTH SUBSTR LEFT RIGHT CENTRE POS LASTPOS COPIES REVERSE UPPER LOWER
  STRIP SPACE WORD WORDS WORDLENGTH WORDINDEX SUBWORD WORDPOS VERIFY
  ABS SIGN TRUNC MAX MIN RANDOM D2C C2D D2X X2D DATATYPE
  ARG ADDRESS SYMBOL VALUE QUEUED DATE TIME
  LINEIN LINES LINEOUT EXISTS PEEK POKE SLEEP KEY

  PEEK/POKE below 65536 are the machine's own address space (so PEEK(53248)
  is VICKY's control register); above it they are the 256 MB of far memory.
  SLEEP counts frames.  KEY returns a key if one was pressed, else ''.

Environments (ADDRESS name)
  COMMAND   K/OS itself: the line is typed at the shell.  RC is 0, or 1
            when the shell reported an error.  This is where RX starts.
  TUBE      the co-processor's shell -- the same door `!` opens, and fitted
            only where `!` is.  RESULT takes the output with the line ends
            turned into blanks; for more than a value's worth, redirect to
            a file and read it with LINEIN.
  <program> any program that answers the port contract below.  RX writes
            /RX/MAIL.CMD, SWAPs to the program with @/RX/MAIL.CMD, and
            reads /RX/MAIL.RPL: RC from the first line, RESULT from the
            rest.  CHESS answers it today (see CHESS.RX).

The port contract, for a program that wants to be driven
  The program is started as  NAME @file .  It reads command lines from
  that file, does them, and writes the answer to the same name with .RPL
  in place of .CMD: the result code alone on the first line, the text
  after it.  Then it exits.  A program keeps nothing in memory between
  calls -- it is swapped in fresh each time -- so anything that must last
  goes in a file of its own (CHESS keeps the position in /CHESS/PORT.GAM).

What a script runs, and how
  K/OS's own commands (DIR, CD, TYPE, MKDIR, PALETTE ...) run as if typed.
  A word that names a program is different: a program is loaded at $6000,
  through the middle of the interpreter, so K/OS swaps -- it puts the
  interpreter aside, runs the program, and brings it back.  The script sees
  RC either way, and what the program drew stays on the screen (SWAP -k).
  A program that a script wants to *talk* to should answer the port instead:
  a file is a better channel than a screen.

Two things to know about PROCEDURE EXPOSE
  EXPOSE var makes one variable of the caller's visible and writable here.
  EXPOSE stem. does the same for every member the stem has NOW; a member
  the procedure invents is its own.  Twenty exposed members cost twenty of
  the interpreter's 176 variables, so expose what you use.

The examples in this directory
  HELLO.RX     the shape of a script: arguments, a loop, a command, RC
  MACHINE.RX   what the machine says about itself, read from its registers
  WC.RX        lines, words and characters of a file (LINEIN, LINES)
  SUMFILE.RX   adds up a column of numbers in a text file
  HOSTINFO.RX  ADDRESS TUBE: asks the Linux on the other side about itself
  GUESS.RX     a small game: PULL, RANDOM, SELECT
  WUMPUS.RX    Hunt the Wumpus: a cave of twenty rooms, in 150 lines
  CHESS.RX     plays a game against the chess engine through the port
