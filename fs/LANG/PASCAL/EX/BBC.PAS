unit bbc;
(*
* @type: unit
* @name: The Tube ULA's graphics and sound, for the ports of /LANG/BBCBASIC/EX.
*
* @description:
* BBC BASIC on the K4510 draws through the Tube ULA (core/io.c): BBC
* coordinates (1280x1024, origin bottom left) land on the 640x480 bitmap
* at $200000 -- x/2, 479-y*15/32 -- BBC logical colours live in palette
* entries 16-31 (logical 0 is pixel 0, transparent, so the console shows
* through), CIRCLE is filled by scanlines two BBC units apart, sprites
* are VICKY's (table at $260000, pictures at $261000 + n*4K) captured
* from the bitmap, and SOUND is the sequencer at $D5E0.  This unit does
* the same sums, so a Pascal port of a .BBC program draws and sounds as
* the original does.  BMode(n) is MODE n (1 = four colours, 2 = sixteen),
* BMode(7) is MODE 7: the bitmap off and the console back.  Every routine
* is the BBC word with a B in front: BGcol, BMove, BDraw, BPlot, BClg,
* BCircleFill, BPalette (VDU 19), BSprite... (VDU 23,27), BSound (SOUND),
* BWait (WAIT, centiseconds), BRnd (RND(n), 1..n).
*)

interface

uses graph, k4510;

