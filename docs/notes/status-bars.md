# The status bands: software-definable, and what goes in them

Doc, 2026-09-01, raised three things together: make the bands software
definable (0-10 rows at 640x480, 0-5 at 640x240), decide what actually
goes in them, and make the clock and date format the user's choice
(12h/24h among others). This is the design discussion, not a decision.

## What exists today

Status mode is an F7 boolean that reaches the guest as `$D521` bit 3, and
it works in the 80-column modes only. `video_init` then sets:

| Mode | rows | top band | bottom band | console |
|---|---|---|---|---|
| 640x480 | 60 | 4 (`PROWS/15`) | 6 (`PROWS/10`) | 50 |
| 640x240 | 30 | 2 | 3 | 25 |

Row 0 is the title bar, row `PROWS-1` the status bar, and the rows
between are blank spacers — the comment in `rom/kernal.c` already says
they are "where the widgets go later", so this was always the plan.

Four widgets: `K4510  K/OS` top left, `HH:MM DD.MM.YYYY` top right,
`status mode` bottom left, `NN MHz` bottom right.

Two mechanisms matter for what follows:

- **The console is a scroll region between the bands.** `scroll()` only
  ever moves `OY..OY+ROWS-1`, so the bands sit still — a DECSTBM done in
  the machine's own layout — and `video_init` publishes the result to
  JIM as COLS/ROWS/OX/OY (`$DA05`-`$DA08`).
- **The IRQ paints the clock.** `crt0.s` repaints eight digit cells every
  minute whenever `bband != 0`, so the clock ticks inside a program that
  never calls the console. `draw_clock` lays the whole string down once;
  the IRQ only ever rewrites digits.

Note what Doc's proposed limits are: 0-10 and 0-5 are exactly today's
**totals** (4+6, 2+3). So this is not asking for more screen furniture,
it is asking for today's fixed split to become a choice.

## Mechanism: most of it already exists

The interesting discovery is that setting band heights *is* setting the
console's origin and extent, and JIM's `OX`/`OY`/`COLS`/`ROWS` registers
are **already writable**. What is missing is not a mechanism but three
agreements:

1. **The ROM must stop fighting.** `cls()` calls `draw_bands()`, which
   repaints the ROM's four widgets, and the IRQ repaints the clock. Both
   would scribble over anything a program drew.
2. **Caps.** Nothing stops a program setting a console of zero rows and
   losing the shell inside its own furniture.
3. **A way to give it back**, so leaving a program does not leave the
   machine wearing its bands.

Suggested shape, and it is small:

- Two new JIM registers — `$DA0F` and `$DA16` are free — `BANDTOP` and
  `BANDBOT`, in rows. Writing either re-lays the console and republishes
  the geometry. **Both zero means status mode off**, which folds the
  existing F7 boolean into the same control rather than leaving two ways
  to say it.
- One spare `FLAGS` bit: *the bands are the program's*. While it is set,
  `cls()` leaves them alone and the IRQ does not paint the clock.
- Clamp so the console keeps a workable minimum — 10 rows, say. A
  program asking for more band than that gets the clamp, not the ask.

**Why registers and not an escape sequence.** DECSTBM (`ESC[t;br`) is the
VT100 way to set a scroll region and JIM already implements it — but it
sets a region *inside* the terminal window, and the bands are outside
it. Using DECSTBM would make the bands part of the console and therefore
scrollable through, which is exactly what they must not be.

**Why not a ROM call.** ROM is the scarcest thing here: 627 / 690 / 694
bytes free in the three resident areas, and bank 2's alias table already
collided with the palette once when that bank filled. Registers cost the
ROM nothing.

**Where the state lives.** Not ROM BSS — `BSSR` is 447 of 448 bytes used
and fails loud. This is the standing constraint: new state goes in VICKY,
in JIM, or in a bank.

The hand-back discipline already has a precedent worth copying exactly:
PETSCII mode is `FLAGS` bit 2, a program sets it and must clear it, and
`test/jimtest.sh` checks that the shell survives a program that forgot.
Whatever the bands end up as, the test should be the same shape.

## What goes in them

A principle to argue from, because otherwise this becomes a list of
everything:

> **A status bar should carry what is otherwise invisible, and what
> changes without being asked.** Not what you can go and ask for.

Today's four widgets, scored against that:

