/* demo/opt.h -- POSIX-style options for the machine's programs (2026-10-07),
 * as K/OS's shell words take them (rom/kernal.c, opt_get):
 *
 *   -x            a letter                 -xyz   three of them
 *   --word        the letter it stands for --     the options end
 *
 * Flags only: no program here takes a value.  Set opt_s to the argument
 * string (the ARGS system call's, NUL-terminated, under 256 bytes); each
 * call of opt() moves opt_i past one option and returns its letter, lower
 * case -- '?' for a long name not in LONGS -- or 0 at the first word that is
 * not an option (opt_i then at that word) or after "--".  LONGS lists the
 * long names, each led by its letter: "ssys\0uupper\0" makes --sys -s and
 * --upper -u.  demo/opt.s is the code (link demo/opt.o). */
#ifndef K4510_OPT_H
#define K4510_OPT_H
extern const char *opt_s;
extern unsigned char opt_i;
char __fastcall__ opt(const char *longs);
#endif
