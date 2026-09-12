# K4510 — Video Architecture Options

Input for the video conversation flagged in §2 of `K4510-Design.md`.
Nothing here is ratified; §2 stays frozen until you make the call.

Written 2026-08-21.

---

## 0. Why this is a live question now, and not before

The video plan — VIC-II as substrate, Kawari extensions bolted on,
extend outward — was written when the machine was a C64 successor with
1 MB of RAM. Three decisions since have quietly removed most of its
supports:

- **I-04 dropped C64 compatibility.** No C64 software will ever run on
  this machine. The VIC-II is therefore not a compatibility
  requirement; it is inheritance, exactly like the emulated disks you
  just deferred for the same reason.
- **256 MB and 32-bit flat pointers (C-03/C-15).** The VIC-II's world
  is 16 KB banks reached through a two-bit register. Our CPU can
  dereference anywhere in 256 MB in one instruction. Building the video
  chip around 16 KB banks would be the one part of the machine that
  still thinks it is 1982.
- **The strip-early principle (Phase 1).** You applied it to storage,
  cartridges and host subsystems: don't carry it in, don't delete it
  later. The VIC-II is the largest single thing being carried in, and
  nobody has applied the test to it.

There is also a plan-internal reason. The largest performance lever in
the whole project is already documented as "thin `viciisc`'s per-cycle
machinery down to per-scanline rendering" — that is, gut the VIC-II
implementation we inherited. If we are going to gut it anyway, the
1,647-line Kawari back-port is worth considerably less as a head start
than its line count suggests.

---

## 1. The inversion that governs every option below

**We are writing software, not silicon.** This is stated in §0.5 as a
timing principle, but it has an under-used consequence for video: the
costs that shaped every chip we might copy are not our costs.

| Real silicon | Us |
|---|---|
| Gates, pin count, fetch windows | Free |
| Memory bandwidth in a fixed cycle budget | Effectively free |
| Bitplane fetch (parallel, cheap in HW) | **Expensive** — per-pixel bit gathering across N planes |
| Chunky/packed pixels (needs wide bus) | **Cheap** — one byte load per pixel |
| A sprite limit of 8 (comparators cost gates) | Just a loop bound |
| A coprocessor that rewrites registers per raster line | **Nearly free** — a sorted list consulted at line boundaries |
| Per-pixel work in the render loop | **The one real cost** |

Two conclusions fall out immediately, and they point in opposite
directions from the obvious retro instincts:

1. **Do not borrow the Amiga's bitplanes.** They are the single worst
   fit for a software renderer. (The MEGA65 team reached the same
   conclusion from a different direction — the VIC-IV deprioritises
   bitplane modes in favour of character and bitmap modes on grounds of
   memory efficiency and ease of animation.)
2. **Do borrow the Amiga's copper.** A display-synchronised list that
   pokes registers at given raster positions is close to free for us
   and is the cheapest expressiveness in the entire design space:
   gradients, mid-screen mode changes, per-line scroll for parallax,
   split screens — all without a single CPU raster interrupt.

The real budget: 640×480 at 60 Hz is ~18.4 M pixel-writes per second
per full-screen layer. Four layers plus sprites is the number that has
to be measured on the A53, and it is the *only* number that constrains
any option here.

One hard constraint applies to all options: **BMC64's framebuffer is
8-bit palettized** (`SetPalette(u8 index, u16 rgb565)`). 256
simultaneous colours is the natural ceiling. Anything beyond that
requires switching BMC64 to its RGB path (`crt_pi_rgb.c`, which exists
alongside `crt_pi_idx.c`) — a real but bounded change.

---

## 2. The candidates

### A. Stay the course — VIC-II + Kawari ("VICKY" as an extension)

What the *real* Kawari actually offers, for calibration, since we have
been treating it as more than it is: 640×200 16-colour bitmap with 8×8
colour cells, 320×200 16-colour in 2 bitplanes, 640×200 4-colour in 2
bitplanes, 80-column text, extra video RAM, and a PAL/NTSC switch. It
is a very good *VIC-II replacement*. It is not a modern video
architecture, and there is essentially no software for its extensions —
which you already noted is what licenses the artistic freedom.

