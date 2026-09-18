; K4510: MARK's measured loops (demo/mark.c is the stopwatch and the report).
;
; They are in assembly so that what is timed is the CPU and not this month's
; cc65: a C sieve gets faster when the compiler does, and a number that moves
; without the machine moving is no use for comparing machines.  65C02 code, no
; 45GS10 instruction anywhere, so the same source runs on anything from a BBC
; Master up and the comparison is of like with like.  (STZ is "store Z" on the
; 45GS10: the register is 0 under K/OS and nothing here changes it.)
;
; mk_mandel is gfoot's mandelbrot6502 (github.com/gfoot/mandelbrot6502, the
; Unlicense -- public domain), itself after Gordon Henderson's BASIC benchmark:
; the same region, the same 134 x 80, the same 34 iterations, the same 8.8 fixed
; point with a 24-bit shift-and-add multiply, instruction for instruction.  One
; change: the iteration count goes into a buffer instead of out of a serial
; port, so what is timed is arithmetic and not somebody's UART.
;
; And one addition (Doc, 2026-09-18: "with and without using the MATH unit"):
; with mk_math set, the three multiplies of an iteration -- a*a, a*b, b*b, up to
; sixteen shift-and-add passes between them -- are three writes to the MATH
; unit's multiplier at $D770 instead.  Everything else is the same code, and
; the picture must come out the same to the byte: mark.c checks the sum.  The
; test of the flag is five cycles in an iteration of well over a thousand.

        .setcpu "65C02"
        .export _mk_spin, _mk_bcd, _mk_sieve, _mk_mandel, _mk_copy
        .export _mk_img, _mk_flags, _mk_tobcd
        .exportzp mk_end_of_zp, _mk_math

        .zeropage
p:      .res 2
q:      .res 2
n1:     .res 1
n2:     .res 1
n3:     .res 1
ia:     .res 1
ib:     .res 1
vc:     .res 1
s:      .res 1
res:    .res 1
rc:     .res 1
err:    .res 2
cnt:    .res 2
i:      .res 2
k:      .res 2
pr:     .res 2
; gfoot's, under his names
zp_a:     .res 2
zp_b:     .res 2
zp_aa:    .res 2
zp_bb:    .res 2
zp_ab:    .res 2
zp_x:     .res 2
zp_y:     .res 2
zp_aasub: .res 1
zp_bbsub: .res 1
zp_absub: .res 1
zp_a24:   .res 3
zp_b24:   .res 3
zp_iter:  .res 1
zp_xcount: .res 1
zp_ycount: .res 1
_mk_math:  .res 1              ; 0: the multiplies by shift and add, as gfoot wrote them; 1: by the MATH unit
mk_end_of_zp:

        .bss
_mk_img:   .res 134 * 80        ; the Mandelbrot: iterations LEFT at each point (0 = never escaped)
_mk_flags: .res 8192            ; the sieve's flags (8191 of them), and where COPY copies to
_mk_tobcd: .res 200             ; n -> n mod 100 in BCD; filled in by mark.c

        .code

; ---- SPIN ------------------------------------------------------------------
; void __fastcall__ mk_spin(unsigned char n)
; A loop whose length in NMOS 6502 cycles is known exactly, so that time taken
; turns into "the work of a 6502 at so many MHz":
;   the X loop   256 x DEX(2) + 255 x BNE taken(3) + 1 x BNE not(2)      =   1 279
;   the Y loop   256 x (1279 + DEY 2) + 255 x 3 + 2                      = 328 703
;   the n loop   n x (328703 + DEC zp 5) + (n-1) x 3 + 2                 = n x 328 711 - 1
; The branches must not cross a page or each is a cycle longer: asserted below.
_mk_spin:
        sta n1
        ldx #0
        ldy #0
spin_loop:
        dex
        bne spin_loop
        dey
        bne spin_loop
        dec n1
        bne spin_loop
spin_end:
        rts
        .assert >spin_loop = >spin_end, error, "mk_spin crosses a page: its cycle count is wrong"

