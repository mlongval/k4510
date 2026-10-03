// K4510: ANIMAL -- guess the animal, and learn from every miss.
// Mad Pascal port of the BBC BASIC version (Nathan Teichholtz &
// Steve North, after Arthur Luehrmann; Creative Computing, 1978;
// BBC BASIC by Chris Atkinson and Richard Russell).
//
// The BASIC kept its tree as strings ("\QDoes it fly\N2\Y3\").  Here
// it is the same tree as a table of nodes plus a pool of text, all in
// one byte array, so that one SaveFile writes the lot to ANIMAL.DAT
// and one LoadFile brings it back.
//
//   store[0..1]  'AN'          magic
//   store[2]     node count
//   store[4..5]  pool top      bytes of text used
//   node n       5 bytes at HEADER + (n-1)*5:
//                text offset (word), text length, yes node, no node
//                (yes = 0: a leaf, the text is an animal)
//   pool         the texts, from POOLBASE
program animal;
uses crt, k4510;

const
  DATAFILE  = 'ANIMAL.DAT';
  MAXNODES  = 200;
  NODESIZE  = 5;
  HEADER    = 6;
  POOLBASE  = HEADER + MAXNODES * NODESIZE;
  STORESIZE = 12288;
  POOLSIZE  = STORESIZE - POOLBASE;
  ROOT      = 1;

var
  store: array[0..STORESIZE - 1] of byte;

// ---- the tree ----------------------------------------------------------

function NodeCount: byte;
begin
  Result := store[2];
end;

function PoolTop: word;
begin
  Result := store[4] + word(store[5]) shl 8;
end;

procedure SetPoolTop(w: word);
begin
  store[4] := lo(w);
  store[5] := hi(w);
end;

function NodeAt(n: byte): word;
begin
  Result := HEADER + word(n - 1) * NODESIZE;
end;

function YesOf(n: byte): byte;
begin
  Result := store[NodeAt(n) + 3];
end;

function NoOf(n: byte): byte;
begin
  Result := store[NodeAt(n) + 4];
end;

function IsAnimal(n: byte): boolean;
begin
  Result := YesOf(n) = 0;
end;

procedure SetLinks(n, y, no: byte);
var p: word;
begin
  p := NodeAt(n);
  store[p + 3] := y;
  store[p + 4] := no;
end;

function TextOf(n: byte): string;
var p, ofs: word;
    len, i: byte;
begin
  p := NodeAt(n);
  ofs := POOLBASE + store[p] + word(store[p + 1]) shl 8;
  len := store[p + 2];
  SetLength(Result, len);
  for i := 1 to len do Result[i] := chr(store[ofs + i - 1]);
end;

// Text goes on the end of the pool; FALSE when it does not fit.
function SetText(n: byte; const s: string): boolean;
var p, top: word;
    i: byte;
begin
  top := PoolTop;
  if top + length(s) > POOLSIZE then Exit(false);
  for i := 1 to length(s) do store[POOLBASE + top + i - 1] := ord(s[i]);
  p := NodeAt(n);
  store[p] := lo(top);
  store[p + 1] := hi(top);
  store[p + 2] := length(s);
  SetPoolTop(top + length(s));
  Result := true;
end;

// A new node at the end of the table; 0 when the table is full.
function NewNode: byte;
begin
  if NodeCount >= MAXNODES then Exit(0);
  Inc(store[2]);
  Result := store[2];
  SetLinks(Result, 0, 0);
end;

procedure CopyNode(src, dst: byte);
var i: byte;
begin
  for i := 0 to NODESIZE - 1 do store[NodeAt(dst) + i] := store[NodeAt(src) + i];
end;

// The BASIC's DATA line: Does it fly? no: goldfish, yes: sparrow.
procedure Seed;
begin
  store[0] := ord('A');
  store[1] := ord('N');
  store[2] := 0;
  SetPoolTop(0);
  NewNode; NewNode; NewNode;
  SetText(1, 'Does it fly');
  SetText(2, 'goldfish');
  SetText(3, 'sparrow');
  SetLinks(1, 3, 2);
end;

procedure LoadTree;
var size: cardinal;
begin
  size := LoadFile(DATAFILE, word(@store));
  if (size >= POOLBASE) and (store[0] = ord('A')) and (store[1] = ord('N')) then
    writeln('Just let me refresh my memory')
  else
    Seed;
end;

procedure SaveTree;
begin
  if SaveFile(DATAFILE, word(@store), POOLBASE + PoolTop) then
    writeln('Saved to ', DATAFILE, '.')
  else
    writeln('Could not save ', DATAFILE, '.');
end;

// ---- strings (the BASIC's FNs) -----------------------------------------

