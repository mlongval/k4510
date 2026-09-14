; ---- LOAD "name" / SAVE "name" for EhBASIC on the K4510 ------------------
; LOAD reads the text file into far memory with the ROM's LOAD ($FF89) and
; then feeds it through the input vector as if it were typed (output muted
; while it loads); SAVE lists the program through the output vector into
; far memory and writes it with the ROM's SAVE ($FF8C).  Files are plain
; text in the machine's fs/ directory, so a PC can write them too.

FILE_BUF	= $0C0000		; far: up to 64 KB of program text
ROM_LOAD	= $FF89
ROM_SAVE	= $FF8C
P_NAME		= $F0			; ROM parameter block (zero page)
P_ADDR		= $F2
P_LEN		= $F6

fname	= $03C0			; the file name, NUL-terminated (max 63)
fptr	= $03			; zero page $03-$06: far pointer into FILE_BUF (EhBASIC leaves $03-$09 unused)
fleft	= $0400			; bytes left to feed, 32-bit
savedin	= $0404			; saved input vector while feeding a file
savedout= $0406			; saved output vector while muted / capturing
savecnt	= $0408			; bytes captured by SAVE, 32-bit
ffirst	= $040C			; 1 until the first feed byte: a CR that EhBASIC's Ctrl-C check swallows
chain	= $040D			; 1: LOAD was executed by a running program -> RUN the new program (CHAIN)
cidx	= $040E			; index into k_chainstr while chaining
; $040F is free for programs: the DEMOS/BENCH menus use PEEK(1039) as "return to the menu" flag
ROM_SHELL	= $FF8F

; get the string argument into fname (upper-cased: the file system is case-sensitive)
k_getname
	JSR	LAB_EVEX		; evaluate the expression
	JSR	LAB_EVST		; must be a string
	JSR	LAB_22B6		; A = length, X/Y = pointer
	CMP	#63
	BCC	k_gn_ok
	LDA	#63
k_gn_ok
	STX	ut1_pl
	STY	ut1_ph
	TAY
	LDA	#0
	STA	fname,Y			; terminator
k_gn_copy
	DEY
	BMI	k_gn_done
	LDA	(ut1_pl),Y
	CMP	#$61
	BCC	k_gn_st
	CMP	#$7B
	BCS	k_gn_st
	AND	#$DF
k_gn_st
	STA	fname,Y
	BRA	k_gn_copy
k_gn_done
	RTS

; the ROM's parameter block at $F0-$F9 overlaps EhBASIC's number-to-string
; area ($EF-$FF, used by LIST), so SAVE sets it only after LIST has run
k_setname
	LDA	#<fname
	STA	P_NAME
	LDA	#>fname
	STA	P_NAME+1
	LDA	#<FILE_BUF
	STA	P_ADDR
	LDA	#>FILE_BUF
	STA	P_ADDR+1
	LDA	#^FILE_BUF
	STA	P_ADDR+2
	STZ	P_ADDR+3
	LDA	#<FILE_BUF
	STA	fptr
	LDA	#>FILE_BUF
	STA	fptr+1
	LDA	#^FILE_BUF
	STA	fptr+2
	STZ	fptr+3
	RTS

; ---- @command: the rest of the statement goes to the ROM shell ----------
; The cruncher left the text verbatim after the @, terminated by the [EOL] 0.
K_AT
	JSR	k_ateditor		; *VI / *EDIT on their own edit THIS program (and do not return)
	LDY	#1
	LDA	(Bpntrl),Y		; @BYE / @EXIT / @QUIT: leave BASIC for the shell (a cold start)
	CMP	#'B'
	BEQ	K_AT_bye0
	CMP	#'E'
	BEQ	K_AT_bye0
	CMP	#'Q'
	BNE	K_AT_go
K_AT_bye0
	INY
	LDA	(Bpntrl),Y
	CMP	#'Y'
	BEQ	K_AT_bye
	CMP	#'X'
	BEQ	K_AT_bye
	CMP	#'U'
	BNE	K_AT_go
K_AT_bye
	JSR	k4510_hush
	JMP	k4510_quit
K_AT_go
	LDA	Bpntrl			; A/X = pointer past the @
	CLC
	ADC	#1
	TAY
	LDA	Bpntrh
	ADC	#0
	TAX
	TYA
	JSR	k_shell_cur		; run it (prints through the ROM), then the cursor back on
K_AT_skip
	JSR	LAB_IGBY		; advance to the [EOL]
	BNE	K_AT_skip
	RTS

; ---- LOAD ----
; In immediate mode: load and stop at Ready. From a running program: load
; and RUN the new program (CHAIN, as on the C64). Variables do not survive.
k4510_load
	JSR	k_getname
	JSR	k_setname
	STZ	chain
	STZ	cidx
	LDA	#1
	STA	ccflag			; no Ctrl-C sampling while the file feeds (it would eat bytes)
	LDA	Clineh
	CMP	#$FF			; $FF = immediate mode
	BEQ	k_ld_imm
	INC	chain
	BRA	k_ld_imm

; RUN "name": LOAD the file and run it, from immediate mode or a program
k4510_runfile
	JSR	k_getname
	JSR	k_setname
	STZ	cidx
	LDA	#1
	STA	chain			; the feed types RUN after the file
	STA	ccflag
k_ld_imm
	JSR	ROM_LOAD		; A = status
	CMP	#0
	BEQ	k_ld_ok
	JMP	LAB_FCER		; not found: Function call error
