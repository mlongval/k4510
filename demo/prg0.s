; K4510 .prg startup for cc65 programs run from the system ROM.
; Header: load address, run address. The ROM jumps here with JSR; we come
; back with RTS after unmapping. Also the helpers cc65 cannot express:
; MAP (a 16 KB window at $2000-$5FFF, the text screen's RAM, unused while a
; program runs) and 45GS02 flat 32-bit pokes/peeks.
        .export   __STARTUP__ : absolute = 1
        .export   _exit, _map_window, _far_poke, _far_peek, _far_poke16
        .import   _main, zerobss, initlib, incsp4
        .import   __PRG_START__, __PRG_SIZE__
        .importzp sp, sreg

        .zeropage
fp:     .res 4

        .segment "HEADER"
        .word __PRG_START__
        .word start

        .segment "STARTUP"
start:  lda #<(__PRG_START__ + __PRG_SIZE__)
        sta sp
        lda #>(__PRG_START__ + __PRG_SIZE__)
        sta sp+1
        jsr zerobss
        jsr initlib
        jsr _main
_exit:
.ifndef NOMAP
        lda #0                  ; MAP everything off
        tax
        tay
        .byte $A3, $00          ; LDZ #0
        .byte $5C               ; MAP
        .byte $EA               ; EOM
.endif
        rts                     ; back to the ROM shell
; NOMAP (demo/prg0-nomap.o): for a program that lives in the RAM under the
; ROM -- MONITOR at $E000.  There MAP-all-off shows the ROM again at the very
; address of the RTS after it, the RTS is fetched from ROM, and the machine
; restarts (2026-09-13: every MON ended in "exec: name?" from the boot
; path).  Such a program must not use map_window, and has nothing to undo.

; void __fastcall__ map_window(unsigned long phys)
; MAP CPU $2000-$5FFF (blocks 1-2, 16 KB) onto phys. phys: multiple of 256,
; >= $2000 within its megabyte, bits 16-19 not all ones.
_map_window:
        stx fp                  ; bits 8-15
        lda sreg
        sta fp+1                ; bits 16-23
        lda sreg+1
        asl
        asl
        asl
        asl
        sta fp+2
        lda fp+1
        lsr
        lsr
        lsr
        lsr
        ora fp+2
        sta fp+2                ; megabyte
        lda fp
        sec
        sbc #$20
        sta fp                  ; offset bits 8-15
        lda fp+1
        and #$0F
        sbc #0
        and #$0F
        sta fp+1                ; offset bits 16-19
        lda fp+2
        tay
        ldx #$0F
        .byte $A3, $0F          ; LDZ #$0F
        .byte $5C               ; MAP: megabytes
        lda fp+1
        .byte $4B               ; TAZ  (high half: no blocks)
        lda fp+1
        ora #$60                ; low half: blocks 1,2
        tax
        lda fp
        tay
        .byte $5C               ; MAP: offsets + masks
        .byte $EA               ; EOM
        .byte $A3, $00          ; LDZ #0
        rts

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

; void __fastcall__ far_poke16(unsigned long a, unsigned int v)
_far_poke16:
        pha
        phx
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
        plx
        pla
        .byte $EA
        sta (fp)
        txa
        .byte $A3, $01          ; LDZ #1
        .byte $EA
        sta (fp)
        .byte $A3, $00          ; LDZ #0
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
