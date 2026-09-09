; Microsoft BASIC for 6502 on the K4510, as a .prg the system ROM loads
; at $7000.  Microsoft released the original source in 2025 under MIT;
; mist64/msbasic is the buildable ca65 reconstruction of it, vendored
; unmodified under msbasic/ (see msbasic/VENDORED-FROM.txt).  This file
; is the whole K4510 port: the configuration, the console glue and the
; .prg header.  Nothing under msbasic/ is edited.
;
; The machine's second native BASIC, beside EhBASIC (basic/basic.asm).
; The two do not share a line: EhBASIC is an MS-alike with the K4510
; extension words (GRAPHICS, PLOT, SPRITE, ...); this is the real thing,
; 1977 vintage, and for now it is the plain interpreter.
;
; Configuration: CONFIG_2C -- the newest Microsoft base in the tree (the
; one MicroTAN forked from), 9-digit floating point, every bugfix up to
; 2C, and none of the OEM additions.  That is the pure-MS build the
; licence research called for (docs/BUILD-LOG.md, 2026-08-24): it rests
; on Microsoft's MIT release alone, with no Commodore or Apple
; reconstruction in it.
;
; Memory map while MS BASIC runs (all inside user RAM):
;   $0000-$00FF  the whole zero page is BASIC's.  The ROM's own zp is
;                $02-$21, but every jump-table stub swaps it in and out
;                (rom/crt0.s: zp_in / zp_out), so we may have all of it
;   $0100-$01FF  the hardware stack; COLD_START resets SP, so there is no
;                returning to the shell (see "Leaving", below)
;   $0200-$07FF  NOT OURS: the ROM's data, bss and C stack
;   $0800-$6FFF  BASIC's program and variable RAM -- 26 KB
;   $6FFC-$6FFF  the .prg header (load address, run address)
;   $7000-.....  this image, loaded by K:OS from /LANG/MSBASIC/MSBASIC.PRG
;
; $7000 is where EhBASIC's .prg is documented to land, and it is kept
; here so the two arrangements read the same.  It leaves the RAM above
; the image ($9000-$CFFF) unused: raising the load address is free
; program RAM whenever it is wanted, and costs one number in msbasic.cfg
; plus the matching MEMTOP below.
;
; Console: the ROM jump table -- CHROUT $FF80 and CHRIN $FF83, both
; wrapped to preserve X and Y, which the stubs do not promise.  Ctrl-C
; is the keyboard queue's own break flag at $D103, so a key typed while
; a program runs is not lost the way polling the queue would lose it.
;
; Star commands: a line beginning with "*" typed at the READY prompt is
; not BASIC's, it is K:OS's.  "*BYE" leaves; anything else is handed to
; the shell as if typed there, so *DIR, *TYPE, *CD and the rest all work
; from inside BASIC.  This is the BBC's arrangement and it is borrowed
; deliberately: 1977 Microsoft BASIC has no vendor words at all, and the
; alternative -- adding tokens -- means editing the vendored interpreter.
; A star command is caught in MONRDKEY, one level below BASIC, so not a
; byte of msbasic/ knows it happened.
;
; Leaving, in detail.  MS BASIC has no BYE and COLD_START resets the
; stack pointer before BASIC is up, so by the time anything of ours runs
; the shell's return frame is already gone.  So k4510_start saves it:
; the stack pointer and the live bytes above it are copied out before
; COLD_START, and *BYE puts them back and returns normally, through the
; ROM's trampoline, which restores the ROM's zero page on the way (see
; _call_prog in rom/crt0.s).  The shell comes back to its own prompt with
; its screen and its working directory intact -- which is better than
; EhBASIC's @BYE, that reaches the shell by cold-starting the machine.
; If the frame is deeper than the buffer, *BYE says so and stays put
; rather than returning to a stack it only half remembers.

; ---- the K4510 configuration ---------------------------------------------
; (this is what defines_<machine>.s is for the OEM builds; ours lives out
; here so that nothing under msbasic/ has to be touched)

CONFIG_2C               := 1     ; the newest MS base: all bugfixes, 9-digit FP

CONFIG_PEEK_SAVE_LINNUM := 1     ; PEEK does not clobber LINNUM
CONFIG_SAFE_NAMENOTFOUND := 1    ; check both bytes of the caller's address
CONFIG_SCRTCH_ORDER     := 1     ; where in init SCRTCH is called

