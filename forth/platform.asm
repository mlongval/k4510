; K4510 platform file for Tali Forth 2 (public-domain Forth for the
; 65c02 by Scot W. Stevenson, maintained by Sam Colwell; vendored
; unmodified under tali/ -- see tali/COPYING.txt, tali/VENDORED-FROM.txt).
; The machine's third language, and unlike BBC BASIC it is native: pure
; 45GS10 code in bank 0, no Tube involved.
;
; Memory map while Forth runs (all of it inside USER RAM $0000-$9FFF):
;   $0000-$007F  Tali zero page incl. the data stack (X is the stack ptr);
;                the ROM's own zp is $02-$21 but the jump-table stubs swap
;                it in and out, so the whole page belongs to Forth
;   $0100-$01FF  return stack (the hardware stack). Tali resets the stack
;                pointer at COLD and uses the whole page, so kernel_init
;                saves the bytes above the entry SP (K/OS's call frames)
;                and BYE puts them back before returning
;   $0200-$07FF  NOT OURS: the ROM's data, bss and C stack live here
;   $0800-$08FF  input buffer
;   $0900-$87FF  dictionary RAM: ~31.7 KB for user words (stage 3 moved
;                the image high: programs own $0800-$CFFF now)
;   $8800-$8BFF  input history buffers (ctrl-p / ctrl-n)
;   $8C00-.....  this image (~17 KB), loaded by K/OS from /LANG/FORTH/FORTH.PRG
;
; Console I/O is the ROM jump table: CHROUT $FF80 and CHRIN $FF83 (both
; wrapped to preserve X/Y, which the stubs do not promise); KEY? peeks the
; keyboard status register $D101 directly, consuming nothing. BYE restores
; the stack pointer K/OS called us with and RTSes back to the shell.

        .cpu "65c02"            ; strict subset of the 45GS10 (Z stays 0)
        .enc "none"

TALI_ARCH := "k4510"

ram_start = $0000
ram_end   = $8C00-1             ; dictionary RAM ends where the image begins
buffer0   = $0800               ; input buffer: skip the ROM's RAM at $0200-$07FF
                                ; (cp0, the dictionary start, follows it at $0900)

; Everything except the block words ("block", "ramdrive", the block
; "editor") and the "ed" line editor -- there is no block device, and ed
; without disk persistence is a foot-gun. The interactive 65c02 assembler
; and DISASM stay: on this machine they are the whole point.
TALI_OPTIONAL_WORDS := [ "assembler", "disassembler", "wordlist", "environment?" ]
TALI_OPTION_CR_EOL := [ "lf" ]  ; the console newline is LF, as everywhere
TALI_OPTION_MAX_COLS := 80      ; 80x60 text mode
TALI_OPTION_HISTORY := 1
TALI_OPTION_TERSE := 0

; .prg header: K/OS loads the image at $8C00 and JSRs to kernel_init
        * = $8BFC
        .word $8C00             ; load address
        .word kernel_init       ; run address

; ---------------------------------------------------------------------
; Kernel routines (the whole porting surface)

kernel_init:
        .byte $a3, $00          ; LDZ #0: pin the 45GS10's Z register
        ldx #0                  ; the console cursor back on: the ROM hides it for programs (2026-09-11);
_cur:   lda cursor_on,x         ; told to JIM in its stream, ESC [ ? 2 5 h (2026-10-05)
        sta $DA00
        inx
        cpx #6
        bne _cur
        tsx
        stx run_sp              ; remember K/OS's stack pointer for BYE
        ldy #0                  ; save the stack bytes above the entry SP:
-       inx                     ; K/OS's call frames live there and Tali's
        beq _saved              ; return stack (COLD does TXS with $FF)
        lda $0100,x             ; would overwrite them
        sta stack_save,y
        iny
        bra -
_saved: ldx #0
-       lda s_kernel_id,x
        beq _done
        jsr kernel_putc
        inx
        bra -
_done:  jmp forth

cursor_on:                      ; ESC [ ? 2 5 h, for kernel_init (after a jmp: never run into)
        .byte 27
        .text "[?25h"
kernel_bye:
        ldx run_sp
        txs
        ldy #0                  ; put K/OS's stack frames back
-       inx
        beq _restored
        lda stack_save,y
        sta $0100,x
        iny
        bra -
_restored:
        lda #0                  ; MAP everything off, like a .prg exit
        tax
        tay
        .byte $a3, $00          ; LDZ #0
        .byte $5c               ; MAP
        .byte $ea               ; EOM
        rts                     ; back to the K/OS shell

; The break (2026-10-08, as RX and LOGO have it): an ESC or Ctrl-C waiting
; anywhere in the keyboard queue -- $D103 hands it over and takes it out --
; stops what is running, says so, and ABORTs to the prompt.  Looked for at
; every character Forth prints and every KEY?, so a loop that prints or polls
; the keys stops; a silent one (BEGIN AGAIN) still needs the reset chord.
kernel_putc:
        pha
        lda $d103               ; KBDBREAK
        bne forth_break
        pla
        phx
        phy
        jsr $ff80               ; CHROUT
        ply
        plx
        rts

forth_break:
        ldx #0
-       lda s_break,x
        beq +
        phx
        jsr $ff80
        plx
        inx
        bra -
+       jmp xt_abort            ; the stacks reset, back to the interpreter

s_break:
        .text AscLF, "stopped", AscLF, 0

kernel_getc:
        phx
        phy
        jsr $ff83               ; CHRIN: blocks, no echo
        ply
        plx
        rts

kernel_kbhit:
        lda $d103               ; the break first (above)
        bne forth_break
        lda $d101               ; KBDST: bit 7 = a key is waiting
        and #$80                ; (peeked, not consumed)
        rts

run_sp: .byte 0                 ; lives in the loaded image: plain RAM
stack_save: .fill 256

s_kernel_id:
        .text "Tali Forth 2 on the K4510 (native 45GS10)", AscLF, 0

; ---------------------------------------------------------------------
; Tali itself
.include "tali/taliforth.asm"
