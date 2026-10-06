; K4510 system ROM startup for cc65. 65C02 code: a strict subset of the 45GS02.
        .export   _exit, __STARTUP__ : absolute = 1
        .import   _main, __DATA_LOAD__, __DATA_RUN__, __DATA_SIZE__, __BSS_RUN__, __BSS_SIZE__
        .import   __RAM_START__, __RAM_SIZE__, __STACKSIZE__, __STK_START__, __STK_SIZE__
        .import   copydata, zerobss, initlib
        .importzp sp, sreg
        .import   incsp4
        .import   _k_chrout, _k_chrin, _k_getin, _k_load, _k_save, _k_shell, _k_video, _k_args
        .export   _ticks, _speed_loop, _far_poke, _far_peek, _call_prog
        .export   _rtc_latch

        .zeropage
cnt:          .res 2
fp:           .res 4           ; far pointer for the flat forms

        .bss
_ticks:       .res 1
t0:           .res 1

; K/OS's own base page and stacks (2026-10-06).  The ROM runs on base page
; KOS_BP -- the 45GS10's B register relocates every zero-page access -- and
; its 6502 stack on page KOS_SP, with its C stack in its workspace in the I/O
; page ($DB00-$DEFF, core/mem.h).  A program keeps base page
; $00 and stack page 1 to itself: the system-call stub switches both, where
; it used to copy the ROM's 32 bytes of zero page in and out (zp_in/zp_out,
; ~1400 cycles a call).  Only LOAD, SAVE and ARGS carry arguments in the
; caller's $F0-$F9; those three copy them through ARGS (args_in/args_out).
KOS_BP  = $06                   ; the ROM's base page (k4510.cfg: ZP is linked at $02-$2F)
KOS_SP  = $07                   ; the ROM's 6502 stack page
ARGS    = $0630                 ; the caller's $F0-$F9, while a call runs (kernal.c P_NAME...)
        .export ARGS

        .segment "STARTUP"
reset:  sei
        cld
        lda #KOS_BP
        .byte $5B               ; TAB: K/OS's base page
        ldy #KOS_SP
        .byte $2B               ; TYS: K/OS's stack page
        ldx #$FF
        txs
        lda #<(__STK_START__ + __STK_SIZE__)      ; cc65 software stack: the workspace's top, down
        sta sp
        lda #>(__STK_START__ + __STK_SIZE__)
        sta sp+1
        lda #KOS_BP             ; the stack fence (core/mem.h): the pointer is in base page KOS_BP,
        sta $D551
        lda #<__STK_START__     ; its stack may not reach below __STK_START__
        sta $D552
        lda #>__STK_START__
        sta $D553
        lda #<sp                ; and the pointer's address, which arms it
        sta $D550
        jsr copydata
        jsr zerobss
        lda #$FF                ; the programs' stack: page 1, empty (after zerobss: it is BSS)
        sta prog_sl
        lda #1
        sta prog_sh
        jsr initlib
        cli
        jsr _main
_exit:  jmp _exit

; unsigned speed_loop(void): iterations of a fixed loop during one frame.
; 18 cycles per iteration on the 40.5 MHz timing table (see INFO -c).  Kept
; ahead of the IRQ and the clock painter so later additions there cannot
; shift it across a page boundary -- the taken branch below costs a cycle
; more when it does, and that cycle, times ~37000 iterations, moves the
; measured MHz by ~5%.
_speed_loop:
        lda _ticks
@w:     cmp _ticks              ; wait for a tick edge
        beq @w
        lda _ticks
        sta t0
        stz cnt
        stz cnt+1
@l:     inc cnt
        bne @s
        inc cnt+1
@s:     lda t0
        cmp _ticks
        beq @l
        lda cnt
        ldx cnt+1
        rts

; IRQ: pure assembly -- cc65 C code must never run here (it would clobber
; the zero-page temporaries of whatever was interrupted).
; All it does now is count frames: the status-bar clock was painted here
; until 2026-10-05 (the bands are JIM's, drawn by the emulator, core/term.c),
; and the console cursor's blink until 2026-10-01 (the cursor is JIM's too).
irq:    pha
        .byte $DB               ; PHZ
        lda $D004               ; VICKY IRQSTAT
        pha
        and #1                  ; vblank?
        beq @ack
        inc _ticks
@ack:   pla
        sta $D004               ; acknowledge what we saw
        .byte $FB               ; PLZ
        pla
        rts                     ; back to the stub (s_irq), which banks the ROM out again and RTIs

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
        sta (fp)                ; STA [fp],Z
        jmp incsp4

; unsigned char __fastcall__ far_peek(unsigned long a)
; The counterpart of far_poke, which the ROM went without until 2026-09-02.
; peek() in kernal.c reads far memory through a one-byte DMA -- right for a
; monitor dump, far too heavy for anything in a poll loop.  This is the flat
; load the 45GS10 already has, about ten cycles.
;
; With ONE argument and __fastcall__, cc65 passes the whole long in registers:
; A and X the low half, sreg and sreg+1 the high.  Nothing on the C stack.
; (far_poke reads its address off the stack because there the long is not the
; last argument -- the char is.)
_far_peek:
        sta fp
        stx fp+1
        lda sreg
        sta fp+2
        lda sreg+1
        sta fp+3
        .byte $EA               ; NOP prefix: 32-bit flat
        lda (fp)                ; LDA [fp],Z
        ldx #0
        rts

