; K4SG header for PROG (demo/prog-header.s): two segments -- the program at
; $2000-$CFFF, and the rest of its code at $E000-$FEFF, the RAM under the ROM
; that a program owns while it runs (the launcher engages blocks 5-7 onto it).
; MS-DOS EDIT's clothes (2026-10-02) took PROG past what $2000-$CFFF holds.
; And VI's keys (2026-10-03), two overlays linked for $E000 and loaded to far
; memory, where the far-call gate banks them in (demo/dosvi.h: DOSVI_P1 and
; DOSVI_P2 in prog.c are these addresses).  ld65 writes the memory areas in
; prog.cfg's order (HDR, PRG, HI, VIO1, VIO2), so the bytes follow the table.  BSS is at $0800 and is not in the file.
        .export __K4SG__ : absolute = 1
        .import __PRG_START__, __PRG_LAST__, __HI_START__, __HI_LAST__
        .import __VIO1_START__, __VIO1_LAST__, __VIO2_START__, __VIO2_LAST__
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
        .dword $0EF00000                ; VIO1: far memory (DOSVI_P1)
        .dword __VIO1_LAST__ - __VIO1_START__
        .byte $FF, 0, 0, 0              ; banked by the gate when called, not here
        .dword $0EF10000                ; VIO2 (DOSVI_P2)
        .dword __VIO2_LAST__ - __VIO2_START__
        .byte $FF, 0, 0, 0