| Widget | Verdict |
|---|---|
| `NN MHz` | **Earns it twice.** The governor steps the clock DOWN on its own when frames run late. This is the one widget that shows the machine doing something behind your back. |
| the clock | Earns it. Changes on its own; nothing else displays it. |
| `K4510  K/OS` | Decoration. It tells you what you already knew and never changes. |
| `status mode` | Decoration. It names the thing you are looking at. |

So half the furniture is a nameplate. That matters because the top-left
is the best real estate on the screen and it is spent on a constant.

Candidates, ranked by the same test:

1. **Which sound chip has the machine** (OPL2 / reSID). After
   2026-09-01 the first question anyone will ask is why `SIDS` is
   silent, and the answer is currently only in `INFO`.
2. **Host load** — ms of frame, or a two-cell bar. The machine is a
   fantasy whose speed is a property of the host; this is the number the
   governor watches, and showing it makes the machine's honesty visible.
   It is also the most K4510-ish thing that could be up there.
3. **The Tube** — whether BBC BASIC or CP/M has core 3. Invisible
   otherwise, and it explains where the sound went on a Pi.
4. **Caps lock.** Cheap, genuinely invisible, and classic.
5. **Network** — `N:` activity, and the remote directory when
   `CD tnfs://` is in force.
6. **Bank registers.** The machine's signature feature and a
   programmer's readout — but for a programmer's band, not the default.
7. **Disk light** — `$D300` busy. Charming; nearly free.

Not the working directory: the shell prompt already has it. It only
earns a place for a full-screen program that has taken the prompt away.

**The recommendation, and it is a direction rather than a list.** Do not
grow the ROM's widget set. The ROM cannot afford it, and a fixed set is
wrong for somebody by definition. The clock already shows the shape of
the answer: *a table the IRQ walks*. Generalise what `crt0.s` does for
eight digit cells into a small table of (cell, source, format) entries
that the IRQ paints once a frame or once a minute. The ROM ships a
default table; a program replaces it and hands it back. That gets
software-definable content without a program having to run continuously,
which matters because this machine has no multitasking and a painter
that only runs when its program does is not a status bar.

First step, if this is wanted before the whole design: the heights and
the ownership bit. The widget table can come later and the two do not
block each other.

## The clock and the date

Doc wants the format to be the user's: 12h/24h, and the date order.

**The constraint that shapes this.** The IRQ painter rewrites eight
digit cells at *fixed positions* and knows nothing else about the
layout; `draw_clock` lays the separators and the year once. That is why
the clock is nearly free, and it is worth keeping. So:

- **Keep every field fixed width**, whatever the format. 12h pads the
  hour (`01:05`, or ` 1:05`) rather than shrinking the field. The IRQ
  stays a dumb digit-poker and the format only decides which cells it
  writes.
- **AM/PM** is two more cells that change twice a day. Let `draw_clock`
  lay them and have the IRQ rewrite them only when the hour rolls — or
  simply accept two more cells in the per-minute repaint, which is
  cheaper to write than to optimise.
- **Date orders worth having:** `DD.MM.YYYY` (today), `YYYY-MM-DD` (ISO,
  and the only one that sorts), `MM/DD/YYYY`. Three is enough; a general
  strftime is not the K4510's kind of feature.

**Where the setting lives.** This is a *user preference*, not a program's
business, so it belongs in the F7 menu and the settings file, reaching
the guest the way the other menu settings do. But `$D521` is **full** —
bit 0 CP/M-by-name, bit 1 margin, bit 2 STARTUP.BAT, bit 3 status mode,
bit 4 mode request, bits 5-7 the video mode. A second options byte is
needed. `$D52D`-`$D52F` are free (SYS is used up to `$D52C`, then
`$D530`-`$D539`), so `$D52D` as SYSOPT2 with one bit for 12/24h and two
for the date order.

That also gives the next three menu settings somewhere to land, which
`$D521` cannot do for anybody.

## Open questions for Doc

- Should the two bands be set **independently** (top and bottom), or as
  one total the ROM splits as it does now? Independent is more useful
  and no harder.
- When a program claims the bands, should the ROM's widgets come back
  automatically when it exits, or only when asked? (PETSCII mode says:
  the program must hand it back, and the test enforces it.)
- Is the **top-left nameplate** worth keeping for the look of the thing,
  or should it go and take a widget that changes?
