/LANG/C -- C on the K4510, with cc65.

Each .C here has its .prg beside it.  CC NAME compiles NAME.C in the
current directory into name.prg (the compiler runs on the Linux beside
the machine); NAME then runs it.  The header is demo/k4510.h in the
source tree: the registers, far memory, the ROM's calls.

HELLO.C    the smallest program
SIEVE.C    the Byte Sieve, timed on the frame counter
