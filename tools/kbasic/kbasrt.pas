// kbasrt -- the runtime of K4510 BASIC (k4510-bas), compiled into every
// program it makes.  K4510-Ed, 2026-10-08.
//
// Written round Mad Pascal's corners (2026-10-08): strings are joined
// byte by byte (s + 'x' and 'lit' + s do not compile), an index is
// never "w - constant" (it reads the wrong element), no name here is a
// constant's or a procedure's elsewhere (Pascal ignores case).
unit kbasrt;

interface

uses crt, k4510;

var
  kb_file: string;                 // the .BAS, for MAKE.ERR
  kb_col: byte;                    // PRINT's column, for "," and TAB

// Strings travel as HANDLES: a byte naming one of NSLOT slots.  Mad Pascal
// passes a string a function returned through one shared buffer, so in
// SCat(LEFT$(..), RIGHT$(..)) the second result overwrote the first
// (2026-10-08: "S"+"T"+"U" printed STT).  A handle is a byte, and bytes
// do not share anything.  SPut fills the next slot; SGet reads one back.
function SPut(const s: string): byte;
function SGet(h: byte): string;
function SVar(const s: string): byte;
function SLit(const s: string): byte;
function SCat(a, b: byte): byte;
function SCh(c: smallint): byte;
function SNul: byte;
function SMid(h: byte; st, n: smallint): byte;
function SLeft(h: byte; n: smallint): byte;
function SRight(h: byte; n: smallint): byte;
function SUpper(h: byte): byte;
function SLower(h: byte): byte;
function SSpace(n: smallint): byte;
function SStrN(x: single): byte;
function SVal(h: byte): single;
function SAsc(h: byte): smallint;
function SLen(h: byte): smallint;
function SInstr(a, b: byte): smallint;
function SCmp(a, b: byte): smallint;
function SInkey: byte;

procedure PrintS(h: byte);
procedure PrintN(x: single);
procedure PrintTab;
procedure PrintTabTo(n: smallint);
procedure PrintSpc(n: smallint);
procedure PrintNL;

function KLine(prompt: byte): byte;
function KField(h: byte; n: byte): byte;

function KRnd: single;
function KInt(x: single): single;
function KFix(x: single): single;
function KN(x: single): single;
function KIv(x: smallint): smallint;
function KF(x: smallint): single;
function KAddr(x: single): word;
function KAbs(x: single): single;
function KSgn(x: single): single;
function KPow(a, b: single): single;
function KBool(b: boolean): single;
function KToI(x: single; line: word): smallint;
function KDiv(a, b: single; line: word): single;
function KIDiv(a, b: smallint; line: word): smallint;
function KMod(a, b: smallint; line: word): smallint;
function KIdx(i: smallint; max: smallint; line: word): word;
procedure KSleep(x: single);
procedure KColor(f, b: smallint);
procedure KLocate(r, c: smallint);
// The strings of recursive SUBs and FUNCTIONs: Mad Pascal gives a recursive
// routine fresh locals, but a string among them (or a string parameter)
// wrecks its stack (2026-10-08).  So those strings live here, a stack of
// LSLOT, LAlloc'd on entry and LFree'd on the way out.
function LAlloc(n: byte): byte;
procedure LFree(n: byte);
function LH(i: byte): byte;
procedure LSet(i: byte; h: byte);
procedure KVarInit(n: word);
function VH(i: word): byte;
procedure VSet(i: word; h: byte);

function KPeek(a: single): byte;
procedure KPoke(a: single; v: smallint);
procedure KPalette(i, r, g, b: smallint);
procedure KSprDef(n, page, w, h, bpp: smallint);
procedure KSprite(n, x, y: smallint);
procedure KSprOff(n: smallint);
procedure KError(line: word; const msg: string);
function SString(n, c: smallint): byte;
function SLtrim(h: byte): byte;
function SRtrim(h: byte): byte;
function SHex(x: single): byte;

procedure KSound(hz, sec: single);
procedure KSoundOff;
procedure KPlay(h: byte; ch: smallint);
function KJoy: byte;
procedure KWaitFrame(n: smallint);
function KTimer: single;
function KFrames: single;
procedure KMovSpr(n, x, y: smallint);
procedure KSprVel(n, dx, dy: smallint);
function KSprX(n: smallint): smallint;
function KSprY(n: smallint): smallint;
function KHit(n: smallint): boolean;
function KHitBg(n: smallint): boolean;

procedure KOpen(k, h, mode: byte; line: word);
procedure KFPrintS(k, h: byte; line: word);
procedure KFPrintN(k: byte; x: single; line: word);
procedure KFComma(k: byte; line: word);
procedure KFNL(k: byte; line: word);
function KFLine(k: byte; line: word): byte;
function KFEof(k: byte): boolean;
procedure KClose(k: byte);
procedure KCloseAll;

procedure KUsingStart(h: byte);
procedure KUsingN(x: single);
procedure KUsingS(h: byte);
procedure KUsingEnd;

procedure KStart;
procedure KEnd;

implementation

const
  ERRBUF = $4F0000;                // far memory for MAKE.ERR's line
  SPRTAB = $4E0000;                // the sprite table, 128 x 16 bytes (not EhBASIC's $03F800: under K/OS's text it wiped the screen)
  NSLOT = 48;
  SLOTS = $4C0000;                 // the string slots (NSLOT x 256 bytes)

var
  VK: array[0..255] of byte absolute $D000;
  kb_lp: byte;
  kb_sf: byte;                     // the lowest slot SPut may reuse: a recursive call's caller keeps the ones below
  kb_sprinit: boolean;
  kb_u, kb_v: string;              // the handle wrappers' scratch (one each would cost 256 bytes a routine)
  kb_sn: byte;

