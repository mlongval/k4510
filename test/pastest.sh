#!/bin/sh
# Mad Pascal programs (pascal/README.md), as committed in fs/LANG/PASCAL: they run
# from the ROM's RUN, print through JIM, and hand the shell back.
cd "$(dirname "$0")/.."
out=$(./test/headless rom/kernal.bin "RUN HELLO one two
~~~~" 600 2>&1) || { echo "$out"; echo "pastest: FAILED: hello"; exit 1; }
echo "$out" | grep -q "Hello from Mad Pascal" || { echo "$out"; echo "pastest: FAILED: hello did not print"; exit 1; }
echo "$out" | grep -q "You said: one two" || { echo "$out"; echo "pastest: FAILED: ParamStr"; exit 1; }
out=$(./test/headless rom/kernal.bin "PSIEVE
~~~~" 900 2>&1) || { echo "$out"; echo "pastest: FAILED: psieve"; exit 1; }
echo "$out" | grep -q "1899 primes" || { echo "$out"; echo "pastest: FAILED: the sieve's count"; exit 1; }
out=$(./test/headless rom/kernal.bin "PFLOAT
~~~~" 900 2>&1) || { echo "$out"; echo "pastest: FAILED: pfloat"; exit 1; }
for want in "1.5 + 2.25 = 3.75" "7 / 2 = 3.5" "trunc(-3.7) = -3" "round(2.5) = 3" "1.5 < 2.25: yes" "sqrt(2) = 1.414" "2^10 = 1024" "SYSTEM: sqrt(2) = 1.414" "cos(1) = 0.5403" "exp(1) = 2.7182"; do
  echo "$out" | grep -q -F "$want" || { echo "$out"; echo "pastest: FAILED: pfloat: '$want'"; exit 1; }
done
out=$(./test/headless rom/kernal.bin "PGRAPH
~~~~x
~ECHO BACK FROM GRAPH
~~" 900 2>&1) || { echo "$out"; echo "pastest: FAILED: pgraph"; exit 1; }
echo "$out" | grep -q "BACK FROM GRAPH" || { echo "$out"; echo "pastest: FAILED: pgraph did not hand the shell back"; exit 1; }
echo "pastest: OK (hello + ParamStr, sieve 1899 primes, single on the MATH unit, GRAPH on VICKY, all back to the shell)"