; Deliberately NOT set:
;   CONFIG_ROR_WORKAROUND  -- the workaround is for the broken ROR of the
;                             1975/76 6502s.  The 45GS02 has a working one.
;   CONFIG_PRINT_CR        -- BASIC would emit a CR on reaching the last
;                             column, but k_chrout already wraps at COLS
;                             (rom/kernal.c), so that would double-space
;                             every full line.  BASIC still counts the
;                             column, which is what TAB and comma need.
;   CONFIG_MONCOUT_DESTROYS_Y -- our MONCOUT preserves Y itself, which is
;                             cheaper than making BASIC save it at every
;                             call site.
;   CONFIG_CBM_ALL, CONFIG_FILE, CONFIG_CBM1_PATCHES -- Commodore
;                             additions.  Out of scope and out of licence.

; zero page (the CONFIG_2C layout)
ZP_START1 = $17
ZP_START2 = $2F
ZP_START3 = $24
ZP_START4 = $85

; extra zero page variables the non-OEM build still expects
USR             := $0021         ; the USR() vector: JMP <addr>
TXPSV           := $00BA

; constants
STACK_TOP       := $FE
SPACE_FOR_GOSUB := $3E
NULL_MAX        := $F0
WIDTH           := 80            ; the K4510 console is 80x60
WIDTH2          := 56            ; last column a comma tab may start in

; memory layout
RAMSTART2       := $0800         ; BASIC's program RAM starts above the ROM's
MEMTOP          := $7000         ; ... and ends where this image begins

; the monitor entry points BASIC calls; defined in the glue below
MONRDKEY        := k4510_in
MONCOUT         := k4510_out

; ---- the ROM jump table ---------------------------------------------------
ROM_CHROUT      = $FF80
ROM_CHRIN       = $FF83          ; blocks until a key
ROM_SHELL       = $FF8F          ; A/X = a NUL-terminated command line; runs it as if typed
K4510_STKMAX    = 96             ; how much of the shell's stack frame we can keep
K4510_LINEMAX   = 72             ; the longest star command line
KBD_BREAK       = $D103          ; Ctrl-C / RUN-STOP seen anywhere in the queue
                                 ; (reading takes it; other keys stay for GET)
K_CR            = $0D
K_LF            = $0A

; ---- the .prg header ------------------------------------------------------
; K:OS loads the image and JSRs to the run address.
        .segment "PRGHDR"
        .word   $7000            ; load address
        .word   k4510_start      ; run address

; ---- Microsoft BASIC ------------------------------------------------------
        .include "msbasic.s"

; ---- the K4510 glue -------------------------------------------------------
        .segment "CODE"

k4510_start:
        cld
; ---- the way back ----------------------------------------------------------
; Copy the live hardware stack out before COLD_START overwrites it.  The
; bytes from $0101+S to $01FF are the shell's frames -- the JSR in the RAM
; trampoline at $02D8, _call_prog's JSR, and the ROM C code above them.
        tsx
        stx     k4510_sp
        txa
        eor     #$FF                     ; count = $FF - S
        sta     k4510_stkn
        cmp     #K4510_STKMAX + 1
        bcc     @stk_ok
        lda     #0                       ; too deep: no way back, and *BYE says so
        sta     k4510_stkn
@stk_ok:
        ldy     #0
@stk_copy:
        cpy     k4510_stkn
        beq     @stk_done
        txa
        clc
        adc     #1
        tax                              ; X walks $01(S+1) .. $01FF
        lda     $0100,x
        sta     k4510_stk,y
        iny
        bne     @stk_copy
@stk_done:
        ldy     #0
@banner:
        lda     k4510_banner,y
        beq     @go
        jsr     k4510_out
        iny
        bne     @banner
@go:
        jmp     COLD_START

k4510_banner:
        .byte   "MICROSOFT BASIC ON THE K4510", K_CR, K_LF
        .byte   "*HELP FOR K:OS COMMANDS, *BYE TO LEAVE", K_CR, K_LF, 0

