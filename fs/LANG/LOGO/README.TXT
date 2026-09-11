LOGO -- turtle graphics on the 45GS10.  Type LOGO at the prompt.

  FD 100   RT 90   REPEAT 4 [FD 100 RT 90]
  TO SQUARE :N          a procedure, ended by END
    REPEAT 4 [FD :N RT 90]
  END
  SQUARE 50
  LOAD "TREE            runs EX/TREE.LGO (or a .LGO here)
  EDIT "MINE            VI on MINE.LGO, then runs it
  FILL                  paints the area under the turtle
  HT  ST                hide / show the turtle
  PRINT 2 + 3 * 4       numbers are real: the MATH unit does them
  HELP                  the words   BYE   back to K/OS

Words: FD BK RT LT PU PD HT ST HOME CS FILL SETXY SETX SETY SETH SETPC
       XCOR YCOR HEADING RANDOM SQRT SIN COS INT ABS REPCOUNT
       REPEAT IF IFELSE TO..END STOP OUTPUT MAKE :x PRINT SHOW LOAD EDIT BYE
