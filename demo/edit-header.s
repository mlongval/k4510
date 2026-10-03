; K4SG header for EDIT (demo/edit-header.s): three segments -- the program
; at $2000-$CBFF, code at $E000-$FEFF (the RAM under the ROM that a
; program owns while it runs; the launcher engages blocks 5-7 onto it), and
; code at $1800-$1FFB, under the image, where BSS does not reach.  VI's keys
; (2026-10-02) took EDIT past what the first two hold.  ld65 writes the
; memory areas in edit.cfg's order (HDR, PRG, HI, LO), so the bytes follow
; the table.  BSS is at $0800 and is not in the file.
        .export __K4SG__ : absolute = 1
        .import __PRG_START__, __PRG_LAST__, __HI_START__, __HI_LAST__, __LO_START__, __LO_LAST__
        .segment "SEGHDR"
        .byte "K4SG"
        .byte 3                         ; segments
        .byte 0                         ; flags
        .word __PRG_START__             ; entry: prg0's start is the first byte of PRG
        ; phys[4] len[4] block pad[3]
        .dword __PRG_START__
        .dword __PRG_LAST__ - __PRG_START__
        .byte $FF, 0, 0, 0              ; main: in the unmapped view
        .dword __HI_START__             ; $E000: physical = CPU, block 7's own RAM
        .dword __HI_LAST__ - __HI_START__
        .byte $FF, 0, 0, 0              ; no bank: the launcher engages block 7 there anyway
        .dword __LO_START__             ; $1800: physical = CPU, bank 0's own RAM
        .dword __LO_LAST__ - __LO_START__
        .byte $FF, 0, 0, 0
