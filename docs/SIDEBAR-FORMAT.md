# The sidebar zip

What a sidebar is on the disk: step 2 of `docs/SIDEBARS-PLAN.md`
(2026-09-15). `tools/mksidebar.py` packs one and checks one; `make test`
checks every zip in `/SYSTEM/SIDEBARS`.

## Where

    /SYSTEM/SIDEBARS/NAME.ZIP       the sidebar: read-only, never written
    /SYSTEM/SIDEBARS/NAME/          made when it is first shown: OPTIONS.CFG, and STATE.DAT if it keeps one
    /SYSTEM/SIDEBARS/SIDEBARS.CFG   made then too: what is not one sidebar's

`NAME` is up to 16 capital letters, digits, `_` or `-`; the file ends in
`.ZIP`.
Installing a sidebar is copying its zip in; removing it is deleting the zip.

## What the zip holds

    SIDEBAR.INF     required: what it is and how it is drawn
    OPTIONS.CFG     its options and their defaults, commented
    PREVIEW.PIC     its picture, for the menu and the handbook (step 4 makes these)
    ...             anything else it needs, in any folders

Stored or deflated, names without `../`, a leading `/` or a backslash --
what `MOUNT` accepts. The built-in sidebars' zips are stored and dated
1980-01-01 so the same files always make the same bytes.

## SIDEBAR.INF

One `name = value` a line; `#` starts a comment; names are lower case.

| name | | |
|---|---|---|
| `name` | required | what the menu shows: `Ant farm` |
| `about` | required | one line, at most 60 characters |
| `author` | required | |
| `version` | required | a whole number, raised with each release; `STATE.DAT` from another version is set aside (step 5) |
| `draw` | required | how the picture is made, below |
| `season` | | the months it belongs to, `10` or `11, 12`; empty for any time |

`draw`:

- `builtin NAME` -- drawn by the emulator's own code. `NAME` is one of
  `border gradient knot registers halloween christmas space river dreamfall
  tetris antfarm matrix`.
- `scene` and `program NAME.PRG` -- planned (pictures and a description;
  a K4510 program on the Tube). `mksidebar.py` refuses them until they are
  drawn.

A zip never carries host code: there is no way to write one that does.

## OPTIONS.CFG

The same format. The zip's copy is the defaults and their explanation; when
the sidebar is first shown it is copied to `/SYSTEM/SIDEBARS/NAME/OPTIONS.CFG`,
which is the one you edit (F12 → Video → Edit options: VI on it, at the
prompt). It is read again within a second of being saved. Every sidebar that
moves has `speed` (0.25 to 4, 1 as designed; anything else is 1); the rest are
its own -- the ant farm's `day` (`real`, the host's clock, or a length:
`30m`, `1h`).

## SIDEBARS.CFG

For all of them; which one is shown stays F12 → Video → Sidebar (saved in
`k4510.cfg` as `video.sidebars`).

    right   = same     # the right-hand side: same, or another sidebar's name
    change  = never    # or how often the next one comes: 10m, 1h, 1d
    seasons = on       # while changing, a seasonal sidebar only in its months

Changing goes round every sidebar but the register panel, in order, from the
clock -- nothing is written, and a machine switched on at 10:05 with
`change = 10m` shows what it would have shown had it been on all along. A
sidebar you choose yourself is shown whatever the month.

## STATE.DAT

What a sidebar keeps across a power cycle -- the ant farm's colony -- written
every five minutes and when the emulator stops, by way of `STATE.NEW`. A
header line names the sidebar and its `version`; one written by another
version is renamed `STATE.OLD` and the sidebar starts new. Delete the folder
and the sidebar is as it came.

## The built-in sidebars

Their sources are `sdl/sidebars/NAME/` (lower case); `make` packs each into
`fs/SYSTEM/SIDEBARS/NAME.ZIP`. The zips are tracked, like the programs in
`fs/SYSTEM/BIN`, and `check-artifacts` fails a build whose zips are not what
their sources pack to.
