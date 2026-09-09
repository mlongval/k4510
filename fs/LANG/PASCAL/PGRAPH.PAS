// K4510: the GRAPH unit on VICKY -- lines, bars and triangles by the
// blitter, circles pixel by pixel, text from the ROM's font, over the
// console. Any key ends it.
program pgraph;
uses crt, graph, k4510;
var i: byte;
    x, y: smallint;
begin
  InitGraph(0);
  for i := 0 to 15 do begin SetColor(i); FillBar(8 + i * 24, 400, 8 + i * 24 + 20, 470); end;
  for i := 0 to 35 do begin
    SetColor(1 + (i mod 15));
    DrawLine(320, 200, 320 + trunc(180 * MathCos(i * 0.1745)), 200 + trunc(150 * MathSin(i * 0.1745)));
  end;
  SetColor(YELLOW); for i := 1 to 6 do Circle(320, 200, i * 25);
  SetColor(LIGHT_GREEN); FillTriangle(40, 40, 160, 60, 90, 180);
  SetColor(LIGHT_RED); FillTriangle(480, 40, 600, 100, 520, 180);
  SetColor(WHITE); Rectangle(4, 4, 635, 475);
  SetColor(WHITE); OutTextXY(200, 300, 'GRAPH on VICKY: Mad Pascal draws');
  SetColor(LIGHT_BLUE); OutTextXY(200, 312, 'blitter lines, bars, triangles; circles by pixel');
  GotoXY(1, 1); TextColor(YELLOW); write('PGRAPH: the console is still here, under the picture. Any key.');
  ReadKey;
  CloseGraph;
  writeln;
end.
