# Retired programs

Programs that were part of the machine and are not any more. They are kept
here, out of `fs/` and out of the build, because the machine's filesystem is
what a person browses with `DIR` and it should hold what the machine actually
does. Nothing here is built by the Makefile; `git log` has every version of
each one.

Moved out on Doc's instruction, 2026-09-01, in the consolidation whose
standing record is `docs/CAPABILITIES.md`.

## The SID demos — `sids.c`, `sid6.c`, `sid12.c`, `sidorch.h`

One, two and four SIDs playing the same Pachelbel progression, so the chips
could be put beside each other and beside the OPL2's `OPL2.PRG`. They were
the SIDs' showcase, and on 2026-09-01 the SIDs stopped being what the machine
sounds through — the OPL2 is the default on both hosts now. A demo that
plays into a muted chip demonstrates nothing, and a person running it gets
silence with no explanation, which is worse than its absence.

`sid12.c` earned its keep on the way out: sounding four chips at once is what
exposed the phase-carry bug in `core/sid.cc` (one stale sample per lagging
chip on 4.8% of calls). That fix is still in the machine.

## The SID player — `sidplay.c`, `sidplay0.s`, `sidplay-header.s`, `sidplay.cfg`

Played `.sid` files from `fs/SID`. Same reason. Note that `fs/SID` is a
symlink to `sidfiles/EC64SC_SID_Files` and is still in place: nothing reads
it now, and whether it stays is Doc's call.

## `romout.c`

Demonstrated the RAM under the ROM — bank blocks 5-7 out, prove the RAM is
there, bank them back. **It was broken**, and had been for a long time: it
hung on the committed ROM as well as on the current one, so this is not a
casualty of the consolidation. The smoke pass of 2026-09-01 found it and got
one cause: it fills `$A000-$CFFF`, and `$CC00-$CFFF` is its own C stack
(`demo/prg.cfg` puts PRG at `$6000` for `$7000`, `__STACKSIZE__` `$0400`).
The RAM under the ROM is the *same RAM* — banking does not move the stack out
of the way — so the fill overwrote the return addresses of the call doing the
filling. Stopping at `$CC00` gets past that and it dies differently, with a
blank screen, so there is a second fault behind the first.

None of that indicts the banking: `test/banktest` and `test/maptest` pass and
the machine is healthy after the hang. This demo ate itself. Doc, 2026-09-01:
"I never used it anyway."

If it is ever wanted back, the second fault is the interesting part and it is
unexplored.
