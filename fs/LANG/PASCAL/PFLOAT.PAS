// K4510: SINGLE arithmetic on the MATH unit ($D700) -- the runtime's
// + - * / compare trunc round frac are register writes there; the k4510
// unit adds the transcendentals. Assemble with mads -d:SOFTFLOAT=1 for the
// software library, to compare.
program pfloat;
uses k4510;
var a, b, c: single;
    i: word;
    f0: byte;
    n: integer;
begin
  a := 1.5; b := 2.25;
  writeln('1.5 + 2.25 = ', a + b);
  writeln('1.5 - 2.25 = ', a - b);
  writeln('1.5 * 2.25 = ', a * b);
  writeln('7 / 2 = ', 7.0 / 2.0);
  writeln('trunc(-3.7) = ', trunc(-3.7), '  round(2.5) = ', round(2.5), '  frac(3.75) = ', frac(3.75));
  if a < b then writeln('1.5 < 2.25: yes') else writeln('1.5 < 2.25: NO');
  writeln('sqrt(2) = ', MathSqrt(2.0), '  sin(1) = ', MathSin(1.0), '  ln(10) = ', MathLn(10.0));
  writeln('2^10 = ', MathPow(2.0, 10.0), '  atan2(1,1)*4 = ', MathAtan2(1.0, 1.0) * 4.0);
  a := 2.0; b := 1.0;                  { variables: the compiler folds constant arguments itself }
  writeln('SYSTEM: sqrt(2) = ', Sqrt(a), '  cos(1) = ', Cos(b), '  exp(1) = ', Exp(b), '  arctan(1)*4 = ', ArcTan(b) * 4.0);
  f0 := SYS_FRAMES; c := 0.0;
  for i := 1 to 5000 do c := c + (a * b) / 3.0 - 0.5;
  n := (SYS_FRAMES - f0) and 255;
  writeln('5000 x (mul, div, add, sub): ', n, ' frames, c = ', c);
end.
