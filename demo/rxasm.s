; RX's one piece of assembly: cc65's C stack pointer, so the interpreter can
; refuse a recursion that would otherwise grow down into its own buffers
; (which is what silently corrupted the variable pool, 2026-09-07).
        .importzp sp
        .export _rx_sp
; unsigned rx_sp(void)
_rx_sp: lda sp
        ldx sp+1
        rts
