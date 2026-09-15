# The sidebar zip

What a sidebar is on the disk: step 2 of `docs/SIDEBARS-PLAN.md`
(2026-09-15). `tools/mksidebar.py` packs one and checks one; `make test`
checks every zip in `/SYSTEM/SIDEBARS`.

## Where

    /SYSTEM/SIDEBARS/NAME.ZIP       the sidebar: read-only, never written
    /SYSTEM/SIDEBARS/NAME/          made on first use (step 5): OPTIONS.CFG, STATE.DAT

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
  tetris antfarm`.
- `scene` and `program NAME.PRG` -- planned (pictures and a description;
  a K4510 program on the Tube). `mksidebar.py` refuses them until they are
  drawn.

A zip never carries host code: there is no way to write one that does.

## OPTIONS.CFG

The same format. The zip's copy is the defaults and their explanation; on
first use it is copied to `/SYSTEM/SIDEBARS/NAME/OPTIONS.CFG`, which is the
one you edit (F12 → Edit options). Every sidebar has `speed` (0.25 to 4, 1
as designed); the rest are its own.

## The built-in sidebars

Their sources are `sdl/sidebars/NAME/` (lower case); `make` packs each into
`fs/SYSTEM/SIDEBARS/NAME.ZIP`. The zips are tracked, like the programs in
`fs/SYSTEM/BIN`, and `check-artifacts` fails a build whose zips are not what
their sources pack to.
