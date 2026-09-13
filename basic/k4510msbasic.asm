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
; byte of msbasic/ knows it happened.  *VI on its own is the port's: it
; SAVEs the program, edits it in VI, and LOADs it back (see LOAD / SAVE).
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

; the host filesystem (core/io.h, IO_STORAGE): a command is done by the
; time the write that gives it is, so the status can be read straight after
FS_CMD          = $D300
FS_STATUS       = $D301          ; 0 = ok
FS_NAME         = $D304          ; 28-bit pointer to a NUL-terminated name
FS_ADDR         = $D308          ; 28-bit RAM address for READ / WRITE
FS_LEN          = $D30C          ; 32-bit: bytes asked for, then bytes done
FS_OPEN_READ    = 1
FS_OPEN_WRITE   = 2
FS_READ         = 3
FS_WRITE        = 4
FS_CLOSE        = 5
K4510_IOBUF     = $9800          ; LOAD's and SAVE's one buffer (never both at once), 255
                                 ; bytes of the free RAM above the image -- below $A000,
                                 ; so no bank is mapped over it, and SWAP keeps it
K4510_IOMAX     = 255
K4510_NAMEMAX   = 40             ; the file name, NUL and ".BAS" included

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
        lda     $DA0E                    ; the ROM hides the console cursor for every program;
        ora     #1                       ; a BASIC prompt wants it back (Doc, 2026-09-11: "does NOT show a cursor")
        sta     $DA0E
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
;
; While SAVE is listing (k4510_tofile), a line is steered by how it begins:
; LIST prints a program line as a space (FOUT's sign column) and the line
; number, so " 1".." 9" makes it the file's -- written without that space,
; ended by LF -- and anything else (the CR/LF/"OK" of RESTART) the
; screen's.  k4510_ostate: 0 at a line's start, 1 the file's, 2 the
; screen's, 3 a leading space held until the next character decides.
k4510_out:
        cmp     #K_LF
        beq     @done
        sta     k4510_ch
        txa
        pha
        tya
        pha
        lda     k4510_tofile
        bne     @file
        lda     k4510_ch
        jsr     ROM_CHROUT
@back:
        pla
        tay
        pla
        tax
        lda     k4510_ch
@done:
        rts
@file:
        lda     k4510_ch
        ldx     k4510_ostate
        beq     @start
        cpx     #3
        bne     @have
        ldx     #1                       ; space, digit: a program line
        cmp     #'0'
        bcc     @notnum
        cmp     #'9'+1
        bcc     @setst
@notnum:
        lda     #' '                     ; only a space: the screen's after all
        jsr     ROM_CHROUT
        lda     k4510_ch
        ldx     #2
        bne     @setst
@start:
        cmp     #K_CR
        beq     @back                    ; an empty line goes nowhere
        cmp     #' '
        bne     @first
        ldx     #3
        stx     k4510_ostate
        jmp     @back
@first:
        ldx     #1
        cmp     #'0'
        bcc     @toscr
        cmp     #'9'+1
        bcc     @setst
@toscr:
        ldx     #2
@setst:
        stx     k4510_ostate
@have:
        cpx     #1
        bne     @scr
        cmp     #K_CR
        bne     @put
        lda     #K_LF                    ; a line of the file ends in LF
        jsr     k4510_put
        jmp     @eol
@put:
        jsr     k4510_put
        jmp     @back
@scr:
        jsr     ROM_CHROUT
        lda     k4510_ch
        cmp     #K_CR
        bne     @back
@eol:
        lda     #0
        sta     k4510_ostate
        jmp     @back

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
;
; k4510_feed 1 is that canned line (or a SAVE"..."/LOAD"..." fed by *VI);
; 2 is a file being LOADed, which is not echoed -- a LOAD is quiet.
k4510_in:
        txa
        pha
        tya
        pha
        jsr     k4510_endsave            ; a SAVE's LIST ends by coming back here
@again:
        lda     k4510_feed
        beq     @live
        cmp     #2
        beq     @file
        ldx     k4510_feedx
        lda     k4510_answer,x
        beq     @feed_done
        inc     k4510_feedx
        jmp     @echo    ; echoed too: the boot shows what it answered
@feed_done:
        lda     #0
        sta     k4510_feed
        lda     k4510_after              ; *VI: the SAVE it fed has run
        beq     @live
        jsr     k4510_vi                 ; so edit, and feed the LOAD
        jmp     @again
@file:
        jsr     k4510_fget
        bcs     @again                   ; the file has ended: the keyboard again
        cmp     #'a'
        bcc     @out
        cmp     #'z'+1
        bcs     @out
        and     #$DF                     ; folded up, as typing is
        jmp     @out
@live:
        jsr     ROM_CHRIN
; What a line cannot hold is not echoed.  INLIN refuses $7D and up, and the
; arrows and function keys are $80-$9B -- echoed, they drew Ç ü é â on the
; glass and BASIC silently threw them away (Doc, 2026-09-12, screenshot).
; Only when a LINE is being read (the call came from GETLN, whose JSR
; MONRDKEY returns to GETLN+2): GET takes them, and a game may want them.
        cmp     #$7D
        bcc     @typed
        sta     k4510_ch
        tsx
        lda     $0103,x          ; our caller's return address, low byte
        cmp     #<(GETLN+2)
        bne     @keepkey
        lda     $0104,x
        cmp     #>(GETLN+2)
        beq     @live            ; a line: drop it, and wait for the next key
@keepkey:
        lda     k4510_ch
@typed:
; A star command, if this is the first character of a line AND BASIC is at
; its OK prompt, so a "*" typed at an INPUT prompt inside a running program
; is just a character, which is what INPUT's caller is entitled to expect.
;
; "At the prompt" is read off the stack: the prompt's line comes from
; RESTART's one JSR INLIN (at L2351; every way back to the prompt goes
; there), INLIN's JSR GETLN, and GETLN's JSR MONRDKEY -- so above the two
; bytes pushed here sit three return addresses, the third L2351+2.  INPUT
; calls INLIN from elsewhere, GET calls MONRDKEY directly.  This used to
; ask CURLIN+1 = $FF, MS BASIC's "direct mode", but RESTART sets that only
; AFTER the line is read: after a RUN, CURLIN still held the program's
; last line, and the first *BYE was a ?SYNTAX ERROR (Doc, 2026-09-12:
; "the first time I type *BYE ... I type it again and it works").
        cmp     #$2A            ; '*'
        bne     @nostar
        ldx     k4510_col0
        beq     @star_no
        tsx
        lda     $0107,x         ; low byte of the third return address
        cmp     #<(L2351+2)
        bne     @star_no
        lda     $0108,x
        cmp     #>(L2351+2)
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
        ldx     #WORD_VI                 ; *VI alone: this program, in VI.  SAVE is
        jsr     k4510_match              ; typed for BASIC as if by hand; the rest
        bne     @notvi                   ; happens when BASIC next asks for a key
        ldx     #FEED_SAVE               ; (k4510_in, @feed_done).  *VI NAME is the
        jsr     k4510_feedcmd            ; shell's, and edits a file.
        lda     #1
        sta     k4510_after
        rts
