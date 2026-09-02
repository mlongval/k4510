# The names

**Decision, 2026-08-29 (Doc), extended 2026-09-01.** There are now
THREE names, because there are three ways to deliver the machine. The
2026-08-29 decision below settled the first two; the third arrived when
Doc described a Linux appliance built round the emulator.

- **K4510** — the machine, and the emulator that runs it on somebody
  else's operating system. Linux under SDL2 today; macOS and Windows are
  wanted. You have a desktop, and one of the windows on it is a K4510.
- **BMC-K4510** — the bare-metal appliance: a Raspberry Pi 3B+ (a 4 is
  wanted too) with an SD card and no operating system underneath. *BMC*
  is Randy Rossi's, for BMC64, the platform it was built on.
- **K4510x** — a minimal Linux distribution that boots straight into the
  emulator, with no Wayland and no X under it. Intended for old laptops,
  thin clients and whatever else is on its way to being e-waste. What it
  shares with the hosted build and the appliance does not have is a
  **Linux underneath**: the machine's filesystem is a real directory on a
  real operating system, so the cross-compilers, git and an editor are
  all right there beside it.

The three are one machine and one ROM. What differs is what is beneath
it: somebody else's desktop, nothing at all, or a Linux that exists only
to hold it up.

## Where the line falls between the three

The 2026-08-29 test still decides most cases and is still the one to
reach for. Two more, for the new name:

> **What is underneath, and whose is it?** Nothing at all, only Circle:
> **BMC-K4510**. An operating system that is somebody else's and was
> there first: **K4510**. An operating system that exists only to hold
> the machine up: **K4510x**.

**A correction, 2026-09-01 (Doc).** This section first said that `SWAP`,
the CP/M bridge and the shell escape "want a host underneath; the Pi has
none". That is wrong on every count. `SWAP` is a K/OS command, `CPM` is
RunCPM compiled into the machine, and the shell escape is K/OS's own —
all three are on the SD card and always have been. There is no escape to
a *host* shell anywhere in the tree, on any of the three, and the machine
cannot shell into Linux.

The real difference is quieter and it is about **you**, not the machine.
On a K4510 or a K4510x the machine's filesystem is an ordinary directory
on a running Linux, so cc65, Mad Pascal, git and an editor are beside it
while it runs — you cross-compile into `fs/` and the machine sees it. On
a BMC-K4510 the same files are on an SD card with Circle over them, and
the only way to reach them from outside is to take the card out.

So K4510x is not "the appliance on a PC". It is the *hosted* build with
the desktop taken away and the toolchain kept.

## A caution about the first name

`K4510` now does two jobs: the machine in the abstract, and the hosted
build specifically. That is how the project already spoke and it is
usually harmless, because on a desktop the two are the same thing. It
goes wrong in one place: **performance**. "The K4510 holds 40.5 MHz" is
the old error in new clothes — what holds a clock is the host, not the
machine. Say "the hosted build holds 40.5", "the BMC-K4510 holds 15",
and let `SETUP` answer for K4510x, which will be different on every
scrapped ThinkPad it lands on.

---

## The original decision, 2026-08-29

The project had been using one name for two different things. From
here:

- **K4510** is the machine — the architecture and its software. The
  45GS02, VICKY, SHEILA, the four SIDs, K/OS, the ROM, the handbook,
  every `.prg`. It is what you are using whichever way you run it, and
  on the desktop under SDL2 it is simply *the K4510*.
- **BMC-K4510** is the bare-metal Raspberry Pi appliance. The SD card,
  Circle, the four cores, HDMI and the headphone jack, no operating
  system between the machine and the board.

One is the computer. The other is the computer as a piece of hardware
you switch on.

## This is not a rename

`docs/K4510-Design.md` settled the etymology on 2026-08-21, and it
already said this:

> *BMC* is for Randy Rossi's BMC64 and its family on the Pi 3B and
> earlier boards — the platform this is built on.

