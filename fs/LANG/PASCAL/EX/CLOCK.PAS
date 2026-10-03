// K4510: a port of /LANG/BBCBASIC/EX/CLOCK.BBC -- an analogue clock on the
// machine clock.  MODE 1; once a second (WAIT 100) CLG, a circle of 400,
// twelve ticks, the hour hand in colour 1, the minute hand in 2, the
// second hand in 3, and TIME$ printed at TAB(28,29).  BBC BASIC's TIME$
// ("Sat.03 Oct 2026,00:53:19") is built here from the RTC at $D505
// (a read of $D504 latches it).  The BBC original runs until Escape; here
// any key leaves and MODE 7 brings the console back.
program clock;
uses crt, graph, k4510, bbc;
const
  days: array[0..6] of string[3] = ('Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat');
  months: array[0..11] of string[3] = ('Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun',
                                       'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec');
var h, m, s, i, w: byte;
    a, sa, ca: single;
    quit: boolean;
    t: string;
    y: string[6];

function Two(n: byte): string;             { two digits }
var d: string[4];
begin
  Str(n, d);
  if n < 10 then Result := Concat('0', d) else Result := d;
end;

procedure Hand(len: smallint);             { MOVE 640,512: DRAW 640+len*SIN(A),512+len*COS(A) }
begin
  BMove(640, 512);
  BDraw(trunc(sa * len + 640), trunc(ca * len + 512));
end;

begin
  BMode(1);
  quit := false;
  repeat
    i := SYS[4];                           { latch the clock }
    s := SYS[5]; m := SYS[6]; h := SYS[7];
    t := Concat(days[SYS[12] mod 7], '.');
    t := Concat(t, Two(SYS[8]));
    t := Concat(t, ' ');
    t := Concat(t, months[(SYS[9] + 11) mod 12]);
    t := Concat(t, ' ');
    Str(SYS[10] + 256 * SYS[11], y);
    t := Concat(t, y);
    t := Concat(t, ',');
    t := Concat(t, Two(h)); t := Concat(t, ':');
    t := Concat(t, Two(m)); t := Concat(t, ':');
    t := Concat(t, Two(s));
    BClg;
    BGcol(3); BCircle(640, 512, 400);
    for i := 0 to 11 do begin
      a := i * 30; a := a * PI / 180;
      sa := MathSin(a); ca := MathCos(a);
      BMove(trunc(sa * 370 + 640), trunc(ca * 370 + 512));
      BDraw(trunc(sa * 395 + 640), trunc(ca * 395 + 512));
    end;
    BGcol(1); a := (h mod 12) * 30 + m / 2; a := a * PI / 180; sa := MathSin(a); ca := MathCos(a); Hand(220);
    BGcol(2); a := m * 6 + s / 10; a := a * PI / 180; sa := MathSin(a); ca := MathCos(a); Hand(320);
    BGcol(3); a := s * 6; a := a * PI / 180; sa := MathSin(a); ca := MathCos(a); Hand(360);
    GotoXY(29, 30); write(t);
    for w := 1 to 60 do begin             { WAIT 100 }
      Pause;
      if KeyPressed then quit := true;
    end;
  until quit;
  ReadKey;
  BMode(7);
end.
