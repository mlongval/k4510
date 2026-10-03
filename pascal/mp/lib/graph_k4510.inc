(* [K4510] GRAPH on VICKY. See graphh_k4510.inc. *)

var	gfx_bk, gfx_ctrl: byte;
	MULA: cardinal absolute $D770;		(* the MEGA65-style multiplier: A * B -> MULOUT *)
	MULB: cardinal absolute $D774;
	MULOUT: cardinal absolute $D778;
	VREG: array[0..255] of byte absolute $D000;
	DMAS: cardinal absolute $D200;
	DMAD: cardinal absolute $D204;
	DMAL: cardinal absolute $D208;
	DMAC: byte absolute $D20C;

procedure gfx_blit(op: byte); assembler;   (* the whole bitmap as the blitter's destination, colour as source *)
asm
	lda GetColor
	sta $D070
	stz $D071
	stz $D072
	stz $D073
	stz $D074			; DST $200000
	stz $D075
	lda #$20
	sta $D076
	stz $D077
	mwa #640 $D078			; W
	mwa #480 $D07A			; H
	mwa #640 $D07E			; DS
	lda op
	sta $D080
	lda #1
	sta $D082
end;


procedure ClearDevice;
begin
	DMAS := 0; DMAD := GFX_BASE; DMAL := GFX_W * GFX_H; DMAC := 2;
end;


procedure InitGraph(mode: byte); overload;
(*
@description:
Init graphics mode: VICKY layer 1 as a 640x480 8-bit bitmap over the console
*)
begin
	ScreenWidth := GFX_W;
	ScreenHeight := GFX_H;
	VREG[$21] := 0; VREG[$22] := 0; VREG[$23] := 0; VREG[$24] := 0; VREG[$25] := 0;   (* palette offset, scroll *)
	VREG[$26] := lo(GFX_W); VREG[$27] := hi(GFX_W);                                  (* stride *)
	VREG[$28] := 0; VREG[$29] := 0; VREG[$2A] := $20; VREG[$2B] := 0;                (* data at $200000 *)
	ClearDevice;
	gfx_ctrl := VREG[0];
	VREG[0] := gfx_ctrl and $F9;                                                     (* 640x480, no line doubling: the console's MODE 1 is 640x240 *)
	VREG[$20] := $19;                                                                (* enable | bitmap | 8 bpp *)
	GetColor := 1;
	CurrentX := 0; CurrentY := 0;
end;


procedure InitGraph(driver, mode: byte; dev: PString); overload;
(*
@description:
Init graphics mode
*)
begin

 InitGraph(mode);

end;


procedure SetBkColor(color: byte); assembler;
(*
@description:
Sets the background colour: VICKY's ground, seen where neither text nor picture is
*)
asm
	lda color
	sta gfx_bk
	sta $D001
end;


procedure SetColor(color: byte); assembler;
(*
@description:
Sets the drawing colour (a palette index; 0 draws transparent)
*)
asm
	lda color
	sta GetColor
end;


procedure PutPixel(x,y: smallint); assembler; overload;
(*
@description:
Puts a point at (X,Y) in the current colour; off-screen points are dropped
*)
asm
	lda y+1
	bmi done
	cmp #2
	bcs done
	bne yok
	lda y
	cmp #480-256
	bcs done
yok	lda x+1
	bmi done
	cmp #3
	bcs done
	cmp #2
	bne xok
	lda x
	cmp #640-512
	bcs done
xok	mwa y $D770			; y * 640
	stz $D772
	stz $D773
	mwa #640 $D774
	stz $D776
	stz $D777
	clc
	lda $D778
	adc x
	sta :bp
	lda $D779
	adc x+1
	sta :bp+1
	lda $D77A
	adc #$20
	sta :bp2
	stz :bp2+1
	lda GetColor
	dta $A3,$00			; LDZ #0
	dta $EA,$92,:bp			; STA [bp],Z
	stz :bp			; bp's low byte back to 0: Mad Pascal's (:bp),y array code assumes it (2026-10-03)
done
end;


procedure PutPixel(x,y: smallint; color: byte); overload;
(*
@description:
Puts a point at (X,Y) using color Color
*)
begin
    SetColor(color);
    PutPixel(x,y);
end;


function GetPixel(x,y: smallint): byte; assembler;
(*
@description:
Return the colour of a pixel (0 off screen)
*)
asm
	lda #0
	sta Result
	lda y+1
	bmi done
	cmp #2
	bcs done
	bne yok
	lda y
	cmp #480-256
	bcs done
yok	lda x+1
	bmi done
	cmp #3
	bcs done
	cmp #2
	bne xok
	lda x
	cmp #640-512
	bcs done
xok	mwa y $D770
	stz $D772
	stz $D773
	mwa #640 $D774
	stz $D776
	stz $D777
	clc
	lda $D778
	adc x
	sta :bp
	lda $D779
	adc x+1
	sta :bp+1
	lda $D77A
	adc #$20
	sta :bp2
	stz :bp2+1
	dta $A3,$00			; LDZ #0
	dta $EA,$B2,:bp			; LDA [bp],Z
	sta Result
	stz :bp			; bp's low byte back to 0: Mad Pascal's (:bp),y array code assumes it (2026-10-03)
done
end;


procedure DrawLine(x0, y0, x1, y1: smallint);
begin
	asm
		mwa x0 $D084
		mwa y0 $D086
		mwa x1 $D088
		mwa y1 $D08A
	end;
	gfx_blit(6);
end;


procedure FillTriangle(x0, y0, x1, y1, x2, y2: smallint);
begin
	asm
		mwa x0 $D084
		mwa y0 $D086
		mwa x1 $D088
		mwa y1 $D08A
		mwa x2 $D08C
		mwa y2 $D08E
	end;
	gfx_blit(7);
end;


procedure FillBar(x0, y0, x1, y1: smallint);
var t: smallint;
    w, h: word;
begin
	if x0 > x1 then begin t := x0; x0 := x1; x1 := t; end;
	if y0 > y1 then begin t := y0; y0 := y1; y1 := t; end;
	if (x1 < 0) or (y1 < 0) or (x0 >= GFX_W) or (y0 >= GFX_H) then exit;
	if x0 < 0 then x0 := 0;
	if y0 < 0 then y0 := 0;
	if x1 >= GFX_W then x1 := GFX_W - 1;
	if y1 >= GFX_H then y1 := GFX_H - 1;
	MULA := y0; MULB := GFX_W;
	asm
		lda GetColor
		sta $D070
		stz $D071
		stz $D072
		stz $D073
		clc
		lda $D778
		adc x0
		sta $D074
		lda $D779
		adc x0+1
		sta $D075
		lda $D77A
		adc #$20
		sta $D076
		stz $D077
	end;
	w := x1 - x0 + 1; h := y1 - y0 + 1;	(* into a word first: hi(x1 - x0 + 1) compiled to 0, so no bar was wider than 255 (2026-10-03) *)
	VREG[$78] := lo(w); VREG[$79] := hi(w);
	VREG[$7A] := lo(h); VREG[$7B] := hi(h);
	VREG[$7E] := lo(GFX_W); VREG[$7F] := hi(GFX_W);
	VREG[$80] := 2; VREG[$82] := 1;
end;


procedure LineTo(x, y: smallint);
(*
@description:
Draw a line starting from current position to a given point
*)
begin
	DrawLine(CurrentX, CurrentY, x, y);
	CurrentX := x;
	CurrentY := y;
end;


procedure LineRel(dx, dy: smallint);
(*
@description:
LineRel draws a line starting from the current pointer position to the point(DX,DY),
relative to the current position, in the current line style and color.
The Current Position is set to the endpoint of the line.
*)
begin

 LineTo(CurrentX + dx, CurrentY + dy)

end;


procedure CloseGraph; assembler;
(*
@description:
The bitmap layer off, the console's video mode back
*)
asm
	stz $D020
	lda gfx_ctrl
	sta $D000
end;


function gfx_font(a: cardinal): byte; assembler;   (* a byte of the ROM's font at $010000 *)
asm
	lda a
	sta :bp
	lda a+1
	sta :bp+1
	lda a+2
	sta :bp2
	lda a+3
	sta :bp2+1
	dta $A3,$00
	dta $EA,$B2,:bp
	sta Result
	stz :bp			; bp's low byte back to 0: Mad Pascal's (:bp),y array code assumes it (2026-10-03)
end;


procedure OutTextXY(x,y: smallint; s: string);
(*
@description:
Text into the picture, 8x8, the console's font, in the current colour
*)
var i, r, b, g: byte;
begin
	for i := 1 to length(s) do begin
		for r := 0 to 7 do begin
			g := gfx_font($010000 + byte(s[i]) * 8 + r);
			for b := 0 to 7 do
				if (g and ($80 shr b)) <> 0 then PutPixel(x + b, y + r);
		end;
		x := x + 8;
	end;
end;
