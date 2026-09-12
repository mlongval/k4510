# VICKY — Video Chip Specification

*VIC-Kawari extended. Pronounced "Vicky".*

The K4510's video chip. Clean-sheet design, decided 2026-08-21
after the survey in `VIDEO-OPTIONS.md`. This document is the capability
list; it is a proposal until you edit it.

---

## 0. Design principles

These five decide every detail below. Where a later item looks
arbitrary, it is one of these applied.

1. **Chunky pixels only.** No bitplanes anywhere in the design. One
   byte load per pixel is what a software renderer is fast at; bit
   gathering across N planes is what it is slow at.
2. **No video memory.** Every pointer is a 28-bit address into main
   RAM. No VRAM, no banks, no data ports, no upload step.
3. **Per-scanline composition.** The chip is defined at line
   granularity, never per cycle. Nothing in it can observe a
   half-drawn line.
4. **Prefer cheap-in-software over authentic-in-silicon.** Where a real
   chip has a limit that exists to save gates, we do not inherit the
   limit.
5. **The register file is ours.** Flat, orthogonal, byte-addressed from
   a documented base. Nothing is squeezed into `$D000`, and no register
   does two jobs.

---

## 1. Display and output

- **Base mode 640×480 @ 60 Hz**, progressive, one fixed video timing.
  No PAL/NTSC switch (B-17).
- **Resolution is a parameter, not a constant.** Guaranteed:
  320×240, 640×200, 640×400, 640×480. Stretch, benchmark-gated:
  800×600, 1024×768. *(Built 2026-08-22: CTRL bit1 = lowres, 320×240
  with every pixel doubled; layers and sprites see the small screen,
  raster lines still count 0–479. Extended 2026-08-25: bit2 halves the
  lines only (640×240), bit3 shortens the field to 200 lines with 40
  blank lines above and below, bit4 quarters the columns (160). The
  shell's MODE 0–4 picks 640×480, 640×240, 320×240, 320×200,
  160×200.)*
- **8 bits per pixel output** into BMC64's indexed framebuffer — 256
  colours on screen. The RGB framebuffer path (`crt_pi_rgb.c`) is a
  documented upgrade, not a v1 requirement.
- **Programmable active area and border.** Border colour register;
  border may be reduced to zero. No forced overscan.
- **Readable current raster line**, raster-compare register, vertical
  blank flag.

## 2. Colour

- **256-entry palette**, 24-bit RGB (8:8:8) internally, truncated or
  dithered to whatever the output path provides.
- **Writable at any time**, including mid-frame. With the copper (§6)
  this means far more than 256 colours per frame.
- **Per-layer and per-sprite palette offset** (4-bit), so a 4 bpp layer
  or sprite selects which 16-colour bank of the 256 it uses.
- **Colour 0 is transparent** in every layer and sprite; the background
  colour register is the ground. (Revised 2026-08-22 during the build:
  the earlier "opaque in the bottom layer" rule hid SHEILA gradients
  under text.)
- No colour cells in the VIC-II sense. **Attribute clash is
  structurally impossible.**
- *Stretch:* a HAM-like layer mode (hold previous pixel, modify one
  channel) for near-truecolour stills. Requires the RGB output path.

## 3. Layers

- **4 layers** in v1. The design is written for N; 4 is the number the
  Phase 0 benchmark is asked to justify.
- Each layer is independently **disabled / bitmap / tile / text**.
- Each layer independently **1, 2, 4 or 8 bits per pixel**.
- Per-layer registers, all independent:
  - 28-bit **data pointer** into main RAM
  - 28-bit **map pointer** (tile and text modes)
  - **width, height and stride** — layer size is independent of screen
    size, so scrolling over a larger virtual canvas is the normal case
  - **scroll X and scroll Y**, pixel-granular, no 38-column trick needed
  - **palette offset**
  - **Z-order slot**
  - **enable**
- **Z-order is fully programmable** between layers and sprite slots.
  Nothing has a fixed priority.
- *Stretch:* per-layer blend modes (additive, average) beyond colour-key
  transparency.

## 4. Tile mode

- **Tile sizes 8×8, 16×16, 32×32, 64×64**, selectable per layer.
- **Tilemaps up to 1024×1024 tiles.** With 28-bit pointers there is no
  reason to be stingy.
- **Per-tile attributes:** 16-bit tile index (65,536 unique tiles),
  horizontal flip, vertical flip, palette offset, priority bit.
- **8 bpp tiles are full-colour tiles** — 256 colours within a single
  tile, no attribute restrictions. This is the MEGA65's Full Colour
  Mode arrived at from the other direction, and the convergence is the
  main evidence that this is the right abstraction.

## 5. Text mode

Text is a tile-mode variant rather than a separate engine.

- **Cell sizes 8×8 and 8×16.** At 640×480 that is **80×60** or
  **80×30** characters.
- **16-bit character index — 65,536 glyphs.** This is what makes
  ballot section J tractable: a full Latin repertoire, box drawing,
  PETSCII, and room for UTF-8 transcoding to land somewhere real.
- **Byte-wide foreground and background colour per cell** (B-08), not
  the VIC-II's nibbles.
- **Per-cell attributes:** reverse, underline, blink, H-flip, V-flip.
- Glyph data is a normal tile set at 1, 2, 4 or 8 bpp — so
  antialiased or full-colour text costs nothing structurally.

## 6. SHEILA — the display-list coprocessor

Named SHEILA (2026-08-22). The cheapest expressiveness in the design, and
the piece with no equivalent in either reference chip; the Amiga's copper
is the ancestor.

- A **list of instructions in main RAM**, pointed to by a register,
  executed once per frame in raster order.