; ---- BCD -------------------------------------------------------------------
; unsigned mk_bcd(void)  -> how many of 40 000 decimal-mode results were wrong
; Every valid pair 00-99 x 00-99, carry in clear and set, ADC and SBC, the
; accumulator and the carry out checked against the same sum done in binary.
; (Invalid BCD digits are left alone: there the chips themselves disagree, and
; a benchmark's gate should not fail a CPU for the taste of its designer.)
; Interrupts are held off for the six instructions with D set: a handler that
; does not expect decimal mode is somebody else's bug to find.
_mk_bcd:
        stz err
        stz err+1
        stz vc
bcd_c:  stz ia
bcd_a:  stz ib
bcd_b:
        ; ADC: s = ia + ib + c
        clc
        lda ia
        adc ib
        clc
        adc vc
        sta s
        ldx ia
        ldy ib
        lda vc
        lsr                     ; C = carry in
        php
        sei
        sed
        lda _mk_tobcd,x
        adc _mk_tobcd,y
        cld
        sta res
        lda #0
        rol
        sta rc
        plp
        jsr bcd_check
        ; SBC: s = ia + 99 + c - ib   (so 100 and over means no borrow)
        clc
        lda ia
        adc #99
        adc vc
        sec
        sbc ib
        sta s
        ldx ia
        ldy ib
        lda vc
        lsr
        php
        sei
        sed
        lda _mk_tobcd,x
        sbc _mk_tobcd,y
        cld
        sta res
        lda #0
        rol
        sta rc
        plp
        jsr bcd_check
        inc ib
        lda ib
        cmp #100
        bne bcd_b
        inc ia
        lda ia
        cmp #100
        bne bcd_a
        inc vc
        lda vc
        cmp #2
        bne bcd_c
        lda err
        ldx err+1
        rts
bcd_check:                      ; res and rc against table[s] and s >= 100
        ldx s
        lda res
        cmp _mk_tobcd,x
        bne bcd_bad
        cpx #100                ; C = s >= 100
        lda #0
        rol
        cmp rc
        beq bcd_ok
bcd_bad:
        inc err
        bne bcd_ok
        inc err+1
bcd_ok: rts

; ---- SIEVE -----------------------------------------------------------------
; unsigned mk_sieve(void)  -> the primes found by the last pass: 1899
; Gilbreath's Byte Sieve (BYTE, September 1981), as published: 8191 flags, ten
; passes, prime = i + i + 3, strike k = i + prime while k <= 8190.
_mk_sieve:
        lda #10
        sta n1
sv_pass:
        lda #<_mk_flags
        sta p
        lda #>_mk_flags
        sta p+1
        ldx #32                 ; 32 pages: all 8192 of the buffer, 8191 used
        lda #1
        ldy #0
sv_fill:
        sta (p),y
        iny
        bne sv_fill
        inc p+1
        dex
        bne sv_fill
        stz cnt
        stz cnt+1
        stz i
        stz i+1
sv_i:
        clc
        lda i
        adc #<_mk_flags
        sta p
        lda i+1
        adc #>_mk_flags
        sta p+1
        ldy #0
        lda (p),y
        beq sv_next
        lda i                   ; prime = i + i + 3
        asl
        sta pr
        lda i+1
        rol
        sta pr+1
        clc
        lda pr
        adc #3
        sta pr
        bcc sv_k0
        inc pr+1
sv_k0:  clc                     ; k = i + prime
        lda i
        adc pr
        sta k
        lda i+1
        adc pr+1
        sta k+1
sv_k:   lda k                   ; while k <= 8190
        cmp #<8191
        lda k+1
        sbc #>8191
        bcs sv_kend
        clc
        lda k
        adc #<_mk_flags
        sta p
        lda k+1
        adc #>_mk_flags
        sta p+1
        lda #0
        tay
        sta (p),y
        clc
        lda k
        adc pr
        sta k
        lda k+1
        adc pr+1
        sta k+1
        bra sv_k
sv_kend:
        inc cnt
        bne sv_next
        inc cnt+1
sv_next:
        inc i
        bne sv_cmp
        inc i+1
sv_cmp: lda i
        cmp #<8191
        lda i+1
        sbc #>8191
        bcs sv_done
        jmp sv_i
sv_done:
        dec n1
        beq sv_out
        jmp sv_pass
sv_out: lda cnt
        ldx cnt+1
        rts

; ---- MANDEL ----------------------------------------------------------------
; void mk_mandel(void)   gfoot's, see the top of the file
YMIN   = $fec0                  ; -1.25
YSTEP  = $0008                  ;  0.03125
XMIN   = $fe74                  ; -1.546875
XSTEP  = $0004                  ;  0.015625
XCOUNT = 134
YCOUNT = 80
MAXITER = 34

_mk_mandel:
        lda #<_mk_img
        sta q
        lda #>_mk_img
        sta q+1
        lda #<YMIN
        sta zp_y
        lda #>YMIN
        sta zp_y+1
        lda #YCOUNT
        sta zp_ycount
yloop:
        lda #<XMIN
        sta zp_x
        lda #>XMIN
        sta zp_x+1
        lda #XCOUNT
        sta zp_xcount
xloop:
        lda zp_x
        sta zp_a
        lda zp_x+1
        sta zp_a+1
        lda zp_y
        sta zp_b
        lda zp_y+1
        sta zp_b+1
        ldx #MAXITER
        stx zp_iter

iterloop:
        ; aa = a*a, bb = b*b, ab = a*b, on absolute values; Y bit 0 = the sign b is owed
        ldy #0
        lda zp_a+1
        bpl a_pos
        iny
        sec
        lda #0
        sbc zp_a
        sta zp_a
        sta zp_a24
        lda #0
        sbc zp_a+1
        sta zp_a+1
        sta zp_a24+1
        bra a_done
a_pos:  sta zp_a24+1
        lda zp_a
        sta zp_a24
a_done: stz zp_a24+2

        lda zp_b+1
        bpl b_pos
        iny
        sec
        lda #0
        sbc zp_b
        sta zp_b
        sta zp_b24
        lda #0
        sbc zp_b+1
        sta zp_b+1
        sta zp_b24+1
        bra b_done
b_pos:  sta zp_b24+1
        lda zp_b
        sta zp_b24
b_done: stz zp_b24+2

        stz zp_aasub
        stz zp_aa
        stz zp_aa+1
        stz zp_bbsub
        stz zp_bb
        stz zp_bb+1
        stz zp_absub
        stz zp_ab
        stz zp_ab+1

        lda _mk_math
        beq bitloop_a
        jmp math_mul

bitloop_a:
        lsr zp_a+1
        ror zp_a
        bcc bitloop_b
        clc                     ; aa += a24
        lda zp_aasub
        adc zp_a24
        sta zp_aasub
        lda zp_aa
        adc zp_a24+1
        sta zp_aa
        lda zp_aa+1
        adc zp_a24+2
        sta zp_aa+1
        clc                     ; ab += b24
        lda zp_absub
        adc zp_b24
        sta zp_absub
        lda zp_ab
        adc zp_b24+1
        sta zp_ab
        lda zp_ab+1
        adc zp_b24+2
        sta zp_ab+1
bitloop_b:
        lsr zp_b+1
        ror zp_b
        bcc b_bit_done
        clc                     ; bb += b24
        lda zp_bbsub
        adc zp_b24
        sta zp_bbsub
        lda zp_bb
        adc zp_b24+1
        sta zp_bb
        lda zp_bb+1
        adc zp_b24+2
        sta zp_bb+1
b_bit_done:
        asl zp_a24
        rol zp_a24+1
        rol zp_a24+2
        asl zp_b24
        rol zp_b24+1
        rol zp_b24+2
        lda zp_a
        ora zp_a+1
        bne bitloop_a
        lda zp_b
        ora zp_b+1
        bne bitloop_b

after_mul:
        lda zp_absub            ; b = 2ab
        asl
        lda zp_ab
        rol
        sta zp_b
        lda zp_ab+1
        rol
        sta zp_b+1
        tya
        and #1
        beq sign_done
        sec
        lda #0
        sbc zp_b
        sta zp_b
        lda #0
        sbc zp_b+1
        sta zp_b+1
sign_done:
        clc                     ; aa + bb against 5
        lda zp_aa
        adc zp_bb
        lda zp_aa+1
        adc zp_bb+1
        cmp #5
        bcs iterloopend
        sec                     ; a = aa - bb + x
        lda zp_aa
        sbc zp_bb
        sta zp_a
        lda zp_aa+1
        sbc zp_bb+1
        sta zp_a+1
        clc
        lda zp_a
        adc zp_x
        sta zp_a
        lda zp_a+1
        adc zp_x+1
        sta zp_a+1
        clc                     ; b = b + y
        lda zp_b
        adc zp_y
        sta zp_b
        lda zp_b+1
        adc zp_y+1
        sta zp_b+1
        dec zp_iter
        beq iterloopend
        jmp iterloop

iterloopend:
        lda zp_iter             ; gfoot prints chars[iter] here; we keep iter
        sta (q)
        inc q
        bne xnext
        inc q+1
xnext:  clc
        lda zp_x
        adc #<XSTEP
        sta zp_x
        lda zp_x+1
        adc #>XSTEP
        sta zp_x+1
        dec zp_xcount
        beq xloopend
        jmp xloop
xloopend:
        clc
        lda zp_y
        adc #<YSTEP
        sta zp_y
        lda zp_y+1
        adc #>YSTEP
        sta zp_y+1
        dec zp_ycount
        beq mandel_out
        jmp yloop
mandel_out:
        rts

; The MATH unit's multiplier: MULTINA $D770, MULTINB $D774 (unsigned 32-bit, LE),
; MULTOUT $D778 = A * B, ready the cycle after the write.  |a| and |b| are 16
; bits; the top halves of both inputs are zeroed once, in mk_mandel's caller.
; gfoot keeps 24 bits of each product (sub, lo, hi): the same three bytes here.
MULTINA = $D770
MULTINB = $D774
MULTOUT = $D778
math_mul:
        lda zp_a
        sta MULTINA
        sta MULTINB
        lda zp_a+1
        sta MULTINA+1
        sta MULTINB+1
        lda MULTOUT             ; a * a
        sta zp_aasub
        lda MULTOUT+1
        sta zp_aa
        lda MULTOUT+2
        sta zp_aa+1
        lda zp_b
        sta MULTINB
        lda zp_b+1
        sta MULTINB+1
        lda MULTOUT             ; a * b
        sta zp_absub
        lda MULTOUT+1
        sta zp_ab
        lda MULTOUT+2
        sta zp_ab+1
        lda zp_b
        sta MULTINA
        lda zp_b+1
        sta MULTINA+1
        lda MULTOUT             ; b * b
        sta zp_bbsub
        lda MULTOUT+1
        sta zp_bb
        lda MULTOUT+2
        sta zp_bb+1
        jmp after_mul

; ---- COPY ------------------------------------------------------------------
; void __fastcall__ mk_copy(unsigned char times)
; 8192 bytes from the picture to the flags, a byte at a time through (zp),Y --
; the only way a 6502 has.  16 NMOS cycles a byte.
_mk_copy:
        sta n2
cp_t:   lda #<_mk_img
        sta p
        lda #>_mk_img
        sta p+1
        lda #<_mk_flags
        sta q
        lda #>_mk_flags
        sta q+1
        ldx #32
        ldy #0
cp_b:   lda (p),y
        sta (q),y
        iny
        bne cp_b
        inc p+1
        inc q+1
        dex
        bne cp_b
        dec n2
        bne cp_t
        rts
