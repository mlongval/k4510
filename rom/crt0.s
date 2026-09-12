; K4510 system ROM startup for cc65. 65C02 code: a strict subset of the 45GS02.
        .export   _exit, __STARTUP__ : absolute = 1
        .import   _main, __DATA_LOAD__, __DATA_RUN__, __DATA_SIZE__, __BSS_RUN__, __BSS_SIZE__
        .import   __RAM_START__, __RAM_SIZE__, __STACKSIZE__, __STK_START__, __STK_SIZE__
        .import   copydata, zerobss, initlib
        .importzp sp, sreg
        .import   incsp4
        .import   _k_chrout, _k_chrin, _k_getin, _k_load, _k_save, _k_shell, _k_video, _k_args
        .import   _bband                    ; bottom-band height
        .import   _OY                       ; top-band height: the clock lives there, so this is what
                                            ; decides whether the IRQ paints it (a bottom band of zero
                                            ; must not stop the clock -- the heights are independent)
        .export   _ticks, _cursor_far, _cursor_vis, _speed_loop, _far_poke, _far_peek, _call_prog

        .zeropage
cnt:          .res 2
fp:           .res 4           ; far pointer for the flat forms

        .bss
_ticks:       .res 1
_cursor_vis:  .res 1           ; nonzero while the ROM wants a cursor shown
_cursor_far:  .res 4           ; far address of the attribute byte under the cursor
t0:           .res 1
zp_rom:       .res 32          ; the ROM's zero page $02-$21 while a program runs
zp_tmp:       .res 32
zp_save:      .res 6           ; IRQ scratch
irq_min:      .res 1           ; the minute the IRQ last painted into the status clock

        .segment "STARTUP"
reset:  sei
        cld
        ldx #$FF
        txs
        lda #<(__STK_START__ + __STK_SIZE__)      ; cc65 software stack: $0800 down
        sta sp
        lda #>(__STK_START__ + __STK_SIZE__)
        sta sp+1
        jsr copydata
        jsr zerobss
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
; The cursor cell is in far memory; the IRQ borrows $02-$05 for the flat
; pointer and restores them, so it is safe whatever program owns the zero page.
irq:    pha
        .byte $DB               ; PHZ
        .byte $A3, $00          ; LDZ #0: the flat [$02],Z ops below assume it, and a program
                                ; interrupted inside far_poke16 (Z=1) or map_window (Z=$0F) does not
        lda $D004               ; VICKY IRQSTAT
        pha
        and #1                  ; vblank?
        beq @ack
        inc _ticks
        lda _ticks
        and #31
        bne @ack
        ; --- the status-bar clock, the machine's own tick: when the minute
        ; rolls, repaint the eight digit cells (in status mode only) ---
        lda $DA0E               ; has a PROGRAM claimed the bands?  Then they are not ours to paint,
        and #$08                ; and a clock ticking through somebody else's status line is the
        bne @curs               ; loudest possible way to get this wrong
        lda $D521               ; the host's own switch: are the bands up at all?
        and #$08
        beq @curs
        lda _OY                 ; ... and is there a top band to put a clock in?
        beq @curs
        lda $D504               ; latch the RTC
        lda $D506               ; the minute
        cmp irq_min
        beq @curs
        sta irq_min
        phy                     ; the stub saved A and X for us; Y is ours to keep
        jsr clk_paint
        ply
@curs:  lda _cursor_vis
        beq @ack
        phx
        ldx #3
@sv:    lda $02,x
        sta zp_save,x
        lda _cursor_far,x
        sta $02,x
        dex
        bpl @sv
        .byte $EA               ; NOP prefix: 32-bit flat
        lda ($02)               ; LDA [$02],Z   (Z = 0)
        eor #$80
        .byte $EA
        sta ($02)
        ldx #3
@rs:    lda zp_save,x
        sta $02,x
        dex
        bpl @rs
        plx
@ack:   pla
        sta $D004               ; acknowledge what we saw
        .byte $FB               ; PLZ
        pla
        rts                     ; back to the stub (s_irq), which banks the ROM out again and RTIs

; The status clock's painter, run from the IRQ.  It rewrites the eight digit
; cells of HH:MM DD.MM straight into the text map at $030100 (row 0, column
; 64 = SCREEN + 64*4; status mode is always 80 columns).  The separators and
; the year are the C code's (draw_clock); only the digits change each minute.
; Borrows $02-$05 in zp_save, the way the cursor blink borrows them.  A and X
; are the stub's to restore; the caller kept Y.
clk_paint:
        cld                     ; the sbc below must be binary, whatever ran before
        ldx #3
@sv:    lda $02,x
        sta zp_save,x
        dex
        bpl @sv
        ; The field is right-anchored and its width depends on the format: 16
        ; cells at 24-hour, 19 with the AM/PM.  Status mode is always 80
        ; columns, so the row-0 byte offset is 64*4 = $0100 or 61*4 = $00F4.
        lda $D52F
        and #$01                ; bit 0: 24-hour
        beq @h12
        lda #$00                ; 24-hour: start at column 64
        bra @addr
