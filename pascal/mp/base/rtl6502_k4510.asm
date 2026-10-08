	opt l-
	opt c+			; the 45GS02 has the 65C02 set: stz, bra, (zp)

/* -----------------------------------------------------------------------
/*                CPU 6502 runtime library - K4510  [K4510]
/* -----------------------------------------------------------------------
/* rtl_default.asm's list, with two substitutions: SINGLE arithmetic on
/* the MATH unit at $D700 (k4510\single.asm) instead of the software
/* library -- unless it is assembled with mads -d:SOFTFLOAT=1 -- and the
/* whole-number multiply and divide on FRED's integer unit at $D770
/* (k4510\fredint.asm, word.asm, cardinal.asm) -- mads -d:SOFTINT=1 not.
/* The console is JIM, the terminal at $DA00: @putchar and @ClrScr
/* write there.
/* -----------------------------------------------------------------------

@AllocMem
@FreeMem

*/

; -----------------------------------------------------------------------

	icl 'runtime\macros.asm'

; -----------------------------------------------------------------------

MAXSIZE = 4

.enum	e@file
	eof = 1, open, assign
.ende

.struct	s@file
pfname	.word		; pointer to string with filename
record	.word		; record size
chanel	.byte		; channel *$10
status	.byte		; status bit 0..7
buffer	.word		; load/write buffer
nrecord	.word		; number of records for load/write
numread	.word		; pointer to variable, length of loaded data
.ends

	icl 'runtime\int.asm'
	icl 'runtime\add.asm'
	icl 'runtime\sub.asm'
	icl 'runtime\shl.asm'
	icl 'runtime\shr.asm'
	icl 'runtime\neg.asm'
	icl 'runtime\expand.asm'
	icl 'runtime\ini.asm'
	icl 'runtime\mov.asm'
	icl 'runtime\hi.asm'

	icl 'common\screensize.asm'
	icl 'common\cmpstr.asm'
	icl 'common\memmove.asm'
	icl 'common\memset.asm'
	icl 'common\strmove.asm'
	icl 'common\recmove.asm'
	icl 'common\strcat.asm'

	icl 'common\shortint.asm'
	icl 'common\smallint.asm'
	icl 'common\integer.asm'
	icl 'common\byte.asm'
	icl 'k4510\fredint.asm'		; whole-number * and / on FRED's integer unit ($D770)
	icl 'k4510\word.asm'		; common\'s, leaf routines through fredint (mads -d:SOFTINT=1: not)
	icl 'k4510\cardinal.asm'

	icl 'common\shortreal.asm'
	icl 'common\shortreal_trunc.asm'
	icl 'common\shortreal_round.asm'
	icl 'common\shortreal_frac.asm'

	icl 'common\real.asm'
	icl 'common\real_trunc.asm'
	icl 'common\real_round.asm'
	icl 'common\real_frac.asm'

	.ifdef SOFTFLOAT		; mads -d:SOFTFLOAT=1
	icl 'common\single.asm'		; the software library (for comparison)
	.else
	icl 'k4510\single.asm'		; SINGLE on the MATH unit
	.fi
	icl 'common\float16_add_sub.asm'
	icl 'common\float16_mul.asm'
	icl 'common\float16_div.asm'
	icl 'common\float16_int.asm'
	icl 'common\float16_round.asm'
	icl 'common\float16_frac.asm'
	icl 'common\float16_cmp.asm'
	icl 'common\float16_i2f.asm'

	icl 'common\mul40.asm'
	icl 'common\mul48.asm'
	icl 'common\mul64.asm'
	icl 'common\mul96.asm'
	icl 'common\mul320.asm'

	icl 'common\int2hex.asm'
	icl 'common\int2str.asm'
	icl 'common\str2int.asm'

	icl 'common\printchr.asm'
	icl 'common\printstr.asm'
	icl 'common\printbool.asm'
	icl 'common\printint.asm'
	icl 'common\printsingle.asm'
	icl 'common\printfloat.asm'
	icl 'common\printfloat16.asm'

	icl 'common\allocmem.asm'

; -----------------------------------------------------------------------

	icl 'k4510\k4510.hea'
	icl 'k4510\putchar.asm'		; @putchar
	icl 'k4510\clrscr.asm'		; @clrscr

; -----------------------------------------------------------------------

	opt l+