function ReadLine: string;
var c: char;
begin
  Result := '';
  repeat
    c := ReadKey;
    if c = #8 then begin
      if length(Result) > 0 then begin
        SetLength(Result, length(Result) - 1);
        write(#8, ' ', #8);
      end;
    end else if (c >= ' ') and (c < #127) and (length(Result) < 60) then begin
      SetLength(Result, length(Result) + 1);
      Result[length(Result)] := c;
      write(c);
    end;
  until c = #13;
  writeln;
end;

function LowerStr(const s: string): string;
var i: byte;
begin
  Result := s;
  for i := 1 to length(Result) do
    if (Result[i] >= 'A') and (Result[i] <= 'Z') then
      Result[i] := chr(ord(Result[i]) + 32);
end;

function TrimStr(const s: string): string;
var a, b: byte;
begin
  a := 1;
  b := length(s);
  while (a <= b) and (s[a] = ' ') do Inc(a);
  while (b >= a) and (s[b] = ' ') do Dec(b);
  Result := Copy(s, a, b - a + 1);
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

// FNart: "a sparrow", "an owl".
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
    Result := Join('an ', noun)
  else
    Result := Join('a ', noun);
end;

// FNstrip: "The Cat" -> "cat".
function AnimalName(const s: string): string;
begin
  Result := TrimStr(LowerStr(s));
  if Copy(Result, 1, 2) = 'a ' then Result := TrimStr(Copy(Result, 3, 255))
  else if Copy(Result, 1, 3) = 'an ' then Result := TrimStr(Copy(Result, 4, 255))
  else if Copy(Result, 1, 4) = 'the ' then Result := TrimStr(Copy(Result, 5, 255));
end;

// FNcapital, kinder than the BASIC's: only the first letter changes,
// so "Does it live in the USA" keeps its USA.
function Question(const s: string): string;
begin
  Result := TrimStr(s);
  while (length(Result) > 0) and (Result[length(Result)] = '?') do
    SetLength(Result, length(Result) - 1);
  if (length(Result) > 0) and (Result[1] >= 'a') and (Result[1] <= 'z') then
    Result[1] := chr(ord(Result[1]) - 32);
end;

// FNquery: ask until the answer starts with Y or N.
function Query(const prompt: string): char;
var a: string;
begin
  repeat
    write(prompt, '? ');
    a := TrimStr(ReadLine);
    if length(a) > 0 then
      case a[1] of
        'Y', 'y': Exit('Y');
        'N', 'n': Exit('N');
      end;
  until false;
end;

// ---- the game ----------------------------------------------------------

// PROCnew: the guess at leaf k was wrong; k becomes a question.
procedure Learn(k: byte);
var name, q, old, prompt: string;
    a: char;
    oldLeaf, newLeaf: byte;   // not newNode: Pascal ignores case, and NewNode is the function
begin
  old := TextOf(k);
  writeln('OK, I give up.');
  write('What animal were you thinking of? ');
  name := AnimalName(ReadLine);
  if length(name) = 0 then Exit;
  writeln('Please tell me a question that would');
  writeln('distinguish ', Article(name), ' from ', Article(old));
  q := Question(ReadLine);
  if length(q) = 0 then Exit;
  prompt := Join('For ', Article(name));
  a := Query(Join(prompt, ' the answer would be'));

  if (NodeCount + 2 > MAXNODES) or
     (PoolTop + length(name) + length(q) > POOLSIZE) then begin
    writeln('My memory is full -- I cannot learn that one.');
    Exit;
  end;
  oldLeaf := NewNode;
  newLeaf := NewNode;
  CopyNode(k, oldLeaf);        // the old animal keeps its text
  SetText(newLeaf, name);
  SetText(k, q);
  if a = 'Y' then SetLinks(k, newLeaf, oldLeaf)
  else SetLinks(k, oldLeaf, newLeaf);
end;

procedure Play;
var k: byte;
begin
  k := ROOT;
  while not IsAnimal(k) do
    if Query(TextOf(k)) = 'Y' then k := YesOf(k) else k := NoOf(k);
  if Query(Join('Is it ', Article(TextOf(k)))) = 'Y' then begin
    writeln('Why not try another one?');
    writeln;
  end else
    Learn(k);
end;

// PROCexit: what I know, and an offer to save it.
procedure Finish;
var n, col, i: byte;
    name: string;
begin
  writeln;
  writeln('Animals I already know are:');
  col := 0;
  for n := 1 to NodeCount do
    if IsAnimal(n) then begin
      name := TextOf(n);
      write(name);
      Inc(col);
      if col = 4 then begin writeln; col := 0; end
      else for i := 1 to 10 - length(name) mod 10 do write(' ');
    end;
  if col > 0 then writeln;
  writeln;
  writeln('Room for ', (MAXNODES - NodeCount) div 2, ' more.');
  if Query('Do you want to save these on disk') = 'Y' then SaveTree;
end;

begin
  ClrScr;
  writeln('               ANIMAL');
  writeln('Creative Computing  Morristown, New Jersey');
  writeln;
  writeln('Play ''Guess the Animal''');
  writeln;
  LoadTree;
  while Query('Are you thinking of an animal') = 'Y' do Play;
  Finish;
end.
