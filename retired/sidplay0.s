; K4510 startup for sidplay: a cc65 program that lives UNDER the ROM at
; $E000-$FEFF (block 7 banked to RAM by the K4SG loader), so a C64 tune can
; own $0400-$CFFF. Exit goes through a trampoline in low RAM: the banks must
; be cleared by code that is not itself banked away.
        .export   __STARTUP__ : absolute = 1
        .export   _exit, _far_poke, _far_peek
        .export   _tune_call
        .import   _main, zerobss, initlib, incsp4
        .import   __PRG_START__, __PRG_SIZE__
        .importzp sp, sreg, ptr1

        .zeropage
fp:     .res 4

        .segment "STARTUP"
start:  lda #<(__PRG_START__ + __PRG_SIZE__)
        sta sp
        lda #>(__PRG_START__ + __PRG_SIZE__)
        sta sp+1
        jsr zerobss
        jsr initlib
        jsr _main
_exit:  ldx #tramp_end - tramp - 1      ; the exit trampoline to $0230 (free low RAM; the tune is silent and dead by now)
@c:     lda tramp,x
        sta $0230,x
        dex
        bpl @c
        jmp $0230
tramp:  lda #0                  ; MAP everything off: clears the banks too
        tax
        tay
        .byte $A3, $00          ; LDZ #0
        .byte $5C               ; MAP
        .byte $EA               ; EOM
        rts                     ; to the ROM's call_prog
tramp_end:

; void __fastcall__ tune_call(unsigned addr)  -- the song number in tune_a:
; the player's zero page is $40-$63 (36 bytes, sidplay.cfg); the tune owns the
; rest. Around a call those 36 bytes are swapped for the tune's copy, so both
; sides keep their state. The target lives outside the zero page.
        .export _tune_a
_tune_call:
        sta target
        stx target+1
        ldx #35
@s:     lda $40,x
        sta player_zp,x
        lda tune_zp,x
        sta $40,x
        dex
        bpl @s
        lda _tune_a
        ldx #0
        ldy #0
        jsr dojsr
        ldx #35
@r:     lda $40,x
        sta tune_zp,x
        lda player_zp,x
        sta $40,x
        dex
        bpl @r
        rts
dojsr:  jmp (target)

        .segment "LOWBSS"
_tune_a:   .res 1
target:    .res 2
player_zp: .res 36
tune_zp:   .res 36

        .segment "CODE"
; ---- the flat-memory helpers, copied from prg0.s (see there) ----
; void __fastcall__ far_poke(unsigned long a, unsigned char v)
_far_poke:
        pha
        ldy #0
        lda (sp),y
        sta fp
        iny
        lda (sp),y
        sta fp+1
        iny
        lda (sp),y
        sta fp+2
        iny
        lda (sp),y
        sta fp+3
        pla
        .byte $EA               ; NOP prefix: 32-bit flat
        sta (fp)                ; STA [fp],Z  (Z = 0)
        jmp incsp4


; unsigned char __fastcall__ far_peek(unsigned long a)
_far_peek:
        sta fp
        stx fp+1
        lda sreg
        sta fp+2
        lda sreg+1
        sta fp+3
        .byte $EA
        lda (fp)                ; LDA [fp],Z
        ldx #0
        rts
