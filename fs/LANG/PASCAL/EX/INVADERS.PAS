// K4510: a port of /LANG/BBCBASIC/EX/INVADERS.BBC -- a marching, animated,
// flipping row of captured sprites.  The two frames of the invader are
// drawn once as 4x4-pixel cells (MOVE X,Y: PLOT 101,X+7,Y+7.9, with
// Y = (7-R)*8.5334 -- the integer results are tabled below), captured as
// sprites 0 and 1 (64x32), and ten sprites (2-11) show one or the other
// frame (VDU 23,27,5), step 12 units a beat, turn and drop 40 at the
// edges (VDU 23,27,3 flips them to face the way they go), with a
// SOUND 0 noise blip each beat (WAIT 25).  The BBC original runs until
// Escape; here any key leaves and MODE 7 brings the console back.
program invaders;
uses crt, graph, k4510, bbc;
const
  shape: array[0..15] of string[11] = (
    '..X.....X..', '...X...X...', '..XXXXXXX..', '.XX.XXX.XX.',
    'XXXXXXXXXXX', 'X.XXXXXXX.X', 'X.X.....X.X', '...XX.XX...',
    '..X.....X..', 'X..X...X..X', 'X.XXXXXXX.X', 'XXX.XXX.XXX',
    'XXXXXXXXXXX', '.XXXXXXXXX.', '..X.....X..', '.X.......X.');
  { row R: Y = (7-R)*8.5334 and Y+7.9, as BBC BASIC truncates them for PLOT }
  ylo: array[0..7] of byte = (59, 51, 42, 34, 25, 17, 8, 0);
  yhi: array[0..7] of byte = (67, 59, 50, 42, 33, 24, 16, 7);
var f, r, c, i: byte;
    x, x0, y0, dx: smallint;
    t: word;
    a: string[11];
begin
  BMode(2);
  for f := 0 to 1 do begin
    for r := 0 to 7 do begin
      a := shape[f * 8 + r];               { a string of the table, then its characters }
      for c := 0 to 10 do
        if a[c + 1] = 'X' then begin
          BGcol(2); x := f * 200 + c * 8;
          BMove(x, ylo[r]); BPlot(101, x + 7, yhi[r]);
        end;
    end;
    BMove(f * 200, 0); BSpriteCapture(f, 64, 32);
  end;
  BClg;
  dx := 12; x0 := 40; y0 := 760; f := 0; t := 0;
  repeat
    f := 1 - f; t := t + 1;
    for i := 2 to 11 do BSpriteShow(i, f);
    x0 := x0 + dx;
    if (x0 > 340) or (x0 < 40) then begin
      dx := -dx; y0 := y0 - 40;
      for i := 2 to 11 do BSpriteFlip(i, ord(dx < 0));
    end;
    for i := 0 to 4 do begin
      BSpriteSelect(i + 2); BSpritePlot(x0 + i * 176, y0);
      BSpriteSelect(i + 7); BSpritePlot(x0 + i * 176 + 88, y0 - 90);
    end;
    BSound(0, -8, 20 + (t mod 4) * 10, 2);
    BWait(25);
    if y0 < 200 then y0 := 760;
  until KeyPressed;
  ReadKey;
  BMode(7);
end.
