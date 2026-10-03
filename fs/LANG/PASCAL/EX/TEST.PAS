// K4510: a port of /LANG/BBCBASIC/EX/TEST.BBC -- the BBC BASIC self-test,
// as a Mad Pascal self-test of the same areas: arithmetic, the MATH unit,
// integers and hex, strings, control flow and arrays, the machine (the
// frame counter, the keyboard, a file round-trip), graphics and sound
// through the bbc unit, and a star command through Shell.  Each area
// prints its goal, what it expects, a sample result and PASS or FAIL;
// press a key within 5 seconds of the start to step through them.
// What differs: single-precision reals are compared to 1E-6 (BBC BASIC's
// doubles to 1E-8); INT(-3.7) is MathFloor; there is no EVAL; the file is
// written and read whole with the k4510 unit's SaveFile/LoadFile; MODE 7 at the end of
// the graphics test clears the text, as it does in BBC BASIC.  Two more
// compiler quirks met here: Val's position argument must be a byte, and
// an array element compared in a boolean expression (v[i] = 100) is typed
// byte -- copy it to a variable first.
program test;
uses crt, graph, k4510, bbc;

var N, F, AC, AF: word;
    IV: boolean;

procedure TBegin(const t, g, e: string);
begin
  AC := 0; AF := 0;
  writeln; writeln('--- ', t);
  writeln('    goal:   ', g);
  writeln('    expect: ', e);
end;

procedure Check(c: boolean);
begin
  inc(AC); inc(N);
  if not c then begin inc(AF); inc(F); writeln('    ** check ', AC, ' FAILED'); end;
end;

procedure TDone;
begin
  if AF = 0 then writeln('    [PASS] ', AC, ' checks')
  else writeln('    [FAIL] ', AF, ' of ', AC);
  if IV then begin write('    (press a key for the next test)'); ReadKey; writeln; end;
end;

function Sq(x: integer): integer;        { DEF FNsq(x)=x*x }
begin
  Result := x * x;
end;

var a, b: single;
    i, s: integer;
    u: cardinal;
    w: word;
    st, t: string;
    v: array[0..10] of integer;
    m: array[0..3, 0..3] of integer;
    f0: byte;
    outbuf, inbuf: array[0..31] of char;
    sz: cardinal;
    ok: boolean;

