// K4510: a port of /LANG/BBCBASIC/EX/BOUNCE.BBC -- a bouncing ball with a
// blip.  MODE 1; the ball is erased in GCOL 0,0 and redrawn in colour 1,
// gravity 1.2 a step, the floor gives back 94 %, a SOUND 1,-10,100,1 on
// the walls and SOUND 1,-10,60,1 on the floor, a kick of 22 when it has
// come to rest; WAIT 2 a step.  The BBC original runs until Escape; here
// any key leaves and MODE 7 brings the console back.
program bounce;
uses crt, graph, k4510, bbc;
var x, y, u, v: single;
begin
  BMode(1);
  x := 200.0; y := 800.0; u := 14.0; v := 0.0;
  repeat
    BGcol(0); BCircleFill(trunc(x), trunc(y), 40);
    v := v - 1.2; x := x + u; y := y + v;
    if (x > 1240.0) or (x < 40.0) then begin u := -u; BSound(1, -10, 100, 1); end;
    if y < 40.0 then begin y := 40.0; v := -v * 0.94; BSound(1, -10, 60, 1); end;
    if (abs(v) < 0.5) and (y < 45.0) then v := 22.0;
    BGcol(1); BCircleFill(trunc(x), trunc(y), 40);
    BWait(2);
  until KeyPressed;
  ReadKey;
  BMode(7);
end.