; ---- console --------------------------------------------------------------
; Out: A = character.  The ROM's k_chrout makes a full newline of CR *and*
; of LF, so the LF of BASIC's CR/LF pair has to be dropped here or every
; line would be double spaced.  (EhBASIC's glue does the same thing.)
k4510_out:
        cmp     #K_LF
        beq     @done
        sta     k4510_ch
        txa
        pha
        tya
        pha
        lda     k4510_ch
        jsr     ROM_CHROUT
        pla
        tay
        pla
        tax
        lda     k4510_ch
@done:
        rts

k4510_ch:     .byte 0           ; the character in flight (A across the call)

; In: blocking, returns the key in A.  Enter arrives as CR ($0D), which is
; what BASIC wants, so no translation -- but lower case is folded up: the
; 1977 tokenizer only knows upper-case keywords.
;
; **This routine echoes, and it must.**  MS BASIC never echoes what is
; typed: INLIN reads through GETLN -> MONRDKEY and prints nothing back
; (msbasic/inline.s), because on a KIM or a PET it was the monitor's input
; routine that echoed.  The K4510's does not -- the ROM's own echoing lives
; in readline(), which BASIC bypasses by calling CHRIN directly.  Without
; the echo here you type and the screen stays empty, which does not read as
; "no echo", it reads as a machine ignoring the keyboard.
;
; Backspace is the same story from the other end.  BASIC's delete character
; is "_" ($5F) and its handler is a bare DEX -- it erases nothing on the
; glass (msbasic/inline.s, L2420).  The host's Backspace ($08) is below $20,
; so BASIC discards it outright.  So: translate $08 to "_" and do the
; destructive erase (BS, space, BS) ourselves.  "@" still kills the whole
; line, as it did in 1977.
;
; While k4510_feed is set, keys come from k4510_answer instead of the
; keyboard.  That is how the "MEMORY SIZE?" prompt gets answered: BASIC's
; own alternative is to walk RAM upwards probing for the top, and the walk
; would march straight through this image.  Feeding it the number is the
; documented way to say where memory ends, and it is one line of canned
; input rather than a fork of init.s.
k4510_in:
        txa
        pha
        tya
        pha
        lda     k4510_feed
        beq     @live
        ldx     k4510_feedx
        lda     k4510_answer,x
        beq     @feed_done
        inc     k4510_feedx
        jmp     @echo    ; echoed too: the boot shows what it answered
@feed_done:
        lda     #0
        sta     k4510_feed
@live:
        jsr     ROM_CHRIN
; A star command, if this is the first character of a line AND BASIC is at
; the direct prompt.  CURLIN+1 = $FF is how MS BASIC itself says "direct
; mode", so a "*" typed at an INPUT prompt inside a running program is just
; a character, which is what INPUT's caller is entitled to expect.
        cmp     #$2A            ; '*'
        bne     @nostar
        ldx     k4510_col0
        beq     @star_no
        ldx     CURLIN+1
        inx                     ; $FF -> 0
        bne     @star_no
        jsr     k4510_star
        lda     #K_CR           ; BASIC gets an empty line and re-prompts
        jmp     @out
@star_no:
        lda     #$2A
@nostar:
        cmp     #$08            ; host Backspace: BASIC's delete is "_", and
        bne     @fold           ; nothing but this routine erases the glass
        jsr     k4510_rubout
        lda     #$5F            ; "_" -- echoed already, destructively
        jmp     @out
@fold:
        cmp     #'a'
        bcc     @echo
        cmp     #'z'+1
        bcs     @echo
        and     #$DF            ; fold to upper case
@echo:
        jsr     k4510_out       ; BASIC will not; see above
@out:
        sta     k4510_ch
        ldx     #0              ; the next character starts a line only after CR
        cmp     #K_CR
        bne     @out_col
        inx
@out_col:
        stx     k4510_col0
        pla
        tay
        pla
        tax
        lda     k4510_ch
        rts

; erase the character to the left: BS, space, BS.  At column 0 the ROM's
; k_chrout ignores the backspace, so this cannot walk off the line.
k4510_rubout:
        lda     #$08
        jsr     k4510_out
        lda     #' '
        jsr     k4510_out
        lda     #$08
        jmp     k4510_out

; ---- star commands --------------------------------------------------------
; Called with the "*" already read and nothing echoed.  Reads the rest of
; the line straight from the ROM (BASIC is not involved and never sees any
; of it), runs it, and returns; the caller hands BASIC a bare CR so it
; re-prompts.  Deliberately its own tiny line editor rather than a detour
; through BASIC's: BASIC's INLIN fills INPUTBUFFER, and borrowing it here
; would mean unpicking what the tokenizer had already done to it.
k4510_star:
        lda     #$2A
        jsr     k4510_out
        lda     #0
        sta     k4510_lp
@read:
        jsr     ROM_CHRIN                ; NOTE: the ROM stubs do not preserve Y
        cmp     #K_CR                    ; (rom/crt0.s), which is why the index
        beq     @eol                     ; lives in k4510_lp and not in Y
        cmp     #$08                     ; backspace: erase, unless at the "*"
        bne     @store
        ldy     k4510_lp
        beq     @read
        dey
        sty     k4510_lp
        jsr     k4510_rubout
        jmp     @read
@store:
        ldy     k4510_lp
        cpy     #K4510_LINEMAX
        bcs     @read                    ; full: refuse rather than wrap
        sta     k4510_line,y
        iny
        sty     k4510_lp
        jsr     k4510_out
        jmp     @read
@eol:
        ldy     k4510_lp
        lda     #0
        sta     k4510_line,y
        lda     #K_CR                    ; close the line on the glass
        jsr     k4510_out
        lda     k4510_lp
        beq     @done                    ; a bare "*" does nothing at all
        ldx     #WORD_BYE
        jsr     k4510_match
        beq     @bye
        ldx     #WORD_QUIT               ; BBC BASIC on the Tube says *QUIT;
        jsr     k4510_match              ; same family, same word
        bne     @notbye
@bye:
        jmp     k4510_leave
@notbye:
        ldx     #WORD_HELP
        jsr     k4510_match
        bne     @toshell
        lda     #<QT_STARHELP
        ldy     #>QT_STARHELP
        jsr     STROUT                   ; ours first, then the shell's own list
@toshell:
        lda     #<k4510_line
        ldx     #>k4510_line
        jsr     ROM_SHELL
@done:
        rts

; Does k4510_line say exactly the word at k4510_words+X?  The comparison
; folds case on both sides and both must end at the same place, so "*BYES"
; is a shell command and not a misspelt exit.  Returns with Z set on a
; match.  Indexed absolute rather than an indirect pointer: the zero page
; belongs to BASIC while this runs and there is no pair here to borrow.
k4510_match:
        ldy     #0
@loop:
        lda     k4510_words,x
        beq     @wantend
        jsr     k4510_upper
        sta     k4510_mc
        lda     k4510_line,y
        jsr     k4510_upper
        cmp     k4510_mc
        bne     @no
        inx
        iny
        bne     @loop
@wantend:
        lda     k4510_line,y             ; the word ended: the line must too
        beq     @yes
@no:
        lda     #1                       ; Z clear
        rts
@yes:
        lda     #0                       ; Z set
        rts

k4510_upper:
        cmp     #'a'
        bcc     @u
        cmp     #'z'+1
        bcs     @u
        and     #$DF
@u:
        rts

k4510_mc:     .byte 0
k4510_lp:     .byte 0                    ; how much of k4510_line is filled
WORD_BYE      = 0
WORD_HELP     = 4
WORD_QUIT     = 9
k4510_words:  .byte "BYE", 0, "HELP", 0, "QUIT", 0
k4510_line:   .res  K4510_LINEMAX + 1

QT_STARHELP:
        .byte   "* HANDS THE LINE TO K:OS.  *BYE LEAVES.", K_CR, K_LF, 0

; ---- *BYE -----------------------------------------------------------------
; Put the shell's stack frame back and return through it.  The ROM's
; trampoline turns the banks off and _call_prog restores the ROM's zero
; page, so from the shell's side this is an ordinary program returning.
k4510_leave:
        lda     k4510_stkn
        bne     @can
        lda     #<QT_NOWAYBACK
        ldy     #>QT_NOWAYBACK
        jsr     STROUT
        rts
@can:
        ldx     k4510_sp
        ldy     #0
@put:
        cpy     k4510_stkn
        beq     @go
        inx
        lda     k4510_stk,y
        sta     $0100,x
        iny
        bne     @put
@go:
        ldx     k4510_sp
        txs
        rts                              ; ... into the shell

QT_NOWAYBACK:
        .byte   "NO WAY BACK: USE THE RESET CHORD", K_CR, K_LF, 0

k4510_sp:     .byte 0
k4510_stkn:   .byte 0
k4510_stk:    .res  K4510_STKMAX
k4510_col0:   .byte 1                    ; the next key begins a line
k4510_feed:   .byte 1
k4510_feedx:  .byte 0
; The two questions CONFIG_2C asks at cold start, answered in order:
;   MEMORY SIZE?    28672 = $7000, where this image begins (keep in step
;                   with MEMTOP above)
;   TERMINAL WIDTH? 80, the K4510 console
k4510_answer: .byte "28672", K_CR, "80", K_CR, 0

; ---- Ctrl-C ---------------------------------------------------------------
; Called once per statement.  No break: return with A non-zero (Z clear),
; which is what the caller's "bne RET1" wants.  Break: enter STOP with
; Z and C set, exactly as the OEM versions fall into it.
ISCNTC:
        lda     KBD_BREAK
        bne     @break
        lda     #$01
        rts
@break:
        lda     #$03
        cmp     #$03            ; Z=1, C=1
        jmp     STOP

; ---- LOAD / SAVE ----------------------------------------------------------
; Not this stage.  The ROM has both ($FF89 / $FF8C, name pointer in $F0/$F1,
; a 28-bit address in $F2..$F5, the length in $F6..$F9), so wiring them is
; a contained job -- but it is the job after this one.  Say so rather than
; raise a misleading ?SYNTAX ERROR.
LOAD:
SAVE:
        lda     #<QT_NO_LOADSAVE
        ldy     #>QT_NO_LOADSAVE
        jsr     STROUT
        rts

QT_NO_LOADSAVE:
        .byte   "NOT YET ON THIS BASIC -- USE EHBASIC", K_CR, K_LF, 0
