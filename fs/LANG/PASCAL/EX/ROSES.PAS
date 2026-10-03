// K4510: a port of /LANG/BBCBASIC/EX/ROSES.BBC -- rose curves, a new one
// every few seconds: r = 380 sin(k t), k = RND(9)+2, t from 0 to 16 pi in
// steps of 0.02, SIN and COS on the MATH unit.  The BBC original runs
// until Escape; here any key leaves (the two-second WAIT 200 between
// roses watches for it) and MODE 7 brings the console back.
program roses;
uses crt, graph, k4510, bbc;
var k, t, r, tmax: single;
    i: word;
    quit: boolean;
begin
  BRandomize;
  BMode(1);
  tmax := PI; tmax := tmax * 16;
  quit := false;
  repeat
    k := Random(9) + 2;
    BClg; BGcol(BRnd(3));
    BMove(640, 512);
    t := 0.0;
    while t <= tmax do begin
      r := MathSin(k * t) * 380;
      BDraw(trunc(r * MathCos(t) + 640), trunc(r * MathSin(t) + 512));
      t := t + 0.02;
    end;
    for i := 1 to 120 do begin           { WAIT 200: two seconds, a frame at a time }
      Pause;
      if KeyPressed then quit := true;
    end;
  until quit;
  ReadKey;
  BMode(7);
end.
