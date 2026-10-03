// K4510: a port of /LANG/BBCBASIC/EX/CIRCLES.BBC -- random filled circles
// in MODE 2's sixteen colours until a key is pressed, then MODE 7.
// GCOL 0,RND(15): CIRCLE FILL RND(1280),RND(1024),50+RND(150).
program circles;
uses crt, graph, k4510, bbc;
begin
  BRandomize;
  BMode(2);
  repeat
    BGcol(BRnd(15));
    BCircleFill(BRnd(1280), BRnd(1024), 50 + BRnd(150));
  until KeyPressed;
  ReadKey;
  BMode(7);
end.
