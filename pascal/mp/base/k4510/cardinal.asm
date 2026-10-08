; [K4510] common\cardinal.asm with the leaf multiply/divide on FRED (k4510\fredint.asm)

/*
	imulECX
	imulEAX_CX
	imulCARD
	idivCARD
	idivEAX_ECX
	idivEAX_CX
*/

;
; changes: 2023-04-22
;

;---------------------------------------------------------------------------

.proc	imulCARD

	jsr @iniEAX_ECX_CARD

	jmp imulECX
.endp


.proc	idivCARD

MOD
	jsr @iniEAX_ECX_CARD

	jmp idivEAX_ECX.CARD
.endp


;---------------------------------------------------------------------------
; *** MUL32: 32-bit multiply
; EAX * ECX -> ZTMP8-ZTMP11
.ifdef SOFTINT
.proc	imulECX

	lda #0
	sta :ZTMP10
	sta :ZTMP9
	sta :ZTMP8

	ldy #32
MUL320	lsr :ZTMP10
	ror :ZTMP9
	ror :ZTMP8
	ror @
	ror :eax+3
	ror :eax+2
	ror :eax+1
	ror :eax
	bcc MUL321
	clc
	adc :ecx
	pha
	lda :ecx+1
	adc :ZTMP8
	sta :ZTMP8
	lda :ecx+2
	adc :ZTMP9
	sta :ZTMP9
	lda :ecx+3
	adc :ZTMP10
	sta :ZTMP10
	pla
MUL321	dey
       	bpl MUL320

	rts
.endp
.else
.proc	imulECX				; EAX * ECX -> EAX (low 32); A, ZTMP8-10 the high bytes 4, 5-7
	lda :eax
	sta F_MA
	lda :eax+1
	sta F_MA+1
	lda :eax+2
	sta F_MA+2
	lda :eax+3
	sta F_MA+3
	lda :ecx
	sta F_MB
	lda :ecx+1
	sta F_MB+1
	lda :ecx+2
	sta F_MB+2
	lda :ecx+3
	sta F_MB+3
	lda F_MO
	sta :eax
	lda F_MO+1
	sta :eax+1
	lda F_MO+2
	sta :eax+2
	lda F_MO+3
	sta :eax+3
	lda F_MO+5
	sta :ZTMP8
	lda F_MO+6
	sta :ZTMP9
	lda F_MO+7
	sta :ZTMP10
	lda F_MO+4
	rts
.endp
.endif


/*
;---------------------------------------------------------------------------
; *** MUL32: 32-bit multiply
; EAX * CX -> ZTMP8-ZTMP11

.proc	imulEAX_CX

	lda #0
	sta :TMP

	ldy #32
MUL320	lsr :TMP
	ror @
	ror :eax+3
	ror :eax+2
	ror :eax+1
	ror :eax
	bcc MUL321

	clc
	adc :ecx
	sta :TMP+1
	lda :ecx+1
	adc :TMP
	sta :TMP
	
	lda :TMP+1
MUL321	dey
       	bpl MUL320

	rts
.endp
*/

;---------------------------------------------------------------------------
; *** UDIV32: 32-bit unsigned division
; input: dividend at ZTMP0-ZTMP3
;        divisor at ZTMP4-ZTMP7
; output: result at ZTMP0-ZTMP3
;         remainder at ZTMP8-ZTMP11
; X,Y preserved


.ifdef SOFTINT
.proc	@CARDINAL

DIV	.local
A	= :EAX
B	= :ECX
	.endl

MOD	.local
A	= :EAX
B	= :ECX

RESULT	= :EAX+4
	.endl

	mva :ecx ecx0
	sta ecx0_
	mva :ecx+1 ecx1
	sta ecx1_
	mva :ecx+2 ecx2
	sta ecx2_
	mva :ecx+3 ecx3

	LDA #0
	sta :eax+4
	sta :eax+5
	sta :eax+6
	sta :eax+7

	LDY #32
	jmp UDIV321
	
UDIV320	DEY
	BEQ stop
UDIV321
	ASL :eax
	ROL :eax+1
	ROL :eax+2
	ROL :eax+3
	ROL :eax+4
	ROL :eax+5
	ROL :eax+6
	ROL :eax+7
			;do a subtraction
	LDA :eax+4
	CMP ecx0: #0
	LDA :eax+5
	SBC ecx1: #0
	LDA :eax+6
	SBC ecx2: #0
	LDA :eax+7
	SBC ecx3: #0
	BCC UDIV320
 			;overflow, do the subtraction again, this time store the result
	STA :eax+7	;we have the high byte already

	LDA :eax+4
	SBC ecx0_: #0	;byte 0
	STA :eax+4
	LDA :eax+5
	SBC ecx1_: #0
	STA :eax+5	;byte 1
	LDA :eax+6
	SBC ecx2_: #0
	STA :eax+6	;byte 2

	INC :eax	;set result bit

	DEY
	BEQ stop

	JMP UDIV321