**For:** the only option with code already written (14 files, +1,647
lines, on `kawari-phase1`). Phase 1 has something that renders on day
one. Continuity with the "K" in the machine's name.

**Against:** inherits the VIC-II's cramped `$D000` register space,
16 KB bank thinking, colour-cell attribute model, and badline logic —
all of it silicon economy we do not pay and cannot benefit from. The
extensions live in `extraRegs[64]`, which is a bolt-on, and every new
feature makes the seam wider. Carrying it forward contradicts the
strip-early rule everywhere else in the plan.

### B. Port the VIC-IV (the MEGA65 sibling)

Coherent with the CPU decision: same machine lineage, and the cover of
the design document now names the MEGA65 as an ancestor.

The VIC-IV is a rasterised chip with no framebuffer, supporting all
VIC-II and VIC-III modes plus its own. The genuinely good idea in it is
**Full Colour Mode**: a character becomes 64 bytes — one byte per pixel
— so text mode and 256-colour graphics stop being different things. Add
16-bit character mode (8,192 unique characters per screen) and you have
a tile engine wearing a character-mode costume. It also runs separate
256-colour palettes for sprites, bitplanes and characters, so up to
1,024 colours can be on screen at once.

**For:** proven design, documented, an obvious sibling relationship,
and Xemu — already our CPU donor — implements it.

**Against:** it is large, and much of its size is VIC-II and VIC-III
backward compatibility we have explicitly refused. Its legacy "hot"
registers (`$D011`, `$D016`) reset video modes as a compatibility
behaviour — we would be porting bug-compatibility for software that
will never run. Taking the VIC-IV also makes the machine a MEGA65
clone in the one area where you have the most freedom to be
interesting. The 1,024-colour capability requires the RGB framebuffer
path.

### C. A VERA-class design (Commander X16)

The closest thing to a direct precedent: a video chip designed from
scratch, in the modern era, for a *new* 8-bit machine. Worth studying
even if we build our own.

Its shape: 128 KB of VRAM off the CPU bus, reached through two data
ports with programmable auto-increment; two layers, each independently
tile or bitmap at 1–8 bpp; 128 sprites from 8×8 to 64×64 at 4 or 8 bpp
with four Z-depth levels and a collision bitmask with interrupt; a
256-entry palette of 12-bit colour; and a display composer with
independent horizontal and vertical scaling, adjustable active area and
border. Its "FX" feature set is mostly addressing-mode tricks that
*assist* CPU rendering (affine transforms, polygon fill, cache
operations) rather than accelerating it.

**For:** the ergonomics are demonstrably good — people write real
software for it. The layer/composer/sprite-table model is exactly the
right abstraction for a software renderer: composition per scanline,
chunky pixels, no bitplanes. It is well documented, so we can crib the
*model* without porting anything.

**Against:** nothing to borrow in code (it is Verilog). Two layers is
fewer than we want. Its 128 KB VRAM behind data ports is a hardware
constraint we do not share and should not imitate — see §3.

### D. Amiga model — bitplanes + copper + blitter

**For:** the copper is the best idea in 1980s home computing and the
blitter model is proven. Names like Agnus/Denise/Paula are the reason
"VICKY" is a good name.

**Against:** bitplanes, for the reasons in §1. Borrow the copper, leave
the planes.

### E. Atari ANTIC/GTIA — the display list

**For:** ANTIC's display list is the same insight as the copper arrived
at independently: the screen is a *program*, not a fixed layout, and
mode can change per scanline. Extremely cheap for us. Mixed-mode
screens (a hires status bar over a multicolour playfield) fall out for
free.

**Against:** as a whole architecture it is dated and its colour model
is poor. Borrow the display list, leave the rest.

### F. Smart peripheral — the Agon / Neo6502 model

