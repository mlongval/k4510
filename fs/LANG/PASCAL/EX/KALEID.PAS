// K4510: a port of /LANG/BBCBASIC/EX/KALEID.BBC -- a kaleidoscope of
// mirrored lines, in the old Beeb style.  MODE 2, ORIGIN 640,512, four
// mirrored DRAWs in a random colour, a CLG one time in sixty.  The BBC
// original runs until Escape; here any key leaves and MODE 7 (CloseGraph)
// brings the console back.
program kaleid;
uses crt, graph, k4510, bbc;
var x, y, u, v: smallint;
begin
  BRandomize;
  BMode(2);
  BOrigin(640, 512);
  repeat
    BGcol(BRnd(7));
    x := BRnd(600); y := BRnd(500);
    u := BRnd(600); v := BRnd(500);
    BMove(x, y); BDraw(u, v);
    BMove(-x, y); BDraw(-u, v);
    BMove(x, -y); BDraw(u, -v);
    BMove(-x, -y); BDraw(-u, -v);
    if Random(60) = 0 then BClg;
  until KeyPressed;
  ReadKey;
  BMode(7);
end.