@h12:   lda #$F4                ; 12-hour: three cells further left
@addr:  sta $02
        lda #$01
        sta $03
        lda #$03
        sta $04
        lda #$00
        sta $05
        lda $D507               ; hours, as the RTC has them
        ldx $D52F
        cpx #$00                ; (only bit 0 matters; the branch below reads it)
        pha
        lda $D52F
        and #$01
        bne @h24
        pla                     ; 12-hour: 0 and 12 both read 12
        cmp #12
        bcc @lt
        sec
        sbc #12
@lt:    cmp #0
        bne @go
        lda #12
        bra @go
@h24:   pla
@go:    jsr cp_field
        lda #':'
        jsr cp_put
        lda $D506               ; minutes
        jsr cp_field
        ; The date is NOT painted here.  It changes once a day, and teaching
        ; the interrupt three date orders would be a lot of assembler guarding
        ; that.  k_getin repaints it instead, off a far_peek of the day cell.
        ldx #3
@rs:    lda zp_save,x
        sta $02,x
        dex
        bpl @rs
        rts

; A = 0..99 -> two cells: the tens digit, then (falling through) the ones.
cp_field:
        ldy #'0'
@t:     cmp #10
        bcc @d
        sbc #10                 ; the cmp set carry when A >= 10
        iny
        bra @t
@d:     ora #'0'                ; the ones digit, 0..9, to ASCII
        pha
        tya
        jsr cp_put              ; the tens
        pla                     ; the ones, then fall through
; cp_put: write A (a glyph) to the cell at [$02], advance $02-$05 by four.
cp_put:
        .byte $EA               ; NOP prefix: 32-bit flat
        sta ($02)               ; STA [$02],Z   (Z = 0, as the blink assumes)
        clc
        lda $02
        adc #4
        sta $02
        bcc @d
        inc $03
        bne @d
        inc $04
        bne @d
        inc $05
@d:     rts

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

; void __fastcall__ call_prog(unsigned addr): run a program that may own the
; whole zero page. The ROM's $02-$1F is kept in zp_rom and swapped back in
; around every jump-table call and on return.
_call_prog:
        sta prog_addr
        stx prog_addr+1
        ldx #31
@s:     lda $02,x
        sta zp_rom,x
        dex
        bpl @s
        jsr go_prog
        ldx #31
@r:     lda zp_rom,x
        sta $02,x
        dex
        bpl @r
        rts
go_prog: jmp (prog_addr)

; a jump-table call from a program: program zp -> zp_tmp, ROM zp in, call,
; ROM zp -> zp_rom, program zp back. A and X carry the argument / result.
zp_in:  sta zp_a
        ldx #31
@a:     lda $02,x
        sta zp_tmp,x
        lda zp_rom,x
        sta $02,x
        dex
        bpl @a
        lda zp_a
        rts
zp_out: sta zp_a
        ldx #31
@b:     lda $02,x
        sta zp_rom,x
        lda zp_tmp,x
        sta $02,x
        dex
        bpl @b
        lda zp_a
        rts
        .bss
zp_a:   .res 1
prog_addr: .res 2
        .segment "CODE"
w_chrout: jsr zp_in
        jsr _k_chrout
        jmp zp_out
w_chrin:  jsr zp_in
        jsr _k_chrin
        jmp zp_out
w_getin:  jsr zp_in
        jsr _k_getin
        jmp zp_out
w_load:   jsr zp_in
        jsr _k_load
        jmp zp_out
w_save:   jsr zp_in
        jsr _k_save
        jmp zp_out
w_shell:  phx                   ; A/X = pointer to a NUL-terminated command line (zp_in uses X)
        jsr zp_in
        plx
        jsr _k_shell
        jmp zp_out
w_video:  jsr zp_in
        jsr _k_video
        jmp zp_out
w_args:   jsr zp_in
        jsr _k_args
        jmp zp_out

; ---- the stub page $FF00-$FFFF: always the ROM, whatever is banked (K-05) ----
; A program may bank blocks 5-7 ($A000-$CFFF, $E000-$FEFF; the I/O page stays)
; onto the RAM under the ROM (far.h: rom_out()). Every system call and interrupt passes
; through here: the stub saves bank registers 5-7 ($D614-$D61F, 12 bytes) on
; the stack, banks the ROM in, calls, restores. Stack-based, so calls nest;
; the IRQ path uses no temporaries, so it may land anywhere in a call.
; ~80 cycles per call.
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
                                ;        A command that RUNs a second program comes back with $02-$21
                                ;        as that program's system calls left them (zp_in/zp_out keep
                                ;        one save), so a caller that lives in $02-$21 must not SHELL a
                                ;        program -- use SWAP, as RANGER does (review 2026-09-05, 12).
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
        lda stub_r+1
        pha
        lda stub_r
        pha
        lda stub_a
        ldx stub_x
        rts
rom_pop: sta stub_a
        stx stub_x
        ldx #0
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
