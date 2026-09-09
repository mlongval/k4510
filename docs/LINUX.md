# K4510x — the machine as a Linux appliance

**Decisions, 2026-09-02 (Doc).** Debian, x86-64, the toolchain installed
by default, delivered as an image that boots from a USB stick *and* can
install itself to internal storage. Target hardware: an **HP t520
thin client**.

`docs/NAMING.md` says what K4510x *is*; this says what it is made of.

## Why Debian

The toolchain decided it. K4510x exists to be the hosted build with the
desktop taken away and the compilers kept, and `pascal/README.md` needs
**FPC** to rebuild Mad Pascal's `mp`. Debian packages every piece —
`cc65`, `64tass`, `nasm`, `fp-compiler`, `python3`, `git`, `libsdl2-dev`
— it is glibc, which is what FPC and the prebuilt tools expect, and
`setup.sh` already speaks apt, so an image build and an ordinary desktop
build stay the same recipe.

Alpine was the runner-up and would have been half the size, but musl is
a risk for exactly the compiler this distribution exists to run.
Buildroot and Yocto build appliances beautifully and fight you the
moment the appliance has to host a compiler suite. Arch and Void are
rolling, which is a liability in something meant to be left alone.

## What the machine needs from the host

Measured, not assumed:

| | |
|---|---|
| Video | SDL2's **KMSDRM** driver. No X, no Wayland, no code change — `SDL_GetVideoDriver` already lists it. |
| Audio | SDL2's **alsa** driver. Likewise already there. |
| RAM | **512 MB floor.** The 256 MB flat space is lazily committed; a machine sitting at the shell measured **143 MB resident** (1.9 GB of address space, which is why this is 64-bit only). |
| Disk | ~1.5 GB for base + toolchain + firmware. The t520's flash is 8-32 GB, so this is not tight. |
| CPU | Whatever it holds. See below. |

## The HP t520 in particular

AMD G-series SoC (GX-212JC or GX-215JJ), two Jaguar cores around
1.2 GHz, integrated Radeon, typically 4 GB RAM, M.2 flash, DisplayPort
and VGA, UEFI or legacy boot. Three things follow:

- **The GPU firmware is not optional.** KMSDRM is the *only* video path
  here, and without `firmware-amd-graphics` (Debian's
  `non-free-firmware` component) there is no KMS at all — which shows up
  as a black screen and an SDL init failure, not as a missing driver
  message. It is in the package list for that reason.
- **The storage is M.2 SATA, not NVMe.** Doc asked for NVMe; the t520
  generally has SATA-on-M.2. This costs nothing to get right — the
  installer targets whatever block device is there — but do not expect
  `/dev/nvme0n1` on the t520 itself. Verify on the unit.
- **Expect a modest clock.** Two Jaguar cores at 1.2 GHz are a long way
  from the desktop this starts at. K4510x should ship with `cpu.auto`
  on and a conservative `cpu.clock`, and let `SETUP` measure the machine
  properly the way it does everywhere else. What a host holds is a
  property of the host (`docs/NAMING.md`), and this one will not hold
  what a laptop does.

## The boot path

No display manager and no X. An autologin getty on tty1, and the
emulator `exec`ed from a profile script. **Quitting the emulator leaves
you at a Linux shell on the same tty**, and Ctrl+Alt+F2 gives another
one — which is the whole distinction K4510x is built on, obtained
without inventing anything. It also gives F7 → *Quit the emulator* a
meaning it does not have on the appliance, where the Pi simply halts.

The user needs `video`, `input` and `audio` group membership for DRM
master and evdev.

## Building the image

    sudo ./k4510x/build-image.sh        # -> k4510x-<date>-amd64.img

**A raw disk image, not a live ISO.** `dd` it to a USB stick and the
machine boots and runs from it with a writable root that remembers
things; `dd` it to the internal drive and it is installed. That covers
"written and run from a USB stick or installed to a hard drive" more
directly than an ISO plus an installer would, and it is how the Pi
appliance already ships, so the two deliveries are one idea.

live-build was the first plan and was dropped. The only `live-build` on
the build host is Ubuntu's 3.0-alpha fork, which defaults to building
Ubuntu images and whose debian-installer path for a Debian suite is a
gamble; `--mode debian` works, but betting the delivery on an alpha fork
of somebody else's tool was the wrong trade against a partition table
and `debootstrap`, which are boring and entirely ours.

GPT, an EFI system partition and a BIOS boot partition, GRUB installed
both ways, so it boots on UEFI and legacy firmware alike -- the t520
can do either.

## Built and boot-tested, 2026-09-02

The image exists: 6 GB, Debian trixie, kernel 6.12. Verified by mounting
it (the machine's own binaries built inside it -- `sdl/k4510`,
`rom/kernal.bin`, `rom/wozmon.bin`, `cpm/runcpm`; the toolchain present,
`fpc` included; 638 amdgpu and 247 radeon firmware blobs) and then by
**booting it in qemu, twice**: once through OVMF for UEFI and once
through SeaBIOS for legacy. Both reach the machine's own banner
full-screen on KMSDRM, with the status bands up.

Two things that came out of the boot test:

- **The governor works, visibly.** The image ships 20 MHz with
  `cpu.auto` on; under qemu's TCG interpreter the machine stepped itself
  down to 15 and said so in the band. That is the governor doing exactly
  its job on a slow host, which is the behaviour a t520 will lean on.
- **The timezone had to be set.** With none, Debian is UTC and the
  machine's status clock read four hours wrong. `TZ_AREA` defaults to
  `America/Toronto` now.

**Still untested on the metal.** No t520 has run this. The GPU firmware
and the M.2 story above are the two things most likely to bite there,
and neither is exercised by qemu.

## Open

- Whether the live user and the installed user share a name and a home.
- Whether `/STARTUP.BAT` and `k4510.cfg` live in the image or in the
  installed user's home (they differ: the live session is read-only).
- Persistence for a USB-stick session that is never installed.
