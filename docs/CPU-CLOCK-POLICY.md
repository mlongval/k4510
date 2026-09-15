# The CPU clock is a policy, not a constant

Status: proposal 1 is built (`core/calib.c`, 2026-08-27, handbook session at
Doc's request); proposal 2 is not.
Written 2026-08-27, from Doc's design thinking plus the three-host clock
sweep of the same day. Owner when built: the coding session (`core/`,
`core/ui/settings.c`). Filed under `docs/` by a third session at Doc's
request -- see the note at the foot.

## The problem

`cpu.clock` is a five-step enum with two compiled-in defaults: 40.5 MHz
on the desktop, 15 MHz on the Pi. Both numbers are guesses about a host
nobody has measured. The desktop default is a MEGA65 homage; the Pi
default is a safe retreat from 20 MHz after the sound starved.

Two things are wrong with that.

**It ages badly in both directions.** A 2016 i7 fits about 125 MHz in a
frame. p15 fits about 210. Both are offered 40.5 and nothing more, so
the machine runs at a fifth of what the host can do. Meanwhile the same
compiled-in 15 MHz will be wrong for a Pi 5, wrong for a throttled Pi in
a hot room, and wrong for a Pi on a marginal power supply -- which is a
real fault we have already hit, not a hypothetical.

**It puts the question to the user.** "What clock should I run at" is not
a question a person can answer without a benchmark. Today the honest
answer to "how fast is a K4510" is "it depends on your machine, and we
will not tell you which setting that means."

## What the sweep established

`test/bench` at `919e385`, four SIDs sounding on a sawtooth under a busy
EhBASIC SIN*COS loop, 300 frames, host ms per frame against a 16.67 ms
budget:

| Host | CPU | ~ceiling |
|---|---|---|
| ubuntu-s1 | i7-6700 @ 3.4 GHz | ~125 MHz |
| t480i5 | (laptop) | ~125 MHz |
| p15 | | ~210 MHz |

The shape of the cost, on ubuntu-s1 at 40.5 MHz: cpu 4.68, vicky 0.32,
resid 0.56, palette 0.41 ms.

- **The core is the whole variable cost.** About 0.124 ms of host time
  per emulated MHz on ubuntu-s1, on top of ~0.9 ms of fixed work. That
  number is the yardstick for whether an optimisation to the read path
  is worth anything.
- **VICKY and reSID do not scale with the CPU clock.** reSID looks like
  it should -- `sid_render()` takes cycles -- but `core/sid.cc:43` divides
  by `cpu_hz`, so the SID cycles rendered per frame come out constant. The
  SID's clock is not the CPU's. This was tested precisely because it was
  predicted to compete, and it does not.
- **Palette expansion is desktop-only.** The Pi writes 8-bit directly, so
  0.41 ms of that table comes off on the card.
- **reSID cost is latched, not acoustic.** `active[c]` is set by
  `sid_write` and cleared only by `sid_reset`. A program that pokes one
  SID register during setup and then goes quiet pays the full per-chip
  render for the rest of the session. Silent-but-untouched is 0.02 ms;
  four chips written is 0.56.

The last point is why any calibration MUST run with sound on. A silent
measurement flatters the host by half a millisecond, and half a
millisecond is most of a clock step near the ceiling.

## Proposal 1: measure the host at boot, do not look it up

Pick the clock by running the real core over a fixed workload for a
fixed number of frames, timing it, and choosing the highest step that
fits the frame with margin.

A lookup table kept by SoC name was considered and is worse. It has to
know about every part, every clock and governor setting, every thermal
state and every power supply; it is wrong the day a new board ships; and
it cannot see the under-voltage that is actually throttling the machine
in front of it. A measurement sees all of that by construction, because
it is the thing being measured.

Sketch:

- Run at power-on, before the shell, from the same code path as
  `test/bench` -- not a separate benchmark that can drift from the core.
- **Sound on.** Gate a voice on each SID the platform has, for the
  reasons above.
- Short: a few hundred ms is enough to separate five steps. It must not
  become the 25-second self-test that currently sits in a `STARTUP.BAT`.
- Choose the highest step whose measured frame time leaves headroom --
  the margin covers the workload being lighter than a real program's, and
  thermal drift later in the session.
- Cache the result in `k4510.cfg` with the host fingerprint it was
  measured on, so it is paid once rather than every boot, and re-measure
  when the fingerprint changes.
- Never silently exceed what the user chose by hand. Calibration sets
  the *default*; an explicit setting wins.

Open: what workload. The SIN*COS loop is a fair CPU-bound case but not a
worst case; a program doing heavy VICKY work has a different mix. A
pessimistic workload risks leaving performance unclaimed, an optimistic
one risks the choppiness this is meant to end.

## What proposal 1 turned out to be, once built

Three findings from the first days of it, all from runs on real hosts
rather than from reasoning:

**The governor must watch frame time, not audio gaps.** The first version
counted empty audio callbacks. On Doc's laptop the machine sat at 38 frames
a second with the sound perfectly clean and the governor content, because
`952daa6` decoupled the SIDs from a late CPU on purpose -- a slow frame
sustains a note instead of cutting it. Gaps therefore say nothing across the
whole band where a host is merely losing rather than drowning. It now
measures the machine's own half of the frame against the 16.67 ms it has.
A frames-per-second floor was considered and rejected: it reads a 50 Hz or
compositor-paced display as a slow machine, and it would ratchet the clock
down against a slow *present*, which the CPU clock does not control.

**The governor only has an opinion while a program is running.** An idle
shell at 81 MHz genuinely costs under the threshold, so it holds there and
is right to. It steps when a program works the machine hard -- which is the
moment it matters, but it also means the machine cannot converge on its own
while nobody is using it. Giving the governor its own load would be
calibration again, later, and a machine that quietly benchmarks itself while
you are trying to type is worse than one that is occasionally optimistic.
Doc's call if it is ever wanted.

**BENCH cannot evaluate the governor**, and this is a trap for whoever tries
next. BENCH dwells two seconds a step; the governor's window is three and
restarts on any clock change; so during a sweep the window never closes. A
BENCH run can only ever show the governor's *first* step. Anything concluded
about convergence from one is measuring the guard that keeps the two out of
each other's way, not the governor.

The last of these is the real argument for proposal 2 below. The honest
clock is not one number per host: the shell is fine at 81 on a laptop where
BENCH's polling loop starves at 60. No boot-time measurement can know which
program is coming, and only the program can say what it needs.

## Proposal 2: let software ask for a clock

A title should be able to say what it needs, and the machine should
answer honestly. This is the part that turns a setting into a contract.

- A program declares a **requested** clock and a **minimum** clock.
- The machine raises to the requested clock for that title if the host's
  measured ceiling allows it, and drops back when the program exits.
- If the host cannot meet the minimum, say so plainly *before* the
  program starts -- "this needs 20 MHz, this machine measured 12" -- and
  let the user run it anyway. The Pi is a fantasy machine; refusing
  outright would be the wrong manners.
- The declaration belongs with the program, not in a global setting the
  user has to guess at.

This also gives a real answer to "what speed is a K4510": whatever the
title asks for, within what the host can prove it can deliver.

Open: where the declaration lives. A header in the `.prg`, a line in a
sidecar file, and a `*CLOCK` style command from BASIC are all plausible
and they are not exclusive.

## The Pi 3B+ at 20 MHz

Doc's standing question: can the core be made fast enough that a Pi 3B+
holds 20 MHz without dropping sound. Current default there is 15.

The gap is about **33% more throughput**, not a factor of two. The core
is a callback-per-byte interpreter running on an in-order A53, which is
the shape where a page table of direct RAM pointers -- skipping the
indirect call on every ordinary read and write -- has historically been
worth 20-40%. So it is a plausible target, not a promise.

**It can now be measured, though not with `test/bench`.** That file is a
second `main()` wanting argv and stdout, and a card has neither, so it
will not be built for the Pi. It does not need to be: `sdl/main.c` *is*
the Pi frontend, and its PERF window already carries the same CPU /
VICKY / SID split, writing `SYSTEM/PERF.TXT` onto the card. As of
`c25a295` changing `cpu.clock` reopens that window and appends a block
headed by the new clock, so the Pi sweeps from the F12 menu the way the
desktop sweeps from `K4510_CPU_HZ`. Procedure: boot, F12, pick a clock,
wait 300 frames, repeat; carry the card off and read the file.

The desktop numbers do not transfer -- different cost mix, no palette
pass, a much narrower budget -- so the Pi ceiling has to come from the
card itself.

Also worth separating before blaming throughput: choppy sound on the Pi
has had non-throughput causes already, including a marginal power
supply, and the audio path has its own failure modes independent of
frame time.

## Notes for whoever builds this

- `core/ui/settings.c:34` still comments that the Pi defaults to 20 MHz.
  The code says `CPUCLK_15`. Fix the comment with the feature.
- `settings_cpu_hz()` already centralises the five steps. Calibration
  should set the setting, not bypass it, so `INFO` keeps reporting the
  truth.
- `sid_set_cpu_hz()` must be called whenever the clock changes, or reSID
  renders at the wrong rate by exactly the clock multiple. `test/bench`
  had this bug until `919e385`; a per-title clock change is a new place
  to get it wrong.

---

Filed by a third session (repo housekeeping, not code) at Doc's request,
in the handbook session's `docs/` area, because it is a design note
rather than a status note and `docs/` is where the other design notes
live. Small, additive, a new file, nothing rewritten. Handbook session:
move or rename it if it sits wrong.
