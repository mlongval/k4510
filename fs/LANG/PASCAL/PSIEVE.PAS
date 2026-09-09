// K4510: the sieve of Eratosthenes, ten times, timed by the frame counter.
program psieve;
uses k4510;
const N = 8190;
var flags: array[0..N] of boolean;
    i, k, count, iter: word;
    f0: byte;
    frames: word;
begin
  f0 := SYS_FRAMES;
  for iter := 1 to 10 do begin
    count := 0;
    for i := 0 to N do flags[i] := true;
    for i := 0 to N do
      if flags[i] then begin
        k := i + i + 3;
        while i + k <= N do begin flags[i + k] := false; k := k + i + i + 3; end;
        inc(count);
      end;
  end;
  frames := (SYS_FRAMES - f0) and 255;
  writeln(count, ' primes, ten passes in ', frames, ' frames (', frames * 16, ' ms).');
end.
