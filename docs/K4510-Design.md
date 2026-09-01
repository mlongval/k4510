# BMC-K4510 — Design Document

A fantasy 8/16-bit computer inspired by the C64 and the MEGA65,
running bare metal on a Raspberry Pi 3B+.

**The name.** *BMC* is for Randy Rossi's BMC64 and its family on the
Pi 3B and earlier boards — the platform this is built on. *K* is for
his VIC-II Kawari extensions, from which the video side takes both its
starting code and a great deal of artistic licence, there being almost
no software written for the real thing. *4510* is an hommage in two
directions at once: to the MEGA65's 45GS02 by way of the C65's 4510,
and to the C64's own 6510. Hence **BMC-K4510**. Ballot item I-01 is
closed.

This file is the **single source** for `K4510-Design.pdf` and
`K4510-Design.epub`. Edit it, then run `./make-pdf.sh` and
`./make-epub.sh` to regenerate both.

**Annotating the prose:** add your notes on their own line starting
with `> NOTE:`. Strike through anything you disagree with rather than
deleting it, if you want me to see what you rejected. In nvim, `,N`
opens a prefilled `NOTE: ` line and `,S` toggles `~~strikethrough~~`
on the word or selection.

**Revision 2026-08-21b.** Second pass of the day, folding in your
annotations on the first. Three decisions taken: **256 MB of RAM, not
1 MB** (the 45GS02's full 28-bit space); **the REU is dropped** as
redundant against it; and **emulated disks are deferred** rather than
shipped in v1. Those cascade through §0, §3, §4, §5, §9, Plan §1,
Plan §2, Phase 1 and Phase 3. Video (§2) is deliberately untouched
pending a separate conversation — see `VIDEO-OPTIONS.md`.

**Revision 2026-08-21.** This edition folds in the CPU-target decision
taken in a separate conversation and recorded in
`bmc-k4510-45gs02-decision.md`: the machine is now **BMC-K4510**, and
the CPU target is the **full 45GS02 instruction set** rather than a
plain 4502/4510. Everything that decision touches is marked below —
§0, §3, §9, Plan §1, Plan §2, Phase 2, Phase 3 and the risk table. The
`FEATURES.txt` ballot is left as the dated historical record; its C-01
and C-04 rows are superseded by §3 here.

**Deciding what's still open:** the one section awaiting your decisions
is **Appendix A** at the end of this file — ballot section J, character
set and text. Edit it there; nothing else needs your input to proceed.

I'll fold your notes in and rebuild both formats.

---

# K4510 — Ratified Capability Matrix

Decisions taken from your edited `FEATURES.txt` ballot, 2026-08-20.
Every "already there" claim was checked against the actual BMC64 tree
(`third_party/vice-3.3`, branch `kawari-phase1`), not against VICE docs.

Working name: **BMC-K4510** — renamed from BMC-K4502 on 2026-08-21,
after the CPU target moved to the 4510/45GS02 line (§0). I-01 is
narrowed, not closed: the *number* is settled, the marketing name isn't.

Governing principle: **Plan §0.5** (second half of this document) — a fantasy machine, it only has to
work. As of this ballot that principle is now **absolute**: dropping C64
compatibility (I-04) removed the last place where timing mattered at
all.

---

## 0. The decisions that reshaped the project

### I-04 + I-02 — no C64 compatibility, our own ROMs (2026-08-20)

**I-04: no C64 compatibility mode. I-02: new KERNAL/BASIC from an open
ROM base.**

Together these change what this project *is*. It was "an emulator with
extensions"; it is now "an emulator, plus a new platform, plus that
platform's system software".

Concretely:

- **No existing software runs on this machine.** Not C64 titles, not
  demos, not BASIC listings from magazines. Everything must be written
  for it.
- **The machine cannot boot until we write something for it to boot.**
  Phase 1 and Phase 2 previously ended with "boots to a C64 BASIC
  prompt" — that exit no longer exists, so bring-up needs a new target
  (see §7).
- **The last timing obligation is gone.** §0.5's one exception was C64
  compatibility. Nothing now constrains us to any historical timing.

This is a coherent choice for a fantasy machine and I'm not arguing
against it — but it is the largest scope change in the design so far,
and system software now sits on the critical path rather than in
cleanup. The Plan half of this document is structured accordingly.

### CPU target — the full 45GS02 instruction set (2026-08-21)

The CPU was specified as a 4502. It is now specified as a **45GS02**,
and the machine is renamed BMC-K4510 to match the silicon it descends
from. The lineage, briefly:

- **6502 → 65C02 → 65CE02** (CSG, 1988) — the base line.
- **4502** — Commodore's enhanced 65CE02 *core* for the unreleased C65
  (1990–91). Instruction set only. `MAP` relocates memory within 1 MB
  at 8 KB granularity.
- **4510** — the actual silicon in C65 prototypes: the 4502 core plus
  integrated I/O and memory management. This is the machine's namesake.
- **45GS02** — MEGA65's FPGA CPU, a *strict extension* of the 4510: a
  32-bit pseudo-register **Q** (A/X/Y/Z combined) reached through a
  `NEG NEG` opcode prefix, 32-bit flat zero-page-indirect addressing
  through a `NOP` prefix, a 28-bit address space via extended `MAP`,
  hypervisor mode, and assorted new opcodes (`LDZ`, `PHX/PHY/PHZ`,
  `TAB`, `BRA`, two-byte branch offsets). Its clock is whatever the host
  can hold at 60 fps with clean sound (see `CPU-CLOCK-POLICY.md`); 40.5
  MHz, the MEGA65's number, is the desktop's starting point and nothing
  more.

The extensions are prefix-based, so they are invisible to 4502/4510
code: backward compatibility holds by construction. Five reasons this
is the right target:

1. **Donor-code alignment.** The CPU core we are borrowing — Xemu's,
   see Phase 2 — *already is* a 45GS02. Holding at 4510 means actively
   stripping or gating the extensions: extra work now, permanent
   divergence from upstream, harder merges of future fixes.
2. **No compatibility cost.** A strict superset. Plain 4510 code runs
   identically.
3. **32-bit flat pointers.** This one alone justifies the choice. With
   1 MB of RAM, `MAP`'s 8 KB granularity is tedious to program against;
   32-bit ZP-indirect dereferencing reaches anywhere in physical memory
   without touching the MMU at all.
4. **Toolchain.** The living cross-assemblers target 45GS02 — ACME
   (`!cpu m65`), ca65 (`CPU_45GS02`), 6502.Net. 4510-only tooling is
   archaeology.
5. **Community gravity.** There is no meaningful C65 software base to
   inherit; every program for this machine will be new code, and new
   code in this lineage is written for the 45GS02.

**Scope caveat — the ISA and its address space, not the platform.**
"45GS02" names both an instruction set and a machine. We want the
instruction set *and* the memory model it addresses. Explicitly
**out of scope**:

- Hypervisor mode and the Hyppo trap interface — including its
  non-6502 reset-vector handling
- The MEGA65 I/O personality (VIC-IV I/O-mode key sequences and friends)

Explicitly **in** scope, revised 2026-08-21: **the full 28-bit address
space — 256 MB**, via the extended `MAP` and the mega-byte register.
Going "full 45GS02" means taking the address space too; a 45GS02 that
can only see 1 MB is a 4510 wearing a costume. The 1 MB C65 model
survives as the *shape of the low window*, not as the ceiling.

Stated honestly, the target is: **the 45GS02 instruction set — Q
operations, 32-bit ZP indirect, full 4502 base — over its full 28-bit
256 MB address space, with no hypervisor and no MEGA65 I/O.** That is a
coherent machine, not a half-MEGA65.

**What 256 MB costs.** On a 1 GB Pi 3B+ this is no longer noise the way
1 MB was, but it is not a wall either: guest RAM should be allocated
lazily by page, so the machine pays for the memory software actually
touches rather than for its ceiling. A program using 2 MB costs 2 MB.
The honest open question is how much headroom is left for the host side
once video buffers and the emulator itself are accounted for, and that
is a Phase 0 measurement, not a design argument. See the risk table.

**This is provisional on one measurement** — see ASK-7 in §8. If Xemu's
core turns out to have hypervisor entry/exit woven through its
instruction dispatch rather than sitting behind clean read/write
callbacks, the trimming cost is real and the scope may need revisiting.
Nothing else in the design depends on that answer.

---

## 1. Sound — ratified

| # | Feature | Decision |
|---|---|---|
| A-01 | SID chips | **4** — engine already supports it; 8 as a stretch goal if the Pi allows |
| A-02 | Engine | ~~ReSID default, FastSID as escape hatch~~ — **superseded 2026-09-01**: the OPL2 is the default on both hosts, reSID is the escape hatch (`audio.chip` in k4510.cfg, no menu row), and FastSID was removed from the tree |
| A-03 | Models | 6581 and 8580, selectable per chip |
| A-04 | teensy-resid | Drop — Pi 3B+ only |
| A-05 | FM | **Built-in fitted chip** at a documented address, always present |
| A-06 | Yamaha core | YM3812 (OPL2) default, YM3526 (OPL) selectable |
| A-07 | OPL3 | Defer to post-v1 |
| A-08 | FM chip count | 1, with 2 as a stretch goal |
| A-09 | DigiMAX PCM | Built-in, always present |
| A-10/11 | Sampler input, userport DAC | Drop |
| A-12 | Drive sound | Keep |
| A-13 | Sampling methods | All four selectable |
| A-14 | Load ceiling | Benchmark worst-case audio in Phase 0 |

**Resulting spec: 4 SIDs + built-in OPL2 + 4-channel PCM**, nearly all
of it already written and sitting unexposed in the tree.

### On OPL vs OPL2 vs OPL3 (your A-06/A-07 note)

- **OPL (YM3526)** — 9 FM channels, 2 operators each. The original.
- **OPL2 (YM3812)** — same 9 channels and 2 operators, plus more
  waveforms (4 instead of 1) and a percussion mode trading 3 melodic
  channels for 5 drum voices. This is the AdLib / Sound Blaster chip;
  it is what most FM music you remember from DOS games was written for.
- **OPL3 (YM262)** — 18 channels, optional **4-operator** mode (richer,
  more expressive timbres), 8 waveforms, and **stereo**. This is the
  Sound Blaster Pro 2 / SB16 chip.

Practical upshot: OPL2 is strictly better than OPL and costs the same,
which is why it's the default. OPL3 is a genuine step up — 4-operator
voices are the real difference, not just the channel count — but it
isn't in VICE and needs porting. Your ballot's OPL2-now/OPL3-later
ordering is the right one.

---

## 2. Video — ratified

**Resolved 2026-08-21c: clean sheet. The chip is VICKY.** The survey in
`VIDEO-OPTIONS.md` is closed and its recommendation taken: the VIC-II is
not carried into this machine as a substrate. It was inheritance — no
software requires it, its constraints are silicon economies we do not
pay, and keeping it meant carrying a cycle-exact renderer the plan had
already committed to gutting. The same test that deferred the disks and
never-carried-in the cartridge subsystem applies to it.

**VICKY** — VIC-Kawari extended, pronounced "Vicky", named in the
company of Agnus, Denise and Paula. The full capability list is
`VICKY-SPEC.md`; the ballot rows below are revised to match it, and
where a row simply no longer applies it says so rather than being
deleted, so the reasoning stays visible.

The one-line summary: **four layers of chunky-pixel tile or bitmap
graphics at 1–8 bpp, 128 sprites with no per-line limit, a 256-entry
24-bit palette, a copper, and a blitter — every one of them addressing
main RAM directly by 28-bit pointer, with no video memory of its own.**

| # | Feature | Decision |
|---|---|---|
| B-01 | VIC-II base | **Drop** — revised 2026-08-21c. Scaffolding for Phase 1 only, removed as soon as VICKY's first layer renders |
| B-02 | Kawari hires engine | **Superseded** by VICKY's layer model |
| B-03 | Extended registers | **Superseded** — VICKY has its own flat register file, not an `extraRegs[]` bolt-on |
| B-04 | Magic-knock unlock | **Drop** — registers always live |
| B-05 | Blitter | **Keep and extend** — the one piece of the Kawari port carried forward largely intact |
| B-06 | Private video RAM | **Drop** — VICKY addresses main RAM by 28-bit pointer; there is no video memory to be private |
| B-07 | Colour depth | 256 colours |
| B-08 | Colour cells | Byte-wide |
| B-09 | Resolutions | **Multiple: 640×200, 640×400, 640×480**, with 800×600 and 1024×768 as stretch |
| B-10 | Layer coexistence | **Superseded** — there is no VIC-II to composite. Four peer layers with programmable Z-order |
| B-11 | Sprites | **128** — revised 2026-08-21c. The old figure of 16 was a compromise with the VIC-II's register decode; with a sprite table in main RAM the count costs nothing |
| B-12 | Sprite extras | **Both** — lift the per-line limit *and* per-sprite 256-colour |
| B-13 | VDC | Drop |
| B-14/15/16 | Shaders, colour adjust, borders | Keep — borders now a programmable active area, reducible to zero |
| B-18 | Layers | **New — 4, each independently tile or bitmap at 1–8 bpp, programmable Z-order** |
| B-19 | SHEILA (display-list coprocessor) | **New — keep, built 2026-08-22.** Raster-scheduled register writes from a list in main RAM |
| B-20 | Video memory | **New — none.** All pointers are 28-bit into main RAM |
| B-17 | PAL/NTSC | **Drop** — one fixed video timing |

### Your B-01 question: are strict timings essential? Would dropping them help?

**Yes, and yes — this is the largest single performance lever in the project.**

`viciisc` is VICE's *cycle-exact* VIC-II. It exists to run demos that
depend on raster-cycle-level behaviour: sprite/badline conflicts, mid-
line register changes, the "grey dot" bug. It does per-cycle dispatch
and per-cycle state updates for all of that, and you are right that we
will never run those demos.

**This section is largely retired by the VICKY decision, and it is
worth saying why it stopped being hard.** The catch used to be that the
Kawari port is built on top of `viciisc`, with the hires engine's
counters and badline logic woven into that per-cycle structure — so the
work was "keep Kawari's engine while thinning the per-cycle machinery
underneath it", a delicate operation on somebody else's code.

With a clean sheet there is nothing to thin. VICKY is *defined* at
scanline granularity (`VICKY-SPEC.md` §0.3); the per-cycle machinery is
never written in the first place. The largest single performance lever
in the project turns out to be a decision rather than a refactor, which
is the strongest practical argument the clean sheet had.

The seam flagged in Plan §1 — a loose CPU meeting a cycle-oriented video
chip — closes with it: both sides are now line-granular by construction.
The original reasoning, kept because it is still the justification:
**render per scanline rather than per cycle**, keeping only the per-
cycle behaviours that something actually observes. I've moved this from
"prototype early" to an explicit Phase 4 work item, since your answers
to B-01, B-09 and C-10 all point the same way.

### Your B-07 question: programmable palettes on Kawari *and* VIC-II?

**Yes to both, and Kawari already has the mechanism.**

The port already carries `overlayMem[256]` — a 256-byte palette overlay
— and builds its palette from 6-bit RGB values through
`CBM_PALETTE_RGB`. So a programmable palette is not new work on the
Kawari side; it is exposing what's there.

For the VIC-II's 16 colours: those come from a palette table in
`vicii-color.c`, which the Kawari port already replaced with an RGB one.
Making those 16 entries writable from software is a small, natural
extension of the same code. So: **one programmable palette mechanism
serving both**, which is cleaner than two.

With 256 colours (B-07) and byte-wide cells (B-08), the natural design
is a 256-entry palette of 18-bit or 24-bit RGB, with the VIC-II's 16
colours as the first 16 entries.


NOTE: Would this 256 or 18 or 24 bit RGB and the
    on the fly redefinable palette give us something
    like the HAM mode on the Amiga?

### Your B-09 question: bitplanes, smooth scrolling, parallax?

Three separate things, worth separating:

**Smooth scrolling — already there.** The VIC-II has hardware smooth
scroll registers (XSCROLL/YSCROLL, 0–7 pixels) and they work today. Not
a new feature.

**Bitplanes — not present, and probably not what you want.** Bitplanes
(Amiga/VIC-IV style) store each bit of a pixel's colour in a separate
memory plane. Their advantage in *silicon* is that you can add colour
depth by adding planes, and shift planes independently. But in a
software emulator that advantage largely evaporates — we aren't
fetching from real memory banks in parallel — while the drawing code
gets substantially more complex. Recommendation: **skip bitplanes**,
and get their benefits directly instead (below).

**Parallax — this is the one to actually build.** Parallax needs
multiple independent layers, each with its own scroll offset, composited
back-to-front. In silicon that's expensive; in software it is nearly
trivial — it's just compositing, which we are already doing to merge the
Kawari layer with the VIC-II (B-10). Since we're building a compositor
anyway, **making it N layers with per-layer scroll registers costs
little and buys real capability**: parallax backgrounds, independent
scrolling playfields, HUD overlays that don't scroll.

Proposal for the ballot: a **multi-layer compositor with per-layer
scroll**, in place of bitplanes. This is exactly the kind of thing §0.5
exists to enable — cheap in software, expensive in silicon, and nobody
can tell us it's not authentic.

### Your B-02/B-09 resolution wish list

640×480 is safe. Higher is an *empirical* question and here's the honest
shape of it: at 8 bits per pixel, 640×480 is ~300 KB per frame, 800×600
is ~470 KB, 1024×768 is ~786 KB. At 50–60 fps that's 15–47 MB/s of
framebuffer writes — trivial for the RAM itself. **The cost is not
bandwidth, it's the per-pixel emulation work** in the render loop,
which is where the A53's time actually goes.

So: build the resolution as a *parameter*, not a constant, ship 640×480
as the guaranteed mode, and let the benchmark decide how far up the
Christmas list we get. 800×600 looks likely; 1024×768 depends on how
much the Phase 4 per-scanline rewrite buys back. You'll get a measured
answer rather than a promise.

---

## 3. CPU and memory — ratified

| # | Feature | Decision |
|---|---|---|
| C-01 | CPU core | **45GS02 instruction set** (4510-class + Q ops + 32-bit ZP indirect) — the project's core work. Revised 2026-08-21, see §0 |
| C-02 | 6502-compatible execution | Keep — needed for Wozmon/EhBASIC-class code; free, since 45GS02 is a strict superset |
| C-03 | Base RAM | **256 MB** — the 45GS02's full 28-bit space. Revised 2026-08-21 |
| C-04 | 28-bit addressing | **Keep** — this *is* C-03. The extended `MAP` and mega-byte register are in scope; the 20-bit C65 model survives only as the shape of the low window |
| C-05 | REU | **Drop** — revised 2026-08-21, see below |
| C-06 | REU DMA | **Drop** with C-05; replaced by C-18 |
| C-07 | Other RAM expansions | Drop |
| C-08 | Clock | Configurable including unlimited |
| C-09 | Cycle-exact timing | Drop |
| C-10 | CPU/video sync | Raster-line granularity |
| C-11/12/13/14 | SuperCPU, DTV, Ultimax, model variants | Drop |
| C-15 | 45GS02 Q register + 32-bit ZP-indirect addressing | **Keep** — the headline reason for the 45GS02 target |
| C-16 | Hypervisor mode / Hyppo | **Drop** — out of scope; reset stays 6502-style |
| C-17 | MEGA65 I/O personality | **Drop** — our I/O map is our own |
| C-18 | Storage DMA | **New** — block transfer from mass storage into RAM, taking over the job the REU was doing by accident |

The C-01 row is the first 2026-08-21 revision: the ballot said "4502
core", the target is the full 45GS02 instruction set (§0). C-16 and C-17
are new rows making the "ISA, not platform" boundary explicit.

**C-03/C-04 — 256 MB (revised 2026-08-21b).** The previous revision took
the 45GS02 instruction set but stopped at 1 MB of address space,
arguing the mega-byte `MAP` extension was moot. That was too clever.
Going full 45GS02 means going full 45GS02: 28-bit addressing, 256 MB,
with the 1 MB C65 arrangement kept as the familiar shape of the low
window rather than as a ceiling. The consequence is that the machine
stops being a C65-that-might-have-been and becomes an acknowledged
sibling of the MEGA65, which is what the cover of this document now
says it is.

**C-05/C-06 — the REU is dropped (revised 2026-08-21b).** It only ever
earned its place as "16 MB of extra memory, free, from code already in
the tree". Against 256 MB of directly addressable RAM it is a second
address space with its own semantics bolted onto a machine that no
longer needs one, and every line of documentation explaining how it
relates to the MMU is a line explaining a problem we invented.

**C-18 — but the REU was quietly doing a second job.** It was also the
reason this design needed no DMA controller: it is a block
copy/swap/compare engine, and the plan leaned on that. Dropping it
reopens the hole, so the replacement is stated directly rather than
inherited by accident: **a storage DMA path** that moves blocks from the
host filesystem into RAM without the CPU copying them byte by byte. That
is the operation this machine actually performs — getting a program and
its data into memory — and it belongs to the storage subsystem (§4),
not to a phantom cartridge.

---

## 4. Storage — ratified

| # | Feature | Decision |
|---|---|---|
| D-01 | D64/D71/D81 images | **Defer** — revised 2026-08-21b; late phase, not v1 |
| D-02 | Virtual IEC device | **Defer** with D-01 |
| D-03 | True drive emulation | **Drop entirely** |
| D-04 | Drives 8–11 | Keep |
| D-05 | Drive models | Keep selectable |
| D-06 | Datasette / tape | **Drop** |
| D-07/08 | Tapecart, IEEE-488, TCBM | Drop |
| D-09 | Native mass storage | **The primary and only v1 path** — resolved 2026-08-21b |
| D-10 | Storage DMA (= C-18) | **New** — block loads from storage straight into RAM |

**Resolved 2026-08-21b: the machine's storage is a filesystem, and the
Commodore-shaped layer is deferred.** Your reasoning stands on its own —
no software exists for this machine, so the only operation that matters
is *getting a program and its data into memory*, and every hour spent on
emulated floppies is an hour not spent on the thing that actually has to
work. The system "hard drive" plus USB/SD is the path from the start.

**D-09 — the shape of it.** Mount a host directory as the machine's
filesystem, reached through system ROM calls rather than emulated IEC
serial. No 170 KB image limits, no disk-swapping, no D64 fiction; a
K4510 program just opens a file. Paired with **D-10/C-18**, a load is a
block DMA from storage into RAM rather than a byte-at-a-time transfer
through a serial protocol that no longer exists on this machine.

**On deferring rather than dropping (D-01 through D-05).** These are
deferred, not killed. The code is in the tree and costs nothing to leave
alone; what it costs is *carrying it forward* into the new machine, and
that is the part being declined. Concretely, per the strip-early
principle in Phase 1: the disk subsystem is not carried into
`src/k4510/`. If a reason to want D64 images turns up later — reading
archival material, say, or a cross-development convenience — it comes
back as an addition to a working machine rather than as baggage on a
machine that does not boot yet.

The previous revision recommended keeping emulated disks for v1 "because
they're free and already working". They are neither, once you count the
cost of keeping them alive through a machine port.

---

## 5. Cartridges and expansion — ratified

| # | Feature | Decision |
|---|---|---|
| E-01 | Cartridge subsystem | **Extend — DigiMAX and Sound Expander become base-system hardware, not cartridges** (the REU was the third of these; dropped 2026-08-21b, C-05) |
| E-02 | The other 83 cartridges | Drop |
| E-03 | When | **Never carried in** — revised 2026-08-21b, see below |
| E-04 | Freezer functionality | **Keep — but built into the system ROM, not as a cart** |
| E-05 | Copy-protection dongles | Drop |
| E-06 | Magic Voice | Drop |
| E-07 | Native expansion mechanism | Defer — sketch below |

**On E-03 — you weren't wrong, the row was.** "Phase 6 cleanup" implied
the cartridge subsystem gets carried into the new machine and then
deleted later, which is exactly backwards. The correct statement is that
it is **never carried in**: `src/k4510/` starts as a copy of `src/c64/`
in Phase 1, and the cartridge system simply does not come across. Same
for tape, true drive emulation, RS232, printer and MIDI. The only reason
the doc said Phase 6 is that it was thinking of the *menu entries* as
the deliverable rather than the code, and that was a failure of nerve.
See the note on Phase 0 for where this principle does and does not
apply.

**On E-07 — how native expansion could actually work.** The difficulty
is real: with no cartridge port and no bus, "expansion" has nothing
physical to mean. Two workable readings, neither of them Commodore-shaped:

- **A device namespace.** The system ROM enumerates devices at boot and
  exposes them through a table — id, type, and a base address for its
  register block. "Expansion" then means a new entry in that table
  backed by host-side code (a network stack, a sound source, a storage
  provider). This is how a modern machine actually grows, and it costs
  us a documented convention rather than any silicon fiction.
- **A reserved I/O window.** Set aside an address range with a
  documented decode, so a future subsystem has somewhere to live that
  isn't already spoken for. Cheap insurance; no design commitment now.

Both are compatible; the first is the interesting one, and it wants the
system ROM's Stage 2 API (§7) to exist before it can be specified. Hence
still Defer — but deferred with a shape, rather than as a shrug.

**On E-01/E-04.** Your instruction — make these part of the base machine
rather than things you plug in — is right and simplifies the code: no
cartridge attach/detach state, no config to get wrong, always at a known
address. Note one consequence worth being deliberate about: VICE
implements them *as* cartridges, so this is a small refactor to lift
them out of the cartridge system into the machine itself, not just a
default setting. With the REU gone this is now two devices rather than
three, and the refactor is correspondingly smaller.

The freezer-in-ROM decision (E-04) pairs naturally with the system ROM
work in §7 — a built-in monitor/freezer is exactly what a new platform
needs for development, and it's the same code path as the bring-up
monitor.

---

## 6. Input, host subsystems, emulator features — ratified

**Input (F):** Keep USB gamepads, joystick ports 1–4, USB keyboard and
keymaps, mouse, paddles, SNES pad. Drop light pen, period keypads,
userport joystick adapters.

**On Bluetooth.** It was absent because it hadn't been checked, not
because it had been ruled out — a gap in the ballot rather than a
decision. It is now **ASK-8**: confirm what Circle actually provides for
Bluetooth HID (keyboards and gamepads are the interesting cases) on the
Pi 3B+'s onboard radio, and whether BMC64 already touches it. Wireless
input is a genuinely modern-feeling feature and worth knowing the price
of before deciding.

**Host subsystems (G):** Drop RS232, printer, MIDI (v1), hardware SID
drivers, A/V capture, sampler drivers, OpenCBM, event recording,
diagnostic harnesses. **Keep RTC as standard fitted hardware** (G-10) —
the Pi has a clock available and a modern machine should know the time.
All of this is Phase 6 cleanup.

**Why these said "Phase 6", and why that was wrong.** Same failure as
E-03: the doc was scheduling the *removal of menu entries* rather than
the *non-inclusion of code*. RS232, printer, MIDI, hardware SID drivers,
A/V capture and the rest are not carried into `src/k4510/` at Phase 1 at
all. Nothing is added and then taken away. What genuinely does belong in
Phase 6 is the opposite kind of work — the emulator features we are
*keeping* (snapshots, settings UI, status overlay) that need real
attention once there is a machine worth polishing.

**Emulator features (H):** Keep the monitor/debugger, snapshots (budget
it as real work), warp mode, autostart, fliplist, ROM swapping,
screenshots, status bar/overlay, settings save/load.

### Your G-04 correction — you were right, I was wrong

I wrote "no TCP/IP stack under Circle here". That's incorrect. Checking:

- Circle ships a **complete TCP/IP stack** — 33 source files in
  `lib/net/`, including `tcpconnection.cpp`, `dhcpclient.cpp`,
  `dnsclient.cpp`, `socket.cpp`.
- It has the **right driver for the Pi 3B+ specifically**:
  `lib/usb/lan7800.cpp` is the 3B+'s USB Ethernet chip.
- **`libnet.a` is already in BMC64's link line** (`Makefile` line 38).

What's actually missing is only the glue: nothing in BMC64's own sources
calls `CNetSubSystem`, `CSocket`, or `CDHCPClient` — I checked, there
are no references. So networking is *linked and available but unused*,
not absent.

**Revised decision: DEFER, not DROP.** And it's a more interesting
prospect than I implied — see §6b, which is now where that idea lives.

One honest limit, checked: this is **Circle version 40**
(`include/circle/version.h`), which has no WLAN support — there is no
`wlan`/`bcm4343` driver in the tree, only the Bluetooth firmware for
that same chip. So networking here means **wired Ethernet**, via
`lan7800.cpp`. Pi 3B+ WiFi would require upgrading Circle, which BMC64
pins deliberately — a real cost, and one to weigh separately.

---

## 6b. Stretch goal: a software Meatloaf / FujiNet stack

This is the right idea, and it lands better here than it does on real
hardware. It also quietly answers two other open questions at once.

**What it is.** Meatloaf (Commodore/IEC) and FujiNet (originally Atari,
now multi-platform) are network adapters that present themselves to a
retro machine as storage and network devices: mount a `.d64` straight
from a URL, open a TCP socket from BASIC, fetch files over TNFS/HTTP.
On real hardware they are ESP32 boards speaking a serial bus protocol.

**Why it's much easier here.** We would implement the *device side* in
software, inside the emulator — and crucially, **we don't have to fake
the bus protocol at all.** Meatloaf and FujiNet contort themselves to
squeeze network semantics through the IEC/SIO serial bus because that's
the only wire a real C64 or Atari has. We control the system ROM (§7),
so a K4510 program can just *call* the network the way it calls anything
else. All the protocol emulation — the hard, fiddly part of the real
projects — simply doesn't apply.

**What's already in place:**
- Circle's TCP/IP stack: 33 files, TCP, DHCP, DNS, sockets.
- The Pi 3B+'s Ethernet driver (`lan7800.cpp`).
- `libnet.a` already in BMC64's link line.
- Circle's own `tftpfileserver` and `webconsole` addons as worked
  examples of driving that stack.
**Ratified as the direction (2026-08-21b), still a stretch goal.** The
appeal is that it is a genuine feature of the machine rather than an
emulator convenience — a fantasy 8-bit that fetches from the network on
its own terms. Note how well it now sits with the storage decision: with
disks deferred and storage already a filesystem (§4), a network
transport is another provider behind the same system-ROM calls, not a
parallel universe with its own protocol.

**What it would resolve.** This is simultaneously the answer to **D-09**
(native mass storage — files come from the network, no D64 fiction
needed) and the payoff for **G-04** (networking). Rather than three
separate ideas, it is one feature: *the machine has a network, and
storage is one of the things it reaches over it.*

**Honest scoping.** This is genuinely a stretch goal, not a v1 item. It
depends on Stage 2 of the system ROM existing (§7) — there is no point
having network calls before there is a ROM API to expose them through.
And it's wired Ethernet only until Circle is upgraded.

**Suggested shape when we get there:** a small set of system-ROM calls
(open, read, write, close, plus a URL-ish namespace), backed by Circle
sockets, with HTTP and TNFS as the first two transports. Mounting a
remote `.d64` as a virtual IEC drive is then a *convenience layer* on
top, not the foundation — the inverse of how the real projects had to
build it.

### Your H-05 question: what is a fliplist?

A saved list of disk images you can cycle through with a keystroke,
instead of navigating the file browser each time. It exists because
multi-disk games constantly said "insert side 2". On a machine with no
C64 software and 256 MB of RAM, it's close to pointless. **H-05: DROP**
(2026-08-21b) — it follows the disks it exists to serve (D-01), and menu
clarity on a machine with a filesystem is worth more than a feature for
swapping images nobody has.

---

## 6c. Character set and text — open (ballot section J)

Missed in the first pass; now section **J** of `FEATURES.txt`, 14 blocks.
Summary of the analysis:

**Three independent decisions get conflated.** *Encoding* (the byte
values `PRINT`/`CHR$`/files/keyboard use), *glyphs* (what the character
generator draws), and *screen codes* (the index in screen memory) are
separable. A machine can draw PETSCII glyphs while using ASCII encoding.
Deciding them together is what makes character sets feel intractable.

**Two facts from the tree make this easier than expected:**

- **Character sets are already RAM-based.** Kawari fetches character
  pixels from its own video RAM via `hires_char_pixel_base` — so
  charsets are software-redefinable and several can live in video memory
  at once. There is no fixed 4 KB chargen ROM constraint to design
  around.
- **Kawari already supports a per-cell alternate charset.** Bit 7 of the
  colour byte (`altc_bit`) selects between two banks *per character
  cell*. This kills PETSCII's worst limitation — the C64 could not show
  lowercase and graphics simultaneously. We already can, for free.

**Recommendations, in brief:**

- **Encoding: ASCII-based.** With C64 compatibility dropped, nothing
  forces PETSCII, whose ordering makes sorting and comparison awkward.
  Every cross-development tool (`cc65`, assemblers, editors) and all
  network content speaks ASCII.
- **Glyphs: keep PETSCII as a bank.** The blocks, suits and rounded
  corners are the visual signature of a Commodore machine even when
  nothing else about ours is. 2 KB well spent — and it's the homage,
  where byte-value compatibility is not.
- **CP437 over CP850.** Worth stating explicitly since you write in
  French: **CP437 already covers French** — é, à, ç, ê, è, î, ô, û are
  all present — while keeping the complete box-drawing and block set.
  CP850 trades box-drawing glyphs away for coverage of languages you're
  unlikely to need. For a machine that will draw TUIs, CP437 is the
  better single choice.
- **Reverse video as a colour attribute, not 128 code points.** PETSCII
  spends half its character space on reversed copies. With byte-wide
  colour cells (B-08) we have attribute bits spare, so all 256 codes can
  stay real glyphs.
- **80 columns falls out for free.** 640 pixels at 8×8 gives **80×60**;
  8×16 gives **80×30**, far more readable on a modern display. The C64
  needed a whole second video chip for 80 columns; we get it from the
  resolution we already chose.
- **One keymap, not a menu of them (decided 2026-08-21b).** The machine
  ships **US International with dead keys** and nothing else. Not a
  French keymap, not a Canadian-French one, not the `de`/`da`/`fi`/
  `no`/`nl` set the tree happens to carry. Dead keys cover the accented
  Latin repertoire from a single layout, which is the whole point of
  choosing it. User-contributed `.vkm` files are welcome and will be
  accepted; they are not maintained by this project. This turns "a
  French keymap must be written, and nobody else will do it" from a work
  item into a policy, which is a strictly better outcome for a one-person
  project.
- **Appendix B (to be written)** will test that policy rather than
  assert it: a coverage analysis of US-International-with-dead-keys
  against the layouts it is replacing — which characters come out in one
  keystroke, which need a dead-key sequence, and which are genuinely
  unreachable. If the unreachable column turns out to be long, the
  policy deserves revisiting; if it is empty, the case is closed.

- **UTF-8 transcoding** down to the active glyph set, once the
  FujiNet-class stack (§6b) starts fetching real web text.

---

## 7. System software — now on the critical path

This section exists because of I-02 and I-04. It was previously a
Phase 6 afterthought; it is now a phase of its own.

Your C-02 note — *"I think Wozmon is the base and then a clean room
rewrite of the ROMs"* — is a sound instinct, and it maps onto a
three-stage path:

**Stage 1 — Wozmon-class monitor (bring-up).**
Woz's Apple-1 monitor is ~256 bytes and does exactly three things:
examine memory, deposit memory, run from an address. That is precisely
the minimum needed to prove a new CPU works and to interact with the
machine at all. It becomes the **new Phase 1 and Phase 2 exit target**,
replacing "boots to a C64 BASIC prompt". Being 6502 code, it runs on
the 45GS02 unchanged (C-02) — a superset executes it without special
handling, which is exactly what makes it a good bring-up target.

**Stage 2 — system ROM / KERNAL-equivalent.**
Screen output, keyboard input, file access, and the entry points
programs call. This one genuinely has to be written for our hardware —
our video chip, memory map and I/O are all ours, so there is nothing to
port. This is also where the built-in freezer/monitor (E-04) and the
native file API (D-09) live.

**Existing system software to survey before writing our own (ASK-9).**
Stage 2 says "there is nothing to port", and that is true of the
hardware-facing half — our video chip, memory map and I/O are ours. It
is not necessarily true of the *structure*: file abstractions, a program
loader, a call interface. Named starting points: **MORPHEUS** (Paul
Robson, for the Neo6502) and **CP/M-65** (David Given). Both deserve a
proper read rather than a mention, along with whatever else a survey
turns up. The question to answer for each is narrow: does it give us a
system-call shape worth adopting, and what does it assume about the
machine underneath it? A 256 MB flat address space is unusual enough in
this lineage that most candidates will assume less than we have.

**Stage 3 — BASIC (or not).**
Options, roughly in order of least work:
- **EhBASIC** (Lee Davison) — a mature, free 6502 BASIC, ~10 KB,
  designed to be retargeted by supplying character in/out routines.
  Easily the fastest route to an interactive machine.
- **OpenROMs** (the MEGA65 project's GPL KERNAL/BASIC) — more capable,
  but explicitly C64-compatible, which is the thing you just dropped.
  Adopting it would drag C64 compatibility back in through the door.
  (Note that the 45GS02 decision does *not* change this: sharing a CPU
  with the MEGA65 does not make its system software any less
  C64-shaped.)
- **Write our own** — maximum character, maximum work.
- **No BASIC at all** — a monitor plus a cross-development toolchain
  (`cc65` targets the 6502 family and would work). Legitimate for a
  machine aimed at people who write code on a real computer.

Recommendation: **EhBASIC for Stage 3**, because it gets you typing
programs on the machine soonest, and character can be added afterward.
This is now an open question — call it **ASK-6** — and it doesn't block
Phases 0–2.

**Added to ASK-6 (2026-08-21b): MMBasic and the Neo6502's BASIC.** Both
go on the list beside EhBASIC. One thing to establish before either can
be compared fairly: on the Neo6502 the division of labour between the
65C02 and the RP2040 support processor is not the same as ours — some of
that stack may run on the support chip rather than on the 6502 itself,
which would change "port it" from a retarget into a rewrite. That is a
question of fact, and it gets answered as part of the ASK-9 survey
rather than guessed at here.

---

## 8. Still open

- ~~**I-01** — machine name.~~ **Closed 2026-08-21b: BMC-K4510**, with
  the derivation recorded at the top of this document.
- **ASK-6** — Stage 3 BASIC: EhBASIC, MMBasic, the Neo6502's BASIC, our
  own, or none?
- **ASK-8** — what does Circle give us for Bluetooth HID on the 3B+?
- **ASK-9** — survey existing 6502-family system software (MORPHEUS,
  CP/M-65, others) for a system-call shape worth adopting.
- **Video paradigm** — VIC-II + Kawari as substrate, or a clean-sheet
  chip? §2 is deliberately frozen pending that conversation; the options
  are laid out in `VIDEO-OPTIONS.md`.
- **Appendix B** — US-International dead-key coverage analysis, to test
  the one-keymap policy in §6c.
- **ASK-7 (new, 2026-08-21)** — **how entangled is Xemu's 45GS02 core
  with MEGA65 plumbing?** Specifically: does the core reach the memory
  decoder and hypervisor through clean read/write callbacks, or is
  hypervisor entry/exit woven into instruction dispatch? The first case
  means minimal trimming; the second is where the real porting effort
  lands. This is a read-the-source task, not a decision, and it should
  be answered before Phase 2 begins — it is the one thing that could
  reopen the §0 scope call.
- ~~**D-09** — native file access~~ — answered by the Meatloaf/FujiNet
  direction (§6b).
- **B-09 stretch** — how far up the resolution list; answered by
  measurement, not decision.
- **Host memory headroom** — how much of the 3B+'s 1 GB is left once
  256 MB of guest RAM, video buffers and the emulator are accounted for.
  A Phase 0 measurement.
- **A-01 stretch** — 8 SIDs, pending the Phase 0 benchmark.
- **New proposal** — multi-layer compositor with per-layer scroll,
  instead of bitplanes (§2). Needs your call.
- **Character set — ballot section J** (§6c), 14 blocks awaiting your edit.
- **Meatloaf/FujiNet stack** (§6b) — accepted as a stretch goal;
  needs system ROM Stage 2 first, and wired Ethernet only unless
  Circle is upgraded.

---

## 9. What this machine is now, in one paragraph

A 45GS02 at unlocked speed — the full MEGA65 instruction set, Q register
and 32-bit flat pointers included — addressing **256 MB of RAM** through
its native 28-bit space, with no hypervisor and no MEGA65 I/O; a video
chip at 640×480 in 256 colours with a programmable palette, a blitter,
sprites with no per-line limit and its own video memory, whose
architecture is the one question still genuinely open; four SIDs
alongside a built-in OPL2 FM chip and a 4-channel PCM DAC, all fitted as
standard rather than plugged in; a real-time clock; **storage as a
filesystem** — a host directory the machine loads from by block DMA,
with no drive emulation, no tape, no disk images in v1 and no cartridge
port; USB gamepads, keyboard and mouse on a single US-International
keymap; a built-in monitor and freezer in ROM; and **no relationship to
the Commodore 64 beyond ancestry** — it boots its own system software,
and every program that runs on it will be written for it. Not a C64
successor: a fantasy 8/16-bit machine that shares the C64's silhouette
and the MEGA65's processor, and owes neither of them compatibility.

---

# BMC-K4510 — Plan

Second plan, adopted 2026-08-22. The first (the VICE route) is kept in
`sources-superseded/PLAN-v1-vice-route.md`. The capability matrix
above is unchanged and is what this plan builds.

---

## 0. What changed, in one paragraph

The machine is no longer "VICE with a new machine directory". It is
**our own core** — Xemu's 45GS02 (unchanged), our memory system, our
video chip, reSID and fmopl as libraries — with a thin SDL2 frontend
for the desktop and an `emux_api` implementation for BMC64's Pi layer.
The VICE route was dropped on 2026-08-21 when it became clear we were
keeping two libraries and a framework full of C64 assumptions, and
that BMC64's Pi layer is emulator-agnostic (`BUILD-LOG.md`). The spike
that tested the alternative — Wozmon on a 45GS02 in an SDL window, no
VICE — ran the same night in 961 lines. As of this draft, **Phases 0
(desktop), 1, 2 and the spine of 3 are done.**

## 0.5 Governing principle — unchanged

A fantasy machine; it only has to *work*. Nothing owes fidelity to any
historical timing. Correctness is architectural: right registers,
flags and memory, not right cycle counts. **New corollary from the
spike:** every subsystem is either a vendored library taken whole, or
ours and small. There is no third category; that was VICE.

---

## 1. Shape of the code

```
k4510/                        t480i5 ~/Projects/BMC-K4510/k4510, git
  core/xemu/    cpu65.c + headers      Xemu, byte-for-byte (GPL2)
  core/         shim, mem, vicky, (sid, opl, io, dma to come)
  sdl/          desktop frontend: window, audio, input
  pi/           emux_api implementation for BMC64 (Phase 6)
  rom/          system ROM sources, ACME --cpu m65
  test/         headless tests; `make test` must stay green
  data/         fonts, palettes
```

Rules that fell out of the spike and should stay rules:

- **`core/` has no SDL, no Circle, no platform.** It takes a framebuffer
  pointer and a key; it returns a frame. Both frontends are thin.
- **Vendored code is never edited.** `cpu65.c` is the model: a shim
  header supplies its world. reSID and fmopl go in the same way.
- **Every capability lands with a headless test** that runs in
  `make test`. The three tests now (`cputest`, `woztest`, `maptest`)
  took minutes to write and caught two of my own mistakes.
- **The ROM is a first-class deliverable** with its own toolchain
  (ACME 0.97 from SourceForge svn — Fedora has none, the GitHub
  mirror is too old).

## 2. Vendored pieces

| Piece | From | Licence | Status |
|---|---|---|---|
| 45GS02 core | Xemu `cpu65.c` | GPL2+ | **in**, unchanged, 60-line shim |
| SID | reSID 1.0 (Dag Lem) | GPL2+ | Phase 4b; the one in VICE 3.3 is fine |
| OPL2 | fmopl (MAME lineage, as in VICE `core/fmopl.c`) | GPL2 | Phase 4b |
| Pi layer | BMC64 `third_party/common` + Circle | GPL3 / BSD | Phase 6, via `emux_api.h` |
| Font | own 8x8 + 8x16, ASCII-ordered | ours | replaces the C64 chargen scaffold |

Not vendored, not wanted: anything from VICE's machine directories,
its monitor, its resource/snapshot framework, its SDL UI.

## 3. Phases — revised

Each phase ends with something that runs and a test that stays green.
Strikethrough = done.

### ~~Phase 0 — desktop toolchain~~ done 2026-08-21
Fedora 43, GCC 15, SDL2, ACME 0.97. `make` builds everything in
seconds. The Pi half of Phase 0 (stock BMC64 boots on the 3B+, baseline
number) moves to Phase 6 — it is not on the critical path any more.

### ~~Phase 1 — skeleton~~ done 2026-08-22
A window, a frame, a running CPU. (`sdl/main.c`, `core/vicky.c` text
layer.)

### ~~Phase 2 — CPU~~ done 2026-08-22
Xemu's core behind eleven callbacks. 6502, 65CE02, flat pointers and
Q ops verified. Wozmon runs from ROM.

### Phase 3 — memory and I/O map
- ~~256 MB lazy-committed physical RAM.~~ done
- ~~MAP with the 45GS02 megabyte convention.~~ done
- **The K4510 I/O map — agreed 2026-08-22.** One 4 KB page at `$D000`
  in the unmapped view; `core/io.h` is the authoritative copy:

  | Range | Device |
  |---|---|
  | `$D000-$D0FF` | VICKY registers |
  | `$D100-$D1FF` | keyboard, joysticks, mouse |
  | `$D200-$D2FF` | DMA (C-18) |
  | `$D300-$D3FF` | storage / host filesystem (D-09) |
  | `$D400-$D47F` | SIDs 0-3 (`$20` each, as on the C64) |
  | `$D480-$D4FF` | OPL2, DigiMAX |
  | `$D500-$D5FF` | system: RTC, timers, IRQ status, reset reason |
  | `$D600-$DFFF` | reserved (E-07 expansion window) |

- ~~Block DMA (C-18): RAM→RAM copy/fill/swap at `$D200`, instant.~~ done
- Storage DMA (D-10): file→RAM via `$D300` + the same engine.
- Interrupts: a single IRQ line with a status/mask register pair; VICKY
  raster and vblank, keyboard, timer. NMI for the monitor's break key.
- **Exit:** `maptest` extended for DMA; Wozmon unchanged on the new map.

### Phase 4a — VICKY
Per `VICKY-SPEC.md` §13, in that order, each a commit with a test:
palette + 8 bpp bitmap layer → remaining depths + layer registers →
tiles → text as a tile variant (retiring the spike's fixed text layer)
→ more layers → sprites → copper → blitter. Own font comes in with the
text step. The register block is the `$D000` page above.

### Phase 4b — sound
reSID ×4 at `$D400`, fmopl at `$D480`, 4-channel PCM. SDL audio
callback in the frontend; `core/` produces samples on request.
**Exit:** a ROM test plays a note on each chip.

### Phase 5 — system ROM (the critical path, unchanged)
- ~~Stage 1: Wozmon-class monitor.~~ done — and it stays as the
  machine's built-in monitor (E-04), reached by NMI.
- Stage 2: system ROM proper — screen/keyboard/file calls, a loader,
  the call interface. Informed by the ASK-9 survey (MORPHEUS, CP/M-65).
  Native filesystem access via `$D300` + DMA.
- Stage 3 (ASK-6): a BASIC, or not.
- **Exit:** boots to its own prompt, loads and runs a program from the
  host filesystem.

### Phase 6 — the Pi

Two routes, decided 2026-08-22 in favour of the first, with the second
kept as the fallback:

**Route A — `circle-libsdl2` (primary).** Xalior's from-scratch SDL2
for bare-metal Circle (github.com/Xalior/circle-libsdl2, zlib licence,
proven by a MAME port). It implements exactly the subset `sdl/main.c`
already uses — software `SDL_Renderer`, streaming ARGB8888 textures,
scaled `RenderCopy`, USB HID keyboard/mouse/joystick, audio callback,
`SDL_RWops` files — on **Pi 3/4/5, AArch64**. The Pi build is then
*our frontend recompiled*: `core/` + `sdl/main.c` + `sdl-app.mk`
after Circle's `Rules.mk`, with the `aarch64-none-elf` Arm GNU
toolchain. Nothing platform-specific is written. What we give up is
BMC64's menu system and virtual keyboard, which we want in our own
form on the desktop anyway.

Its risk is social, not technical: one author, no community. Hence:

**Route B — BMC64 `emux_api` (fallback).** Implement the 78 `emux_*`
functions against BMC64's `third_party/common`, as plus4emu does;
`Makefile-K4510` in the bmc64 tree → `kernel8-32.img` (AArch32).
Gets BMC64's menus, virtual keyboard and Pi-hardening for free. Costs
a second frontend.

Either way: builds on p15 (this is where it earns its keep); Phase 0's
Pi half happens here (stock image first, then ours); `BENCHMARKS.md`
gets its first Pi number.
- **Exit:** the same ROM boots on the 3B+ and on the desktop.

### Phase 7 — clock, polish, snapshots
Find the MHz ceiling on the A53; profile `cpu65_step`. Save-states
(Xemu's core has the hooks but the prefix state is known-incomplete).
Settings, menus (BMC64's on the Pi; minimal on the desktop).

## 4. Open questions carried forward

| Id | Question | Blocks |
|---|---|---|
| ASK-6 | BASIC: EhBASIC / MMBasic / Neo6502 / own / none | Phase 5.3 |
| ASK-8 | Circle Bluetooth HID | Phase 6 |
| ASK-9 | survey MORPHEUS, CP/M-65 for a syscall shape | Phase 5.2 |
| 256-colour ceiling vs RGB path | gates HAM, 1024-colour | Phase 4a / 6 |
| Appendix B | US-International dead-key coverage | §6c |
| Host memory | 256 MB + VICKY buffers + Circle on a 1 GB Pi — measure | Phase 6 |

## 5. Risks — revised

| Risk | Was | Now |
|---|---|---|
| VICE 3.3 age / build fights | Medium | **Gone** — no VICE |
| Xemu core entanglement | Medium–High → Low | **Closed** — it runs |
| CPU↔video sync seam | Medium | **Gone** — both sides line-granular by construction |
| No system software | High | **High**, unchanged; Wozmon is the floor, not the ROM |
| Scope growth | High | **Medium** — the spec is large but every piece is ours and small |
| Pi port | (hidden inside "VICE route") | **Low–Medium** — `circle-libsdl2` makes it a recompile of our own frontend; `emux_api` is the documented fallback |
| Solo scope | Real | Real. Mitigation that worked: desktop-first, tests, one-night steps |

## 6. Repositories

- Code: github.com/mlongval/bmc-k4510 (private); working copy on the
  t480i5, bare mirror on ubuntu-s1 `~/LocalRepositories/k4510.git`.
- Docs: this folder, ubuntu-s1, git.
- The bmc64 fork's `k4510` branch (t480i5) is history; the fork itself
  is kept for Route B.

---

# Appendix A — Ballot section J: character set and text

**STATUS: OPEN — this is the one section still awaiting your decisions.**

Same rules as `FEATURES.txt`: each block lists its choices one per line,
lines marked `>>` are my suggestion. **Delete the lines you don't want,
leaving one choice per block.** Add free text after `NOTE:`. Blocks you
leave untouched, I read as the `>>` line.

The analysis behind these suggestions is in §6c above.

```text
===============================================================================
 J. CHARACTER SET AND TEXT
 Added 2026-08-21. This got missed in the first pass.
===============================================================================
 Three things get conflated here and they are INDEPENDENT decisions:
   (a) ENCODING  - the byte values PRINT/CHR$/files/keyboard use
   (b) GLYPHS    - what the character generator actually draws
   (c) SCREEN CODES - the index stored in screen memory
 A machine can draw PETSCII glyphs while using ASCII encoding, or vice
 versa. Decide them separately.

 TWO FACTS FROM THE TREE, both helpful:
 - Character sets are ALREADY RAM-based, not ROM-based. Kawari fetches
   character pixels from its own video RAM (extraMem) via
   hires_char_pixel_base, so charsets are redefinable by software and
   several can live in video RAM at once.
 - Kawari ALREADY supports a per-character alternate charset: bit 7 of
   the colour byte (altc_bit) selects between two banks PER CELL. This
   kills PETSCII's worst limitation - the C64 could not show lowercase
   and graphics at the same time. We already can.

[J-01] Internal text encoding (what CHR$, PRINT, files and the keyboard use)
       Fact: with no C64 compatibility (I-04), nothing forces PETSCII on
       us. PETSCII's ordering makes sorting and string compare odd, and
       every cross-development tool (cc65, assemblers, editors) plus all
       network content speaks ASCII.
   >>  EXTEND  ASCII-based 8-bit encoding (ASCII low half, chosen set in high half)
       KEEP    PETSCII encoding, for the Commodore feel
       EXTEND  UTF-8 internally (costly on an 8-bit machine, but modern)
       NOTE:

[J-02] Which glyph banks ship in ROM
       4 KB holds 512 glyphs (2 banks of 256 at 8x8). We can ship more
       and page them, since charsets live in video RAM anyway.
   >>  EXTEND  Three banks: PETSCII-style, CP437, and clean ASCII/Latin
       EXTEND  Two banks: PETSCII-style + CP437
       EXTEND  Other: ____
       NOTE:

[J-03] PETSCII GLYPHS (the look - separate from J-01 encoding)
       The blocks, card suits, rounded corners, diagonal lines. This is
       the visual signature of a Commodore machine even if nothing else
       about ours is Commodore.
   >>  KEEP    Ship a PETSCII glyph bank - it costs 2 KB and it IS the look
       DROP    No PETSCII glyphs at all
       NOTE:

[J-04] CP437 vs CP850
       CP437 (IBM PC OEM): full box-drawing and block graphics, plus a
       usable French accent set - e-acute, a-grave, c-cedilla, e-circumflex,
       u-grave and the rest are all present.
       CP850 (Latin-1 "multilingual"): broader accented coverage, but it
       SACRIFICES box-drawing glyphs to get there.
       Since you write in French, note that CP437 already covers French -
       CP850's extra coverage is mostly for languages you likely don't need,
       and it costs you the box-drawing you'd want for TUIs.
   >>  KEEP    CP437 only - covers French AND keeps full box drawing
       KEEP    Both CP437 and CP850 as separate banks
       KEEP    CP850 only
       NOTE:

[J-05] RAM-redefinable character sets
       Already supported by the Kawari fetch path.
   >>  KEEP    Ratify what's already there - software can define its own glyphs
       NOTE:

[J-06] Per-cell alternate charset (Kawari's altc bit)
       Already there: bit 7 of the colour byte picks one of two banks per
       character cell.
   >>  KEEP    Ratify - two charsets on screen simultaneously
       EXTEND  Widen it: more than two banks selectable per cell
       NOTE:

[J-07] Character cell size
       At 640x480: 8x8 gives 80x60 text; 8x16 gives 80x30 (far more readable
       on a modern display).
   >>  EXTEND  Both - 8x8 default, 8x16 selectable
       KEEP    8x8 only
       NOTE:

[J-08] Text modes to offer
   >>  EXTEND  80x60 and 80x30 at 640x480, plus 40x25 for the retro look
       EXTEND  80 columns only
       NOTE:

[J-09] Upper/lower case handling
       The C64 switched whole-screen between two banks. With J-06 we don't
       have to.
   >>  EXTEND  Full upper+lower+graphics available at once, no mode switch
       KEEP    C64-style whole-screen bank switching
       NOTE:

[J-10] Reverse video
       PETSCII burns codes 128-255 on reversed copies of 0-127 - half the
       character space spent on one attribute. With byte-wide colour cells
       (B-08) we have attribute bits to spare.
   >>  EXTEND  Reverse as a colour-cell attribute bit; all 256 codes stay real glyphs
       KEEP    PETSCII-style reversed high half
       NOTE:

[J-11] Keyboard layout
       Fact: the tree ships keymaps for de, da, fi, no, nl - and NO French
       or Canadian-French keymap. You will want one.
   >>  EXTEND  Author a fr-CA keymap (the .vkm format is simple text)
       EXTEND  Author both fr-CA and fr-FR
       KEEP    US only
       NOTE:

[J-12] UTF-8 transcoding for network content
       Once the FujiNet-class stack (sec 6b) fetches real web text, it will
       be UTF-8. Full Unicode is out of scope, but a transcoding layer that
       maps UTF-8 down to the active glyph set is cheap and saves pain.
   >>  EXTEND  Transcode UTF-8 to the active charset in the system ROM
       DEFER   Wait until the network stack exists
       DROP
       NOTE:

[J-13] Line endings in files
       C64 used CR. Unix and most network content use LF.
   >>  EXTEND  LF native, tolerate CR and CRLF on input
       KEEP    CR, Commodore-style
       NOTE:

[J-14] Any PETSCII encoding compatibility at all
       Distinct from J-03 glyphs. This is about whether CHR$ values match
       the C64's.
   >>  DROP    No encoding compatibility - the glyphs are the homage, not the byte values
       KEEP    Keep PETSCII code points so C64 listings paste in
       NOTE:
```