*BMC* was never part of the machine's identity. It names the platform,
exactly the way it does in BMC64. `BMC-K4510` has always parsed as "the
K4510 on the bare-metal Pi"; the project just used it for everything
because for a while there was only one way to run the thing. The *K*
(Kawari) and the *4510* (the C65's 4510 by way of the MEGA65's 45GS02,
and the C64's 6510) are the machine's own, and they stay in both names.

So nothing is being renamed. A name that covered too much is being
returned to what it says.

## The rule, and the test

> **Would the sentence still be true if you unplugged the Pi and opened
> the desktop build?** If yes, it is the **K4510**. If it is only true
> of a board with an SD card in it, it is the **BMC-K4510**.

That test decides essentially every case. Some worked examples:

| Statement | Name |
|---|---|
| 256 MB of RAM, banked through the 45GS02's 28-bit space | K4510 |
| `MODE 2` is 320x240, 40x30 text | K4510 |
| K/OS boots to a shell at `/]` | K4510 |
| Four reSID chips, `HUSH` silences them | K4510 |
| Boots in about two seconds from cold, with no OS underneath | BMC-K4510 |
| Core 1 runs the emulator, core 2 presentation, core 3 the Tube | BMC-K4510 |
| Write the card with `pi/make-sd.sh` | BMC-K4510 |
| Holds 15 MHz at 60 fps; the desktop holds 40.5 | both, separately |

The last row is the interesting one. Performance is a property of the
*host*, not the machine — which is why `core/calib.c` measures it rather
than looking it up. Say "the BMC-K4510 holds 15 MHz", never "the K4510
runs at 15 MHz".

## What each name owns

| | K4510 | BMC-K4510 | K4510x |
|---|---|---|---|
| Code | `rom/`, `core/`, `sdl/`, `basic/`, `forth/`, `demo/`, `mon/`, `cpm/`, `tube/`, `fs/` | `pi/` | nothing yet — it is a distribution, not a port |
| Docs | the handbook, `VICKY-SPEC.md`, `K4510-Design.md`, Appendix A | `pi/README-SD.txt`, the SD-card sections | its own image-building notes, when it exists |
| Anything the guest can see | banner, status bar, `INFO`, the settings file | — | — |
| Anything you hold | — | the card, the board, the cables | the USB stick, the laptop it rescued |

The dividing line falls almost exactly on `pi/`. Seven mentions of
`BMC-K4510` live there and belong there; the other ~150 across the tree
are the machine and should read `K4510`.

## What this changed

**Done, 2026-08-29, and shipped as alpha-0.4 'Imprint'.** By area:

- **The guest, and this is the important one.** The ROM banner says
  `BMC-K4510 -- A FANTASY 8/16-bit COMPUTER`, the status bar says
  `BMC-K4510  K/OS`, and `INFO` says `K/OS ... (the BMC-K4510 operating
  system)`. **The same ROM bytes boot on both hosts**, so the guest
  cannot honestly claim to be the Pi appliance. All three become
  `K4510`. Saves four bytes of rodata and costs nothing.
- **Shared host chrome** — the SDL window title, the F7 menu heading,
  the settings-file header, the dump header in `core/io.c`: `K4510`.
- **File-header comments** across `demo/`, `pascal/`, `basic/`,
  `forth/`, `tube/`, `cpm/`, `mon/`, `test/`, `tools/`: `K4510`. This is
  the bulk of the count and the least urgent part of it.
- **`pi/`** keeps `BMC-K4510` throughout: `kernel.cpp`'s boot line,
  `README-SD.txt`, `make-sd.sh`, `Makefile`, `config.txt`.
- **`README.md`** opens on the machine now, with a *Two names, one
  machine* paragraph, and its Pi section is titled for the appliance.
- **The handbook** is *The K4510 User's and Programmer's Guide*; the
  cover reads `K4510`. A new §1.3, *Two names, one machine*, carries
  this file's argument for readers, and the thanks page now says out
  loud that Randy Rossi's initials are in the appliance's name. Every
  figure was recaptured — the banner is in a dozen of them.
- **The issue form** distinguishes the two, at its source
  (`doc/guide/issue-form.txt`; `.github/ISSUE_TEMPLATE/report.md` is
  generated from the book and must never be edited directly).
- **`install-sd.sh` keeps `BMC-K4510`** — it writes cards, so it belongs
  to the appliance. It is the one root-level file that does.

## What does not change

- **The repository stays `github.com/mlongval/bmc-k4510`.** It is the
  project's name and it is in every clone, release URL and bookmark that
  exists. Renaming it would break those to gain tidiness.
- **`K/OS`**, the register names, `$D5xx`, the file layout, the release
  naming (`alpha-N`) — untouched. This is about prose and chrome.
- **The desktop build gets no new name of its own.** It is the K4510.
  There is no silicon for it to be an emulation *of*: for a fantasy
  machine, the emulator is the machine. (Still true in 2026-09-01's
  three-name world: K4510x names the *distribution*, not a third
  build of the emulator. Same binary, same ROM.)

## The guest names itself correctly — done

This was written up as an open question, on the assumption that telling
the guest which host it was on would cost a new mechanism. It does not:
**`$D522` already carries it**, and `INFO` has been reading it since the
Pi port (`rom/kernal.c:770`). So the banner branches on the same byte —
it prints `BMC-K4510 -- A FANTASY 8/16-bit COMPUTER` from a card and
`K4510 -- ...` on a desktop, from one ROM image. `INFO`'s system line
says `BMC-K4510: bare metal on a Raspberry Pi 3B+` or `K4510 on a
desktop`.

Seventeen bytes of ROM1A. The machine now tells the truth about which of
its two selves you are looking at, which is the whole point of this file.

The **status bar** deliberately does not branch: it says `K4510  K/OS`
either way. It is eighty columns wide at most and it names the machine,
not the box.