function xNul: string;
begin
  Result[0] := chr(0);
end;

function xCh(c: smallint): string;
begin
  Result[0] := chr(1); Result[1] := chr(c and 255);
end;

function xCat(const a, b: string): string;
var i, n, m: byte; t: string;
begin
  n := length(a); m := length(b);
  for i := 1 to n do t[i] := a[i];
  if n + m > 255 then m := 255 - n;
  for i := 1 to m do t[n + i] := b[i];
  t[0] := chr(n + m);
  Result := t;
end;

function xMid(const s: string; st, n: smallint): string;
var i, k, len: smallint;
begin
  len := length(s); k := 0;
  if st < 1 then st := 1;
  i := st;
  while (i < len + 1) and (k < n) do begin inc(k); Result[k] := s[i]; inc(i); end;
  Result[0] := chr(k);
end;

function xLeft(const s: string; n: smallint): string;
begin
  Result := xMid(s, 1, n);
end;

function xRight(const s: string; n: smallint): string;
var len: smallint;
begin
  len := length(s);
  if n > len then n := len;
  if n < 0 then n := 0;
  Result := xMid(s, len - n + 1, n);
end;

function xUpper(const s: string): string;
var i: byte; c: char;
begin
  for i := 1 to length(s) do begin
    c := s[i];
    if (c > '`') and (c < '{') then c := chr(ord(c) - 32);
    Result[i] := c;
  end;
  Result[0] := s[0];
end;

function xLower(const s: string): string;
var i: byte; c: char;
begin
  for i := 1 to length(s) do begin
    c := s[i];
    if (c > '@') and (c < '[') then c := chr(ord(c) + 32);
    Result[i] := c;
  end;
  Result[0] := s[0];
end;

function xSpace(n: smallint): string;
var i: smallint;
begin
  if n > 255 then n := 255;
  if n < 0 then n := 0;
  for i := 1 to n do Result[i] := ' ';
  Result[0] := chr(n);
end;

// a whole number's digits onto r
procedure AddDigits(var r: string; v: cardinal);
var d: string[12]; n, i: byte;
begin
  n := 0;
  repeat inc(n); d[n] := chr(48 + v mod 10); v := v div 10; until v = 0;
  for i := n downto 1 do begin r[0] := chr(length(r) + 1); r[length(r)] := d[i]; end;
end;

// x >= 0 in plain figures: at most six of them, no trailing zeros
function FmtPlain(x: single): string;
var ip, fp, scale: cardinal; digits, places, i, n: byte; f: single;
begin
  Result[0] := chr(0);
  ip := trunc(x);
  f := x - ip;
  digits := 1; scale := ip;
  while scale > 9 do begin scale := scale div 10; inc(digits); end;
  if ip = 0 then digits := 0;
  places := 0;
  if digits < 6 then places := 6 - digits;
  scale := 1;
  for i := 1 to places do scale := scale * 10;
  fp := round(f * scale);
  if fp = scale then begin inc(ip); fp := 0; end;
  while (places > 0) and (fp mod 10 = 0) do begin fp := fp div 10; dec(places); end;
  AddDigits(Result, ip);
  if places > 0 then begin
    n := length(Result) + 1; Result[0] := chr(n); Result[n] := '.';
    scale := 1; for i := 2 to places do scale := scale * 10;
    while scale > 1 do begin                        // the leading zeros of the fraction
      if fp < scale then begin n := length(Result) + 1; Result[0] := chr(n); Result[n] := '0'; end;
      scale := scale div 10;
    end;
    AddDigits(Result, fp);
  end;
end;

// a number as BASIC writes it: 3.75, -12, 1.5E+12
function xStrN(x: single): string;
var neg: boolean; e: smallint; m: string; n: byte;
begin
  neg := x < 0;
  if neg then x := -x;
  e := 0;
  if (x > 999999999.0) or ((x < 0.0001) and (x > 0)) then begin
    while x > 9.999995 do begin x := x / 10; inc(e); end;
    while x < 0.9999995 do begin x := x * 10; dec(e); end;
  end;
  m := FmtPlain(x);
  if neg and (m <> '0') then begin Result[0] := chr(1); Result[1] := '-'; Result := xCat(Result, m); end
  else Result := m;
  if e <> 0 then begin
    n := length(Result) + 1; Result[0] := chr(n); Result[n] := 'E';
    n := length(Result) + 1; Result[0] := chr(n);
    if e < 0 then begin Result[n] := '-'; e := -e; end else Result[n] := '+';
    if e < 10 then begin n := length(Result) + 1; Result[0] := chr(n); Result[n] := '0'; end;
    AddDigits(Result, e);
  end;
end;

function xVal(const s: string): single;
var i, n: byte; neg, eneg: boolean; v, f: single; e: smallint; c: char;
begin
  n := length(s); i := 1; v := 0; neg := false;
  while (i < n + 1) and (s[i] = ' ') do inc(i);
  if (i < n + 1) and ((s[i] = '-') or (s[i] = '+')) then begin neg := s[i] = '-'; inc(i); end;
  while (i < n + 1) and (s[i] > '/') and (s[i] < ':') do begin v := v * 10 + (ord(s[i]) - 48); inc(i); end;
  if (i < n + 1) and (s[i] = '.') then begin
    inc(i); f := 0.1;
    while (i < n + 1) and (s[i] > '/') and (s[i] < ':') do begin v := v + f * (ord(s[i]) - 48); f := f / 10; inc(i); end;
  end;
  if (i < n + 1) and ((s[i] = 'E') or (s[i] = 'e')) then begin
    inc(i); eneg := false; e := 0;
    if (i < n + 1) and ((s[i] = '-') or (s[i] = '+')) then begin eneg := s[i] = '-'; inc(i); end;
    while (i < n + 1) and (s[i] > '/') and (s[i] < ':') do begin e := e * 10 + (ord(s[i]) - 48); inc(i); end;
    while e > 0 do begin if eneg then v := v / 10 else v := v * 10; dec(e); end;
  end;
  if neg then v := -v;
  Result := v;