;; void __fastcall__ call_prog(unsigned addr): run a program.  It gets base
; page $00 and the programs' stack: under the frames of the program whose
; system call this is, or page 1 empty.  Its own system calls come back to
; the ROM's stack where this leaves it (rom_sl/sh); nested, the outer
; call_prog's place is kept on the ROM's stack.
_call_prog:
        sta prog_addr
        stx prog_addr+1
        php
        lda rom_sl              ; an outer call_prog's place, for after
        pha
        lda rom_sh
        pha
        sei                     ; no interrupt between the two halves of a stack switch
        tsx
        stx rom_sl
        .byte $0B               ; TSY
        sty rom_sh
        ldx prog_sl
        ldy prog_sh
        .byte $2B               ; TYS
        txs                     ; the programs' stack
        lda #0
        .byte $5B               ; TAB: base page $00
        cli
        jsr go_prog
        sei
        ldx rom_sl
        ldy rom_sh
        .byte $2B               ; TYS
        txs                     ; the ROM's stack, where it was
        lda #KOS_BP
        .byte $5B               ; TAB
        pla
        sta rom_sh
        pla
        sta rom_sl
        plp
        rts
go_prog: jmp (prog_addr)

; kos_enter: the second half of rom_push, once the ROM is banked in.  On the
; caller's stack: [its return][12 bank bytes]; rom_push has its own return
; (into the s_ entry) in stub_r and A/X in stub_a/stub_x.  Pushes the
; caller's base page there too, switches to the ROM's stack and base page,
; and keeps on the ROM's stack the programs' stack of any call this one is
; nested in.
kos_enter:
        php
        pla
        sta stub_p              ; the caller's flags: its interrupt mask, for the call
        sei
        .byte $7B               ; TBA: the caller's base page
        pha                     ; ...on its own stack, under the banks
        sta stub_b
        tsx
        stx stub_sl
        .byte $0B               ; TSY
        sty stub_sh
        ldx rom_sl
        ldy rom_sh
        .byte $2B               ; TYS
        txs                     ; the ROM's stack
        lda #KOS_BP
        .byte $5B               ; TAB: the ROM's base page
        lda prog_sl             ; the call this one nests in: its stack and base page
        pha
        lda prog_sh
        pha
        lda prog_b
        pha
        lda stub_sl             ; this caller's, for a program the call runs (call_prog)
        sta prog_sl
        lda stub_sh
        sta prog_sh
        lda stub_b
        sta prog_b
        lda stub_r+1            ; back into the s_ entry, on this stack
        pha
        lda stub_r
        pha
        lda stub_p
        pha
        lda stub_a
        ldx stub_x
        plp
        rts
; kos_leave: from rom_pop, A/X in stub_a/stub_x.  The caller's stack and base
; page back, the nesting call's restored, then rom_pop2 (the stub page) takes
; the banks back off the caller's stack.
kos_leave:
        php
        pla
        sta stub_p
        sei
        ldx prog_sl             ; this caller's stack
        ldy prog_sh
        stx stub_sl
        sty stub_sh
        pla
        sta prog_b              ; the nesting call's, back
        pla
        sta prog_sh
        pla
        sta prog_sl
        ldx stub_sl
        ldy stub_sh
        .byte $2B               ; TYS
        txs                     ; the caller's stack
        pla
        .byte $5B               ; TAB: its base page
        lda stub_p
        pha
        plp
        jmp rom_pop2

; the caller's $F0-$F9 through ARGS, for the three calls that use them
args_in:
        pha
        lda prog_b
        .byte $5B               ; TAB: the caller's base page
        ldx #9
@i:     lda $F0,x
        sta ARGS,x
        dex
        bpl @i
        lda #KOS_BP
        .byte $5B
        pla
        rts
args_out:
        pha
        lda prog_b
        .byte $5B
        ldx #9
@o:     lda ARGS,x
        sta $F0,x
        dex
        bpl @o
        lda #KOS_BP
        .byte $5B
        pla
        ldx #0                  ; X = 0: a C program that calls a byte-returning entry
        rts                     ; through a cast tests A|X (every VI save read as failed, 2026-09-14)

        .bss
prog_addr: .res 2
rom_sl:    .res 1               ; the ROM's stack where call_prog left it: a program's calls come back here
rom_sh:    .res 1
prog_sl:   .res 1               ; the programs' stack: the caller's of the call running now, or page 1 empty
prog_sh:   .res 1
prog_b:    .res 1               ; the caller's base page
stub_p:    .res 1
stub_b:    .res 1
stub_sl:   .res 1
stub_sh:   .res 1
        .segment "CODE"
w_chrout: jmp _k_chrout
w_chrin:  jsr _k_chrin
        ldx #0
        rts
w_getin:  jsr _k_getin
        ldx #0
        rts
