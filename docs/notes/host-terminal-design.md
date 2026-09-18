# A terminal to Claude that is always there (design, 2026-09-18)

Doc's brainshot, 2026-09-18 12:20: "a permanently open telnet session to the
host on the k4510, through which I could ssh into ubuntu and use claude under
the tmux sessions.  That would imply being able to task switch between the
active app and the telnet terminal.  Is that feasible?"

Yes -- and most of it is already built.  Nothing here is implemented yet: two
of the steps are Doc's to decide (a key, and who may use it).

## What exists

* **The task switch.**  On the K4510 Linux, Ctrl+Alt+F2..F6 are the other
  consoles (sdl/main.c, VT_ACTIVATE) and Ctrl+Alt+F1 is the K4510 again.  The
  emulator is not suspended by the switch: VI, a game, DOOM on the Tube, all go
  on exactly where they were.  The Tektronix already lives this way on tty2
  (linux/tek40xx/README.md).
* **`!`** -- a command, or a shell, on the Linux beneath, on a pty the emulator
  owns, drawn by JIM.  `!ssh -t doc@ubuntu-s1 tmux new -A -s k4510` works today
  from the prompt, if the key is there.  But it is a K/OS command: it holds the
  machine while it runs, which is the opposite of what is asked.
* **WALL** (same day) for the short things: a notice, a choice, a line of text,
  without leaving the machine at all.

## Three ways, and the one to take

1. **tty2 is the terminal (recommended).**  A systemd unit on tty2 that runs
   `ssh -t doc@100.116.56.10 tmux new -A -s k4510` for the k4510 user and
   starts again when it ends.  Ctrl+Alt+F2: Claude, in the tmux session that
   was there yesterday.  Ctrl+Alt+F1: the machine, as it was left.
   Cost: one unit file in includes.chroot, a key, a font for the console.
   No emulator code, no ROM, nothing to debug in the 45GS10.
   What it is not: it is the Linux console, not JIM -- the K4510's font and
   palette stop at the edge of tty1.  (`setfont` with the K4510's 8x16 as a
   PSF would close most of that; tools/mkk4510font.py already has the glyphs.)

2. **A second JIM inside the emulator**, a hotkey flipping the glass between
   the machine and a terminal the frontend owns (its own pty, as the Tube's).
   It would look right.  Why not now: core/term.c is one static terminal
   (`static struct {...} T`, 854 lines, twelve entry points) and would have to
   become an instance; the frontend would need a second text surface and a
   key route that bypasses the machine.  A week, and every line of it a place
   for the console the machine depends on to break.

3. **Suspend the app, drop to the shell, come back** (SWAP, or a save state).
   Why not: the Tube is not in a save state (core/state.h), so DOOM and the
   Apple would not survive it; SWAP covers the 64 KB and the screen, not a
   program's far memory or its devices.  And it solves a problem way 1 does
   not have -- there, nothing needs suspending.

## What way 1 needs from Doc

* **A key for the k4510 user, authorised on ubuntu-s1.**  It must live on the
  persistent partition (K4510LIVE), never in the layer: the layer is built
  from a public repo and ends up on USB sticks.  A stick that is lost is then
  a login to the server.  So the key should be restricted in
  `authorized_keys` -- `command="tmux new -A -s k4510"`, `no-port-forwarding`,
  `no-agent-forwarding`, `no-X11-forwarding` -- and only on the Dell's own
  install, not on sticks made for anyone else.  Tailscale ACLs can narrow it
  further.  This is the whole of the risk, and it is Doc's to accept.
* **tty2 or tty3?**  The Tektronix uses tty2 when it is run.  tty3 for Claude
  keeps both.

## How WALL and this fit

WALL is for a question that has a short answer, asked while Doc is doing
something else on the machine.  The terminal is for a conversation.  The
brainshot watcher (tools/k4510-brainwatch.timer) is the third leg: what Doc
writes with IDEA is on ubuntu-s1 within two minutes, without anyone asking.