end;

function xAsc(const s: string): smallint;
begin
  if length(s) = 0 then Result := 0 else Result := ord(s[1]);
end;

function xInstr(const a, b: string): smallint;
var i, j, k, n, m: byte; same: boolean;
begin
  Result := 0;
  n := length(a); m := length(b);
  if (m = 0) or (m > n) then exit;
  for i := 1 to n - m + 1 do begin
    same := true;
    for j := 1 to m do begin k := i + j; dec(k); if a[k] <> b[j] then same := false; end;
    if same then begin Result := i; exit; end;
  end;
end;

function xCmp(const a, b: string): smallint;
var i, n: byte;
begin
  n := length(a); if length(b) < n then n := length(b);
  for i := 1 to n do
    if a[i] <> b[i] then begin
      if a[i] < b[i] then Result := -1 else Result := 1;
      exit;
    end;
  if length(a) < length(b) then Result := -1
  else if length(a) > length(b) then Result := 1
  else Result := 0;
end;

function xInkey: string;
begin
  if Keypressed then Result := xCh(ord(ReadKey)) else Result := xNul;
end;

procedure xPrintS(const s: string);
var i: byte;
begin
  for i := 1 to length(s) do begin
    write(s[i]);
    if s[i] = chr(13) then kb_col := 0 else inc(kb_col);
  end;
end;

procedure PrintN(x: single);       // as BASIC: a space for the sign, one after
var s: string;
begin
  s := xStrN(x);
  if x < 0 then xPrintS(s) else begin xPrintS(' '); xPrintS(s); end;
  xPrintS(' ');
end;

procedure PrintTab;                // "," : to the next 14-column zone
begin
  repeat write(' '); inc(kb_col); until kb_col mod 14 = 0;
end;

procedure PrintTabTo(n: smallint);
begin
  while kb_col < n - 1 do begin write(' '); inc(kb_col); end;
end;

procedure PrintSpc(n: smallint);
begin
  while n > 0 do begin write(' '); inc(kb_col); dec(n); end;
end;

procedure PrintNL;
begin
  writeln; kb_col := 0;
end;

// a line typed at the keyboard, after the prompt
function xKLine(const prompt: string): string;
var c: char; n: byte;
begin
  xPrintS(prompt);
  CursorOn;
  n := 0;
  repeat
    c := ReadKey;
    if (c = chr(8)) and (n > 0) then begin dec(n); write(chr(8), ' ', chr(8)); end
    else if (ord(c) > 31) and (ord(c) <> 127) and (n < 250) then begin inc(n); Result[n] := c; write(c); end;
  until c = chr(13);
  Result[0] := chr(n);
  CursorOff;
  PrintNL;
end;

// the n-th (from 0) comma-separated field of s
function xKField(const s: string; n: byte): string;
var i, k, f: byte;
begin
  f := 0; k := 0;
  for i := 1 to length(s) do begin
    if s[i] = ',' then inc(f)
    else if f = n then begin inc(k); Result[k] := s[i]; end;
  end;
  Result[0] := chr(k);
end;

function KRnd: single;
var a, b: byte;
begin
  a := Random(255); b := Random(255);
  Result := (a * 256.0 + b) / 65536.0;
end;

function KFix(x: single): single;   // the whole part, to 2 000 000 000
var c: cardinal; neg: boolean;
begin
  neg := x < 0;
  if neg then x := -x;
  if x > 2000000000.0 then begin if neg then Result := -x else Result := x; exit; end;
  c := trunc(x); Result := c;
  if neg then Result := -Result;
end;

// KN, KIv: an array element read through a call (Mad Pascal compiles
// "a[i] = 100" wrong, 2026-10-08); KF: a whole number as a number
function KN(x: single): single;
begin
  Result := x;
end;

function KIv(x: smallint): smallint;
begin
  Result := x;
end;

function KF(x: smallint): single;
begin
  Result := x;
end;

function KAddr(x: single): word;    // a PEEK or POKE address, 0..65535
var c: cardinal;
begin
  if x < 0 then x := 0;
  if x > 65535 then x := 65535;
  c := trunc(x); Result := c;
end;

function KInt(x: single): single;    // floor
begin
  Result := KFix(x);
  if Result > x then Result := Result - 1;
end;

function KAbs(x: single): single;
begin
  if x < 0 then Result := -x else Result := x;
end;

function KSgn(x: single): single;
begin
  if x < 0 then Result := -1 else if x > 0 then Result := 1 else Result := 0;
end;

function KPow(a, b: single): single;
var n: smallint; r: single;
begin
  if (b = KFix(b)) and (b > -1) and (b < 31) then begin   // whole powers exactly
    r := 1; n := trunc(b);
    while n > 0 do begin r := r * a; dec(n); end;
    Result := r;
  end else Result := MathPow(a, b);
end;

function KBool(b: boolean): single;
begin
  if b then Result := -1 else Result := 0;
end;

function KToI(x: single; line: word): smallint;
begin
  if (x > 32767.4) or (x < -32768.4) then KError(line, 'Overflow: a whole number (%) holds -32768 to 32767');
  Result := round(x);
end;

