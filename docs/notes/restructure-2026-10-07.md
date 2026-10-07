# Restructure, 2026-10-07 (branch `restructure`)

Doc: "review and restructure/reengineer the core/io.c and any other
libraries that are too monolithic"; "jettison the trial balloons".

## core/io.c: one file a device

io.c was 2,799 lines and 166 KB: the keyboard, the file device, the
title, DMA, FRED, SYS, the sequencer, the Tube, the Tube ULA, DOOM, the
Apple IIe's panel, WATCH, DUMP, brainshots, the I/O profile, save
states, STATUS and the second screen, each added at the bottom with its
own `#include`s.  It is now ten files and an internal header:

| file | what | lines |
|---|---|---|
| core/io.c | dispatch by page, keyboard, mouse, WAIT, DMA, reset, save state | 341 |
| core/sys.c | SYS $D500: clock, frames, version, switches, the title | 260 |
| core/seq.c | the sound sequencer, $D5E0-$D5E3 | 123 |
| core/fred.c | FRED, the MATH unit, $D700 | 134 |
| core/hostfs.c | the file device, $D300: sandbox, mounts, URLs | 604 |
| core/status.c | STATUS and MOUNT's report (/proc, statvfs, nmcli) | 179 |
| core/tube.c | the Tube, $D800: children on a pty, the Tube ULA | 578 |
| core/tube_shm.c | DOOM and the Apple IIe: the shared frame buffer | 464 |
| core/screen2.c | the second screen's session | 130 |
| core/debug.c | WATCH, the recorder, DUMP, IDEA's brainshots | 176 |
| core/io_int.h | how those files reach each other (not for sdl/) | 132 |

What changed besides the cut:

- **Each device decodes its own registers.**  io_read/io_write switch on
  the page and hand the low byte to `hostfs_read/write`, `tube_io_read/
  write`, `sys_read/write`, `fred_read/write`; the SYS page's debug
  registers go on to `dbg_reg_read/write`.  The register meanings did
  not move or change.
- **Each device resets and saves itself.**  `io_reset` calls
  `sys_reset`, `tube_reset`, `hostfs_reset`, `dbg_reset`, `fred_reset`,
  `seq_reset` in the old order.  `io_state_save/load` call each
  device's own in the order the chunks were always written: the save
  file format is unchanged and old .k4s files load.
- **The Tube's DOOM/Apple half has an interface**: `tube_shm_open/
  close/quit`, `doom_child_exec`, `apple_child_exec`,
  `apple_launch_name`.  tube_start's forked child now calls those
  instead of carrying 50 lines for each game.
- io.h, the frontend's view, is unchanged: no caller outside core/
  needed editing.  `core/build.h` is now included by sys.c, so sys.o
  is the one object that rebuilds on a new commit.

Verified: the build has no new warnings, `make test` passes
(see "Testing", below).

## sdl/main.c

2,702 lines, of which `k4510_frontend_main` is 1,560: the event loop,
the frame loop, the menu actions and the presentation, inline.  Taken
out now, because they stand alone:

- `sdl/png.c` (53 lines): the screenshot PNG writer.
- `sdl/hostpage.c` (221 lines): F12 -> Host, the battery and network
  polls, the keyboard layout and lid switch on the K4510 Linux.

**Not done, proposed:** break `k4510_frontend_main` into
`frontend_init()`, `handle_event(SDL_Event *)`, `run_frame()`,
`present()` and `menu_action(int act)`, with the state they share in
one `struct frontend`.  That is the change that makes main.c readable,
and the one most likely to collide with the work k4510-c4 does in
main.c every day (HD fonts, F12).  Best done in one sitting with
k4510-c4 paused, right after this branch is merged.

## rom/kernal.c

2,388 lines, and NOT split, on purpose for now: every function is
placed by `#pragma code-name` into resident ROM (CODE, CODE2) or a
sideways bank (SWCODE0-3) according to where there was room, and the
cross-bank calls go through `sw_call()`.  The natural cut is one file
per bank (kernal.c resident, kos_bank0.c .. kos_bank3.c), which makes
the room in each bank visible at a glance.  It changes no behaviour
but does change kernal.bin's bytes (cc65 places code per unit), so it
wants its own branch and the full battery -- and k4510-c4 edits this
file daily too.

## Leaving alone

core/term.c (1,186 lines) is large but is one thing, JIM's VT100
parser and screen; core/vicky.c (644) and core/mem.c (417) are fine.

## Jettisoned

- `__pycache__/`: five .pyc files untracked, and ignored everywhere.
- The WASM build: its code went in f498aae (2026-09-14); the
  `wasm/dist/` ignore lines (.gitignore, .containerignore) go now, and
  coding.md's entry says it is history.  The built page still lying in
  the shared checkout (`wasm/dist/`, untracked, 4 files) is Doc's to
  delete: `rm -rf wasm` there (a copy is in
  ~/containers/k4510web-archive-2026-09-14.tar.gz).

## Testing

The fresh worktree needed three things the shared checkout has and git
does not: the Tube binaries (`make -C tube`), the CP/M disk
(`fs/CPM/A/...`, ignored) and `fs/STARTUP.BAT` (ignored).  Without them
basictest, rangertest and dirtest fail on master too.