- Instructions:
  - `WAIT` line, optionally line+pixel
  - `MOVE` value → VICKY register
  - `SKIP` if beam past position
  - `JUMP` / end
- What it buys, all without a single CPU raster interrupt:
  - **Palette gradients** — a sky that changes every scanline
  - **Per-line scroll** — parallax on a single layer
  - **Mid-screen mode changes** — hires status bar over a
    multicolour playfield, split screens, HUD bands
  - **Mid-frame layer reconfiguration** of any kind
- Cost to us: a sorted list consulted at scanline boundaries.
  Effectively free.
- Can raise an interrupt at a chosen point in the list.

## 7. Sprites

- **128 sprites.**
- **Sizes 8, 16, 32 or 64 pixels** in each axis, chosen independently —
  so 64×8 and 8×64 are legal.
- **4 bpp or 8 bpp**, with per-sprite palette offset.
- Per-sprite: 28-bit data pointer, signed X/Y (so partial off-screen
  positioning is natural), H-flip, V-flip, Z-slot, enable.
- **No per-line sprite limit and no per-line count restriction of any
  kind** (B-12). Sprite multiplexing is not a technique on this
  machine; it is unnecessary.
- **Collision detection:** sprite-to-sprite and sprite-to-layer
  bitmasks, with interrupt. *(Registers `$90-$9F` / `$A0-$AF` since
  2026-08-22 — they were at `$40`, on top of layer 3's block.)*
- The **sprite attribute table lives in main RAM**, pointed to by a
  register — so 128 sprites cost no register space and the table can be
  double-buffered by changing one pointer.
- *Stretch:* per-sprite scaling (nearest-neighbour).

## 8. Blitter

Starting point is the blitter already in the Kawari port, whose
width/height masks already reach 1024×1024.

- **28-bit source and destination pointers**, arbitrary stride on both.
- Operations: copy, colour-keyed copy, solid fill, AND / OR / XOR.
- **LINE and filled TRIANGLE** (added 2026-08-22: ops 6 and 7, endpoints
  in `$84-$8F`, clipped to `BLTW`×`BLTH`). A 45GS02 plotting pixels one
  far-store at a time ran the cube demo at 15 fps; with these it runs
  at 60. "Prefer cheap-in-software" applied.
- Horizontal and vertical flip during blit.
- **Instant** — no cycle metering, no bus contention (§0.5).
- *Stretch:* scaled and affine blits, the equivalent of VERA's FX, for
  rotation and perspective effects.

## 9. Memory interface

This is the headline consequence of the 45GS02 decision and the thing
neither reference chip can do.

- **Everything is a 28-bit pointer into main RAM.** Layers, tilemaps,
  tile data, glyphs, sprite tables, sprite data, the copper list.
- **No VRAM. No banking. No windows. No upload step.**
- CPU writes are visible to the chip immediately; there is no bus to
  contend for.
- A layer can be pointed at a different tilemap by writing one
  register, which makes double buffering and page flipping trivial at
  every level of the design.

## 10. Timing and interrupts

- CPU synchronises with VICKY at **raster-line granularity** (C-10).
- Interrupt sources: vertical blank, raster compare, copper
  instruction, sprite collision.
- **No badlines. No sprite/raster fetch conflicts. No grey dot. No
  cycle-exact behaviour of any kind.**

## 11. Explicitly not in VICKY

Stated so the boundary is not re-litigated later:

- No VIC-II compatibility, no VIC-II register map, no `$D000` layout
- No bitplanes
- No colour cells / attribute clash
- No 16 KB video banks
- No PAL/NTSC modes
- No cycle-exact or per-cycle behaviour
- No light pen

---

## 12. Register model

- Small, flat register file at a documented base: global configuration,
  then **per-layer register groups at a fixed stride**, then pointers.
- **Large structures live in main RAM, not in registers** — the sprite
  attribute table and the copper list are each a single pointer. This
  is what keeps 128 sprites from consuming a register file, and it
  makes both structures double-bufferable.
- Every register does exactly one thing. No mode bits that reset other
  registers as a side effect, and no hot registers.

---

## 13. Implementation order

Each step independently shippable, cheapest first. Phase 4 of the plan.

1. **Palette + one 8 bpp bitmap layer.** Proves the output path and
   retires the inherited VIC-II.
2. **Remaining bit depths and the layer register model.**
3. **Tile mode**, then text mode as a tile variant.
4. **Additional layers**, up to the benchmark's answer.
5. **Sprites.**
6. **SHEILA.**
7. **Blitter** (port and extend the existing one).
8. Stretch items: HAM-like mode, blend modes, scaled blits, sprite
   scaling.

The VIC-II is removed at step 1, not before — Phase 1 keeps it purely
as scaffolding so the machine has something that renders.

---

## 14. Numbers the benchmark decides

Not design choices. Phase 0 and Phase 4 answer these with
measurements:

- **Layer count.** 4 is the proposal. 640×480 at 60 Hz is ~18.4 M
  pixel-writes per second per full-screen layer.
- **Resolution ceiling.** 640×480 guaranteed; 800×600 likely;
  1024×768 open.
- **Sprite count in practice.** 128 is the register-model limit;
  what renders at frame rate with layers active is measured.

## 15. Open

- ~~Does the copper get its own name?~~ **SHEILA** (Doc, 2026-08-22).
- **Is 256 on-screen colours the ceiling**, or do we cost out the RGB
  framebuffer path now? HAM-like modes and any per-class palette scheme
  depend on it.
- **Glyph format for text mode** — ties into ballot section J, still
  open in Appendix A of the design document.