@notvi:
        ldx     #WORD_HELP
        jsr     k4510_match
        bne     @toshell
        lda     #<QT_STARHELP
        ldy     #>QT_STARHELP
        jsr     STROUT                   ; ours first, then the shell's own list
@toshell:
        ; Through SWAP -k: a *program (say.prg at $6000) lands inside BASIC's
        ; $0800-$6FFF and the crt0 rule hands back its zero page; SWAP saves
        ; and restores all 64 KB around the command and -k keeps its output.
        ; (*SAY HI blanked the screen and never came back, review 2026-09-12.)
        ldy     k4510_lp                 ; shift the line (with its NUL) up by 8
@shift: lda     k4510_line,y
        sta     k4510_line+8,y
        dey
        bpl     @shift
        ldy     #7
@pre:   lda     k4510_swap,y
        sta     k4510_line,y
        dey
        bpl     @pre
        lda     #<k4510_line
        ldx     #>k4510_line
        jsr     ROM_SHELL
        jsr     k4510_cursor
@done:
        rts

; The console cursor back on.  The ROM hides it for every program and, when
; one ends, hands the shell a console with it off -- so after a *command that
; ran a program (*VI, *SAY) BASIC's prompt had no cursor (Doc, 2026-09-12).
; SWAP puts RAM back, not this register.
k4510_cursor:
        lda     $DA0E
        ora     #1
        sta     $DA0E
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
WORD_VI       = 14
k4510_words:  .byte "BYE", 0, "HELP", 0, "QUIT", 0, "VI", 0
k4510_line:   .res  K4510_LINEMAX + 9      ; room for the "SWAP -k " prefix
k4510_swap:   .byte "SWAP -k "

