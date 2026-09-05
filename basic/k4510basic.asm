; EhBASIC 2.22 on the K4510, as a .prg the system ROM loads at $7000.
; "Derived from EhBASIC" -- Lee Davison's Enhanced BASIC, see README-EhBASIC.txt.
; Console through the ROM jump table ($FF80 CHROUT, $FF86 GETIN); BASIC RAM
; $0800-$6FFF (26 KB); page 3 holds EhBASIC's vectors and input buffer;
; the whole zero page is EhBASIC's (the ROM saves its own around calls).
; RUN/STOP (Escape) resets the machine back to the shell.

IRQ_vec	= VEC_SV+2		; EhBASIC keeps its page-3 layout (Ibuffs follows)
NMI_vec	= IRQ_vec+$0A
k_crx0	= $03B3			; K4510: the crunch start index (the * prefix is only a prefix there)

; K4SG header, stage 3 of the memory plan: the interpreter loads in three
; segments above BASIC's RAM -- $E000-$FEFF (block 7) + $C000-$CFFF
; (block 6) + a tail at $BC00 in the RAM under the sideways window. The
; loader sets the bank bases; the launch trampoline engages blocks 5-7.
; BASIC's program RAM is $0800-$BFFF: 47103 bytes free.
	.byte	"K4SG"
	.byte	4, 0			; segments, flags
	.word	k4510_start		; entry
	.dword	$E000
	.dword	K4510_SPLIT1 - $E000
	.byte	7, 0, 0, 0
	.dword	$C000
	.dword	K4510_SPLIT2 - $C000
	.byte	6, 0, 0, 0
	.dword	$0230
	.dword	K4510_END - $0230
	.byte	$FF, 0, 0, 0		; the page-2 loan: plain RAM, always visible
	.dword	$BD00
	.dword	K4510_TAIL - $BD00
	.byte	$FF, 0, 0, 0		; the tail: plain RAM at the top of BASIC's, where the
					; *VI / *EDIT hook and the GRAPHICS mode plumbing live.
					; Both interpreter slices are hard against their ceilings
					; ($D000 is the I/O page, $FF00 the ROM stub), so new code
					; goes here and Ram_top comes down to $BD00 to pay for it.

	.include "basic.asm"		; .org $E000 / $C000 inside; Ram_base/Ram_top patched for the K4510

; ---- the K4510 host glue -------------------------------------------------

ROM_CHROUT	= $FF80
ROM_GETIN	= $FF86
CR		= $0D
LF		= $0A
ESC		= $1B

k4510_start
	CLD
	STZ	$03B9			; the sprite table wants clearing again (gsprinit)
	STZ	$03BA			; and no console mode is saved (gprev)
	LDA	#<k4510_in
	STA	VEC_IN
	LDA	#>k4510_in
	STA	VEC_IN+1
	LDA	#<k4510_out
	STA	VEC_OUT
	LDA	#>k4510_out
	STA	VEC_OUT+1
	LDA	#<k4510_load
	STA	VEC_LD
	LDA	#>k4510_load
	STA	VEC_LD+1
	LDA	#<k4510_save
	STA	VEC_SV
	LDA	#>k4510_save
	STA	VEC_SV+1
	LDY	#0
k4510_msg
	LDA	k4510_banner,Y
	BEQ	k4510_go
	JSR	k4510_out
	INY
	BNE	k4510_msg
k4510_go
	JMP	LAB_COLD

; non-halting input: A = char, carry set if one was there
k4510_in
	PHX
	PHY
	JSR	ROM_GETIN
	PLY
	PLX
	CMP	#0
	BEQ	k4510_nokey
	CMP	#$61			; fold a-z to upper case: EhBASIC keywords are upper case
	BCC	k4510_gotkey
	CMP	#$7B
	BCS	k4510_gotkey
	AND	#$DF
k4510_gotkey
	SEC
	RTS
k4510_nokey
	CLC
	RTS
k4510_quit
	JMP	($FFFC)			; back to the shell, cold