w_load:   jsr args_in
        jsr _k_load
        jmp args_out
w_save:   jsr args_in
        jsr _k_save
        jmp args_out
w_shell:  jsr _k_shell          ; A/X = pointer to a NUL-terminated command line
        ldx #0
        rts
w_video:  jmp _k_video
w_args:   jsr _k_args
        jmp args_out

; Latch the RTC: a read of $D504 copies the host's clock into $D505-$D50C.  In
; assembler because cc65 drops a read whose value goes unused -- `(void)REG()`
; and an inline `lda` alike -- so in C the latch compiled to nothing (2026-10-01).
_rtc_latch: lda $D504
        rts

; ---- the stub page $FF00-$FFFF: always the ROM, whatever is banked (K-05) ----
; A program may bank blocks 5-7 ($A000-$CFFF, $E000-$FEFF; the I/O page stays)
; onto the RAM under the ROM (far.h: rom_out()). Every system call and interrupt passes
; through here: the stub saves bank registers 5-7 ($D614-$D61F, 12 bytes) on
; the stack, banks the ROM in, calls, restores. Stack-based, so calls nest;
; the IRQ path uses no temporaries and no base page, so it may land anywhere
; in a call.  The base page and the stack are switched too (kos_enter,
; kos_leave): ~190 cycles a call, where copying the zero page cost ~1500.
        .segment "STUB"
s_chrout: jsr rom_push
        jsr w_chrout
        jmp rom_pop
s_chrin:  jsr rom_push
        jsr w_chrin
        jmp rom_pop
s_getin:  jsr rom_push
        jsr w_getin
        jmp rom_pop
s_load:   jsr rom_push
        jsr w_load
        jmp rom_pop
s_save:   jsr rom_push
        jsr w_save
        jmp rom_pop
s_shell:  jsr rom_push
        jsr w_shell
        jmp rom_pop
s_video:  jsr rom_push
        jsr w_video
        jmp rom_pop
s_args:   jsr rom_push
        jsr w_args
        jmp rom_pop
s_irq:  pha
        phx
        ldx #11
@i:     lda $D614,x             ; banks 5-7 onto the stack
        pha
        dex
        bpl @i
        lda #$FF
        sta $D617               ; ROM in (blocks 5, 6, 7)
        sta $D61B
        sta $D61F
        jsr irq
        ldx #0
@o:     pla                     ; and back, byte 3 of each register last
        sta $D614,x
        inx
        cpx #12
        bne @o
        plx
        pla
        rti
s_nmi:  rti
s_reset: ldx #28                ; a reset clears every bank (F12 does not reset the MMU), then the ROM boots
        lda #$FF
@c:     sta $D603,x             ; byte 3 of bank registers 7..0
        dex
        dex
        dex
        dex
        bpl @c
        jmp reset

; ---- jump table at $FF80: the system call interface ----
        .segment "JUMPTAB"
        jmp s_chrout            ; $FF80  CHROUT  A = char
        jmp s_chrin             ; $FF83  CHRIN   -> A, blocks
        jmp s_getin             ; $FF86  GETIN   -> A, 0 if none
        jmp s_load              ; $FF89  LOAD    name ptr in $F0/$F1, dest in $F2..$F5 -> A status, size in $F6..$F9
        jmp s_save              ; $FF8C  SAVE    name ptr $F0/$F1, src $F2..$F5, len $F6..$F9 -> A status
        jmp s_shell             ; $FF8F  SHELL   A/X = pointer to a command line; runs it as if typed
                                ;        (a command that runs a program from inside a program is
                                ;        swapped: the shell saves the caller whole, kernal.c swap_run)
        jmp s_video             ; $FF92  VIDEO   restore the ROM's video mode and palette (after a program drew)
        jmp s_args              ; $FF95  ARGS    $F0/$F1 = the command tail the shell saved, A = its length

        .segment "STUB2"
; rom_push: called by JSR from an s_ entry. Moves its own return address
; aside, pushes the 12 bank bytes, banks the ROM in, returns with A and X
; intact. The stack then holds [program's return][12 bytes].
; rom_pop: jumped to after the call, A/X carrying the result; pulls the 12
; bytes back (byte 3 of each register last) and returns to the program.
; The temporaries are safe: only system calls use them, and a call inside a
; call (SHELL -> RUN) is past its own rom_push before it can start another.
rom_push: sta stub_a
        stx stub_x
        pla
        sta stub_r
        pla
        sta stub_r+1
        ldx #11
@p:     lda $D614,x
        pha
        dex
        bpl @p
        lda #$FF
        sta $D617
        sta $D61B
        sta $D61F
        jmp kos_enter           ; the ROM is in: the rest is there (CODE)
rom_pop: sta stub_a
        stx stub_x
        jmp kos_leave
rom_pop2: ldx #0
@q:     pla
        sta $D614,x
        inx
        cpx #12
        bne @q
        lda stub_a
        ldx stub_x
        rts
        .bss
stub_a:  .res 1
stub_x:  .res 1
stub_r:  .res 2

        .segment "VECTORS"
        .word s_nmi
        .word s_reset
        .word s_irq