stop
	rts
.endp
.else
.proc	@CARDINAL			; EAX / ECX -> EAX, remainder EAX+4..7

DIV	.local
A	= :EAX
B	= :ECX
	.endl

MOD	.local
A	= :EAX
B	= :ECX

RESULT	= :EAX+4
	.endl

	lda :eax
	sta F_MA
	lda :eax+1
	sta F_MA+1
	lda :eax+2
	sta F_MA+2
	lda :eax+3
	sta F_MA+3
	lda :ecx
	sta F_MB
	lda :ecx+1
	sta F_MB+1
	lda :ecx+2
	sta F_MB+2
	lda :ecx+3
	sta F_MB+3
	jsr @fredDIVREM
	lda :ztmp
	sta :eax+4
	lda :ztmp+1
	sta :eax+5
	lda :ztmp+2
	sta :eax+6
	lda :ztmp+3
	sta :eax+7
	rts
.endp
.endif



/*
.proc	idivEAX_ECX

REAL	mva :STACKORIGIN-1+STACKWIDTH*2,x :STACKORIGIN-1+STACKWIDTH*3,x
	mva :STACKORIGIN-1+STACKWIDTH,x :STACKORIGIN-1+STACKWIDTH*2,x
	mva :STACKORIGIN-1,x :STACKORIGIN-1+STACKWIDTH,x
	mva #$00 :STACKORIGIN-1,x

CARD	;jsr iniEAX_ECX_CARD

CARD.MOD

MAIN	LDA #0
	STA :ZTMP8
	STA :ZTMP9
	STA :ZTMP10
	STA :ZTMP11

	LDY #32

UDIV320	ASL :eax
	ROL :eax+1
	ROL :eax+2
	ROL :eax+3
	ROL :ZTMP8
	ROL :ZTMP9
	ROL :ZTMP10
	ROL :ZTMP11
			;do a subtraction
	LDA :ZTMP8
	CMP :ecx
	LDA :ZTMP9
	SBC :ecx+1
	LDA :ZTMP10
	SBC :ecx+2
	LDA :ZTMP11
	SBC :ecx+3
	BCC UDIV321
 			;overflow, do the subtraction again, this time store the result
	STA :ecx+3	;we have the high byte already
	LDA :ZTMP8
	SBC :ecx	;byte 0
	STA :ZTMP8
	LDA :ZTMP9
	SBC :ecx+1
	STA :ZTMP9	;byte 1
	LDA :ZTMP10
	SBC :ecx+2
	STA :ZTMP10	;byte 2
	INC :eax	;set result bit

UDIV321	DEY
	BNE UDIV320

	rts
.endp
*/


;---------------------------------------------------------------------------
; Ullrich von Bassewitz, 2009-11-04
;
; CC65 runtime: 32by16 => 16 unsigned division
;
;---------------------------------------------------------------------------
; 32by16 division. Divide ptr1:ptr2 by ptr3. Result is in ptr1, remainder
; in sreg.
;
;   lhs         rhs           result      result also in    remainder
; -----------------------------------------------------------------------
;   ptr1:ptr2   ptr3          ax          ptr1              sreg
;

.ifdef SOFTINT
.proc	idivEAX_CX

MOD

ptr1	= :eax
ptr2	= :eax+2
ptr3	= :ecx
sreg	= :ztmp

	LDA #0
	STA sreg
	STA sreg+1
	STA sreg+2
	STA sreg+3

        ldy     #32

L0:     asl     ptr1
        rol     ptr1+1
        rol     ptr2
        rol     ptr2+1
        rol     @
        rol     sreg+1

        sta	sreg
        cmp     ptr3
        lda     sreg+1
        sbc     ptr3+1
        bcc     L1

        sta     sreg+1
        lda	sreg
        sbc     ptr3
        sta	sreg
        inc     ptr1

L1:     lda	sreg
        dey
        bne     L0

        rts
.endp
.else
.proc	idivEAX_CX			; EAX(32) / CX(16) -> EAX, remainder ZTMP

MOD
	lda :eax
	sta F_MA
	lda :eax+1
	sta F_MA+1
	lda :eax+2
	sta F_MA+2
	lda :eax+3
	sta F_MA+3
	lda :ecx
	sta F_MB
	lda :ecx+1
	sta F_MB+1
	stz F_MB+2
	stz F_MB+3
	jmp @fredDIVREM
.endp
.endif
