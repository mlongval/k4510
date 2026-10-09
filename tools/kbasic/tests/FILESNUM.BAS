' numbered files, the old spelling: still accepted (a name is the house style)
OPEN "KBN.TXT" FOR OUTPUT AS #1
PRINT #1, "ann", 120
PRINT #1, "bob"; ","; 95
CLOSE #1
OPEN "KBN.TXT" FOR APPEND AS #1
PRINT #1, "cat", 7.5
CLOSE #1
OPEN "KBN.TXT" FOR INPUT AS #2
n = 0
DO UNTIL EOF(2)
  INPUT #2, who$, pts
  n = n + 1: PRINT "F:"; who$; pts
LOOP
CLOSE #2
OPEN "KBN.TXT" FOR INPUT AS 3
LINE INPUT #3, l$: PRINT "L:"; l$
CLOSE 3
PRINT "N:"; n
