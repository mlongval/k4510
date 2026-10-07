# Design ideas — the 2026-08-31 brainstorm

Doc's list, worked through the same evening. This is **not** a commitment
list: the machine is in alpha and this is larger than the rest of alpha.
It is here so the thinking survives the conversation, and so the next
session does not re-derive it.

Four items are taken up, with steps. Three were considered and declined,
and the reasons are recorded at the bottom so they do not quietly come
back.

---

## Already built: TELNET to the host

One item on the list was *"I would like the emulator to be able to access
the host via telnet so we can run linux commands in the terminal."*

That exists. It was set up on 2026-08-26 and verified — the K4510
connects and JIM shows the `Password:` prompt. A tailnet-only socket on
`100.116.56.10:23`, one systemd instance per connection, and a dedicated
`k4510` account that the login wrapper forces, so the real accounts are
never behind a cleartext door.

**The only thing missing is `sudo passwd k4510`.** The account has no
password, so nothing can log in yet.

The write-up lives in `~/Projects/BMC-K4510/TELNET-SERVER.md` — in the
*archive* folder, untracked. That is why it was forgotten. **Step one of
the networking work below is to move it into this repository.**

---

## 1. `MOUNT` — one filesystem, many backends

Six of Doc's bullets are one feature: mounting USB, host directories,
`tnfs://`, FujiNet, Meatloaf and FTP into the K4510 filesystem. They feel
like six because the pieces exist today as unrelated special cases:

| Today | What it really is |
|---|---|
| the sandbox root (`fs_root`, `core/io.c:58`) | the local backend |
| `fs_remote` (`core/io.c:63`) — "the cwd is a `tnfs://` URL" | a single, global, implicit mount |
| the Meatloaf rule (a URL where a filename goes) | a per-call backend override |
| `N:` at `$D900` | a raw socket, beside the filesystem rather than in it |

`fs_remote` is the shape of the answer already: it is a mount table with
exactly one row and no name.

### The steps

1. **Choose the namespace: paths, not drive letters.** Mount points
   become directories under the sandbox root — `/N`, `/U`, `/FTP`. Drive
   letters are already spoken for by the CP/M bridge (`K:`/`P:`/`D:`,
   and CP/M has only A-P), and paths compose better with the shell and
   the filers.

2. **Generalise `fs_remote` into a table.** Eight rows is plenty:
   `{ prefix, backend, handle }`. Longest-prefix match wins; the local
   backend is the default and matches `/`.

3. **Route inside `fs_resolve` / `fs_path` (`core/io.c:76,133`).** This
   is the important one: **no new registers for the common case.** All
   eighteen existing `$D300` commands keep working unchanged, and so
   does every ROM caller, the shell, both filers and the trash. A mount
   is invisible to the guest except that the path is somewhere else.

4. **One vtable, four backends.** `local` is today's code, lifted.
   `tnfs` already exists in `core/net.c` (sessions, `tnfs_fetch`,
   `tnfs_listdir`). `http` (which is what Meatloaf and FujiNet links
   are) is `net_fetch`, read-only. `ftp` is the only new one, and it
   should be read-only in the first cut.

5. **Three new `$D300` commands, and only three:** `FS_MOUNT` (19),
   `FS_UMOUNT` (20), `FS_MOUNTS` (21, writes the table to `ADDR`).
   Everything else is reuse.

6. **A `MOUNT` shell command in the ROM**, with no arguments listing the
   table. It is small, and `SW2` has 6762 bytes free — it belongs in a
   sideways bank, with its table beside it (see the ROM budget rules).

7. **Tests.** Extend `test/nettest.sh`: mount a local directory at `/U`
   and run the existing filesystem checks *through the mount*, so the
   routing is proved against behaviour that is already known-good.

### The open question, which must be settled first

**Latency.** `$D300` is synchronous: the guest writes `CMD` and the
answer is there when the write returns. `net_fetch` is already called
from inside that write (`core/io.c:207`), so the machine *already*
stalls for the length of a TNFS round trip. Nobody has minded yet
because TNFS on the tailnet is fast and the uses are deliberate.

