; K4SG header for sidplay: two segments. Segment 0 is a 16-byte entry stub at
; $0250 (free low RAM above the ROM's DATA) that banks block 7 onto the RAM at
; $E000 and jumps there; segment 1 is the player at physical $E000. The
; loader must not bank block 7 itself: the ROM still runs from it until the
; program has been entered.
        .import __PRG_START__, __BSS_RUN__
        .segment "SEGHDR"
        .byte "K4SG"
        .byte 2, 0
        .word $0250                     ; entry: the stub
        .dword $00000250
        .dword stub_end - stub
        .byte $FF, 0, 0, 0
        .dword $0000E000
        .dword __BSS_RUN__ - __PRG_START__
        .byte $FF, 0, 0, 0
        .segment "STUB"
stub:   lda #$00
        sta $D61C               ; bank 7 base = $0000E000
        lda #$E0
        sta $D61D
        lda #$00
        sta $D61E
        sta $D61F               ; byte 3 = 0: on
        jmp $E000
stub_end:
