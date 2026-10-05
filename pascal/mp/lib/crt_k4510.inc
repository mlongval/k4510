(* [K4510] CRT on JIM, the terminal at $DA00: a VT100 in hardware.
   Everything goes to it as the byte stream a terminal reads -- GotoXY is
   ESC[y;xH, TextColor an SGR -- not as stores into its registers, so a
   program's screen can be carried wherever JIM's stream goes (2026-10-05,
   the jim-everywhere branch: "Have everything DEFAULT to writing via JIM").
   The colours are the machine's palette entries, which xterm's numbering
   does not reach all of: ESC[?4510h makes 38;5;n entry n itself.  WhereX
   and WhereY still read JIM's cursor, and the keys come from the keyboard
   device. *)

procedure crt_str(const s: string);
var i: byte;
begin
	for i := 1 to length(s) do poke($DA00, ord(s[i]));
end;

procedure CursorOff;
(*
@description: Hide cursor
*)
begin
	crt_str(#27'[?25l');
end;


procedure CursorOn;
(*
@description: Display cursor
*)
begin
	crt_str(#27'[?25h');
end;


procedure ClrScr;
(*
@description: Clear screen
*)
begin
	crt_str(#27'[2J'#27'[H');
end;


procedure DelLine;
(*
@description: Delete line at cursor position
*)
begin
 asm
	lda #27
	sta k4_term
	lda #'['
	sta k4_term
	lda #'M'
	sta k4_term
 end;
end;


procedure InsLine;
(*
@description: Insert an empty line at cursor position
*)
begin
 asm
	lda #27
	sta k4_term
	lda #'['
	sta k4_term
	lda #'L'
	sta k4_term
 end;
end;


function ReadKey: char; assembler;
(*
@description: Read key from keybuffer (waits)

@returns: char
*)
asm
getkey	bit k4_kbdst
	bpl getkey
	lda k4_kbd
	sta Result
end;


procedure TextBackground(a: byte); assembler;
(*
@description: Set text background

@param: a - palette index 0..15
*)
asm
	lda #27
	sta k4_term
	lda #'['
	sta k4_term
	lda #'?'
	sta k4_term
	lda #'4'
	sta k4_term
	lda #'5'
	sta k4_term
	lda #'1'
	sta k4_term
	lda #'0'
	sta k4_term
	lda #'h'
	sta k4_term
	lda #27
	sta k4_term
	lda #'['
	sta k4_term
	lda #'4'
	sta k4_term
	lda #'8'
	sta k4_term
	lda #';'
	sta k4_term
	lda #'5'
	sta k4_term
	lda #';'
	sta k4_term
	lda a
	ldy #'0'		; A as three digits: a VT100 reads 006 as 6
hb	cmp #100
	bcc hdb
	sbc #100
	iny
	bne hb
hdb	sty k4_term
	ldy #'0'
tb	cmp #10
	bcc tdb
	sbc #10
	iny
	bne tb
tdb	sty k4_term
	ora #'0'
	sta k4_term
	lda #'m'
	sta k4_term
end;


procedure TextColor(a: byte); assembler;
(*
@description: Set text color

@param: a - palette index 0..15
*)
asm
	lda #27
	sta k4_term
	lda #'['
	sta k4_term
	lda #'?'
	sta k4_term
	lda #'4'
	sta k4_term
	lda #'5'
	sta k4_term
	lda #'1'
	sta k4_term
	lda #'0'
	sta k4_term
	lda #'h'
	sta k4_term
	lda #27
	sta k4_term
	lda #'['
	sta k4_term
	lda #'3'
	sta k4_term
	lda #'8'
	sta k4_term
	lda #';'
	sta k4_term
	lda #'5'
	sta k4_term
	lda #';'
	sta k4_term
	lda a
	ldy #'0'		; A as three digits: a VT100 reads 006 as 6
hf	cmp #100
	bcc hdf
	sbc #100
	iny
	bne hf
hdf	sty k4_term
	ldy #'0'
tf	cmp #10
	bcc tdf
	sbc #10
	iny
	bne tf
tdf	sty k4_term
	ora #'0'
	sta k4_term
	lda #'m'
	sta k4_term
end;


procedure Delay(count: word); assembler;
(*
@description: Waits a specified number of milliseconds (in frames of 16.7 ms)

@param: count - number of milliseconds
*)
asm
loop	lda count
	ora count+1
	beq stop

	lda $D50D
w1	cmp $D50D
	beq w1

	lda count
	sec
	sbc #17
	sta count
	lda count+1
	sbc #0
	sta count+1
	bpl loop

stop
end;


function Keypressed: Boolean; assembler;
(*
@description: Check if there is a keypress in the keybuffer

@returns: TRUE key has been pressed
@returns: FALSE otherwise
*)
asm
	lda k4_kbdst
	rol @
	lda #0
	rol @
	sta Result
end;


procedure GotoXY(x,y: byte); assembler;
(*
@description:
Set cursor position on screen.

GotoXY positions the cursor at (X,Y), X in horizontal, Y in vertical direction relative to

the origin of the current window. The origin is located at (1,1), the upper-left corner of the window.

@param: x - horizontal positions (1..80)
@param: y - vertical positions (1..60)
*)
asm				; ESC[y;xH, and 0 is 1 as a VT100 has it
	lda #27
	sta k4_term
	lda #'['
	sta k4_term
	lda y
	ldy #'0'		; A as three digits: a VT100 reads 006 as 6
hy	cmp #100
	bcc hdy
	sbc #100
	iny
	bne hy
hdy	sty k4_term
	ldy #'0'
ty	cmp #10
	bcc tdy
	sbc #10
	iny
	bne ty
tdy	sty k4_term
	ora #'0'
	sta k4_term
	lda #';'
	sta k4_term
	lda x
	ldy #'0'		; A as three digits: a VT100 reads 006 as 6
hx	cmp #100
	bcc hdx
	sbc #100
	iny
	bne hx
hdx	sty k4_term
	ldy #'0'
tx	cmp #10
	bcc tdx
	sbc #10
	iny
	bne tx
tdx	sty k4_term
	ora #'0'
	sta k4_term
	lda #'H'
	sta k4_term
end;


function WhereX: byte; assembler;
(*
@description: Return X (horizontal) cursor position

@returns: byte (1..80)
*)
asm
	lda k4_term_cx
	inc @
	sta Result
end;


function WhereY: byte; assembler;
(*
@description: Return Y (vertical) cursor position

@returns: byte (1..60)
*)
asm
	lda k4_term_cy
	inc @
	sta Result
end;


procedure ClrEol;
(*
@description:
ClrEol clears the current line, starting from the cursor position, to the end of the window.

The cursor doesn't move.
*)
begin
 asm
	lda #27
	sta k4_term
	lda #'['
	sta k4_term
	lda #'K'
	sta k4_term
 end;
end;


procedure TextMode(Mode: byte); assembler;
(*
@description: The ROM's text mode and palette back, JIM reset (DECSTR: its modes, ?4510 among them)

@param: Mode - unused value
*)
asm
	txa:pha
	jsr k4_video
	pla:tax
	lda #27
	sta k4_term
	lda #'['
	sta k4_term
	lda #'!'
	sta k4_term
	lda #'p'
	sta k4_term
	lda #27
	sta k4_term
	lda #'['
	sta k4_term
	lda #'H'
	sta k4_term
end;


procedure NoSound; assembler;
(*
@description: Silence every channel of the sound sequencer
*)
asm
	lda #$80
	sta k4_seq
end;


procedure Sound(Chan,Freq,Dist,Vol: byte); assembler;
(*
@description: Plays a note through the sound sequencer ($D5E0), which sounds on the OPL2: Chan 0..2 a tone channel (3 is the noise channel), Freq the pitch in quarter semitones (53 = middle C, 4 per semitone), Dist ignored, Vol 0..15. The note holds until NoSound or the next Sound on that channel.
*)
asm
	lda Chan
	inc @			; 0..2 -> sequencer channels 1..3 (tones); 3 -> 0 (noise)
	and #3
	ora #$10		; flush the channel first: this note now
	sta k4_seq
	lda #0
	sub Vol			; the amplitude is signed: 0 to -15
	sta k4_seq+1
	lda Freq
	sta k4_seq+2
	lda #255		; hold
	sta k4_seq+3
end;
