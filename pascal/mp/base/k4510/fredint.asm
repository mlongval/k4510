; [K4510] whole-number multiply and divide on FRED's integer unit -- the
; MEGA65's, at the same addresses (core/io.h): MULTINA $D770 and MULTINB
; $D774 (unsigned 32-bit), MULTOUT $D778 = A*B (64-bit), DIVOUT $D76C = A/B
; (B = 0: all ones).  The unit recomputes on every write, so a product or a
; quotient is there to read the moment the last byte is in.
;
; Included by k4510\word.asm and k4510\cardinal.asm, which are common\'s
; with the leaf routines (imulCX, imulCX_AL, imulECX, @WORD, @divAX_CL,
; @CARDINAL, idivEAX_CX) put through here instead of shift-and-add loops;
; their inputs and outputs are exactly common\'s.  Only what timed faster
; on the machine is here (pascal/README.md; doc-18 for Doc, 2026-10-08:
; "move all maths to FRED ONLY if it improves speed").  mads -d:SOFTINT=1
; assembles the software routines instead, to compare.
;
; Not for an interrupt handler: a program's IRQ code that multiplied while
; the main code was mid-multiply would hand it the wrong answer.

F_MA	= $D770
F_MB	= $D774
F_MO	= $D778
F_DIV	= $D76C

; A/B with MA and MB already written: quotient -> eax (32), remainder -> ztmp (32).
; The dividend is taken from F_MA itself before it is overwritten.
.proc	@fredDIVREM
	lda F_MA
	sta :ztmp
	lda F_MA+1
	sta :ztmp+1
	lda F_MA+2
	sta :ztmp+2
	lda F_MA+3
	sta :ztmp+3
	lda F_DIV			; the quotient, into eax and back into A: A*B = q*B
	sta :eax
	sta F_MA
	lda F_DIV+1
	sta :eax+1
	sta F_MA+1
	lda F_DIV+2
	sta :eax+2
	sta F_MA+2
	lda F_DIV+3
	sta :eax+3
	sta F_MA+3
	sec				; remainder = dividend - q*B
	lda :ztmp
	sbc F_MO
	sta :ztmp
	lda :ztmp+1
	sbc F_MO+1
	sta :ztmp+1
	lda :ztmp+2
	sbc F_MO+2
	sta :ztmp+2
	lda :ztmp+3
	sbc F_MO+3
	sta :ztmp+3
	rts
.endp