k_ld_ok
	LDA	P_LEN			; size -> fleft
	STA	fleft
	LDA	P_LEN+1
	STA	fleft+1
	LDA	P_LEN+2
	STA	fleft+2
	STZ	fleft+3
	LDA	#1
	STA	ffirst
	LDA	VEC_IN			; feed the file: input from the buffer, output muted
	STA	savedin
	LDA	VEC_IN+1
	STA	savedin+1
	LDA	#<k_filein
	STA	VEC_IN
	LDA	#>k_filein
	STA	VEC_IN+1
	LDA	VEC_OUT
	STA	savedout
	LDA	VEC_OUT+1
	STA	savedout+1
	LDA	#<k_nullout
	STA	VEC_OUT
	LDA	#>k_nullout
	STA	VEC_OUT+1
	JSR	LAB_1463		; NEW: clear the program and flush the stack (it returns to the top frame --
	JMP	LAB_127D		; inside IF..THEN that is the IF tail, so go to the immediate loop explicitly)

k_nullout
	RTS

; input vector while a file is being fed: next byte, or end the feed.
; X and Y must survive (the line editor keeps its index in X).
k_filein
	LDA	ffirst
	BEQ	k_fi_go
	STZ	ffirst
	LDA	#$0D
	SEC
	RTS
k_fi_go
	LDA	fleft
	ORA	fleft+1
	ORA	fleft+2
	BEQ	k_fi_end
	.byte	$EA			; NOP prefix: 32-bit flat
	LDA	(fptr)			; LDA [fptr],Z  (Z = 0)
	PHA
	INC	fptr
	BNE	k_fi_1
	INC	fptr+1
	BNE	k_fi_1
	INC	fptr+2
k_fi_1
	LDA	fleft			; fleft--
	BNE	k_fi_2
	LDA	fleft+1
	BNE	k_fi_1b
	DEC	fleft+2
k_fi_1b
	DEC	fleft+1
k_fi_2
	DEC	fleft
	PLA
	CMP	#$0A			; LF ends a line like CR
	BNE	k_fi_5
	LDA	#$0D
k_fi_5
	SEC
	RTS
k_fi_end
	LDA	chain
	BEQ	k_fi_msg		; a plain LOAD: confirm with LOADED, then restore
	PHX
	LDX	cidx
	LDA	k_chainstr,X		; R U N; the final CR below runs it
	PLX
	CMP	#0			; (PLX changed the flags)
	BEQ	k_fi_restore
	INC	cidx
	SEC
	RTS
k_chainstr
	.byte	"RUN", 0
k_loadedmsg
	.byte	$0D, "LOADED", $0D, 0
k_fi_msg
	PHX				; the line editor keeps its buffer index in X across input calls
	PHY
	STZ	cidx
k_fi_lmsg
	LDX	cidx
	LDA	k_loadedmsg,X
	BEQ	k_fi_lmdone
	JSR	ROM_CHROUT		; straight to the screen: EhBASIC output is still muted here
	INC	cidx
	BRA	k_fi_lmsg
k_fi_lmdone
	PLY
	PLX
k_fi_restore
	STZ	chain
	STZ	ccflag			; Ctrl-C works again
	LDA	savedin			; restore keyboard input and the screen
	STA	VEC_IN
	LDA	savedin+1
	STA	VEC_IN+1
	LDA	savedout
	STA	VEC_OUT
	LDA	savedout+1
	STA	VEC_OUT+1
	LDA	#$0D			; one final return commits the last line and prints Ready
	SEC
	RTS

; ---- SAVE ----
k4510_save
	JSR	k_getname
k_save_named				; entry with fname already set (the *VI / *EDIT hook)
	JSR	k_setname
	STZ	savecnt
	STZ	savecnt+1
	STZ	savecnt+2
	STZ	savecnt+3
	LDA	VEC_OUT
	STA	savedout
	LDA	VEC_OUT+1
	STA	savedout+1
	LDA	#<k_capture
	STA	VEC_OUT
	LDA	#>k_capture
	STA	VEC_OUT+1
	JSR	LAB_GBYT		; LIST wants the flags of the current byte
	JSR	LAB_LIST		; lists the whole program through k_capture
	LDA	savedout
	STA	VEC_OUT
	LDA	savedout+1
	STA	VEC_OUT+1
	JSR	k_setname		; (again: LIST used $EF-$FF)
	LDA	savecnt
	STA	P_LEN
	LDA	savecnt+1
	STA	P_LEN+1
	LDA	savecnt+2
	STA	P_LEN+2
	STZ	P_LEN+3
	JSR	ROM_SAVE
	RTS

; output vector while SAVE captures LIST: CR -> LF so the file is plain text
k_capture
	CMP	#$0A
	BEQ	k_cap_skip		; EhBASIC sends CR LF; keep one newline
	CMP	#$0D
	BNE	k_cap_st
	LDA	#$0A
k_cap_st
	.byte	$EA
	STA	(fptr)			; STA [fptr],Z
	INC	fptr
	BNE	k_cap_1
	INC	fptr+1
	BNE	k_cap_1
	INC	fptr+2
k_cap_1
	INC	savecnt
	BNE	k_cap_skip
	INC	savecnt+1
	BNE	k_cap_skip
	INC	savecnt+2
k_cap_skip
	RTS
