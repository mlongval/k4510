// K4510: a port of /LANG/BBCBASIC/EX/MOUNTAIN.BBC -- a mountain ridge,
// redrawn forever: three ridges, back to front, in MODE 1's colours 3, 2, 1,
// each a random walk of RND(81)-41 every 16 units, clamped to 80..H+180.
// The BBC original runs until Escape; here any key leaves (the three-second
// WAIT 300 watches for it) and MODE 7 brings the console back.
program mountain;
uses crt, graph, k4510, bbc;
var l: byte;
    h, y, x: smallint;
    i: word;
    quit: boolean;
begin
  BRandomize;
  BMode(1);
  quit := false;
  repeat
    BClg;
    for l := 3 downto 1 do begin
      BGcol(l);
      h := 200 + l * 150; y := h; BMove(0, y);
      x := 16;
      while x <= 1280 do begin
        y := y + BRnd(81) - 41;
        if y < 80 then y := 80;
        if y > h + 180 then y := h + 180;
        BDraw(x, y);
        x := x + 16;
      end;
    end;
    for i := 1 to 180 do begin
      Pause;
      if KeyPressed then quit := true;
    end;
  until quit;
  ReadKey;
  BMode(7);
end.