FTP and a slow or dead server change that. A mount makes the stall
*incidental* — a `DIR` in the wrong directory, RANGER walking into a
mount point, the shell's tab completion — and a machine that freezes for
thirty seconds because a filer touched a mount is a bad machine.

Two answers:

- **(a) Keep it synchronous, add a timeout and a `busy`/`timeout`
  status.** Small, consistent with today, and enough for TNFS and HTTP.
- **(b) Make `$D300` asynchronous** — `CMD` returns immediately, the
  guest polls `STATUS`. Correct, and it touches every ROM caller.

**Do (a) now and reserve the status code; do (b) when FTP lands.**
Deciding this *before* the mount table is written is the whole point of
writing it down — it is much cheaper than deciding it afterwards.

---

## 2. JIM as the console

Doc: *"Default screen interaction should always be via jim."*

He is right, and the tree already carries the evidence. There are two
independent renderers drawing on one screen:

- `k_chrout` (`rom/kernal.c:181`) pokes VICKY text32 cells directly and
  keeps its own cursor, wrap, scroll and newline logic (`cell`, `cls`,
  `scroll`, `newline`, `draw_cursor` — roughly `rom/kernal.c:64-193`).
- **JIM** (`$DA00`, `core/term.h`) is a full VT100/ANSI terminal that
  draws on *the same screen*, used by the Tube, CP/M, BBC BASIC and
  `TELNET`.

The duplication is already paying rent in bugs. `k_getin` carries a
workaround for it, in its own words:

> *a program that draws through the terminal (VI, EDIT, anything under
> CP/M) polls this for keys, and the console's cursor would be a second
> one — blinking to a different clock, parked on whatever cell the shell
> last left it on, reversing whatever the program has since drawn there.*

**The strongest argument is not tidiness, it is ROM.** `k_chrout`
becomes a store to `$DA00`; the wrap, scroll, tab and newline logic all
leave the ROM and live in host C. ROM headroom is this machine's chronic
constraint — today `ROM1A` 643 free, `ROM1C` 650, `ROM2` 547, `BSSR` 1,
`ZP` 0 — so a change that *returns* ROM is worth more than its own
merits.

### The steps

1. **Audit JIM against what the console needs.** It looks complete
   already: `DECSTBM` scroll regions, `IL`/`DL`/`ICH`/`DCH`/`ECH`,
   `CHA`/`VPA`, `SU`/`SD`, tabs, SGR colour. Confirm the ROM's
   `page_break` and `scroll` semantics map onto it before moving
   anything.

2. **Turn `k_chrout` into a byte sink** — write to `$DA00`. Delete
   `cls`, `scroll`, `newline` and the tab loop from the ROM; clearing
   becomes `CTRL = 2`.

3. **Give the cursor to JIM** (`FLAGS` bit 0). Delete `draw_cursor` and
   the `k_getin` workaround with it. This is the bug class closing.

4. **Do *not* convert the full-screen UI.** RANGER's miller columns,
   KOMMANDER, the menu and the status bar write cells directly, and for
   full-screen drawing that is *cheaper in ROM* than emitting escape
   sequences. Converting them would spend the ROM step 2 just earned.
   Stream console through JIM; direct cells for full-screen; one cursor,
   owned by JIM.

5. **Measure with `tools/romfree.py` before and after.** If the change
   does not return ROM, it has not been done right.

6. **Two risks to watch.** Every handbook figure showing a console
   screen may shift by a row and will need recapturing — tell the
   handbook session before, not after. And the per-byte cost moves from
   guest cycles to host C: profile the Pi with the existing `p_cpu` /
   `p_vic` / `p_sid` buckets, which is the same rig that measured the
   SIDs.

---

## 3. Networking, and a TNFS server as a service

### The steps

1. **`sudo passwd k4510`**, and move `TELNET-SERVER.md` into `docs/`.
   An operational document that lives only in the archive is a document
   that gets rebuilt from scratch in six months.