function KDiv(a, b: single; line: word): single;
begin
  if b = 0 then KError(line, 'Division by zero');
  Result := a / b;
end;

function KIDiv(a, b: smallint; line: word): smallint;
begin
  if b = 0 then KError(line, 'Division by zero');
  Result := a div b;
end;

function KMod(a, b: smallint; line: word): smallint;
begin
  if b = 0 then KError(line, 'Division by zero');
  Result := a mod b;
end;

function KIdx(i: smallint; max: smallint; line: word): word;
begin
  if (i < 0) or (i > max) then KError(line, 'Index out of range (DIM it bigger?)');
  Result := i;
end;

procedure KSleep(x: single);
var n: word;
begin
  n := round(x * 60);
  while n > 0 do begin WaitVBlank; dec(n); end;
end;

procedure KColor(f, b: smallint);
begin
  if f > -1 then TextColor(f and 15);
  if b > -1 then TextBackground(b and 15);
end;

procedure KLocate(r, c: smallint);
begin
  if r < 1 then r := 1;
  if c < 1 then c := 1;
  GotoXY(c, r); kb_col := c - 1;
end;

const
  LSLOTS = $4D0000;                // SUBs' strings: 250 x 256 bytes, a stack

function LAlloc(n: byte): byte;
var i: byte;
begin
  if kb_lp + n > 250 then KError(0, 'Too deep: SUBs inside each other hold 250 strings at most');
  Result := kb_lp;
  for i := 1 to n do begin FarPoke(LSLOTS + cardinal(kb_lp) * 256, 0); inc(kb_lp); end;
end;

// The program's string variables live in far memory too, a slot each
// (VSLOTS, 256 bytes: a length, then the bytes); the DMA engine copies a
// slot to a handle's and back, at once.
const
  VSLOTS = $800000;                // up to 32768 string variables and array elements

function NextSlot: byte;
begin
  inc(kb_sn); if kb_sn = NSLOT then kb_sn := kb_sf;
  Result := kb_sn;
end;

function LH(i: byte): byte;        // a SUB's string i, as a handle
begin
  Result := NextSlot;
  DmaCopy(LSLOTS + cardinal(i) * 256, SLOTS + cardinal(Result) * 256, 256);
end;

procedure LSet(i: byte; h: byte);
begin
  DmaCopy(SLOTS + cardinal(h) * 256, LSLOTS + cardinal(i) * 256, 256);
end;

procedure LFree(n: byte);
begin
  kb_lp := kb_lp - n;
end;

procedure KVarInit(n: word);       // every string variable ""
begin
  DmaFill(VSLOTS, cardinal(n) * 256, 0);
end;

function VH(i: word): byte;        // string variable i, as a handle
begin
  Result := NextSlot;
  DmaCopy(VSLOTS + cardinal(i) * 256, SLOTS + cardinal(Result) * 256, 256);
end;

procedure VSet(i: word; h: byte);
begin
  DmaCopy(SLOTS + cardinal(h) * 256, VSLOTS + cardinal(i) * 256, 256);
end;

// PEEK and POKE: 0-65535 the CPU's view (I/O at $D000 too), above that
// far memory, flat -- as EhBASIC's programs expect ($040000 for sprites)
function KPeek(a: single): byte;
var c: cardinal;
begin
  if a < 0 then a := 0;
  c := trunc(a);
  if c < 65536 then Result := Peek(c) else Result := FarPeek(c);
end;

procedure KPoke(a: single; v: smallint);
var c: cardinal;
begin
  if a < 0 then a := 0;
  c := trunc(a);
  if c < 65536 then Poke(c, v and 255) else FarPoke(c, v and 255);
end;

procedure KPalette(i, r, g, b: smallint);
begin
  VK[6] := i; VK[7] := r; VK[8] := g; VK[9] := b;   // the fourth write commits
end;

// the sprite table's entry for sprite n, the table pointed at and enabled
function SprBase(n: smallint): cardinal;
begin
  if not kb_sprinit then begin DmaFill(SPRTAB, 2048, 0); kb_sprinit := true; end;
  VK[$0A] := SPRTAB and 255; VK[$0B] := (SPRTAB shr 8) and 255; VK[$0C] := SPRTAB shr 16; VK[$0D] := 0;
  VK[$0E] := 1;
  Result := SPRTAB + cardinal(n and 127) * 16;
end;

function SizeCode(w: smallint): byte;
begin
  if w = 8 then Result := 0 else if w = 32 then Result := 2 else if w = 64 then Result := 3 else Result := 1;
end;

// SPRDEF n,page,w,h,bpp: the picture at page*256, w x h of 8/16/32/64, 4 or 8 bpp
procedure KSprDef(n, page, w, h, bpp: smallint);
var e: cardinal; c: byte;
begin
  e := SprBase(n);
  FarPoke(e + 4, 0); FarPoke(e + 5, page and 255); FarPoke(e + 6, (page shr 8) and 255); FarPoke(e + 7, 0);
  FarPoke(e + 9, SizeCode(w) or (SizeCode(h) shl 2));
  c := (FarPeek(e + 8) and 1) or $30;
  if bpp = 8 then c := c or 2;
  FarPoke(e + 8, c);
  if bpp = 8 then FarPoke(e + 10, 0) else FarPoke(e + 10, 1);
end;

procedure KSprite(n, x, y: smallint);
var e: cardinal;
begin
  e := SprBase(n);
  FarPoke(e, x and 255); FarPoke(e + 1, (x shr 8) and 255);
  FarPoke(e + 2, y and 255); FarPoke(e + 3, (y shr 8) and 255);
  FarPoke(e + 8, FarPeek(e + 8) or 1);
end;

