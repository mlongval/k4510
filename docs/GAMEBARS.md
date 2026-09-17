# Gamebars — the sidebars, when a game has the machine

Doc, 2026-09-17:

> Would like to have DOOM themed artwork that comes up if sidebar(s) is just
> background. Something like I would have seen in an arcade. Let's call them
> gamebars (later could become dynamic or perhaps a game could take over the
> gamebar as a second display —— mini maps come to mind.

Status: **a plan, not built.** Written down while the thought was fresh.

## What it is

A cabinet in an arcade was never just a screen. The screen sat in the middle
of *art* — a painted side panel, a marquee over the top — and the art was how
you knew, across a room, what the machine was. The K4510 has exactly that
shape already: a picture in the middle and two strips either side of it, which
today show a border, a gradient, a knot, or the digital rain.

A **gamebar** is a sidebar that belongs to whatever is running. While DOOM has
the Tube, the strips beside it stop being a generic screensaver and become
that game's side panel.

## The three stages, in the order they are worth doing

**1. Painted.** A static side panel per game, drawn once, shown while that
game runs. Cheap, and it is most of the effect: what sells an arcade cabinet
is that the art is *specific*.

**2. Dynamic.** The same panel, but alive — flame that licks, a marquee that
pulses, something that reacts to the clock the way every sidebar already does
(they are pure functions of time, `s_NAME(cv_t *c, uint32_t t, int side)`).

**3. The game drives it.** The interesting one. The co-processor gets a second
surface and draws whatever it likes there: **a mini-map**, a weapon rack, the
health of the thing chasing you. The machine stops being a screen with
decoration around it and becomes a two-display cabinet.

Stage 3 is not far off, structurally. DOOM already writes its frame into a
shared segment and the emulator blits it (`core/io.c`, `struct doom_shm`).
A second, narrow framebuffer in that same segment — say 120x480 a side, with
its own sequence counter — would be drawn by exactly the same path. The
co-processor would need a way to say "I am driving the bars now", and the
sidebar layer would need to defer to it.

## What has to be decided

- **When does a gamebar take over?** Doc's phrasing is "if sidebar(s) is just
  background" — so: when the chosen sidebar is a plain one (border, gradient),
  a game may claim the strips; when the user has deliberately picked a scene
  (the ant farm, the Matrix rain), it should probably keep it. That wants a
  setting, not a guess.
- **Whose art?** DOOM's own sprites are in the WAD and Freedoom's are BSD, so
  they *may* be used — but a panel assembled out of game sprites looks like a
  screenshot, not like a painted cabinet. Drawing it in the machine's own
  idiom (the way `matrix.c` uses the machine's font rather than an imitation)
  is likely to look better and is certainly more honest.
- **Where do they live?** Sidebars are zips in `/SYSTEM/SIDEBARS` with an
  `SIDEBAR.INF` and `OPTIONS.CFG`, plus twelve built-ins. A gamebar is
  probably a sidebar with an extra line in its `.INF` saying which program it
  belongs to.
- **The strips are narrow.** 120 pixels at the usual size. Arcade side art is
  tall and thin, which suits it — but the sidebar only exists when the picture
  does not fill the frame, so a gamebar is invisible in full screen or at
  640x480 windowed. Worth knowing before promising anyone a marquee.

## Related

- `docs/SIDEBAR-FORMAT.md` — what a sidebar is and how one is packaged
- `core/sidebars.c`, `sdl/sidebars/*.c` — the twelve built in
- `core/io.c`, the DOOM shared segment — the road a stage-3 gamebar would take
