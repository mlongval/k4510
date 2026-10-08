// K4510: a program that asks for its own screen -- 640x480 -- and gets the
// panel's Integer Best Fit of it (the largest whole multiple, centred, in the
// frame colour), then draws on exactly that.  Any key ends it, and K/OS puts its own
// screen back.
program pibf;
uses crt, graph, k4510;
var w, h: smallint;
begin
  SetResolution(640, 480, false);
  InitGraph(0);
  w := GetMaxX; h := GetMaxY;
  SetColor(WHITE); Rectangle(0, 0, w - 1, h - 1);
  SetColor(YELLOW); DrawLine(0, 0, w - 1, h - 1); DrawLine(w - 1, 0, 0, h - 1);
  SetColor(LIGHT_GREEN); Circle(w div 2, h div 2, h div 3);
  GotoXY(2, 2); TextColor(YELLOW); write('PIBF: asked for 640x480, got ', w, 'x', h, '. Any key.');
  ReadKey;
  CloseGraph;

end.