procedure KSprOff(n: smallint);
var e: cardinal;
begin
  e := SprBase(n);
  FarPoke(e + 8, FarPeek(e + 8) and $FE);
end;

procedure EPut(var p: cardinal; const s: string);
var i: byte;
begin
  for i := 1 to length(s) do begin FarPoke(p, ord(s[i])); inc(p); end;
end;

// a runtime error: said on the screen, and left in MAKE.ERR for PROG
procedure KError(line: word; const msg: string);
var p: cardinal; n: string;
begin
  Str(line, n);
  KCloseAll;                       // what was written so far is kept
  if kb_col > 0 then PrintNL;
  TextColor(10); write('Line ', n, ': ', msg); TextColor(1); writeln;
  p := ERRBUF;
  EPut(p, kb_file); EPut(p, ':'); EPut(p, n); EPut(p, ':0:E:'); EPut(p, msg);
  FarPoke(p, 10); inc(p);
  SaveFile('/SYSTEM/LOG/MAKE.ERR', ERRBUF, p - ERRBUF);
  write(chr(27), '[0m'); CursorOn;
  halt;
end;


// --- more strings ------------------------------------------------------------

function SString(n, c: smallint): byte;   // STRING$(n, c): n times character c
var t: string; i: smallint;
begin
  if n > 255 then n := 255;
  if n < 0 then n := 0;
  for i := 1 to n do t[i] := chr(c and 255);
  t[0] := chr(n);
  Result := SPut(t);
end;

function SLtrim(h: byte): byte;
var u, t: string; i, n, k: byte;
begin
  u := SGet(h); n := length(u); i := 1;
  while (i < n + 1) and (u[i] = ' ') do inc(i);
  k := 0;
  while i < n + 1 do begin inc(k); t[k] := u[i]; inc(i); end;
  t[0] := chr(k);
  Result := SPut(t);
end;

function SRtrim(h: byte): byte;
var u: string; n: byte;
begin
  u := SGet(h); n := length(u);
  while (n > 0) and (u[n] = ' ') do dec(n);
  u[0] := chr(n);
  Result := SPut(u);
end;

function SHex(x: single): byte;           // HEX$(255) = "FF"
var c: cardinal; t, r: string; n, i, d: byte;
begin
  if x < 0 then x := 0;
  c := trunc(x); n := 0;
  repeat
    d := c and 15; inc(n);
    if d < 10 then t[n] := chr(48 + d) else t[n] := chr(55 + d);
    c := c shr 4;
  until c = 0;
  for i := 1 to n do r[i] := t[n + 1 - i];
  r[0] := chr(n);
  Result := SPut(r);
end;

// --- sound: the K4510's sound sequencer at $D5E0 (queued notes on MELODY,
// BBC-style: they play while the program goes on).  Pitch in quarter
// semitones, 53 = middle C; duration in 20ths of a second.

var
  SQCH: byte absolute $D5E0;
  SQAMP: byte absolute $D5E1;
  SQPITCH: byte absolute $D5E2;
  SQDUR: byte absolute $D5E3;
  kb_po, kb_pl, kb_pv: byte;       // PLAY's octave, note length, volume (they last from PLAY to PLAY)
  kb_pt: smallint;                 // and its tempo, quarter notes a minute

procedure SeqNote(ch: byte; vol: byte; pitch: smallint; dur: smallint);
begin
  if pitch < 0 then pitch := 0;
  if pitch > 254 then pitch := 254;
  if dur < 1 then dur := 1;
  if dur > 254 then dur := 254;
  SQCH := ch and 3;
  SQAMP := (256 - (vol and 15)) and 255;  // 0 silent, -15 loudest
  SQPITCH := pitch;
  SQDUR := dur;                           // this write queues the note
end;

procedure KSound(hz, sec: single);
var p: smallint;
begin
  if hz < 20 then begin SeqNote(1, 0, 53, round(sec * 20)); exit; end;
  p := 53 + round(MathLn(hz / 261.6256) * 69.2481);   // 48 quarter semitones an octave / ln 2
  SeqNote(1, 15, p, round(sec * 20));
end;

procedure KSoundOff;
begin
  SQCH := $80;
end;

// PLAY "T120 O4 L4 C D E F G2 > C": QBasic's music macro language
var
  kb_pm: string;                   // the PLAY string being read

function PNum(var j: byte; def: smallint): smallint;   // a number in it, or def
var v: smallint; got: boolean; n: byte;
begin
  v := 0; got := false; n := length(kb_pm);
  while (j < n + 1) and (kb_pm[j] > '/') and (kb_pm[j] < ':') do begin v := v * 10 + (ord(kb_pm[j]) - 48); inc(j); got := true; end;
  if got then Result := v else Result := def;
end;

