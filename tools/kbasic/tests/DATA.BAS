' DATA, READ, RESTORE
READ a, b$, c%
PRINT "D1:"; a; b$; c%
READ x$, y$
PRINT "D2:"; x$; "|"; y$
RESTORE Second
READ z: PRINT "D3:"; z
RESTORE
READ a: PRINT "D4:"; a
DATA 1.5, "hello, world", 7
DATA plain text, "  spaced  "
Second:
DATA -42