; Ctrl-C check, called once per statement. EhBASIC's own version pops the
; input and keeps the byte for 32 statements, so a key typed while a program
; is busy is usually lost. This one only peeks ($D102): a Ctrl-C or RUN/STOP
; is taken (from anywhere in the queue), anything else stays for GET/INPUT.
k4510_cc
	LDA	ccflag
	BNE	k4510_cc_done		; checks inhibited (LOAD feeding a file)
	LDA	$D103			; a Ctrl-C or RUN/STOP anywhere in the queue? (removed; other keys stay for GET)
	BEQ	k4510_cc_done
	JSR	k4510_hush		; both stop the program and silence the machine; @BYE leaves to the shell
	LDA	#$03
	STA	ccbyte
	LDX	#$20
	STX	ccnull
	JMP	LAB_1636		; Ctrl-C: STOP
k4510_cc_done
	JMP	LAB_FBA2		; the interrupt checks, as in the stock routine

; silence the machine: flush the sequencer, key off the OPL2's nine voices
k4510_hush
	PHA
	PHX
	LDA	#$80
	STA	$D5E0			; sequencer: silence everything now
	LDX	#8
k4510_hush1
	TXA
	ORA	#$B0			; $B0-$B8: key-on/block/F-number high of voice X
	STA	$D480
	STZ	$D481			; key off
	DEX
	BPL	k4510_hush1
	PLX
	PLA
	RTS

; output A; EhBASIC sends CR LF and the ROM's CHROUT makes a newline of either
k4510_out
	CMP	#LF
	BEQ	k4510_outdone
	PHA
	PHX
	PHY
	JSR	ROM_CHROUT
	PLY
	PLX
	PLA
k4510_outdone
	RTS

	.include "k4510math.asm"
	.include "k4510file.asm"
	.include "k4510gfx.asm"