procedure KPlay(h: byte; ch: smallint);
var i, n, c: byte; semi, len, dur: smallint; dots: byte;
begin
  kb_pm := SGet(h); n := length(kb_pm); i := 1;
  if (ch < 1) or (ch > 3) then ch := 1;
  while i < n + 1 do begin
    c := ord(kb_pm[i]); if (c > 96) and (c < 123) then c := c - 32;
    inc(i);
    semi := -1;
    case c of
      67: semi := 0; 68: semi := 2; 69: semi := 4; 70: semi := 5;     // C D E F
      71: semi := 7; 65: semi := 9; 66: semi := 11;                    // G A B
      79: kb_po := PNum(i, kb_po) and 7;                                // O
      60: if kb_po > 0 then dec(kb_po);                                // <
      62: if kb_po < 7 then inc(kb_po);                                // >
      76: begin kb_pl := PNum(i, 4); if kb_pl = 0 then kb_pl := 4; end; // L
      84: begin kb_pt := PNum(i, 120); if kb_pt < 32 then kb_pt := 32; end;   // T
      86: kb_pv := PNum(i, 12) and 15;                                  // V
      77: if i < n + 1 then inc(i);                                    // MF MB MN ML MS: no meaning here
      78: begin semi := PNum(i, 0); if semi = 0 then semi := -2 else semi := semi + 100; end;   // N n
      80, 82: semi := -2;                                              // P R: a rest
    end;
    if semi <> -1 then begin
      if (semi > -1) and (semi < 100) and (i < n + 1) then begin
        if (kb_pm[i] = '#') or (kb_pm[i] = '+') then begin inc(semi); inc(i); end
        else if kb_pm[i] = '-' then begin dec(semi); inc(i); end;
      end;
      len := PNum(i, kb_pl); if len = 0 then len := kb_pl;
      dur := round(4800.0 / (kb_pt * len));
      dots := 0;
      while (i < n + 1) and (kb_pm[i] = '.') do begin inc(dots); inc(i); end;
      if dots > 0 then dur := dur + dur div 2;
      if dots > 1 then dur := dur + dur div 4;
      if semi = -2 then SeqNote(ch, 0, 53, dur)
      else begin
        if semi > 99 then semi := semi - 100 else semi := kb_po * 12 + semi;
        SeqNote(ch, kb_pv, 53 + 4 * (semi - 48), dur);
      end;
    end;
  end;
end;

// --- game input and time -------------------------------------------------------

function KJoy: byte;              // the keys held now: UP 1 DOWN 2 LEFT 4 RIGHT 8 FIRE 16 A 32 B 64
begin
  Result := Peek($D104);
end;

var
  kb_sx, kb_sy, kb_vx, kb_vy: array[0..15] of smallint;   // sprites 0-15: where, and their speed a frame
  kb_css, kb_csl: array[0..15] of byte;                   // collisions read and not yet asked about

procedure KMovSpr(n, x, y: smallint);
begin
  if (n > -1) and (n < 16) then begin kb_sx[n] := x; kb_sy[n] := y; end;
  KSprite(n, x, y);
end;

procedure KSprVel(n, dx, dy: smallint);
begin
  if (n > -1) and (n < 16) then begin kb_vx[n] := dx; kb_vy[n] := dy; end;
end;

function KSprX(n: smallint): smallint;
begin
  Result := 0;
  if (n > -1) and (n < 16) then Result := kb_sx[n];
end;

function KSprY(n: smallint): smallint;
begin
  Result := 0;
  if (n > -1) and (n < 16) then Result := kb_sy[n];
end;

procedure KWaitFrame(n: smallint);   // n frames (60 a second); moving sprites move once each
var i: byte;
begin
  if n < 1 then n := 1;
  while n > 0 do begin
    WaitVBlank;
    for i := 0 to 15 do
      if (kb_vx[i] <> 0) or (kb_vy[i] <> 0) then KMovSpr(i, kb_sx[i] + kb_vx[i], kb_sy[i] + kb_vy[i]);
    dec(n);
  end;
end;

function KTimer: single;           // seconds, from the machine's millisecond clock
var a, b: cardinal;
begin
  repeat
    a := Peek($D536) or (cardinal(Peek($D537)) shl 8) or (cardinal(Peek($D538)) shl 16) or (cardinal(Peek($D539)) shl 24);
    b := Peek($D536) or (cardinal(Peek($D537)) shl 8) or (cardinal(Peek($D538)) shl 16) or (cardinal(Peek($D539)) shl 24);
  until (a shr 8) = (b shr 8);
  Result := b / 1000.0;
end;

function KFrames: single;
begin
  Result := Peek($D50D) + 256.0 * Peek($D50E) + 65536.0 * Peek($D50F);
end;

procedure ColRead;                 // VICKY's collision bits: reading $90 or $A0 clears them all, so last
var i, j: byte;
begin
  for i := 15 downto 0 do begin
    j := $90 + i; kb_css[i] := kb_css[i] or VK[j];
    j := $A0 + i; kb_csl[i] := kb_csl[i] or VK[j];
  end;
end;

function KHit(n: smallint): boolean;      // HIT(n): sprite n touched another sprite since the last HIT(n)
var b, m: byte;
begin
  ColRead;
  b := (n shr 3) and 15; m := 1 shl (n and 7);
  Result := (kb_css[b] and m) <> 0;
  kb_css[b] := kb_css[b] and (255 xor m);
end;

function KHitBg(n: smallint): boolean;    // HITBG(n): sprite n touched the picture or the text
var b, m: byte;
begin
  ColRead;
  b := (n shr 3) and 15; m := 1 shl (n and 7);
  Result := (kb_csl[b] and m) <> 0;
  kb_csl[b] := kb_csl[b] and (255 xor m);
end;

// --- files: named streams, each a 64 KB buffer in far memory.  The K4510's
// file device keeps one file open at a time, so INPUT loads the whole file
// at OPEN and OUTPUT/APPEND saves it at CLOSE (or when the program ends).

const
  FBUF = $600000;
  FNAMES = $5F0100;
  FNAMEB = $5F0000;

var
  kb_fmode: array[0..3] of byte;    // 0 closed, 1 INPUT, 2 OUTPUT/APPEND
  kb_flen, kb_fpos: array[0..3] of cardinal;
  // the four file names live in far memory at FNAMES + k * 128

function FExists(const name: string): boolean;
var i: byte;
begin
  for i := 1 to length(name) do FarPoke(FNAMEB + i - 1, ord(name[i]));
  FarPoke(FNAMEB + length(name), 0);
  FS_NAMEPTR := FNAMEB; FS_CMD := FS_STAT;
  Result := FS_STATUS = 0;