2. **Run a TNFS server as a container on ubuntu-s1**, bound to the
   tailnet address only. Seventeen containers already run there; this is
   the established shape for a service on that machine.

3. **Bind, do not isolate.** Doc's own note has this right — *"The
   binding is the control that matters."* A socket on the tailnet
   address does not exist on `eno1`, on localhost, or on any of the
   eighteen docker bridges, whatever the firewall says. Docker's
   isolation is much weaker than that sentence, and should not be
   mistaken for it.

4. **Not inside the emulator.** A TNFS server in the emulator process
   would run only while the emulator runs, and could not exist at all in
   the Pi build, which is bare metal under Circle. The emulator is a
   client. The server is a service.

5. **Mount it** — this is where item 1 pays off: `MOUNT tnfs://... /N`.

6. **Two standing limitations, to be stated rather than discovered.**
   There is no TLS, so `https://` is not really available; and the Pi's
   `net_plat` (Circle) path has never been run on hardware and needs
   Ethernet.

---

## 4. PETSCII mode in JIM

A good idea, and unusually well-scoped: PETSCII is a small fixed set of
control codes plus a glyph mapping, not a protocol.

### The steps

1. **A mode flag, not a new device** — a `FLAGS` bit or a `CTRL` value
   at `$DA04`. **ANSI stays the default.** The Tube, CP/M, BBC BASIC and
   ANSI-BBS all need ANSI; making PETSCII the default would break every
   one of them.

2. **The control codes** in `core/term.c`, as a second dispatch beside
   the ANSI one: `$93` clear, `$13` home, `$11`/`$91` cursor down/up,
   `$1D`/`$9D` right/left, `$12`/`$92` reverse on/off, `$0E`/`$8E` case
   set, and the sixteen colour codes (`$05`, `$1C`, `$1E`, `$1F`, `$81`,
   `$90`, `$95`-`$9C`).

3. **Reuse the colour machinery.** The PETSCII colours map onto the SGR
   palette JIM already has; this should add a table, not a renderer.

4. **Use the fonts already vendored** — BESCII (CC0) and openroms
   (LGPL). They give the PETSCII look with no licence question at all.
   Commodore's `chargen.bin` stays exactly what it is today: an optional
   personal drop-in that the repository will not carry (see below).

5. **Test it** the way the BASICs are tested — a program that prints
   known PETSCII and a screen check in the headless harness.

---

## The order, and why it is not the obvious one

**JIM (2) first, then `MOUNT` (1), then networking (3), then PETSCII (4).**

`MOUNT` is the more valuable feature and was ranked first in
conversation. On reflection that is the wrong order to *build* in:

- JIM is self-contained, closes a live bug class, and **returns ROM** —
  and `MOUNT` will want ROM for its shell command.
- `MOUNT` has an unresolved architectural question (the latency answer
  above). Building the smaller, certain thing while that decision is
  made is better than making the decision under pressure halfway
  through a mount table.

Networking follows `MOUNT` because most of its value is the mount.
PETSCII is last because nothing depends on it, and it wants the JIM work
to have settled first.

---

## Palettes — built 2026-09-01

Doc asked how palettes could be defined and switched inside K/OS; this is what
was built, and the one thing that made it non-obvious.

**The constraint.** `video_init` reloaded its own copy of the VIC-II sixteen
into VICKY entries 0-15 *every time it ran* — every mode change, every `VIDEO`
call, every BBC BASIC text mode. So any palette poked into VICKY looked like it
worked and then silently reverted. But the ROM's table was **byte-identical to
what `vicky_reset` already seeds** (`core/vicky.c`), so the reload did nothing
at power-on and undid work afterwards. It is gone. The palette belongs to VICKY
and to whoever last set it; `PALETTE RESET` and the reset chord are the ways
back. That also returned about 80 bytes of resident ROM.

