// K4510: a port of /LANG/BBCBASIC/EX/SPRITES.BBC -- 64 hardware sprites:
// eight balls drawn once with CIRCLE FILL, captured as sprites 0-7
// (VDU 23,27,1), sprites 8-63 shown with those pictures (VDU 23,27,5),
// then VICKY moves them: PLOT 237 a sprite, WAIT 1 a frame.  Logical
// colour 8 is made orange with VDU 19,8,16,255,128,0.  The BBC original
// runs until Escape; here any key leaves and MODE 7 brings the console back.
program sprites;
uses crt, graph, k4510, bbc;
var x, y, u, v: array[0..63] of smallint;
    i, c: byte;
    px, py: smallint;
begin
  BRandomize;
  BMode(2);
  BPaletteRGB(8, 255, 128, 0);
  for c := 1 to 8 do begin
    px := c * 100; py := 100;
    BGcol(c); BCircleFill(px, py, 30);
    BGcol(7); BCircleFill(px - 10, py + 12, 7);
    BMove(px - 32, py - 34); BSpriteCapture(c - 1, 32, 32);
  end;
  for i := 8 to 63 do BSpriteShow(i, i mod 8);
  BClg;
  for i := 0 to 63 do begin
    x[i] := BRnd(1200); y[i] := BRnd(900);
    u[i] := BRnd(14) - 7; v[i] := BRnd(14) - 7;
  end;
  repeat
    for i := 0 to 63 do begin
      x[i] := x[i] + u[i]; y[i] := y[i] + v[i];
      if (x[i] < 0) or (x[i] > 1215) then u[i] := -u[i];
      if (y[i] < 0) or (y[i] > 955) then v[i] := -v[i];
      BSpriteSelect(i); BSpritePlot(x[i], y[i]);
    end;
    BWait(1);
  until KeyPressed;
  ReadKey;
  BMode(7);
end.
