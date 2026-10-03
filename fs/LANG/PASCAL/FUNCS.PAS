// K4510: BBC BASIC's "= value" functions, in Mad Pascal.
//
//   DEF FNsquare(X)              function Square(x: integer): integer;
//   LOCAL Y                      begin
//   Y = X*X                        Result := x * x + 1;
//   = Y + 1                      end;
//
// Result holds the value; Exit(value) sets it and leaves at once.
program funcs;

function Square(x: integer): integer;
begin
  Result := x * x + 1;
end;

// Strings are joined with Concat, two at a time: this Mad Pascal's "+"
// on strings is a syntax error with a literal first ('an ' + s) and,
// with variables (s + t), compiles to an address sum -- an empty string
// at run time.  And no Join(Join(..)): a Mad Pascal function's locals,
// Result too, are static, so the inner call's Result is the outer's.
function Join(const a, b: string): string;
begin
  Result := Concat(a, b);
end;

// FNart: IF ... THEN ="an "+noun$ ELSE ="a "+noun$
function StartsWithVowel(const s: string): boolean;
begin
  Result := false;
  if length(s) > 0 then
    case s[1] of
      'A', 'E', 'I', 'O', 'U', 'a', 'e', 'i', 'o', 'u': Result := true;
    end;
end;

function Article(const noun: string): string;
begin
  if StartsWithVowel(noun) then
    Exit(Join('an ', noun));
  Result := Join('a ', noun);
end;

begin
  writeln('Square(7) = ', Square(7));
  writeln(Article('owl'), ', ', Article('sparrow'));
end.
