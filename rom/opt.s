; rom/opt.s -- POSIX-style options for K/OS's shell words (Doc, 2026-10-07).
; In 6502 because cc65 made a kilobyte of the C, and the resident ROM had a
; little under that.  Resident (CODE): a word in any bank calls it.
;
;   -x   -xyz (a bundle)   -s 2, -s2 (a value)   --word   --word=value, --word value
;   --   the options end
;
; C side (rom/kernal.c):
;   extern const char *opt_p;     the line, moved past each option
;   extern const char *opt_l;     the word's options: each a letter, a ':' if it
;                                 takes a value, its long name, a NUL; an empty
;                                 one last -- "llist\0s:scale\0"
;   extern const char *opt_val;   a value in the option's own word, else 0
;   unsigned char opt_next(void); the letter (lower case), '?' for a long name
;                                 not in the list, 0 at the first word that is
;                                 not an option (opt_p there) or after "--"
        .export _opt_next, _opt_p, _opt_l, _opt_val
        .importzp ptr1, ptr2, tmp1

        .bss
_opt_p:   .res 2
_opt_l:   .res 2
_opt_val: .res 2
bnd:      .res 2                        ; a bundle's next letter; high byte 0: none

        .code
_opt_next:
        lda #0
        sta _opt_val
        sta _opt_val+1
        lda bnd+1
        beq @word
        sta ptr1+1                      ; -xyz: the next letter of the bundle
        lda bnd
        sta ptr1
        lda #0
        sta bnd+1
        ldy #0
        lda (ptr1),y
        ora #$20
        sta tmp1
        iny
        jsr fold                        ; ptr1 past the letter
        jsr takes
        bcc @flag
        jsr inword                      ; a value: the rest of the word, if any
        bcc @ret
        jsr setval
        bra @ret
@flag:  jsr inword                      ; more letters: the bundle goes on
        bcc @ret
        lda ptr1
        sta bnd
        lda ptr1+1
        sta bnd+1
@ret:   lda tmp1
        ldx #0
        rts

@word:  lda _opt_p
        sta ptr1
        lda _opt_p+1
        sta ptr1+1
        ldy #0
@sp:    lda (ptr1),y                    ; past the spaces
        cmp #' '
        bne @at
        iny
        bne @sp
@at:    jsr fold
        jsr savep
        cmp #'-'
        bne @none
        ldy #1
        lda (ptr1),y
        cmp #'!'
        bcc @none                       ; "-" alone is an operand
        cmp #'-'
        beq @long
        ora #$20                        ; -x
        sta tmp1
        iny
        jsr fold                        ; ptr1 at what follows the letter
        jsr inword
        bcc @endw                       ; -x alone
        jsr takes
        bcc @bund
        jsr setval                      ; -s2
        bra @endw
@bund:  lda ptr1                        ; -xyz
        sta bnd
        lda ptr1+1
        sta bnd+1
@endw:  jsr skipw
        bra @ret
@none:  lda #0
        tax
        rts

@long:  ldy #2                          ; --
        jsr fold
        jsr inword
        bcs @lname
        jsr savep                       ; "--": the options end, the operands start here
        bra @none
@lname: lda _opt_l
        sta ptr2
        lda _opt_l+1
        sta ptr2+1
@entry: ldy #0
        lda (ptr2),y                    ; the entry's letter; 0: the list's end
        beq @unk
        sta tmp1
        iny
        lda (ptr2),y
        cmp #':'
        bne @nm
        iny                             ; the name after the ':'
@nm:    tya                             ; ptr2 to the entry's name; the argument is at ptr1
        clc
        adc ptr2
        sta ptr2
        bcc :+
        inc ptr2+1
:       ldy #0
@cmp:   lda (ptr2),y                    ; the name's Y-th letter
        beq @named
        eor (ptr1),y
        and #$DF                        ; caseless (names are letters)
        bne @next
        iny
        bne @cmp
@named: lda (ptr1),y                    ; the argument must end here too: a space, NUL or '='
        cmp #'!'
        bcc @hit
        cmp #'='
        bne @next
        iny                             ; --word=value
        jsr fold
        jsr setval
@hit:   jsr skipw
        jmp @ret
@next:  ldy #0                          ; past this entry's NUL
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
        bra @entry
@unk:   lda #'?'
        sta tmp1
        jsr skipw
        jmp @ret

fold:   tya                             ; ptr1 += Y, Y = 0
        clc
        adc ptr1
        sta ptr1
        bcc :+
        inc ptr1+1
:       ldy #0
        lda (ptr1),y
        rts
savep:  pha                             ; opt_p = ptr1
        lda ptr1
        sta _opt_p
        lda ptr1+1
        sta _opt_p+1
        pla
        rts
setval: lda ptr1                        ; opt_val = ptr1
        sta _opt_val
        lda ptr1+1
        sta _opt_val+1
        rts
inword: ldy #0                          ; C set if ptr1 is inside a word (a character above space)
        lda (ptr1),y
        cmp #'!'
        rts
skipw:  ldy #0                          ; opt_p = the end of ptr1's word
:       lda (ptr1),y
        cmp #'!'
        bcc :+
        iny
        bne :-
:       jsr fold
        jmp savep
takes:  lda _opt_l                      ; C set if tmp1's letter takes a value (a ':' after it)
        sta ptr2
        lda _opt_l+1
        sta ptr2+1
@t:     ldy #0
        lda (ptr2),y
        beq @no
        cmp tmp1
        bne @s
        iny
        lda (ptr2),y
        cmp #':'
        beq @yes
@no:    clc
        rts
@yes:   sec
        rts
@s:     lda (ptr2),y                    ; to the next entry
        iny
        cmp #0
        bne @s
        tya
        clc
        adc ptr2
        sta ptr2
        bcc @t
        inc ptr2+1
        bra @t
