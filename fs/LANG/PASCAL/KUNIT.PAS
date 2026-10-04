// K4510: the k4510 unit's routines, each checked once -- the examples of the
// handbook's reference (Pascal chapter, "The k4510 unit") as a program, so
// that what the reference says is what the unit does.  test/pastest.sh runs
// it, wants "11 of 11", and removes /HOME/KUNIT.DAT after.
program kunit;
uses crt, k4510;

const
  FAR0 = $400000;                    // far memory nothing else uses while we run

var
  buf: array[0..255] of byte;        // a buffer in the program's own 64 KB
  RC: byte absolute $03FF;           // the shell's result: 0 = the command worked
  ok, n, a, b, f0: byte;
  i: word;
  len: cardinal;
  good: boolean;
  long: string;

procedure Check(const what: string; good: boolean);
begin
  inc(n);
  if good then begin inc(ok); writeln('  PASS  ', what); end
  else writeln('  FAIL  ', what);
end;

begin
  writeln('KUNIT: the k4510 unit');
  ok := 0; n := 0;

  FarPoke(FAR0, 42);
  Check('FarPoke then FarPeek', FarPeek(FAR0) = 42);

  DmaFill(FAR0, 256, $77);
  a := FarPeek(FAR0); b := FarPeek(FAR0 + 255);
  Check('DmaFill fills', (a = $77) and (b = $77));

  for i := 0 to 255 do FarPoke(FAR0 + i, lo(i));
  DmaCopy(FAR0, cardinal(@buf), 256);           // far -> the program's array
  a := buf[0]; b := buf[255];
  Check('DmaCopy into a Pascal array', (a = 0) and (b = 255));

  buf[7] := 99;
  DmaCopy(cardinal(@buf), FAR0 + $1000, 256);   // and back out
  Check('DmaCopy out of a Pascal array', FarPeek(FAR0 + $1007) = 99);

  good := SaveFile('/HOME/KUNIT.DAT', FAR0, 256);
  Check('SaveFile', good);
  DmaFill(FAR0 + $2000, 256, 0);
  len := LoadFile('/HOME/KUNIT.DAT', FAR0 + $2000);
  a := FarPeek(FAR0 + $2000 + 200);
  Check('LoadFile gives the length and the bytes', (len = 256) and (a = 200));
  len := LoadFile('/HOME/NO-SUCH.DAT', FAR0);
  Check('LoadFile of a missing file gives 0', len = 0);

  Shell('CD /HOME');
  Check('Shell: RC 0 when the command works', RC = 0);
  Shell('CD /NO-SUCH-DIR');
  Check('Shell: RC set when it fails', RC <> 0);

  long := '';
  for i := 1 to 200 do long := Concat(long, 'x');
  Shell(long);                                  // cut at 95, and no harm done
  Check('Shell survives a 200-character line', true);

  f0 := SYS_FRAMES;
  WaitVBlank;
  a := SYS_FRAMES - f0;
  Check('WaitVBlank waits one frame', a = 1);

  writeln('KUNIT: ', ok, ' of ', n, ' checks passed');
end.