end;

procedure KOpen(k, h, mode: byte; line: word);   // mode 1 INPUT, 2 OUTPUT, 3 APPEND
var t: string; base, n: cardinal;
begin
  if kb_fmode[k] <> 0 then KClose(k);
  t := SGet(h);
  FarPoke(FNAMES + cardinal(k) * 128, length(t) and 127);
  for n := 1 to length(t) and 127 do FarPoke(FNAMES + cardinal(k) * 128 + n, ord(t[n]));
  base := FBUF + cardinal(k) * $10000;
  kb_flen[k] := 0; kb_fpos[k] := 0;
  if mode = 2 then begin kb_fmode[k] := 2; exit; end;
  if not FExists(t) then begin
    if mode = 3 then begin kb_fmode[k] := 2; exit; end;
    KError(line, xCat('File not found: ', t));
  end;
  if FS_SIZE > $FFFF then KError(line, 'The file is bigger than 64 KB');
  n := LoadFile(t, base);
  kb_flen[k] := n;
  if mode = 3 then begin kb_fmode[k] := 2; kb_fpos[k] := n; end
  else kb_fmode[k] := 1;
end;

procedure FPut(k, c: byte; line: word);
begin
  if kb_fmode[k] <> 2 then KError(line, 'That file is not open FOR OUTPUT or APPEND');
  if kb_flen[k] > $FFFE then KError(line, 'The file is full (64 KB)');
  FarPoke(FBUF + cardinal(k) * $10000 + kb_flen[k], c);
  inc(kb_flen[k]);
end;

procedure KFPrintS(k, h: byte; line: word);
var u: string; i: byte;
begin
  u := SGet(h);
  for i := 1 to length(u) do FPut(k, ord(u[i]), line);
end;

procedure KFPrintN(k: byte; x: single; line: word);
var u: string; i: byte;
begin
  u := xStrN(x);
  for i := 1 to length(u) do FPut(k, ord(u[i]), line);
end;

procedure KFComma(k: byte; line: word);
begin
  FPut(k, 44, line);
end;

procedure KFNL(k: byte; line: word);
begin
  FPut(k, 10, line);
end;

function KFLine(k: byte; line: word): byte;   // the next line (without its end)
var t: string; c, n: byte; base: cardinal;
begin
  if kb_fmode[k] <> 1 then KError(line, 'That file is not open FOR INPUT');
  if not (kb_fpos[k] < kb_flen[k]) then KError(line, 'Past the end of the file (check EOF first)');
  base := FBUF + cardinal(k) * $10000;
  n := 0;
  while kb_fpos[k] < kb_flen[k] do begin
    c := FarPeek(base + kb_fpos[k]); inc(kb_fpos[k]);
    if c = 10 then break;
    if (c <> 13) and (n < 255) then begin inc(n); t[n] := chr(c); end;
  end;
  t[0] := chr(n);
  Result := SPut(t);
end;

function KFEof(k: byte): boolean;
begin
  Result := (kb_fmode[k] <> 1) or not (kb_fpos[k] < kb_flen[k]);
end;

procedure KClose(k: byte);
var t: string; n, i: byte;
begin
  if kb_fmode[k] = 2 then begin
    n := FarPeek(FNAMES + cardinal(k) * 128);
    for i := 1 to n do t[i] := chr(FarPeek(FNAMES + cardinal(k) * 128 + i));
    t[0] := chr(n);
    SaveFile(t, FBUF + cardinal(k) * $10000, kb_flen[k]);
  end;
  kb_fmode[k] := 0;
end;

procedure KCloseAll;
var k: byte;
begin
  for k := 0 to 3 do KClose(k);
end;

// --- PRINT USING "###.##"; x: a format with #-fields and literal text ----

var
  kb_uf: string;
  kb_ui: byte;
  kb_uv: boolean;                  // a value has been placed (the format starts over when it runs out)

function UField(i: byte): boolean;
begin
  Result := false;
  if i > length(kb_uf) then exit;
  if (kb_uf[i] = '#') or (kb_uf[i] = '!') or (kb_uf[i] = '&') then Result := true
  else if (kb_uf[i] = '.') and (i < length(kb_uf)) then Result := kb_uf[i + 1] = '#';
end;

procedure UsLit;                   // the literal text up to the next field
begin
  if (kb_ui > length(kb_uf)) and kb_uv then kb_ui := 1;
  while (kb_ui < length(kb_uf) + 1) and not UField(kb_ui) do begin xPrintS(xCh(ord(kb_uf[kb_ui]))); inc(kb_ui); end;
  if (kb_ui > length(kb_uf)) and kb_uv then begin
    kb_ui := 1;
    while (kb_ui < length(kb_uf) + 1) and not UField(kb_ui) do begin xPrintS(xCh(ord(kb_uf[kb_ui]))); inc(kb_ui); end;
  end;
end;

procedure KUsingStart(h: byte);
begin
  kb_uf := SGet(h); kb_ui := 1; kb_uv := false;
end;

