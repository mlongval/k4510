// K4510: the Mandelbrot set in text, 78 x 28, in the machine's sixteen
// colours through JIM. Fixed point, 10 fractional bits, 32-bit integers --
// the flag-plant for the MATH unit ($D700) comes later.
program pmandel;
uses crt, k4510;
const W = 78; H = 28; MAXIT = 32;
      ONE = 1024;                          { 1.0 in 22.10 fixed point }
var px, py: byte;
    cx, cy, zx, zy, zx2, zy2: integer;
    it: byte;
    pal: array[0..15] of byte;
    f0: byte;
begin
  pal[0] := 6;  pal[1] := 14; pal[2] := 3;  pal[3] := 13; pal[4] := 5;  pal[5] := 7;  pal[6] := 8;  pal[7] := 10;
  pal[8] := 2;  pal[9] := 4;  pal[10] := 11; pal[11] := 12; pal[12] := 15; pal[13] := 1; pal[14] := 9; pal[15] := 0;
  ClrScr; CursorOff;
  f0 := SYS_FRAMES;
  for py := 0 to H - 1 do begin
    GotoXY(1, py + 1);
    cy := (integer(py) - 14) * 90;         { -1.23 .. +1.23 }
    for px := 0 to W - 1 do begin
      cx := (integer(px) - 52) * 40;       { -2.03 .. +1.0 }
      zx := 0; zy := 0; it := 0;
      repeat
        zx2 := (zx * zx) div ONE;
        zy2 := (zy * zy) div ONE;
        zy := (2 * zx * zy) div ONE + cy;
        zx := zx2 - zy2 + cx;
        inc(it);
      until (zx2 + zy2 > 4 * ONE) or (it = MAXIT);
      if it = MAXIT then begin TextColor(0); TextBackground(0); write(' '); end
      else begin TextBackground(pal[it and 15]); TextColor(pal[(it + 1) and 15]); write(#177); end;
    end;
  end;
  TextBackground(BLUE); TextColor(YELLOW);
  GotoXY(1, H + 1);
  write('Mandelbrot, Mad Pascal, ', (SYS_FRAMES - f0) and 255, ' frames. Any key.');
  ReadKey;
  CursorOn;
  TextMode(0);
end.
