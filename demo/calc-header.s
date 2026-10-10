; K4SG header for CALC (demo/calc-header.s): three segments -- the program at
; $2000-$CCFF, code at $E000-$FEFF (the RAM under the ROM that a program
; owns while it runs; the launcher engages blocks 5-7 onto it), as EDIT's
; (demo/edit-header.s), and code at $1A00-$1FFB, under the image, where BSS
; does not reach; and the overlay (undo and the lookups), linked for $E000
; and loaded to far memory, where the far-call gate banks it in (CALO_P in
; calc.c is this address).  ld65 writes the memory areas in calc.cfg's
; order (HDR, PRG, HI, LO, CALO), so the bytes follow the table.  BSS is at
; $0800 and is not in the file.
        .export __K4SG__ : absolute = 1
        .import __PRG_START__, __PRG_LAST__, __HI_START__, __HI_LAST__, __LO_START__, __LO_LAST__
        .import __CALO_START__, __CALO_LAST__
        ; the overlay's entries, through the far-call gate ($DF00 + 4n; the
        ; table is CALOTAB, the overlay's first bytes): calc.c calls them as
        ; functions, so a call is a JSR and not cc65's cast-pointer dance
        .export _u_mark, _u_undo, _u_reset, _lk_call
        _u_mark  = $DF00
        _u_undo  = $DF04
        _u_reset = $DF08
        _lk_call = $DF0C
        .segment "SEGHDR"
        .byte "K4SG"
        .byte 4                         ; segments
        .byte 0                         ; flags
        .word __PRG_START__             ; entry: prg0's start is the first byte of PRG
        ; phys[4] len[4] block pad[3]
        .dword __PRG_START__
        .dword __PRG_LAST__ - __PRG_START__
        .byte $FF, 0, 0, 0              ; main: in the unmapped view
        .dword __HI_START__             ; $E000: physical = CPU, block 7's own RAM
        .dword __HI_LAST__ - __HI_START__
        .byte $FF, 0, 0, 0              ; no bank: the launcher engages block 7 there anyway
        .dword __LO_START__             ; $1A00: physical = CPU, bank 0's own RAM
        .dword __LO_LAST__ - __LO_START__
        .byte $FF, 0, 0, 0
        .dword $0E000000                ; CALO: far memory (CALO_P)
        .dword __CALO_LAST__ - __CALO_START__
        .byte $FF, 0, 0, 0              ; banked by the gate when called, not here