procedure KUsingN(x: single);
var w, d, i, n: byte; neg: boolean; v, p: cardinal; t, r: string;
begin
  UsLit;
  w := 0; d := 0;
  while (kb_ui < length(kb_uf) + 1) and (kb_uf[kb_ui] = '#') do begin inc(w); inc(kb_ui); end;
  if (kb_ui < length(kb_uf) + 1) and (kb_uf[kb_ui] = '.') then begin
    inc(kb_ui);
    while (kb_ui < length(kb_uf) + 1) and (kb_uf[kb_ui] = '#') do begin inc(d); inc(kb_ui); end;
  end;
  kb_uv := true;
  neg := x < 0; if neg then x := -x;
  p := 1; for i := 1 to d do p := p * 10;
  v := round(x * p);
  n := 0;                                       // the digits, backwards
  for i := 1 to d do begin inc(n); t[n] := chr(48 + v mod 10); v := v div 10; end;
  if d > 0 then begin inc(n); t[n] := '.'; end;
  repeat inc(n); t[n] := chr(48 + v mod 10); v := v div 10; until v = 0;
  if neg then begin inc(n); t[n] := '-'; end;
  r[0] := chr(0);
  if d > 0 then i := w + d + 1 else i := w;
  if n > i then xPrintS('%')                     // too wide for the field
  else while i > n do begin xPrintS(' '); dec(i); end;
  for i := n downto 1 do xPrintS(xCh(ord(t[i])));
end;

procedure KUsingS(h: byte);
var u: string;
begin
  UsLit;
  u := SGet(h);
  kb_uv := true;
  if kb_ui > length(kb_uf) then begin xPrintS(u); exit; end;
  if kb_uf[kb_ui] = '!' then begin
    if length(u) > 0 then xPrintS(xCh(ord(u[1])));
  end else xPrintS(u);
  inc(kb_ui);
end;

procedure KUsingEnd;
begin
  while (kb_ui < length(kb_uf) + 1) and not UField(kb_ui) do begin xPrintS(xCh(ord(kb_uf[kb_ui]))); inc(kb_ui); end;
end;

procedure KStart;
begin
  kb_col := 0; kb_sn := 0; kb_sprinit := false; kb_lp := 0; kb_sf := 0;
  kb_po := 4; kb_pl := 4; kb_pv := 12; kb_pt := 120;
  FillChar(kb_vx, SizeOf(kb_vx), 0); FillChar(kb_vy, SizeOf(kb_vy), 0);
  FillChar(kb_css, SizeOf(kb_css), 0); FillChar(kb_csl, SizeOf(kb_csl), 0);
  FillChar(kb_fmode, SizeOf(kb_fmode), 0);
  Randomize;
end;

procedure KEnd;
begin
  KCloseAll;
  if kb_col > 0 then PrintNL;
  write(chr(27), '[0m');
  CursorOn;
end;


// --- the handles (see the interface) -------------------------------------------

// The slots live in far memory (SLOTS, 256 bytes each: a length, then the
// bytes): as Pascal strings they were NSLOT x 256 bytes of every program.
function SPut(const s: string): byte;
var a: cardinal; i, n: byte;
begin
  inc(kb_sn); if kb_sn = NSLOT then kb_sn := kb_sf;
  a := SLOTS + cardinal(kb_sn) * 256;
  n := length(s);
  FarPoke(a, n);
  for i := 1 to n do FarPoke(a + i, ord(s[i]));
  Result := kb_sn;
end;

function SGet(h: byte): string;
var a: cardinal; i, n: byte;
begin
  a := SLOTS + cardinal(h) * 256;
  n := FarPeek(a);
  for i := 1 to n do Result[i] := chr(FarPeek(a + i));
  Result[0] := chr(n);
end;

function SVar(const s: string): byte;
begin
  Result := SPut(s);
end;

function SLit(const s: string): byte;
begin
  Result := SPut(s);
end;

function SNul: byte;
begin
  Result := SPut(xNul);
end;

function SCh(c: smallint): byte;
begin
  Result := SPut(xCh(c));
end;

function SCat(a, b: byte): byte;
begin
  kb_u := SGet(a); kb_v := SGet(b);
  Result := SPut(xCat(kb_u, kb_v));
end;

function SMid(h: byte; st, n: smallint): byte;
begin
  kb_u := SGet(h); Result := SPut(xMid(kb_u, st, n));
end;

function SLeft(h: byte; n: smallint): byte;
begin
  kb_u := SGet(h); Result := SPut(xLeft(kb_u, n));
end;

function SRight(h: byte; n: smallint): byte;
begin
  kb_u := SGet(h); Result := SPut(xRight(kb_u, n));
end;

function SUpper(h: byte): byte;
begin
  kb_u := SGet(h); Result := SPut(xUpper(kb_u));
end;

function SLower(h: byte): byte;
begin
  kb_u := SGet(h); Result := SPut(xLower(kb_u));
end;

function SSpace(n: smallint): byte;
begin
  Result := SPut(xSpace(n));
end;

function SStrN(x: single): byte;
begin
  Result := SPut(xStrN(x));
end;

function SVal(h: byte): single;
begin
  kb_u := SGet(h); Result := xVal(kb_u);
end;

function SAsc(h: byte): smallint;
begin
  kb_u := SGet(h); Result := xAsc(kb_u);
end;

function SLen(h: byte): smallint;
begin
  Result := FarPeek(SLOTS + cardinal(h) * 256);
end;

function SInstr(a, b: byte): smallint;
begin
  kb_u := SGet(a); kb_v := SGet(b); Result := xInstr(kb_u, kb_v);
end;

function SCmp(a, b: byte): smallint;
begin
  kb_u := SGet(a); kb_v := SGet(b); Result := xCmp(kb_u, kb_v);
end;

function SInkey: byte;
begin
  Result := SPut(xInkey);
end;

procedure PrintS(h: byte);
begin
  kb_u := SGet(h); xPrintS(kb_u);
end;

function KLine(prompt: byte): byte;
begin
  kb_u := SGet(prompt); Result := SPut(xKLine(kb_u));
end;

function KField(h: byte; n: byte): byte;
begin
  kb_u := SGet(h); Result := SPut(xKField(kb_u, n));
end;

end.
