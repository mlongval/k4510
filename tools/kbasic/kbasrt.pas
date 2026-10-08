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
function KPeek(a: single): byte;
procedure KPoke(a: single; v: smallint);
procedure KPalette(i, r, g, b: smallint);
procedure KSprDef(n, page, w, h, bpp: smallint);
procedure KSprite(n, x, y: smallint);
procedure KSprOff(n: smallint);
procedure KError(line: word; const msg: string);
procedure KStart;
procedure KEnd;

implementation

const
  ERRBUF = $4F0000;                // far memory for MAKE.ERR's line
  SPRTAB = $03F800;                // the sprite table, 128 x 16 bytes (where EhBASIC keeps it)
  NSLOT = 24;

var
  VK: array[0..255] of byte absolute $D000;
  kb_sprinit: boolean;
  kb_ss: array[0..NSLOT - 1] of string;
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
  if kb_col > 0 then PrintNL;
  TextColor(10); write('Line ', n, ': ', msg); TextColor(1); writeln;
  p := ERRBUF;
  EPut(p, kb_file); EPut(p, ':'); EPut(p, n); EPut(p, ':0:E:'); EPut(p, msg);
  FarPoke(p, 10); inc(p);
  SaveFile('/SYSTEM/LOG/MAKE.ERR', ERRBUF, p - ERRBUF);
  write(chr(27), '[0m'); CursorOn;
  halt;
end;

procedure KStart;
begin
  kb_col := 0; kb_sn := 0; kb_sprinit := false;
  Randomize;
end;

procedure KEnd;
begin
  if kb_col > 0 then PrintNL;
  write(chr(27), '[0m');
  CursorOn;
end;


// --- the handles (see the interface) -------------------------------------------

function SPut(const s: string): byte;
begin
  inc(kb_sn); if kb_sn = NSLOT then kb_sn := 0;
  kb_ss[kb_sn] := s;
  Result := kb_sn;
end;

function SGet(h: byte): string;
begin
  Result := kb_ss[h];
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
var u, v: string;
begin
  u := kb_ss[a]; v := kb_ss[b];
  Result := SPut(xCat(u, v));
end;

function SMid(h: byte; st, n: smallint): byte;
var u: string;
begin
  u := kb_ss[h]; Result := SPut(xMid(u, st, n));
end;

function SLeft(h: byte; n: smallint): byte;
var u: string;
begin
  u := kb_ss[h]; Result := SPut(xLeft(u, n));
end;

function SRight(h: byte; n: smallint): byte;
var u: string;
begin
  u := kb_ss[h]; Result := SPut(xRight(u, n));
end;

function SUpper(h: byte): byte;
var u: string;
begin
  u := kb_ss[h]; Result := SPut(xUpper(u));
end;

function SLower(h: byte): byte;
var u: string;
begin
  u := kb_ss[h]; Result := SPut(xLower(u));
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
var u: string;
begin
  u := kb_ss[h]; Result := xVal(u);
end;

function SAsc(h: byte): smallint;
var u: string;
begin
  u := kb_ss[h]; Result := xAsc(u);
end;

function SLen(h: byte): smallint;
begin
  Result := length(kb_ss[h]);
end;

function SInstr(a, b: byte): smallint;
var u, v: string;
begin
  u := kb_ss[a]; v := kb_ss[b]; Result := xInstr(u, v);
end;

function SCmp(a, b: byte): smallint;
var u, v: string;
begin
  u := kb_ss[a]; v := kb_ss[b]; Result := xCmp(u, v);
end;

function SInkey: byte;
begin
  Result := SPut(xInkey);
end;

procedure PrintS(h: byte);
var u: string;
begin
  u := kb_ss[h]; xPrintS(u);
end;

function KLine(prompt: byte): byte;
var u: string;
begin
  u := kb_ss[prompt]; Result := SPut(xKLine(u));
end;

function KField(h: byte; n: byte): byte;
var u: string;
begin
  u := kb_ss[h]; Result := SPut(xKField(u, n));
end;

end.