K4510_SPLIT2				; [K4510] end of the $C000 slice (the expression
					; compiler rides inside basic.asm's $E000 half now)
	.assert K4510_SPLIT2 <= $D000, error, "EhBASIC $C000 slice overflows into the I/O page"
	.org	$0230			; the page-2 loan, $0230-$02CF: ROM DATA ends below it
					; (rom/k4510.cfg pins that), the launch trampoline is
					; at $02D8; the FOUT/POWER glue + the banner live here

	.include "k4510page2.asm"
K4510_END
	.assert K4510_END <= $02D0, error, "the page-2 loan overflows into the launch trampoline"

	.org	$BD00			; the tail, in what used to be the top of BASIC's RAM

; ---- *VI / *EDIT with nothing after: edit THIS program --------------------
; SAVE the program to a temp file, run the editor, LOAD it back.  SWAP is not
; optional: vi.prg lives at $5FFC-$7C64 and EhBASIC at $7000, so the editor
; would land on top of the interpreter -- SWAP puts all 64 KB and the screen
; aside first and restores them afterwards.
; With an argument (*VI notes.txt) none of this happens: the line goes to the
; shell as it always did.  The load never returns -- it feeds the file through
; the input vector and lands at Ready, which is where we want to be.
k_ed_file	.byte	"EDITTMP.BAS",0
k_ed_cvi	.byte	"SWAP VI EDITTMP.BAS",0
k_ed_ced	.byte	"SWAP EDIT EDITTMP.BAS",0

k_ed_cmd	= $07			; zero page: EhBASIC leaves $03-$09 free, fptr has $03-$06

k_upper					; A: fold to upper case
	CMP	#'a'
	BCC	k_up_out
	CMP	#'z'+1
	BCS	k_up_out
	AND	#$DF
k_up_out
	RTS

; Y = index of the byte after the word; accept only end of statement, so
; anything with an argument falls through to the shell unchanged
k_ed_bare
	LDA	(Bpntrl),Y
	BEQ	k_ed_yes
	CMP	#' '
	BNE	k_ed_no
	INY
	BRA	k_ed_bare
k_ed_yes
	SEC
	RTS
k_ed_no
	CLC
	RTS

k_ateditor				; returns (C clear) if this was not *VI / *EDIT
	LDY	#1
	LDA	(Bpntrl),Y
	JSR	k_upper
	CMP	#'V'
	BEQ	k_ae_vi
	CMP	#'E'
	BNE	k_ed_no
	LDY	#2			; E D I T
	LDA	(Bpntrl),Y
	JSR	k_upper
	CMP	#'D'
	BNE	k_ed_no
	LDY	#3
	LDA	(Bpntrl),Y
	JSR	k_upper
	CMP	#'I'
	BNE	k_ed_no
	LDY	#4
	LDA	(Bpntrl),Y
	JSR	k_upper
	CMP	#'T'
	BNE	k_ed_no
	LDY	#5
	JSR	k_ed_bare
	BCC	k_ed_no
	LDA	#<k_ed_ced
	LDX	#>k_ed_ced
	BRA	k_ae_go
k_ae_vi
	LDY	#2			; V I
	LDA	(Bpntrl),Y
	JSR	k_upper
	CMP	#'I'
	BNE	k_ed_no
	LDY	#3
	JSR	k_ed_bare
	BCC	k_ed_no
	LDA	#<k_ed_cvi
	LDX	#>k_ed_cvi
k_ae_go
	STA	k_ed_cmd		; the shell line, kept where SAVE cannot reach it:
	STX	k_ed_cmd+1		; k_getname and LIST both use ut1_pl/ut1_ph
k_ae_skip
	JSR	LAB_IGBY		; past the command, so LIST sees end-of-statement
	BNE	k_ae_skip
	JSR	k_ed_setname
	JSR	k_save_named		; SAVE "EDITTMP.BAS"
	JSR	k_ed_tobuf		; the line has to be somewhere the ROM can read it
	LDA	#<Ibuffs		; ($BE00 is in the sideways window: when the stub hands
	LDX	#>Ibuffs		;  control to the ROM, block 5 is the ROM's, not ours)
	JSR	ROM_SHELL		; SWAP VI EDITTMP.BAS
	JSR	k_ed_setname		; and LOAD it back (never returns: ends at Ready)
	JSR	k_setname
	STZ	chain
	STZ	cidx
	LDA	#1
	STA	ccflag
	JMP	k_ld_imm

; copy the shell line into EhBASIC's input buffer, which is page 3 -- plain
; RAM that both the interpreter and the ROM see the same way, like fname.
; The line we came from has already been stepped past, so the buffer is free.
k_ed_tobuf
	LDY	#0
k_ed_tb
	LDA	(k_ed_cmd),Y
	STA	Ibuffs,Y
	BEQ	k_ed_tb_done
	INY
	BRA	k_ed_tb
k_ed_tb_done
	RTS

k_ed_setname				; fname = "EDITTMP.BAS"
	LDY	#0
k_ed_sn
	LDA	k_ed_file,Y
	STA	fname,Y
	BEQ	k_ed_sn_done
	INY
	BRA	k_ed_sn
k_ed_sn_done
	RTS
; SPROFF n : disable  (lives in the tail: the $C000 slice is full)
K_SPROFF
	LDX	#1
	JSR	k_getargs
	JSR	k_spr_base
	.byte	$A3, $08
	.byte	$EA
	LDA	(sprfp)
	AND	#$FE
	.byte	$EA
	STA	(sprfp)
	.byte	$A3, $00
	RTS

; ---- GRAPHICS mode plumbing (k4510gfx.asm calls these) -------------------
; A = the console MODE digit: run "MODE d" through the ROM's shell.  The
; line is copied to gargs first -- the ROM cannot read this image while it
; runs the command (blocks 5-7 are the ROM's own inside a system call), and
; gargs is low RAM both sides always see.  GRAPHICS takes no gargs itself.
k_gm_run
	PHA
	LDX	#6
k_gm_cp	LDA	k_gmstr,X
	STA	gargs,X
	DEX
	BPL	k_gm_cp
	PLA
	STA	gargs+5
	LDA	#<gargs
	LDX	#>gargs
	JMP	ROM_SHELL

; VICKY CTRL resolution bits (already masked with $1E) -> the console MODE
; digit that matches
k_ctrl2digit
	CMP	#0
	BEQ	k_c2d_0
	CMP	#2			; columns halved: 320x240
	BEQ	k_c2d_2
	LDA	#'1'			; 640x240 -- and the 200-line modes fall back here too
	RTS
k_c2d_0
	LDA	#'0'
	RTS
k_c2d_2
	LDA	#'2'
	RTS
k_gmstr	.byte	"MODE 0",0

K4510_TAIL
	.assert K4510_TAIL <= $BF00, error, "the $BD00 tail has outgrown its 512 bytes -- lower Ram_top again"
