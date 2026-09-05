unit k4510;
(*
* @type: unit
* @author: K4510 project
* @name: The K4510's chips, as typed absolute registers.
* @version: 0.1

* @description:
* Every device of the machine (core/io.h) as a variable at its address,
* plus the 45GS02's flat 28-bit memory (FarPeek/FarPoke), the DMA
* engine, the ROM's system calls (Shell, LoadFile, SaveFile) and JIM,
* the terminal. Register maps: the guide, chapter 11, and core/*.h.
*)

interface

const
	VICKY_BASE = $D000;
	SYS_BASE   = $D500;
	MATH_BASE  = $D700;
	TERM_BASE  = $DA00;

	FS_OPEN_READ = 1;  FS_OPEN_WRITE = 2; FS_READ = 3; FS_WRITE = 4; FS_CLOSE = 5;
	FS_DIR_FIRST = 6;  FS_DIR_NEXT = 7;   FS_STAT = 8; FS_LOAD = 9;  FS_SAVE = 10;
	FS_CHDIR = 11;     FS_MKDIR = 12;     FS_RM = 13;  FS_RMDIR = 14; FS_GETCWD = 15;

var
	VICKY: array[0..255] of byte absolute $D000;	(* @var VICKY registers *)
	KBD: byte absolute $D100;			(* @var the next key (pops), 0 if none *)
	KBDST: byte absolute $D101;			(* @var bit7 key waiting; bit0 shift, bit1 ctrl, bit2 alt *)
	KBDPEEK: byte absolute $D102;
	DMA_SRC: cardinal absolute $D200;		(* @var DMA source, 28-bit *)
	DMA_DST: cardinal absolute $D204;
	DMA_LEN: cardinal absolute $D208;
	DMA_CMD: byte absolute $D20C;			(* @var 1 copy, 2 fill (value = SRC byte 0), 3 swap *)
	FS_CMD: byte absolute $D300;
	FS_STATUS: byte absolute $D301;
	FS_NAMEPTR: cardinal absolute $D304;
	FS_ADDR: cardinal absolute $D308;
	FS_LEN: cardinal absolute $D30C;
	FS_SIZE: cardinal absolute $D310;
	SYS: array[0..255] of byte absolute $D500;
	SYS_FRAMES: byte absolute $D50D;		(* @var frames since reset, low byte *)
	SYS_FRAMES24: array[0..2] of byte absolute $D50D;
	MATH_F: array[0..7] of single absolute $D700;	(* @var the MATH unit's F0..F7 *)
	MATH_FOP: byte absolute $D720;
	MATH_FARG: byte absolute $D721;
	MATH_FFLAGS: byte absolute $D722;
	MATH_FI: integer absolute $D724;
	MULTINA: cardinal absolute $D770;		(* @var the MEGA65-style integer unit *)
	MULTINB: cardinal absolute $D774;
	MULTOUT: cardinal absolute $D778;
	DIVOUT: cardinal absolute $D76C;
	TUBE: array[0..3] of byte absolute $D800;
	NET: array[0..31] of byte absolute $D900;
	TERM: byte absolute $DA00;			(* @var JIM, the terminal: a byte of the VT100 stream *)
	TERM_ST: byte absolute $DA01;
	TERM_REP: byte absolute $DA02;
	TERM_KEY: byte absolute $DA03;
	TERM_CTL: byte absolute $DA04;
	TERM_COLS: byte absolute $DA05;
	TERM_ROWS: byte absolute $DA06;
	TERM_CX: byte absolute $DA09;
	TERM_CY: byte absolute $DA0A;
	TERM_FG: byte absolute $DA0B;
	TERM_BG: byte absolute $DA0C;
	TERM_FLAGS: byte absolute $DA0E;

function FarPeek(a: cardinal): byte; assembler;
(* @description: a byte from anywhere in the 256 MB (45GS02 flat load) *)
procedure FarPoke(a: cardinal; v: byte); assembler;
(* @description: a byte to anywhere in the 256 MB (45GS02 flat store) *)
procedure DmaCopy(src, dst, len: cardinal);
(* @description: the DMA engine copies len bytes, physical addresses; instant *)
procedure DmaFill(dst, len: cardinal; v: byte);
(* @description: the DMA engine fills len bytes with v *)
procedure Shell(const cmd: string);
(* @description: one shell line, as if typed (the ROM's SHELL call) *)
function LoadFile(const name: string; dest: cardinal): cardinal;
(* @description: a whole file to a physical address (the ROM's LOAD); the size, 0 on failure *)
function SaveFile(const name: string; src, len: cardinal): boolean;
(* @description: len bytes from a physical address to a file (the ROM's SAVE) *)
procedure WaitVBlank; assembler;
(* @description: waits for the next frame *)
procedure TermWrite(const s: string);
(* @description: a string straight to JIM (escape sequences included) *)

(* The MATH unit's transcendentals on SINGLE, one register write each
   (the arithmetic itself -- + - * / compare trunc round frac -- already
   runs there: the runtime's single routines are wired to $D700). *)
function MathSqrt(x: single): single;
function MathSin(x: single): single;
function MathCos(x: single): single;
function MathTan(x: single): single;
function MathAtan(x: single): single;
function MathAtan2(y, x: single): single;
function MathExp(x: single): single;
function MathLn(x: single): single;
function MathPow(x, y: single): single;
function MathFloor(x: single): single;

implementation

function FarPeek(a: cardinal): byte; assembler;
asm
	lda a
	sta :bp
	lda a+1
	sta :bp+1
	lda a+2
	sta :bp2
	lda a+3
	sta :bp2+1
	dta $A3,$00		; LDZ #0
	dta $EA,$B2,:bp		; NOP prefix + LDA (bp),Z = LDA [bp],Z: the 45GS02's flat 32-bit load
	sta Result
end;

procedure FarPoke(a: cardinal; v: byte); assembler;
asm
	lda a
	sta :bp
	lda a+1
	sta :bp+1
	lda a+2
	sta :bp2
	lda a+3
	sta :bp2+1
	lda v
	dta $A3,$00		; LDZ #0
	dta $EA,$92,:bp		; NOP prefix + STA (bp),Z = STA [bp],Z
end;

procedure DmaCopy(src, dst, len: cardinal);
begin
	DMA_SRC := src; DMA_DST := dst; DMA_LEN := len; DMA_CMD := 1;
end;

procedure DmaFill(dst, len: cardinal; v: byte);
begin
	DMA_SRC := v; DMA_DST := dst; DMA_LEN := len; DMA_CMD := 2;
end;

procedure Shell(const cmd: string);
var buf: array[0..95] of char;
    i: byte;
begin
	for i := 1 to length(cmd) do buf[i - 1] := cmd[i];
	buf[length(cmd)] := #0;
	asm
		lda <buf
		ldx >buf
		jsr k4_shell
	end;
end;

function LoadFile(const name: string; dest: cardinal): cardinal;
var buf: array[0..95] of char;
    i, st: byte;
begin
	for i := 1 to length(name) do buf[i - 1] := name[i];
	buf[length(name)] := #0;
	asm
		mwa #buf $F0
		lda dest
		sta $F2
		lda dest+1
		sta $F3
		lda dest+2
		sta $F4
		lda dest+3
		sta $F5
		jsr k4_load
		sta st
		lda $F6
		sta Result
		lda $F7
		sta Result+1
		lda $F8
		sta Result+2
		lda $F9
		sta Result+3
	end;
	if st <> 0 then Result := 0;
end;

function SaveFile(const name: string; src, len: cardinal): boolean;
var buf: array[0..95] of char;
    i, st: byte;
begin
	for i := 1 to length(name) do buf[i - 1] := name[i];
	buf[length(name)] := #0;
	asm
		mwa #buf $F0
		lda src
		sta $F2
		lda src+1
		sta $F3
		lda src+2
		sta $F4
		lda src+3
		sta $F5
		lda len
		sta $F6
		lda len+1
		sta $F7
		lda len+2
		sta $F8
		lda len+3
		sta $F9
		jsr k4_save
		sta st
	end;
	Result := st = 0;
end;

procedure WaitVBlank; assembler;
asm
	lda $D50D
w	cmp $D50D
	beq w
end;

procedure TermWrite(const s: string);
var i: byte;
begin
	for i := 1 to length(s) do TERM := byte(s[i]);
end;

function MathUnary(x: single; op: byte): single;
begin
	MATH_F[0] := x; MATH_FARG := 0; MATH_FOP := op; Result := MATH_F[0];
end;
function MathBinary(x, y: single; op: byte): single;
begin
	MATH_F[0] := x; MATH_F[1] := y; MATH_FARG := 1; MATH_FOP := op; Result := MATH_F[0];
end;
function MathSqrt(x: single): single; begin Result := MathUnary(x, 5); end;
function MathSin(x: single): single; begin Result := MathUnary(x, 6); end;
function MathCos(x: single): single; begin Result := MathUnary(x, 7); end;
function MathTan(x: single): single; begin Result := MathUnary(x, 8); end;
function MathAtan(x: single): single; begin Result := MathUnary(x, 9); end;
function MathAtan2(y, x: single): single; begin Result := MathBinary(y, x, 10); end;
function MathExp(x: single): single; begin Result := MathUnary(x, 11); end;
function MathLn(x: single): single; begin Result := MathUnary(x, 12); end;
function MathPow(x, y: single): single; begin Result := MathBinary(x, y, 13); end;
function MathFloor(x: single): single; begin Result := MathUnary(x, 16); end;

end.
