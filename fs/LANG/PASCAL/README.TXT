/LANG/PASCAL -- Mad Pascal on the K4510.

Each .PAS here has its .prg beside it.  PAS NAME compiles NAME.PAS in
the current directory into name.prg (the compiler runs on the Linux
beside the machine); NAME then runs it.  `uses crt, k4510` brings in the
screen and the machine (pascal/README.md in the source tree).

HELLO.PAS    Write, colours, ParamStr
PSIEVE.PAS   the Byte Sieve
PFLOAT.PAS   reals on the MATH unit
PGRAPH.PAS   the GRAPH unit on VICKY
PMANDEL.PAS  a Mandelbrot
