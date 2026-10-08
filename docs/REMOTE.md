# Remote control is off unless turned on, and it always shows

Rule agreed by Doc, 2026-10-08 (relayed by the doc-18 session), in the same
spirit as docs/CAMERA.md: anything that lets someone else see or drive the
K4510's screen or keyboard is OFF by default, and when it is on the student can
always see that it is.

> Perhaps note in bottom left. It is usually empty so something there would
> attract immediate attention. Also Volume goes there, so it's like "where the
> system tells me stuff that is important".

## What counts, and how it shows

At the left of the bottom band JIM draws ` REMOTE ` reversed, then what:

| word     | when                                                                 |
|----------|----------------------------------------------------------------------|
| `keys`   | the key pipe is on (F12 -> Input -> Key pipe): `k4510-type`, `k4510-remote type/key` |
| `login`  | someone is logged in to the Linux beneath from elsewhere: an sshd session, Tailscale SSH (`tailscaled be-child ssh`), a `mosh-server` -- checked in /proc every five seconds |
| `viewed` | for a minute after the screen was read from outside: `k4510-shot` (SIGUSR1), `k4510-screen` (SIGUSR2) |

The frontend's notes that share the corner (the volume, piped keys) move right
of it.  With the bands off, a program holding them, or the F12 menu up, the
same words are in the bar at the foot of the window instead, for as long as
the state lasts -- there is no screen on which it is hidden.

Brainshots, WALL and `tools/k4510-remote` all reach the machine through a
login and the key pipe, so they show as `login` and `keys`.

## Off by default

The key pipe now starts **off** (k4510.cfg `input.keypipe`; it was "on, shown").
A login needs credentials the institution sets up.  Code: `core/io.h` REMOTE_*,
`core/term.c` (the band), `sdl/hostpage.c` host_remote_login, `sdl/main.c`.
