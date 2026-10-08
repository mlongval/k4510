// K4510: the GRAPH unit on VICKY -- lines, bars and triangles by the
// blitter, circles pixel by pixel, text from the ROM's font, over the
// console. It draws at the glass the machine is showing (GetMaxX x
// GetMaxY: 640x480, or a panel's 720x540, 1440x1080, 360x270 ...), so
// everything is placed as a share of it. Any key ends it.
program pgraph;
uses crt, graph, k4510;
var i: byte;
    w, h, cx, cy, r, rx, bw: smallint;
begin
  InitGraph(0);
  w := GetMaxX; h := GetMaxY;
  cx := w div 2; cy := (h * 5) div 12; r := h div 3; rx := (r * 6) div 5;
  bw := w div 26;
  for i := 0 to 15 do begin SetColor(i); FillBar(bw div 2 + i * (bw + bw div 5), h - h div 6, bw div 2 + i * (bw + bw div 5) + bw, h - h div 60); end;
  for i := 0 to 35 do begin
    SetColor(1 + (i mod 15));
    DrawLine(cx, cy, cx + trunc(rx * MathCos(i * 0.1745)), cy + trunc(r * MathSin(i * 0.1745)));
  end;
  SetColor(YELLOW); for i := 1 to 6 do Circle(cx, cy, (r * i) div 6);
  SetColor(LIGHT_GREEN); FillTriangle(w div 16, h div 12, w div 4, h div 8, w div 7, (h * 3) div 8);
  SetColor(LIGHT_RED); FillTriangle(w - w div 4, h div 12, w - w div 16, (h * 5) div 24, w - w div 5, (h * 3) div 8);
  SetColor(WHITE); Rectangle(2, 2, w - 3, h - 3);
  SetColor(WHITE); OutTextXY(cx - 128, cy + r + 8, 'GRAPH on VICKY: Mad Pascal draws');
  SetColor(LIGHT_BLUE); OutTextXY(cx - 192, cy + r + 20, 'blitter lines, bars, triangles; circles by pixel');
  GotoXY(1, 1); TextColor(YELLOW); write('PGRAPH: ', w, 'x', h, ', the console still under the picture. Any key.');
  ReadKey;
  CloseGraph;
  writeln;
end.