const
	BSPRTAB = $260000;	(* the sprite attribute table, 128 x 16 bytes *)
	BSPRDAT = $261000;	(* sprite n's picture at BSPRDAT + n * 4096 *)

var	BFg: byte = 17;		(* the current GCOL foreground, a palette index 16..31 *)
	BBg: byte = 16;
	BX: array[0..2] of smallint;	(* the graphics cursor and the two before it, BBC units *)
	BY: array[0..2] of smallint;

procedure BMode(n: byte);
(* @description: MODE n: the bitmap up with the mode's default palette, the text cleared (7: off, the console back) *)
procedure BGcol(c: byte);
(* @description: GCOL 0,c: c < 128 the foreground, 128+ the background (CLG's colour) *)
procedure BPalette(l, p: byte);
(* @description: VDU 19,l,p,0,0,0: logical colour l shows physical colour p (0-7, bits = R G B) *)
procedure BPaletteRGB(l, r, g, b: byte);
(* @description: VDU 19,l,16,r,g,b: logical colour l as an RGB triple *)
procedure BOrigin(x, y: smallint);
(* @description: ORIGIN x,y *)
procedure BMove(x, y: smallint);
(* @description: MOVE x,y: the graphics cursor, nothing drawn *)
procedure BDraw(x, y: smallint);
(* @description: DRAW x,y: a line from the cursor, in the foreground *)
procedure BPlot(k: byte; x, y: smallint);
(* @description: PLOT k,x,y: 4/5 move/draw, 69 a point, 85 a triangle with the two points before, 101 a rectangle with the point before, 237 the selected sprite *)
procedure BClg;
(* @description: CLG: the bitmap filled with the background *)
procedure BCircle(x, y, r: smallint);
(* @description: CIRCLE x,y,r *)
procedure BCircleFill(x, y, r: smallint);
(* @description: CIRCLE FILL x,y,r *)
procedure BSpriteSelect(n: byte);
(* @description: VDU 23,27,0,n|: sprite n is the one PLOT 237 moves *)
procedure BSpriteCapture(n, w, h: byte);
(* @description: VDU 23,27,1,n,w,h|: sprite n's picture is the w x h pixels (8/16/32/64) whose bottom-left is the graphics cursor *)
procedure BSpriteHide(n: byte);
(* @description: VDU 23,27,2,n| *)
procedure BSpriteFlip(n, f: byte);
(* @description: VDU 23,27,3,n,f|: f bit0 horizontal, bit1 vertical *)
procedure BSpriteShow(n, m: byte);
(* @description: VDU 23,27,5,n,m|: sprite n shows sprite m's picture *)
procedure BSpritePlot(x, y: smallint);
(* @description: PLOT 237,x,y: the selected sprite with its bottom-left at x,y *)
procedure BSpritesOff;
(* @description: the sprite layer off (MODE 7 does it too) *)
procedure BSound(ch: byte; amp: shortint; pitch, dur: byte);
(* @description: SOUND ch,amp,pitch,dur: ch 0 noise, 1-3 tones; amp 0 to -15; pitch in quarter semitones, 53 = middle C; dur in 1/20 s, 255 holds *)
procedure BSoundOff;
(* @description: every channel silent, the queues emptied *)
procedure BWait(cs: word);
(* @description: WAIT cs: cs centiseconds, in frames *)
function BRnd(n: word): word;
(* @description: RND(n): 1..n, from a 32-bit xorshift (Mad Pascal's Random is a 16-bit LCG whose consecutive pairs fall on a lattice; the lines of KALEID showed it) *)
procedure BRandomize;
(* @description: a fresh seed from the frame counter and the clock *)
function BSx(x: smallint): smallint;
(* @description: a BBC x as a bitmap column *)
function BSy(y: smallint): smallint;
(* @description: a BBC y as a bitmap row *)

implementation

var	BSeqCh: byte absolute $D5E0;
	BSeqAmp: byte absolute $D5E1;
	BSeqPitch: byte absolute $D5E2;
	BSeqDur: byte absolute $D5E3;
	bspr_w: array[0..127] of byte;
	bspr_h: array[0..127] of byte;
	bspr_cur: byte;
	bspr_on: boolean;
	bon: boolean;
	box, boy: smallint;

(* x >> 1 and 479 - (y * 15 >> 5), as the ULA has them (C's >> rounds
   toward minus infinity): shifts on words -- a smallint div and * cost
   thousands of cycles each in this Mad Pascal, and a circle calls these
   for every row. *)
function BSx(x: smallint): smallint;
var t: word;
begin
	if x >= 0 then Result := word(x) shr 1
	else begin t := word(-x); Result := -((t + 1) shr 1); end;
end;

function BSy(y: smallint): smallint;
var t: word;
begin
	if y >= 0 then begin t := word(y); t := ((t shl 4) - t) shr 5; Result := 479 - t; end
	else begin t := word(-y); t := ((t shl 4) - t + 31) shr 5; Result := 479 + t; end;
end;

var	bseed: cardinal = $2545F491;

procedure BRandomize;
begin
	bseed := SYS[4];				(* a read latches the clock *)
	bseed := (cardinal(SYS[5]) shl 24) or (cardinal(SYS[6]) shl 16) or (cardinal(SYS[$0D]) shl 8) or SYS[$0E] or $00010001;
end;

function BRnd(n: word): word;
begin
	bseed := bseed xor (bseed shl 13);
	bseed := bseed xor (bseed shr 17);
	bseed := bseed xor (bseed shl 5);
	if n = 0 then Result := 0 else Result := (bseed shr 8) mod n + 1;
end;

function bpix(c: byte): byte;
begin
	if c = 16 then Result := 0 else Result := c;
end;

function bmul(a, b: cardinal): cardinal;		(* the MEGA65-style multiplier at $D770 *)
begin
	MULTINA := a; MULTINB := b; Result := MULTOUT;
end;

procedure BPalette(l, p: byte);
var r, g, b: byte;
begin
	r := 0; g := 0; b := 0;
	if (p and 1) <> 0 then r := 255;
	if (p and 2) <> 0 then g := 255;
	if (p and 4) <> 0 then b := 255;
	BPaletteRGB(l, r, g, b);
end;

procedure BPaletteRGB(l, r, g, b: byte);
begin
	VICKY[6] := 16 + (l and 15);
	VICKY[7] := r; VICKY[8] := g; VICKY[9] := b;
end;

procedure BSpritesOff;
begin
	if bspr_on then begin VICKY[$0E] := 0; bspr_on := false; end;
end;

procedure bspr_init;
begin
	DmaFill(BSPRTAB, 128 * 16, 0);
	FillChar(bspr_w, 128, 8); FillChar(bspr_h, 128, 8);
	VICKY[$0A] := 0; VICKY[$0B] := 0; VICKY[$0C] := $26; VICKY[$0D] := 0;
	VICKY[$0E] := 1; bspr_on := true; bspr_cur := 0;
end;

procedure BMode(n: byte);
var i, p: byte;
begin
	BSoundOff;
	if (n = 3) or (n = 6) or (n = 7) then begin
		BSpritesOff;
		if bon then begin CloseGraph; bon := false; end;
		TERM_CTL := 2;
		exit;
	end;
	InitGraph(0);
	TERM_CTL := 2;			(* the console's MODE clears the text too *)
	bon := true;
	for i := 0 to 15 do begin
		if n = 2 then p := i and 7
		else if (n = 1) or (n = 5) then begin
			case i and 3 of
				0: p := 0; 1: p := 1; 2: p := 3;
			else p := 7;
			end;
		end
		else if (i and 1) <> 0 then p := 7 else p := 0;
		BPalette(i, p);
	end;
	if n = 2 then BFg := 16 + 7
	else if (n = 1) or (n = 5) then BFg := 16 + 3
	else BFg := 16 + 1;
	BBg := 16;
	box := 0; boy := 0;
	for i := 0 to 2 do begin BX[i] := 0; BY[i] := 0; end;
	BClg;
end;

procedure BGcol(c: byte);
begin
	if (c and $80) <> 0 then BBg := 16 + (c and 15) else BFg := 16 + (c and 15);
end;

procedure BOrigin(x, y: smallint);
begin
	box := x; boy := y;
end;

procedure BClg;
begin
	if bon then DmaFill(GFX_BASE, GFX_W * GFX_H, bpix(BBg));
end;

procedure brect(x0, y0, x1, y1: smallint; c: byte);	(* pixel coordinates, any order *)
begin
	SetColor(bpix(c));
	FillBar(x0, y0, x1, y1);
end;

(* CIRCLE as the ULA does it: scanlines two BBC units apart, each a
   rectangle from -s to +s where s = round(sqrt(r^2 - d^2)); s is kept
   by an integer walk (no floating point: a MathSqrt a row made a
   40-unit ball cost half a frame). *)
procedure bcircdraw(cx, cy, r: smallint; c: byte; fill: boolean);
var r2, q, s2: cardinal;
    d, s, px, py, x0, x1: smallint;
begin
	if r < 0 then r := -r;
	r2 := bmul(r, r);
	s := 0;
	d := -r;
	while d <= r do begin
		if d < 0 then q := bmul(-d, -d) else q := bmul(d, d);
		q := (r2 - q) shl 2;				(* 4 (r^2 - d^2) *)
		s2 := 2 * s + 1;
		while bmul(s2, s2) <= q do begin inc(s); s2 := 2 * s + 1; end;	(* (s + 1/2)^2 <= r^2 - d^2: s is too small *)
		while s > 0 do begin
			s2 := 2 * s - 1;
			if bmul(s2, s2) > q then dec(s) else break;			(* (s - 1/2)^2 > r^2 - d^2: too big *)
		end;
		py := BSy(cy + d); x0 := BSx(cx - s); x1 := BSx(cx + s);
		if fill then
			brect(x0, py, x1, py, c)
		else begin
			brect(x0, py, x0, py, c);
			brect(x1, py, x1, py, c);
			px := BSx(cx + d);
			py := BSy(cy - s); brect(px, py, px, py, c);
			py := BSy(cy + s); brect(px, py, px, py, c);
		end;
		d := d + 2;
	end;
end;

procedure BCircle(x, y, r: smallint);
begin
	BMove(x, y); BMove(x + r, y);
	if bon then bcircdraw(x, y, r, BFg, false);
end;

procedure BCircleFill(x, y, r: smallint);
begin
	BMove(x, y); BMove(x + r, y);
	if bon then bcircdraw(x, y, r, BFg, true);
end;

procedure bpush(x, y: smallint);
begin
	BX[2] := BX[1]; BY[2] := BY[1];
	BX[1] := BX[0]; BY[1] := BY[0];
	BX[0] := x; BY[0] := y;
end;

procedure BMove(x, y: smallint);
begin
	bpush(box + x, boy + y);
end;

procedure BDraw(x, y: smallint);
begin
	BPlot(5, x, y);
end;

procedure BPlot(k: byte; x, y: smallint);
var c: byte;
    nx, ny, px0, py0, px1, py1, px2, py2: smallint;	(* the pixel coordinates, one call at a time: BSx(..), BSx(..) in one argument list went wrong *)
begin
	if (k and 4) <> 0 then begin nx := box + x; ny := boy + y; end
	else begin nx := BX[0] + x; ny := BY[0] + y; end;
	bpush(nx, ny);
	c := k and 3;
	if (c = 0) or not bon then exit;
	if c = 3 then c := BBg else c := BFg;
	px0 := BSx(BX[1]); py0 := BSy(BY[1]);
	px1 := BSx(nx); py1 := BSy(ny);
	case k shr 3 of
		29: BSpritePlot(nx, ny);
		8: begin SetColor(bpix(c)); DrawLine(px1, py1, px1, py1); end;
		10: begin SetColor(bpix(c));
			px2 := BSx(BX[2]); py2 := BSy(BY[2]);
			FillTriangle(px2, py2, px0, py0, px1, py1); end;
		12: brect(px0, py0, px1, py1, c);
		18: bcircdraw(BX[1], BY[1], nx - BX[1], c, false);
		19: bcircdraw(BX[1], BY[1], nx - BX[1], c, true);
	else
		if k < 64 then begin
			SetColor(bpix(c)); DrawLine(px0, py0, px1, py1);
		end;
	end;
end;

procedure BSpriteSelect(n: byte);
begin
	if not bon then exit;
	if not bspr_on then bspr_init;
	bspr_cur := n and 127;
end;

function bsize(v: byte): byte;
begin
	if v >= 64 then Result := 3 else if v >= 32 then Result := 2 else if v >= 16 then Result := 1 else Result := 0;
end;

procedure BSpriteCapture(n, w, h: byte);
var wc, hc, pw, ph, y: byte;
    sx, sy, py: smallint;
    e, a: cardinal;
begin
	if not bon then exit;
	if not bspr_on then bspr_init;
	n := n and 127;
	e := BSPRTAB + n * 16;
	wc := bsize(w); hc := bsize(h); pw := 8 shl wc; ph := 8 shl hc;
	sx := BSx(BX[0]); sy := BSy(BY[0]) - (ph - 1);
	a := BSPRDAT + cardinal(n) * 4096;
	for y := 0 to ph - 1 do begin
		py := sy + y;
		if (py >= 0) and (py < GFX_H) and (sx >= 0) and (sx + pw <= GFX_W) then
			DmaCopy(GFX_BASE + cardinal(py) * GFX_W + sx, a + y * pw, pw)
		else
			DmaFill(a + y * pw, pw, 0);
	end;
	FarPoke(e + 4, a and 255); FarPoke(e + 5, (a shr 8) and 255); FarPoke(e + 6, (a shr 16) and 255); FarPoke(e + 7, 0);
	FarPoke(e + 8, (FarPeek(e + 8) and 1) or $02 or $10);
	FarPoke(e + 9, wc or (hc shl 2));
	bspr_w[n] := pw; bspr_h[n] := ph;
	bspr_cur := n;
end;

procedure BSpriteHide(n: byte);
var e: cardinal;
begin
	if not bspr_on then exit;
	e := BSPRTAB + (n and 127) * 16;
	FarPoke(e + 8, FarPeek(e + 8) and $FE);
end;

procedure BSpriteFlip(n, f: byte);
var e: cardinal;
begin
	if not bspr_on then exit;
	e := BSPRTAB + (n and 127) * 16;
	FarPoke(e + 8, (FarPeek(e + 8) and $F3) or ((f and 3) shl 2));
end;

procedure BSpriteShow(n, m: byte);
var e, s: cardinal;
    i: byte;
begin
	if not bspr_on then exit;
	n := n and 127; m := m and 127;
	e := BSPRTAB + n * 16; s := BSPRTAB + m * 16;
	for i := 4 to 7 do FarPoke(e + i, FarPeek(s + i));
	FarPoke(e + 8, (FarPeek(e + 8) and 1) or (FarPeek(s + 8) and $FE));
	FarPoke(e + 9, FarPeek(s + 9));
	bspr_w[n] := bspr_w[m]; bspr_h[n] := bspr_h[m];
end;

procedure BSpritePlot(x, y: smallint);
var e: cardinal;
    sx, sy: smallint;
begin
	if not bspr_on then exit;
	e := BSPRTAB + bspr_cur * 16;
	sx := BSx(x); sy := BSy(y) - (bspr_h[bspr_cur] - 1);
	FarPoke(e, lo(sx)); FarPoke(e + 1, hi(sx));
	FarPoke(e + 2, lo(sy)); FarPoke(e + 3, hi(sy));
	FarPoke(e + 8, FarPeek(e + 8) or 1);
	VICKY[$0E] := 1;
end;

procedure BSound(ch: byte; amp: shortint; pitch, dur: byte);
begin
	BSeqCh := ch; BSeqAmp := byte(amp); BSeqPitch := pitch; BSeqDur := dur;
end;

procedure BSoundOff;
begin
	BSeqCh := $80;
end;

procedure BWait(cs: word);
begin
	Pause((cs * 6 + 5) div 10);
end;

end.