**The command** lives in bank 2 (`sw_call(2, ...)`):

    PALETTE                list entries 0-15
    PALETTE n rr gg bb     set one entry, hex, n may be 0-FF
    PALETTE LOAD name      apply a .PAL from /SYSTEM/PALETTES
    PALETTE SAVE name      write entries 0-15 out
    PALETTE RESET          back to the VIC-II sixteen

**The format is text**, because this machine has `VI` — a palette you cannot
edit on the machine wastes what makes it interesting. Lines of `index rr gg bb`
in hex, `#` comments, and **a file only changes the entries it names**, so a
theme can be two lines. A `.PAL` may also carry a `COLOR f b` line, which the
loader honours: without it a ramp is a trap, because fifteen levels of amber
under the shell's default `COLOR 7 6` is level seven on level six, which cannot
be read well enough to type the fix.

Four ship in `/SYSTEM/PALETTES`: `C64` (as it boots), `PEPTO` (Timmermann's
measured VIC-II colours — same hues, duller and warmer), `GREY` (sixteen even
steps) and `AMBER` (black plus fifteen levels, a VT220).

**What a program gets, since the console's sixteen is only a convention:** 256
entries of 24-bit RGB, one palette shared by everything. 8bpp bitmap reaches all
256; 4bpp gets 16 from any of 16 banks; tile cells carry 4 bits of palette
offset each; sprites choose a bank per sprite; and text32 cells already carry
byte-wide fg *and* bg, so text can use all 256 too. SHEILA can rewrite entries
between scanlines, so more than 256 can appear in one frame.

**A bug this turned up.** Bank 2's code grew past `$B000`, where the alias
engine builds its table at runtime, and the alias records overwrote the palette
table — `PALETTE RESET` produced `MBASIC` and `CPM` as colours. The linker had
no idea `$B000` was reserved. `rom/k4510.cfg` now splits SW2 into code
(`$A000-$B3FF`) and table (`$B400-$BFFF`) so the linker enforces it, and
`test/palettetest.sh` checks an alias still works after all of this.

## Considered and declined

**Downloading `chargen.bin` from zimmers.net.** The `.gitignore` states
the position: *"Commodore's character ROM, if you drop one in /SYSTEM:
not ours to publish."* A menu item that fetches it moves the project from
"will not redistribute" to "will fetch on your behalf", and the clean
licence record — `LICENSES.md`, `THIRD_PARTY_SOURCES.md`, the pure-MS
msbasic vendoring — is a real asset worth protecting. It is also
unnecessary: openroms and BESCII are already vendored and give the same
look. The Commodore ROM stays a hand-placed personal file, documented in
the manual.

**The emulator in a Docker container.** It is an SDL application needing
GPU, audio, keyboard and a display connection; containerising it buys
X11 and audio plumbing in exchange for nothing. It also does nothing for
the Pi, which is bare metal and has no Docker. Containers are right for
the *services* (item 3), not for the machine.

**Porting Morpheus / NeoBASIC.** Deferred, not rejected — and worth
recording properly, because it was described in conversation as a
language port and it is not one. **Morpheus is Paul Robson's API
firmware** (MIT): the Neo6502's 65C02 talks to an RP2040 that provides
graphics, sound, USB and filesystem through a command mailbox. NeoBASIC
rides on top of it. The K4510 already has that exact shape — a 6502
talking to host services through I/O registers (`$D300`, `$D900`, VICKY,
JIM) — so a Morpheus-compatible register surface would import a whole
software ecosystem rather than one interpreter, and Mad Pascal already
targets this machine, so there is precedent for adopting Neo6502
toolchain pieces.

It is deferred because it is a **second personality for the machine**,
not a feature. That is a decision to take deliberately, when the four
items above are done and the machine's own identity is settled — not by
drift.

**Also read:** `X65/os-816` (0BSD) — an OS for a 65C816 machine whose two
headline features are a VFS and virtual consoles, which is to say it
arrived independently at items 1 and 2. The code is no use here (65816
native mode), but the design documents are free to read and worth it.