QT_STARHELP:
        .byte   "* HANDS THE LINE TO K:OS.  *VI EDITS THIS PROGRAM.", K_CR, K_LF
        .byte   "SAVE ", $22, "NAME", $22, " / LOAD ", $22, "NAME", $22, " KEEP IT.  *BYE LEAVES.", K_CR, K_LF, 0

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
              .res  K4510_NAMEMAX + 8    ; room for a fed SAVE"NAME" / LOAD"NAME" (k4510_feedcmd)

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
; A program is kept as TEXT -- its LIST -- so a .BAS reads like the listing,
; edits in VI or on the host, and comes back in by being typed, exactly as
; at the keyboard.  Tokens would be smaller and faster to load, and useless
; to every other program on the machine.  Doc, 2026-09-12: "type *VI and
; have it load the current program so I could edit it".
;
;   SAVE "NAME"   LIST, with k4510_out steering the program's lines into
;                 NAME.BAS and the rest (RESTART's OK) onto the screen.
;                 LIST never returns -- it ends in RESTART -- so the file is
;                 closed by the next k4510_in, which RESTART always reaches.
;                 In a running program SAVE ends it, as LIST does.
;   LOAD "NAME"   NEW, then NAME.BAS fed to BASIC through k4510_in, quietly,
;                 a buffer at a time: LF ends a line, CR and the other
;                 controls are dropped, lower case folds up as it does when
;                 typed.  A line BASIC would refuse from the keyboard it
;                 refuses from the file.  Nothing is lost if the file is not
;                 there: the NEW comes only after it has opened.
;
; Alone, SAVE and LOAD use the last name (PROGRAM.BAS until there is one),
; which is also the one *VI edits.  A name with no dot gets ".BAS".
SAVE:
        jsr     k4510_getname
        lda     #FS_OPEN_WRITE
        sta     FS_CMD
        lda     FS_STATUS
        beq     @ok
        lda     #0
        sta     k4510_after              ; *VI must not go on to LOAD an older file
        lda     #<QT_NOSAVE
        ldy     #>QT_NOSAVE
        jmp     STROUT
@ok:
        lda     #0
        sta     k4510_ostate
        sta     k4510_opos
        lda     #1
        sta     k4510_tofile
        jsr     CHRGOT                   ; LIST's flags: the statement's end, so all of it
        jmp     LIST                     ; ... which drops our caller's return, as it drops its own

LOAD:
        jsr     k4510_getname
        lda     #FS_OPEN_READ
        sta     FS_CMD
        lda     FS_STATUS
        beq     @ok
        lda     #<QT_NOLOAD
        ldy     #>QT_NOLOAD
        jmp     STROUT
@ok:
        lda     #0
        sta     k4510_fpos
        sta     k4510_flen
        lda     #1
        sta     k4510_fcr                ; so an empty file adds no empty line
        lda     #2
        sta     k4510_feed
        jmp     SCRTCH                   ; NEW, the way NEW does it: STKINI keeps our caller's return

QT_NOSAVE:    .byte   "?CANNOT WRITE THAT FILE", K_CR, K_LF, 0
QT_NOLOAD:    .byte   "?FILE NOT FOUND", K_CR, K_LF, 0

; The statement's argument, a string, into k4510_name (".BAS" added when it
; has no dot), and the device pointed at it.  Entered with CHRGET's flags:
; Z set means nothing follows, so the last name stands.
k4510_getname:
        beq     @point
        jsr     FRMEVL
        jsr     FRESTR                   ; A = length, (INDEX) = the characters
        tax
        beq     @point                   ; "" is nothing too
        cmp     #K4510_NAMEMAX - 5       ; room for ".BAS" and the NUL
        bcc     @len
        lda     #K4510_NAMEMAX - 5
@len:
        sta     k4510_nlen
        ldy     #0
        sty     k4510_dot
@copy:
        lda     (INDEX),y
        cmp     #'.'
        bne     @nodot
        sta     k4510_dot
@nodot:
        sta     k4510_name,y
        iny
        cpy     k4510_nlen
        bne     @copy
        ldx     #0
        lda     k4510_dot
        beq     @ext
        ldx     #4                       ; it has a dot: only the NUL
@ext:
        lda     k4510_bas,x
        sta     k4510_name,y
        beq     @point
        inx
        iny
        bne     @ext
@point:
        lda     #<k4510_name
        sta     FS_NAME
        lda     #>k4510_name
        sta     FS_NAME+1
        lda     #0
        sta     FS_NAME+2
        sta     FS_NAME+3
        rts

; A byte into SAVE's buffer, written out when it fills.  (X is used.)
k4510_put:
        ldx     k4510_opos
        sta     K4510_IOBUF,x
        inx
        stx     k4510_opos
        cpx     #K4510_IOMAX
        bne     @r
        jsr     k4510_flush
@r:
        rts

k4510_flush:
        lda     k4510_opos
        beq     @r
        sta     FS_LEN
        lda     #0
        sta     FS_LEN+1
        sta     FS_LEN+2
        sta     FS_LEN+3
        sta     k4510_opos
        jsr     k4510_bufaddr
        lda     #FS_WRITE
        sta     FS_CMD
