; ---- K4510 graphics commands for EhBASIC ---------------------------------
; GRAPHICS n   0 = off, 1 = 320x240, 2 = 640x480, 3 = the screen as it is --
;              whatever *MODE made it (2026-10-08) (8 bpp bitmap on VICKY layer 1,
;              above the text; index 0 is transparent; clears the bitmap)
; GCLS         clear the bitmap
; PLOT x,y,c   LINE x1,y1,x2,y2,c   TRI x1,y1,x2,y2,x3,y3,c   (blitter ops)
; PALETTE i,r,g,b
; Arguments are evaluated with EhBASIC's own expression code into gargs.

GFX_BUF		= $200000		; the bitmap, far memory (640*480 max)
VK		= $D000
DMA		= $D200

gargs	= $03A0			; 8 x 16-bit argument slots (above EhBASIC's Ibuffs, which ends at $039F)
gmode	= $03B0			; current GRAPHICS mode
				; $03B1-$03B4 are NOT free: k_crx0 (the tokenizer's crunch index) is
				; $03B3, and the old gw/gh/gsprinit here overlapped it and each other
				; (gw+1 WAS gsprinit) -- typing a graphics statement corrupted gh.
gw	= $03B5			; bitmap width low/high
gh	= $03B7			; bitmap height low/high
gsprinit = $03B9		; 1: the sprite table has been cleared and pointed at
gprev	= $03BA			; console MODE digit ('0'-'2') to put back at GRAPHICS 0; 0 = none saved
sprfp	= $03			; zero page $03-$06: far pointer to a sprite table entry (EhBASIC leaves $03-$09 free)
SPRTAB	= $03F800		; 128 x 16 bytes, above the ROM's row template
; ROM_SHELL comes from k4510basic.asm (A/X = a command line, run as if typed)

; get N comma-separated integer arguments into gargs
k_getargs				; X = count
	STX	gcount
	LDX	#0
k_ga_loop
	PHX
	JSR	LAB_EVNM		; evaluate, must be numeric
	JSR	LAB_F2FX		; to Itempl/h (handles negative)
	PLX
	LDA	Itempl
	STA	gargs,X
	LDA	Itemph
	STA	gargs+1,X
	INX
	INX
	TXA
	LSR
	CMP	gcount
	BCS	k_ga_done
	JSR	LAB_1C01		; scan for "," else syntax error
	BRA	k_ga_loop
k_ga_done
	RTS
gcount	.byte 0

; set up the blitter for the current surface; A = op
k_blt_setup
	PHA
	LDA	#<GFX_BUF
	STA	VK+$74
	LDA	#>GFX_BUF
	STA	VK+$75
	LDA	#^GFX_BUF
	STA	VK+$76
	STZ	VK+$77
	LDA	gw
	STA	VK+$78			; BLTW
	STA	VK+$7E			; BLTDS (stride = width, 8 bpp)
	LDA	gw+1
	STA	VK+$79
	STA	VK+$7F
	LDA	gh
	STA	VK+$7A			; BLTH
	LDA	gh+1
	STA	VK+$7B
	PLA
	STA	VK+$80			; BLTOP
	RTS

K_GRAPHICS
	JSR	LAB_EVNM
	JSR	LAB_F2FX
	LDA	Itempl
	BNE	k_gfx_some
	STA	gmode
	JMP	k_gfx_off
k_gfx_some
	STA	gmode
	CMP	#3			; 3: the screen MODE left, no mode change (k_gfx_cur, in the tail)
	BNE	k_gfx_mode
	JMP	k_gfx_cur
k_gfx_mode
	LDA	gprev			; remember the console mode we found, once
	BNE	k_gfx_saved		; (GRAPHICS 2 after GRAPHICS 1 keeps the first)
	LDA	VK+$00
	AND	#$1E			; CTRL bits 1-4: which resolution the machine is in
	JSR	k_ctrl2digit
	STA	gprev
k_gfx_saved
	LDA	gmode
	CMP	#1
	BEQ	k_gfx_lo
	LDA	#<640
	STA	gw
	LDA	#>640
	STA	gw+1
	LDA	#<480
	STA	gh
	LDA	#>480
	STA	gh+1
	LDA	#'0'			; console MODE 0: the ROM sets CTRL to 640x480 itself
	JMP	k_gfx_on
k_gfx_lo
	LDA	#<320
	STA	gw
	LDA	#>320
	STA	gw+1
	LDA	#<240
	STA	gh
	STZ	gh+1
	LDA	#'2'			; console MODE 2: 320x240, 40x30 text
k_gfx_on
	; Ask the ROM for the matching console mode rather than writing VICKY's
	; CTRL behind its back: the ROM owns the text layout (PCOLS/PROWS/bands),
	; and a CTRL the console is not laid out for cuts the screen in half.
	; MODE n does CTRL + layout + palette + cls, and the host's menu follows.
	JSR	k_gm_run
k_gfx_lay
	; layer 1: bitmap, 8 bpp, stride = width, data = GFX_BUF
	; (video_init turned every layer above the text off; set ours up fresh)
	STZ	VK+$21			; palofs
	STZ	VK+$22			; scroll
	STZ	VK+$23
	STZ	VK+$24
	STZ	VK+$25
	LDA	gw
	STA	VK+$26
	LDA	gw+1
	STA	VK+$27
	LDA	#<GFX_BUF
	STA	VK+$28
	LDA	#>GFX_BUF
	STA	VK+$29
	LDA	#^GFX_BUF
	STA	VK+$2A
	STZ	VK+$2B
	LDA	#$19			; enable | bitmap | 8 bpp
	STA	VK+$20
	; fall into GCLS
K_GCLS
	LDA	gmode
	BEQ	k_gfx_rts
	STZ	DMA+0			; fill value 0
	LDA	#<GFX_BUF
	STA	DMA+4
	LDA	#>GFX_BUF
	STA	DMA+5
	LDA	#^GFX_BUF
	STA	DMA+6
	STZ	DMA+7
	JSR	k_gfx_len		; length = gw * gh, any size (the tail)
	STZ	DMA+11
	LDA	#2
	STA	DMA+12			; DMA fill
k_gfx_rts
	RTS
k_gfx_off
	STZ	VK+$20			; layer 1 off
	LDA	gprev			; the console mode GRAPHICS found, saved on the way in
	BNE	k_go_have
	LDA	#'1'			; GRAPHICS 0 with none on: the default text mode
k_go_have
	STZ	gprev
	CMP	#$FF			; GRAPHICS 3 changed no mode: none to put back
	BEQ	k_gfx_rts
	JMP	k_gm_run		; MODE n: the ROM puts layout, palette and a clean screen back

; k_gm_run / k_ctrl2digit / k_gmstr live in the $BE00 tail (k4510basic.asm):
; the $C000 slice is nearly full and code there may call the ROM all the same.

; PLOT x,y,c : a one-pixel LINE from (x,y) to (x,y)
K_PLOT
	LDX	#3
	JSR	k_getargs
	LDA	gargs+4			; colour
	STA	gargs+8
	LDA	gargs+0
	STA	gargs+4
	LDA	gargs+1
	STA	gargs+5
	LDA	gargs+2
	STA	gargs+6
	LDA	gargs+3
	STA	gargs+7
	BRA	k_line_go
K_LINE
	LDX	#5
	JSR	k_getargs
k_line_go
	LDA	gargs+8			; colour
	STA	VK+$70			; BLTSRC byte 0 = colour
	LDA	#6
	JSR	k_blt_setup
	LDX	#7
k_lc	LDA	gargs,X
	STA	VK+$84,X
	DEX
	BPL	k_lc
	STA	VK+$82			; go
	RTS

K_TRI
	LDX	#7
	JSR	k_getargs
	LDA	gargs+12		; colour
	STA	VK+$70
	LDA	#7
	JSR	k_blt_setup
	LDX	#11
k_tc	LDA	gargs,X
	STA	VK+$84,X
	DEX
	BPL	k_tc
	STA	VK+$82
	RTS

K_PALETTE
	LDX	#4
	JSR	k_getargs
	LDA	gargs+0
	STA	VK+$06			; PALIDX
	LDA	gargs+2
	STA	VK+$07
	LDA	gargs+4
	STA	VK+$08
	LDA	gargs+6
	STA	VK+$09			; commits
	RTS


; ---- sprites (K4510): SPRITE n,x,y   SPRDEF n,page,w,h,bpp   SPROFF n ----
; The attribute table lives at SPRTAB ($03F800); each statement re-points
; VICKY at it and enables sprites, so the ROM's VIDEO restore (which turns
; sprites off) is undone by the next sprite statement. The table is cleared
; once (gsprinit, reset at cold start).

; fp = SPRTAB + n*16 (n = gargs 0), table pointed, sprites on
k_spr_base
	LDA	gsprinit
	BNE	k_sb_ok
	; clear the table with the DMA: 2 KB of zeroes
	STZ	DMA+0			; fill value 0
	LDA	#<SPRTAB
	STA	DMA+4
	LDA	#>SPRTAB
	STA	DMA+5
	LDA	#^SPRTAB
	STA	DMA+6
	STZ	DMA+7
	STZ	DMA+8
	LDA	#8
	STA	DMA+9			; $0800 = 2 KB
	STZ	DMA+10
	STZ	DMA+11
	LDA	#2
	STA	DMA+12			; fill
	LDA	#1
	STA	gsprinit
k_sb_ok
	LDA	#<SPRTAB		; VICKY: table pointer + enable (cheap, every call)
	STA	VK+$0A
	LDA	#>SPRTAB
	STA	VK+$0B
	LDA	#^SPRTAB
	STA	VK+$0C
	STZ	VK+$0D
	LDA	#1
	STA	VK+$0E
	LDA	gargs			; n
	AND	#$7F
	ASL
	ASL
	ASL
	ASL				; n*16 (n<128 so it fits 11 bits)
	STA	sprfp
	LDA	gargs
	AND	#$7F
	LSR
	LSR
	LSR
	LSR				; n>>4 = high bits of n*16
	CLC
	ADC	#>SPRTAB
	STA	sprfp+1
	LDA	#^SPRTAB
	STA	sprfp+2
	STZ	sprfp+3
	RTS

; SPRITE n,x,y : position and enable
K_SPRITE
	LDX	#3
	JSR	k_getargs
	JSR	k_spr_base
	LDA	gargs+2			; X
	.byte	$EA
	STA	(sprfp)			; entry+0 (Z=0)
	.byte	$A3, $01		; LDZ #1
	LDA	gargs+3
	.byte	$EA
	STA	(sprfp)
	.byte	$A3, $02		; LDZ #2: Y
	LDA	gargs+4
	.byte	$EA
	STA	(sprfp)
	.byte	$A3, $03
	LDA	gargs+5
	.byte	$EA
	STA	(sprfp)
	.byte	$A3, $08		; ctrl |= enable
	.byte	$EA
	LDA	(sprfp)
	ORA	#$01
	.byte	$EA
	STA	(sprfp)
	.byte	$A3, $00		; Z = 0 again
	RTS

; SPROFF n : K_SPROFF lives in the $BD00 tail (k4510basic.asm) -- the slice is full

; SPRDEF n,page,w,h,bpp : data = page*256 (28-bit reach from a 16-bit
; argument); w,h in pixels from {8,16,32,64}; bpp 4 or 8. Z-slot 3, no flips.
K_SPRDEF
	LDX	#5
	JSR	k_getargs
	JSR	k_spr_base
	.byte	$A3, $05		; DATA pointer = page << 8: bytes 5,6 of the entry get the page
	LDA	gargs+2
	.byte	$EA
	STA	(sprfp)
	.byte	$A3, $06
	LDA	gargs+3
	.byte	$EA
	STA	(sprfp)
	.byte	$A3, $04		; data low byte = 0
	LDA	#0
	.byte	$EA
	STA	(sprfp)
	.byte	$A3, $07
	.byte	$EA
	STA	(sprfp)
	; size byte: w code | h code << 2
	LDA	gargs+4			; w
	JSR	k_sizecode
	STA	gargs+4
	LDA	gargs+6			; h
	JSR	k_sizecode
	ASL
	ASL
	ORA	gargs+4
	.byte	$A3, $09
	.byte	$EA
	STA	(sprfp)
	; ctrl: keep enable bit, set bpp + Z=3
	.byte	$A3, $08
	.byte	$EA
	LDA	(sprfp)
	AND	#$01			; keep enable only
	ORA	#$30			; Z = 3
	LDX	gargs+8			; bpp
	CPX	#8
	BNE	k_sd_4bpp
	ORA	#$02
k_sd_4bpp
	.byte	$EA
	STA	(sprfp)
	.byte	$A3, $0A		; palofs = 1 for 4 bpp (colours 16-31), 0 for 8 bpp
	LDA	#1
	CPX	#8
	BNE	k_sd_po
	LDA	#0
k_sd_po
	.byte	$EA
	STA	(sprfp)
	.byte	$A3, $00
	RTS

; 8/16/32/64 -> 0/1/2/3 (anything else: 1)
k_sizecode
	CMP	#8
	BEQ	k_sc0
	CMP	#32
	BEQ	k_sc2
	CMP	#64
	BEQ	k_sc3
	LDA	#1
	RTS
k_sc0
	LDA	#0
	RTS
k_sc2
	LDA	#2
	RTS
k_sc3
	LDA	#3
	RTS
