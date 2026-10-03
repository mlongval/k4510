// K4510: far memory as an ordinary Pascal array, through a bank window.
//
// A Mad Pascal program lives in $0800-$CFFF and its pointers are 16 bits,
// so its own arrays stay in the 64 KB.  But the bank registers at $D600
// (core/io.h, K-01) map any 8 KB block of the CPU's view onto any 8 KB of
// the 256 MB: write the physical base to $D600 + 4 * block, low byte first
// -- the fourth byte switches the block on (bit 7 clear) or off (set).  An
// array declared ABSOLUTE at that block then IS the far memory: indexed,
// read and written at full speed, no FarPeek in sight.
//
// The window here is block 4, $8000-$9FFF: above this program (it ends far
// below $8000 -- a bigger one must pick a block it does not occupy), and
// not 5-7, which the ROM's stub banks in and out round every system call.
// A writeln while the window is open is fine: the ROM keeps its state in
// $0200-$07FF and never looks at $8000.
//
// What it checks: four pages of far memory at $400000, each filled with
// its own value by the DMA engine, are each seen through the window; a
// write through the window lands in far memory; with the window shut the
// RAM that was at $8000 is back; and the window against FarPeek, timed.
//
// Found with it (2026-10-03): the k4510 unit's FarPeek/FarPoke and the
// GRAPH unit's pixel calls borrowed Mad Pascal's zero-page pointer :bp for
// the 32-bit address and left its low byte set -- and the compiler's code
// for any array over 256 bytes indexes through (:bp),y on the promise that
// that byte is 0.  So after one FarPoke, win[i] read win[i + low byte].
// They put it back to 0 now.
program bankwin;
uses crt, k4510;

const
  WBASE = $8000;                       // the CPU address of the window: block 4
  FAR0  = $400000;                     // the far memory it looks at (nothing else uses 4 MB)
  PAGE  = 8192;

var
  win: array[0..PAGE - 1] of byte absolute WBASE;   // the window, as an array
  BANK4: cardinal absolute $D610;      // block 4's bank register ($D600 + 4 * 4)
  BANK4HI: byte absolute $D613;        // its fourth byte: bit 7 set = off
  p, v, a, b, r, ok, n, f0, f1: byte;
  good: boolean;
  i: word;
  sum1, sum2: cardinal;

procedure Open(pg: byte);              // the window onto FAR0 + pg * 8 KB (not "page": Pascal ignores case, and PAGE is the size)
begin
  BANK4 := FAR0 + cardinal(pg) * PAGE;   // the fourth byte (clear bit 7) switches it on
end;

procedure Shut;
begin
  BANK4HI := $FF;
end;

procedure Check(const what: string; good: boolean);
begin
  inc(n);
  if good then begin inc(ok); writeln('  PASS  ', what); end
  else writeln('  FAIL  ', what);
end;

begin
  writeln('BANKWIN: far memory through a bank window at $8000');
  ok := 0; n := 0;
  win[0] := $A5; win[PAGE - 1] := $5A;            // the RAM under the window, marked

  for p := 0 to 3 do DmaFill(FAR0 + cardinal(p) * PAGE, PAGE, (p + 1) * $11);

  for p := 0 to 3 do begin
    Open(p);
    v := (p + 1) * $11;
    a := win[0]; b := win[PAGE - 1];
    Check(Concat('page ', Concat(chr(48 + p), ' seen through the window')), (a = v) and (b = v));
  end;

  Open(2);
  win[100] := $C3;                                 // a write through the window...
  Check('a write through the window lands in far memory', FarPeek(FAR0 + 2 * PAGE + 100) = $C3);
  Open(1);
  Check('and another page does not see it', win[100] = $22);

  Shut;
  a := win[0]; b := win[PAGE - 1];                 // into variables first: an array element
  Check('window shut: the RAM at $8000 is back', (a = $A5) and (b = $5A));   // in a boolean expression is typed byte

  // the same 8 KB summed both ways
  for i := 0 to PAGE - 1 do FarPoke(FAR0 + 3 * PAGE + i, lo(i));
  Open(3);
  f0 := SYS_FRAMES;
  for r := 1 to 8 do begin sum1 := 0; for i := 0 to PAGE - 1 do sum1 := sum1 + win[i]; end;
  f1 := SYS_FRAMES - f0;
  Shut;
  f0 := SYS_FRAMES;
  for r := 1 to 8 do begin sum2 := 0; for i := 0 to PAGE - 1 do sum2 := sum2 + FarPeek(FAR0 + 3 * PAGE + i); end;
  f0 := SYS_FRAMES - f0;
  good := (sum1 = sum2) and (sum1 = 1044480);
  Check('the sums agree', good);
  writeln('  64 KB summed (8 KB, 8 times): window ', f1, ' frames, FarPeek ', f0, ' frames');

  writeln('BANKWIN: ', ok, ' of ', n, ' checks passed');
end.
