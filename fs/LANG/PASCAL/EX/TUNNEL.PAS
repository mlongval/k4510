// K4510: a port of /LANG/BBCBASIC/EX/TUNNEL.BBC -- a tunnel that never
// redraws: fifteen filled circles in MODE 2's colours 15 down to 1, then
// only the palette moves (VDU 19,l,16,r,g,b every WAIT 3), hue l+k over
// fifteen steps, FNw the triangle wave of a hue wheel.  The BBC original
// runs until Escape; here any key leaves and MODE 7 brings the console back.
program tunnel;
uses crt, graph, k4510, bbc;
var i: byte;
    k: word;
    h, third, r, g, b: single;

function W(h: single): single;         { DEF FNw(H) }
var t: single;
begin
  h := h - MathFloor(h);
  t := h * 3;
  if t < 1 then Result := 1 - t
  else if t < 2 then Result := 0
  else Result := t - 2;
end;

begin
  BMode(2);
  for i := 15 downto 1 do begin
    BGcol(i); BCircleFill(640, 512, i * 34);
  end;
  third := 1; third := third / 3;
  k := 0;
  repeat
    k := k + 1;
    for i := 1 to 15 do begin
      h := (i + k) mod 15; h := h / 15;
      r := W(h); g := W(h + third); b := W(h + third + third);   { one call at a time: a function's Result is static }
      BPaletteRGB(i, trunc(r * 255), trunc(g * 255), trunc(b * 255));
    end;
    BWait(3);
  until KeyPressed;
  ReadKey;
  BMode(7);
end.