@r:
        rts

k4510_bufaddr:
        lda     #<K4510_IOBUF
        sta     FS_ADDR
        lda     #>K4510_IOBUF
        sta     FS_ADDR+1
        lda     #0
        sta     FS_ADDR+2
        sta     FS_ADDR+3
        rts

; The end of a SAVE: what is left in the buffer, and close.
k4510_endsave:
        lda     k4510_tofile
        beq     @r
        jsr     k4510_flush
        lda     #FS_CLOSE
        sta     FS_CMD
        lda     #0
        sta     k4510_tofile
        sta     k4510_ostate
@r:
        rts

; LOAD's next character, C clear; or C set at the end, the file closed and
; the feed off.  A last line with no LF still gets its CR.
k4510_fget:
@next:
        ldx     k4510_fpos
        cpx     k4510_flen
        bne     @have
        jsr     k4510_bufaddr            ; the buffer is used up: read more
        lda     #K4510_IOMAX
        sta     FS_LEN
        lda     #0
        sta     FS_LEN+1
        sta     FS_LEN+2
        sta     FS_LEN+3
        sta     k4510_fpos
        lda     #FS_READ
        sta     FS_CMD
        lda     FS_LEN                   ; bytes read: 0 at the end (or on an error)
        sta     k4510_flen
        bne     @next
        lda     #FS_CLOSE
        sta     FS_CMD
        lda     #0
        sta     k4510_feed
        lda     k4510_fcr
        bne     @end
        inc     k4510_fcr
        lda     #K_CR
        clc
        rts
@end:
        sec
        rts
@have:
        lda     K4510_IOBUF,x
        inc     k4510_fpos
        cmp     #K_LF
        beq     @cr
        cmp     #$09                     ; a tab is a space
        bne     @notab
        lda     #' '
@notab:
        cmp     #' '
        bcc     @next                    ; CR and the other controls: nothing
        ldx     #0
        stx     k4510_fcr
        clc
        rts
@cr:
        lda     #1
        sta     k4510_fcr
        lda     #K_CR
        clc
        rts

; *VI, second half.  BASIC has run the SAVE the star command fed it, so the
; program is in k4510_name: edit it through the shell, then feed LOAD so
; it comes back.  (If that SAVE failed it cleared k4510_after, and this
; never runs.)
k4510_vi:
        lda     #0
        sta     k4510_after
        ldy     #0
@pre:
        lda     k4510_vicmd,y
        beq     @name
        sta     k4510_line,y
        iny
        bne     @pre
@name:
        ldx     #0
@cp:
        lda     k4510_name,x
        sta     k4510_line,y
        beq     @run
        inx
        iny
        bne     @cp
@run:
        lda     #<k4510_line
        ldx     #>k4510_line
        jsr     ROM_SHELL
        jsr     k4510_cursor             ; VI's end left the cursor off
        ldx     #FEED_LOAD
        ; fall into k4510_feedcmd

; Feed BASIC  WORD"NAME"<CR>  as if typed (echoed, so it can be seen):
; X = the word's offset in k4510_feedw.
k4510_feedcmd:
        ldy     #0
@w:
        lda     k4510_feedw,x
        beq     @q1
        sta     k4510_answer,y
        inx
        iny
        bne     @w
@q1:
        lda     #$22
        sta     k4510_answer,y
        iny
        ldx     #0
@n:
        lda     k4510_name,x
        beq     @q2
        sta     k4510_answer,y
        inx
        iny
        bne     @n
@q2:
        lda     #$22
        sta     k4510_answer,y
        iny
        lda     #K_CR
        sta     k4510_answer,y
        iny
        lda     #0
        sta     k4510_answer,y
        sta     k4510_feedx
        lda     #1
        sta     k4510_feed
        rts

FEED_SAVE     = 0
FEED_LOAD     = 5
k4510_feedw:  .byte "SAVE", 0, "LOAD", 0
k4510_vicmd:  .byte "SWAP -k VI ", 0
k4510_bas:    .byte ".BAS", 0
k4510_name:   .byte "PROGRAM.BAS", 0
              .res  K4510_NAMEMAX - 12
k4510_nlen:   .byte 0
k4510_dot:    .byte 0
k4510_tofile: .byte 0                    ; SAVE is listing into the file
k4510_ostate: .byte 0                    ; k4510_out's line state, see there
k4510_opos:   .byte 0
k4510_fpos:   .byte 0
k4510_flen:   .byte 0
k4510_fcr:    .byte 0                    ; the last character LOAD gave was a CR
k4510_after:  .byte 0                    ; *VI: edit and LOAD once the fed SAVE has run
