n = 0: WHILE n < 3: n = n + 1: WEND: PRINT "A:"; n
n = 0: DO WHILE n < 5: n = n + 2: LOOP: PRINT "B:"; n
n = 10: DO UNTIL n <= 7: n = n - 1: LOOP: PRINT "C:"; n
n = 0: DO: n = n + 1: LOOP WHILE n < 4: PRINT "D:"; n
n = 0: DO: n = n + 1: LOOP UNTIL n = 3: PRINT "E:"; n
n = 0
DO
  n = n + 1
  IF n = 6 THEN EXIT DO
LOOP
PRINT "F:"; n
