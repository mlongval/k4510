; demo/opt.s -- POSIX-style options for the machine's programs (2026-10-07),
; as K/OS's shell words take them (rom/kernal.c, opt_get).  demo/opt.h has
; the contract; this is it in 6502, because cc65 made 550 bytes of the C and
; EDIT and PROG had not a hundred to spare.
;
;   char __fastcall__ opt(const char *longs);
;
; _opt_s: the argument string (NUL-terminated, under 256 bytes); _opt_i:
; where the next option starts, moved past each.  Returns the option's
; letter (lower case), '?' for a long name not in LONGS, 0 at the first word
; that is not an option or after "--".  LONGS: entries of a letter and a
; name, each NUL-ended, an empty one last: "ssystem\0vvi\0".
        .export _opt, _opt_s, _opt_i
        .importzp ptr1, ptr2, tmp1, tmp2, tmp3

        .bss
_opt_s: .res 2
_opt_i: .res 1
opt_b:  .res 1                          ; inside a bundle (-sv): its next letter, 0 none

        .code
_opt:   sta ptr2                        ; longs
        stx ptr2+1
        lda _opt_s
        sta ptr1
        lda _opt_s+1
        sta ptr1+1
        ldy opt_b                       ; a bundle's next letter
        beq @word
        lda (ptr1),y
        cmp #'!'
        bcc @nob
        iny
        sty opt_b
        ora #$20
        ldx #0
        rts
@nob:   lda #0
        sta opt_b
@word:  ldy _opt_i
@sp:    lda (ptr1),y                    ; past the spaces
        cmp #' '
        bne @at
        iny
        bne @sp
@at:    sty _opt_i
        cmp #'-'
        bne @none
        iny
        lda (ptr1),y
        cmp #'!'
        bcc @none                       ; "-" alone is an operand
        cmp #'-'
        beq @long
        ora #$20                        ; -x: the letter; the rest of the word a bundle
        sta tmp1
        iny
        sty opt_b
        jsr skipw
        lda tmp1
        ldx #0
        rts
@none:  lda #0
        tax
        rts
@long:  iny                             ; --
        sty _opt_i
        lda (ptr1),y
        cmp #'!'
        bcc @none                       ; "--": the options end
@entry: ldy #0
        lda (ptr2),y                    ; this entry's letter; 0: the list's end
        beq @unk
        sta tmp1
        lda _opt_i
        sta tmp2                        ; where in the argument
        lda #1
        sta tmp3                        ; where in the entry
@cmp:   ldy tmp3
        lda (ptr2),y
        beq @named                      ; the name is whole
        ldy tmp2
        eor (ptr1),y
        and #$DF                        ; caseless (names are letters)
        bne @next
        inc tmp2
        inc tmp3
        bne @cmp
@named: ldy tmp2
        lda (ptr1),y
        cmp #'!'
        bcs @next                       ; the argument goes on: not this one
        sty _opt_i
        lda tmp1
        ldx #0
        rts
@next:  ldy #1                          ; past this entry's NUL
@skip:  lda (ptr2),y
        iny
        cmp #0
        bne @skip
        tya
        clc
        adc ptr2
        sta ptr2
        bcc @entry
        inc ptr2+1
        bne @entry
@unk:   ldy _opt_i
        jsr skipw
        lda #'?'
        ldx #0
        rts

skipw:  lda (ptr1),y                    ; Y to the end of the word; that is where the next option starts
        cmp #'!'
        bcc @end
        iny
        bne skipw
@end:   sty _opt_i
        rts
