LOGO -- turtle graphics on the 45GS10.  Type LOGO at the prompt.

  FD 100   RT 90   REPEAT 4 [FD 100 RT 90]
  TO SQUARE :N          a procedure, ended by END
    REPEAT 4 [FD :N RT 90]
  END
  SQUARE 50
  LOAD "TREE            runs EX/TREE.LGO (or a .LGO here)
  PRINT 2 + 3 * 4       numbers are real: the MATH unit does them
  HELP                  the words   BYE   back to K/OS

Words: FD BK RT LT PU PD HOME CS SETXY SETX SETY SETH SETPC
       XCOR YCOR HEADING RANDOM SQRT SIN COS INT ABS REPCOUNT
       REPEAT IF IFELSE TO..END STOP OUTPUT MAKE :x PRINT SHOW LOAD BYE
