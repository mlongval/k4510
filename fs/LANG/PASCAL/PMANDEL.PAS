// K4510: the Mandelbrot set in text, sized to the console it finds, in the
// machine's sixteen colours through JIM. It reads the width and height from
// JIM (TERM_COLS $DA05, TERM_ROWS $DA06 in the k4510 unit), so it fills 80x50
// or 40x25 alike. Fixed point, 10 fractional bits, 32-bit integers -- the
// flag-plant for the MATH unit ($D700) comes later.
program pmandel;
uses crt, k4510;
const MAXIT = 32;
      ONE = 1024;                          { 1.0 in 22.10 fixed point }
var px, py, W, H: byte;
    cx, cy, zx, zy, zx2, zy2, stepx, stepy: integer;
    it: byte;
    pal: array[0..15] of byte;
    f0: byte;
begin
  pal[0] := 6;  pal[1] := 14; pal[2] := 3;  pal[3] := 13; pal[4] := 5;  pal[5] := 7;  pal[6] := 8;  pal[7] := 10;
  pal[8] := 2;  pal[9] := 4;  pal[10] := 11; pal[11] := 12; pal[12] := 15; pal[13] := 1; pal[14] := 9; pal[15] := 0;
  W := TERM_COLS;                          { the console width  (80, or 40 in a 320-wide mode) }
  H := TERM_ROWS - 1;                      { its height, less a row for the line below }
  stepx := 3103 div (W - 1);               { the real axis, -2.03 .. +1.00, over W columns }
  stepy := 2520 div (H - 1);               { the imaginary axis, -1.23 .. +1.23, over H rows }
  ClrScr; CursorOff;
  f0 := SYS_FRAMES;
  for py := 0 to H - 1 do begin
    GotoXY(1, py + 1);
    cy := -1260 + integer(py) * stepy;     { multiply by the small step: integer is 16-bit here }
    for px := 0 to W - 1 do begin
      cx := -2079 + integer(px) * stepx;
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
  write('Mandelbrot ', W, 'x', H, ', Mad Pascal, ', (SYS_FRAMES - f0) and 255, ' frames. Any key.');
  ReadKey;
  CursorOn;
  TextMode(0);
end.