begin
  writeln('MAD PASCAL SELF-TEST -- Mad Pascal on the K4510'); writeln;
  writeln('Each test shows its goal, what it expects, a sample result,');
  writeln('and PASS or FAIL. Press a key within 5 seconds to step');
  writeln('through them one at a time; otherwise they run straight through.'); writeln;
  IV := false;
  for w := 1 to 300 do begin                   { INKEY(500) }
    Pause;
    if KeyPressed then begin IV := true; break; end;
  end;
  if IV then ReadKey;
  N := 0; F := 0;

  TBegin('Arithmetic', 'integer and real operators, div and mod', '6*7=42  7/2=3.5  2^10=1024  7 div 2=3  7 mod 2=1');
  Check(6 * 7 = 42);
  a := 7; b := a / 2; Check(b = 3.5);
  a := MathPow(2, 10); Check(a = 1024);
  a := -3.7; a := MathFloor(a); Check(a = -4);
  Check((7 div 2 = 3) and (7 mod 2 = 1) and (-7 div 2 = -3));
  writeln('    result: 6*7=', 6 * 7, '  2^10=', MathPow(2, 10), '  7 div 2=', 7 div 2);
  TDone;

  TBegin('Reals (the MATH unit at $D700)', 'MathSqrt MathSin MathExp MathLn MathAtan to 6 places', 'sqrt(2)=1.414214  atan(1)*4=pi  ln(10)=2.302585');
  a := MathSqrt(2); Check(abs(a - 1.4142136) < 1E-6);
  a := MathSin(1); Check(abs(a - 0.8414710) < 1E-6);
  a := MathExp(1); Check(abs(a - 2.7182818) < 1E-6);
  a := MathLn(10); Check(abs(a - 2.3025851) < 1E-6);
  a := MathAtan(1); a := a * 4; Check(abs(a - PI) < 1E-6);
  writeln('    result: sqrt(2)=', MathSqrt(2), '  atan(1)*4=', a);
  TDone;

  TBegin('Integers and bases', '32-bit integers, hex and binary literals, and or xor', '$7FFFFFFF+1=2147483648  $FF=255  %1010=10');
  u := $7FFFFFFF; u := u + 1; Check(u = 2147483648);
  Check(($FF = 255) and (%1010 = 10));
  Check(((3 and 5) = 1) and ((3 or 5) = 7) and ((3 xor 5) = 6));
  writeln('    result: $FF=', $FF, '  %1010=', %1010, '  $7FFFFFFF+1=', u);
  TDone;

  TBegin('Strings', 'Concat, Copy, Length/Ord/Chr, Val, Pos', 'A+ WORLD=HELLO WORLD  Pos=3  Val(3.5)=3.5');
  st := 'HELLO';
  t := Concat(st, ' WORLD'); Check(t = 'HELLO WORLD');
  Check((Copy(st, 1, 2) = 'HE') and (Copy(st, 2, 3) = 'ELL') and (Copy(st, 3, 3) = 'LLO'));
  Val('3.5', a, f0);                       { f0: the error position, a byte }
  Check((Length(st) = 5) and (Ord('A') = 65) and (Chr(66) = 'B') and (a = 3.5) and (Pos('LL', st) = 3));
  t := '';
  for i := 1 to 3 do t := Concat(t, 'AB');
  Check(t = 'ABABAB');
  writeln('    result: [', Concat(st, ' WORLD'), ']  Pos(LL,HELLO)=', Pos('LL', st));
  TDone;

  TBegin('Control flow, functions, arrays', 'for/repeat/while, a function, case, 1D and 2D arrays', 'sum 1..100=5050  Sq(7)=49  m[2,3]=77  m[3,2]=0');
  s := 0; for i := 1 to 100 do s := s + i; Check(s = 5050);
  s := 0; repeat inc(s) until s = 10; Check(s = 10);
  s := 0; while s < 5 do inc(s); Check(s = 5);
  Check(Sq(7) = 49);
  for i := 0 to 10 do v[i] := i * i;
  for i := 0 to 3 do for s := 0 to 3 do m[i, s] := 0;
  m[2, 3] := 77;
  s := v[10]; ok := (s = 100);               { an element copied out first: comparing v[i] itself gives a byte, not a boolean }
  s := m[2, 3]; if s <> 77 then ok := false;
  s := m[3, 2]; if s <> 0 then ok := false;
  Check(ok);
  f0 := 2;                                   { a case selector must be a byte }
  case f0 of
    1: Check(false);
    2: Check(true);
  else Check(false);
  end;
  writeln('    result: sum=5050  Sq(7)=', Sq(7), '  m[2,3]=', m[2, 3]);
  TDone;

  TBegin('The machine (frames, keyboard, files)', 'the frame counter advances; KeyPressed; SaveFile/LoadFile', 'frames advance  file round-trips HELLO TUBE,1234');
  f0 := SYS_FRAMES; Pause(6); Check(SYS_FRAMES <> f0);
  Check(not KeyPressed);
  t := 'HELLO TUBE,1234';
  for i := 1 to length(t) do outbuf[i - 1] := t[i];
  ok := SaveFile('TESTOUT.TXT', word(@outbuf), length(t));
  Check(ok);
  for i := 0 to 31 do inbuf[i] := #0;
  sz := LoadFile('TESTOUT.TXT', word(@inbuf));
  Check(sz = length(t));
  ok := sz = length(t);
  if ok then for i := 0 to sz - 1 do if inbuf[i] <> outbuf[i] then ok := false;
  Check(ok);
  st := '';
  if sz > 0 then for i := 0 to sz - 1 do st := Concat(st, inbuf[i]);
  writeln('    result: read back [', st, '], ', sz, ' bytes');
  TDone;

  TBegin('Graphics and sound (the bbc unit on VICKY and the sequencer)', 'BMode 2, BGcol, BPlot, BDraw, BCircleFill, BPalette, sprites, BSound, BMode 7', 'draws, sounds, and MODE 7 returns');
  BMode(2); BGcol(1); BPlot(85, 100, 100); BDraw(500, 500); BCircleFill(640, 512, 100); BPalette(1, 3);
  BSpriteSelect(1); BMove(600, 500); BSpriteCapture(1, 32, 32); BSpritePlot(300, 300); BSpriteHide(1);
  BSound(1, -10, 100, 2); BSound(1, 0, 0, 0);
  Pause(30);
  BMode(7); Check(true);
  writeln('    result: drew in MODE 2, sounded, returned to MODE 7');
  TDone;

  TBegin('Star commands', 'Shell hands a line to K/OS from inside Pascal', 'ECHO prints STAR OK');
  Shell('ECHO STAR OK');
  Check(true);
  writeln('    result: Shell reached the shell');
  TDone;

  writeln; writeln('PASTEST: ', N - F, ' OF ', N, ' CHECKS PASSED');
  if F = 0 then writeln('PASTEST PASSED') else writeln('PASTEST FAILED');
end.
