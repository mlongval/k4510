// K4510: the first Pascal program. Write goes to JIM, the terminal.
program hello;
uses crt, k4510;
var i: byte;
begin
  writeln('Hello from Mad Pascal on the K4510.');
  writeln('The screen is ', TERM_COLS, ' by ', TERM_ROWS, '; frame ', SYS_FRAMES, '.');
  for i := 0 to 15 do begin TextColor(i); write('#'); end;
  TextColor(YELLOW);
  writeln;
  if ParamCount > 0 then writeln('You said: ', ParamStr(0));
end.
