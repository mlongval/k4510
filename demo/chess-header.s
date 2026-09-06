; K4SG header for chess (demo/chess-header.s): two segments -- the program at
; $2000 and the pieces, dropped whole into far memory at
; CHESS_PHYS by the loader. ld65 writes the memory areas in chess.cfg order
; (HDR, PRG, DAT), so the bytes follow the table.
        .export __K4SG__ : absolute = 1
        .import __PRG_START__, __BSS_RUN__, __DAT_START__, __DAT_LAST__
        .segment "SEGHDR"
        .byte "K4SG"
        .byte 2                         ; segments
        .byte 0                         ; flags
        .word __PRG_START__             ; entry: prg0's start is the first byte of PRG
        ; phys[4] len[4] block pad[3]
        .dword __PRG_START__
        .dword __BSS_RUN__ - __PRG_START__      ; the file holds everything up to BSS (prg0 zeroes BSS)
        .byte $FF, 0, 0, 0              ; main: no bank (it is in the unmapped view)
        .dword $00110000                ; CHESS_PHYS, as chess.c has it
        .dword __DAT_LAST__ - __DAT_START__
        .byte $FF, 0, 0, 0              ; data: read by VICKY, never banked in
        .segment "CHESS"
        .incbin "demo/chess.bin"
