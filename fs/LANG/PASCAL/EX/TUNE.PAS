// K4510: a port of /LANG/BBCBASIC/EX/TUNE.BBC -- Frere Jacques, a round for
// three voices.  Each voice is one sequencer channel (SOUND v,-12,pitch,
// duration: 53 is middle C, four steps a semitone, durations in 1/20 s);
// voices 2 and 3 start with a rest of 32 and 64 twentieths.  The whole
// tune is queued at once -- the sequencer at $D5E0 holds 64 notes a
// channel -- so the program prints its line and ends while the music plays.
program tune;
uses crt, k4510, bbc;
const
  P: array[0..31] of byte = (53,61,69,53,53,61,69,53, 69,73,81,69,73,81,
    81,89,81,73,69,53,81,89,81,73,69,53, 53,33,53,53,33,53);
  D: array[0..31] of byte = (8,8,8,8,8,8,8,8, 8,8,16,8,8,16,
    4,4,4,4,8,8,4,4,4,4,8,8, 8,8,16,8,8,16);
var v, i: byte;
begin
  BSound(2, 0, 0, 32); BSound(3, 0, 0, 64);
  for v := 1 to 3 do
    for i := 0 to 31 do BSound(v, -12, P[i], D[i]);
  writeln('THREE VOICES, ONE TUNE. (LISTEN.)');
end.