Here the "video chip" is really a coprocessor driven by a command
protocol (VDU sequences, or an RP2040 executing graphics primitives),
not a register file you poke.

**For:** by far the easiest to implement — the commands become direct
calls into our renderer. Rich primitives are nearly free.

**Against:** it changes what kind of machine this is. Programs would
call a graphics API rather than program a chip, which forfeits the
poke-and-see immediacy that makes 8-bit machines worth emulating at
all. Also puts a protocol between software and the screen, so the
demoscene-adjacent tricks the copper enables become impossible. I would
rule this out on identity grounds, not technical ones.

### G. Clean sheet — VICKY as an actual chip design

Design the chip the fantasy machine deserves, informed by all of the
above.

Concretely, what I would specify:

- **N layers** (4 as a starting number), each independently **tile or
  bitmap**, each at **1/2/4/8 bpp**, each with its own scroll registers
  — the VERA model, widened.
- **Chunky pixels only.** No bitplanes anywhere in the design.
- **Tiles at 8 bpp are the VIC-IV's Full Colour Mode**, arrived at from
  the other direction. The convergence of two independent designs on
  the same abstraction is the strongest evidence available that it is
  the right one.
- **Layers address main RAM by 28-bit pointer.** No separate VRAM, no
  data ports, no 16 KB banks. This is the piece neither VERA nor the
  VIC-IV can do, and our CPU decision hands it to us: a layer's tilemap
  is just a pointer, anywhere in 256 MB. It is simpler than what we
  inherited, not more complex.
- **A sprite table of 128**, 8×8 to 64×64, 4/8 bpp, per-sprite palette
  offset, Z-order against layers, collision bitmask with interrupt.
  (Directly VERA's numbers; they are sensible and battle-tested.)
- **A copper / display list.** Register writes scheduled at raster
  positions, held as a sorted list in main RAM. The cheapest
  expressiveness in the design.
- **A blitter**, taking the in-tree Kawari one as the starting point —
  its width/height masks already reach 1024×1024.
- **A 256-entry palette** at 24-bit RGB internally, dithered or
  truncated to whatever the framebuffer path gives us.

**For:** it is the machine's identity, it is *simpler* than porting
either real chip, and every part of it is a thing a software renderer
does cheaply. Nothing in it is speculative — every element is lifted
from something that shipped.

**Against:** no code to start from. Video joins the CPU as a
from-scratch subsystem, and the project acquires a second large unknown
at the same time as the first.

---

## 3. The observation I would most want you to react to

**Our CPU decision has already obsoleted the concept of "video memory".**

Every chip above hides video RAM behind something: the VIC-II's 16 KB
banks, VERA's data ports, the Kawari port's private 24-bit video
memory. All three exist because on real hardware the video chip and the
CPU cannot both have the bus.

We do not have a bus. We have 256 MB and a CPU that reaches all of it
with a 32-bit pointer in one instruction. A layer can simply *be* a
pointer to a tilemap and a pointer to tile data, anywhere in physical
memory. No banking, no upload step, no window.

This is the single most modernising thing available to the design, it
costs nothing, and it is unavailable to *both* reference chips. It also
happens to be exactly the kind of thing §0.5 exists to authorise.

---

## 4. Comparison

| | Code to start from | Fits software rendering | Identity | Implementation size |
|---|---|---|---|---|
| A. VIC-II + Kawari | **Yes, substantial** | Poor — cycle-oriented, colour cells | Continuity with the name | Small now, grows badly |
| B. VIC-IV port | Yes, via Xemu | Fair — no bitplane reliance | MEGA65 clone | **Large** |
| C. VERA-class | No | **Good** | Borrowed from X16 | Medium |
| D. Amiga model | No | **Poor** (bitplanes) | Strong | Medium |
| E. ANTIC model | No | Good | Dated | Small |
| F. Smart peripheral | Partly (Circle) | **Excellent** | **Wrong** — it's an API, not a chip | **Small** |
| G. Clean sheet | Blitter + palette path only | **Excellent** | **Ours** | Medium |

---

## 5. Recommendation

**G, sequenced so it doesn't stall Phase 1.**

The reasoning is the same one you have applied twice already today. The
VIC-II is inheritance, not a requirement: no software needs it, it
shapes the design around constraints we do not have, and keeping it
means carrying a cycle-exact renderer we have already committed to
gutting. Under your own strip-early rule it does not survive the test.

What G costs is real — it forfeits the one head start the project had.
Two things reduce that cost more than they first appear:

1. The head start lives in `viciisc`, which the plan already intends to
   thin down to per-scanline rendering. Much of those 1,647 lines is
   scheduled for demolition regardless.
2. The parts of the Kawari work that survive any decision — the RGB
   palette path, `overlayMem[256]`, the blitter, and the BMC64
   framebuffer plumbing — are the parts we would keep in option G
   anyway. It is the VIC-II *around* them that goes.

**Sequencing, so Phase 1 still has something that renders:**

- **Phase 1** keeps the inherited VIC-II purely as scaffolding — the
  exit criterion is "the machine initialises and produces a frame", and
  any renderer satisfies it. Do not extend it.
- **Phase 4** builds VICKY properly, in the shippable order the plan
  already uses: palette and chunky bitmap layer first, then tiles, then
  sprites, then the copper, then the blitter. The VIC-II is removed
  when the first VICKY layer renders, not before.
- The **per-scanline rewrite** stops being a rewrite of somebody else's
  cycle-exact chip and becomes simply how VICKY is written in the first
  place. That is a strictly easier job than the plan currently
  describes.

If you would rather not take on a second from-scratch subsystem, the
honest fallback is **C** — build VERA's documented model rather than
designing one, accepting two layers and its VRAM conventions in
exchange for a specification somebody else has already debugged. I do
not think you want that, but it is the sane conservative answer.

---

## 6. Your three notes in §2, answered

**"VIC-Ke, pronounced Vicky."** Adopted throughout this document. It
survives every option including the clean sheet — the "K" reads as
Kawari-heritage in option A and as the machine's own initial in option
G, which is a good property for a name to have. It sits correctly
beside Agnus, Denise and Paula.

**"Would 256 colours at 18 or 24-bit RGB with an on-the-fly
redefinable palette give us something like HAM?"** No — those are
different mechanisms, and it is worth being precise because the answer
turns into a feature.

A 256-entry palette selected from 18- or 24-bit colour is the Amiga's
*AGA 256-colour mode* (or VGA's mode 13h): 256 on screen, chosen from
many. **HAM** — Hold And Modify — is a different trick entirely: each
pixel either selects a palette entry *or* takes the previous pixel's
colour and modifies one channel of it. That yields thousands of
on-screen colours at the cost of horizontal fringing, because you need
up to three pixels to reach an arbitrary colour.

The interesting part: HAM is cheap for us. It is a per-pixel branch in
a renderer we are writing anyway — no silicon, no bandwidth argument.
A "HAM-like" layer mode is a plausible VICKY feature and would give
near-truecolour still images on a machine that otherwise shows 256
colours.

The catch is downstream and unavoidable: HAM needs more than 256
distinct output values, so it requires BMC64's **RGB framebuffer path**
(`crt_pi_rgb.c`) rather than the indexed one. That is the same
prerequisite as the VIC-IV's 1,024-colour capability. Worth costing
once, since two separate wish-list items depend on it.

**"I want another conversation about this before deciding."** This
document is the input to it. §2 of the design document is unchanged and
will stay that way until you decide.

---

## 7. What I would need from you

1. **Clean sheet, or continue the VIC-II?** Everything else follows.
2. **Is 256 on-screen colours the ceiling, or do we cost out the RGB
   framebuffer path?** This gates HAM-like modes and any per-class
   palette scheme.
3. **How many layers?** Four is my proposal; it is a benchmark question
   more than a design one, and Phase 0 can answer it.
