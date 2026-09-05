BMC-K4510 -- a fantasy 8/16-bit computer, running bare metal on a
Raspberry Pi 3B+.

WHAT IT IS
  A new machine, not an emulation of an old one: a 45GS10 CPU (the
  MEGA65's 6502 descendant) at 40.5 MHz with 256 MB of memory, its own
  video chip "VICKY" (640x480, 256 colours, 4 layers, 128 sprites, a
  blitter with line and triangle ops, a display-list coprocessor called
  SHEILA), an OPL2 FM sound chip, a DMA engine, a floating-point math
  unit, a VT100/ANSI terminal chip called JIM, and a system ROM with a
  shell, editors and several languages.  The emulator is our own; the Pi
  runs it with no operating system (Circle + circle-libsdl2).

WHAT YOU NEED
  A power supply that holds 5 V under load: the official Raspberry Pi
  supply (5.1 V, 2.5 A).  A phone charger or a thin cable sags with three
  cores flat out, the firmware caps the clock, and the machine runs at a
  fraction of its speed; INFO and the BENCH report say when this is
  happening.  (The BMC64 keyboard adapter takes positive-centre barrel
  power; on a micro-USB supply, use a USB keyboard.)
  Raspberry Pi 3B+ (3B probably works; a Pi 4 or 5 is not yet built for
  -- the libraries support both, and a Pi 4 is the next thing to try, since
  its processor should carry the full 40.5 MHz), HDMI display,
  USB keyboard, an SDHC card of 4 GB or more (2 GB SDSC cards do not
  work), formatted FAT32.

INSTALL
  Copy everything in this folder to the root of the card:
    bootcode.bin  start.elf  fixup.dat  config.txt  kernel8.img
    k4510/  (rom/, data/, fs/)
  Put the card in the Pi, plug in HDMI and keyboard, power on.  The
  boot log scrolls for a few seconds, then a blue screen with a ] prompt.

TRY
  HELP                 the shell commands
  INFO                 what the machine is; INFO -v names this exact build
  BENCH                measures the machine and sweeps the CPU clocks with
                       a note sounding; writes /SYSTEM/BENCH-NN.TXT
  DIR                  files (CD BASIC, CD PRG, CD OPL move around)
  MODE 0-4             640x480, 640x240, 320x240, 320x200, 160x200
  VI file / EDIT file  the two editors (VI is modal, and pages to far
                       memory: 32000 lines)
  BUG                  asks you about a fault and writes the report to
                       /SYSTEM/BUGREPORTS, build and machine filled in
  OPLPLAY              AdLib tunes from /OPL: cursor keys, Enter plays, Esc leaves
  RUN opl2             Pachelbel on all nine FM voices; keys 1-9 mute voices
  RUN balls / cube / mandel / romout / segdemo / logo
  EHBASIC              EhBASIC (RETURN at "Memory size ?"); then:
     RUN "DEMOS.BAS"           the demo menu (graphics, sound)
     RUN "BENCH.BAS"           the benchmark menu (RF1-8, AHL, sieves)
     *VI / *EDIT               edit the program in memory, and come back
     @DIR  @CD BASIC  @MON     any shell command from BASIC; @BYE leaves
  BBC                  BBC BASIC, on the Tube
  CPM                  CP/M 2.2 on the Z80 (K: is this machine's own
                       filesystem, so files pass both ways)
  FORTH                Tali Forth 2, native
  MON                  the monitor

THE CPU CLOCK
  A Pi 3B+ emulates about 15 MHz of this machine in real time, so that is
  its default (F7 > Machine > CPU clock: 40.5 / 30 / 20 / 15 / 10).  At a
  higher setting programs run faster than the Pi can keep up with, the
  picture drops below 60 frames a second, and the sound -- which keeps
  playing whatever the frame rate -- slows its tempo with it.  The
  desktop emulator runs the full 40.5 MHz.  INFO reports the clock in
  force.

THE HOST MENU
  F7 opens the settings: border, screen font, resolution, scanlines,
  scaling, volume, the menu key, the reset chord, and whether
  /STARTUP.BAT runs at power-on.  Resolution, scanlines and margin
  preview as you move through them.  Super+PgUp resets the machine.

  If a /STARTUP.BAT you wrote wedges the machine, turn it off in
  F7 > Run STARTUP.BAT and reset.

DISPLAY
  config.txt drives the screen at 640x480.  If your TV crops the edges,
  set its picture mode to "Just Scan" / "Screen Fit".  If it shows black
  bars at the sides, that is its 4:3 setting.

NOT YET ON THE PI
  The network side (Meatloaf URLs, TNFS, TELNET, the N: device) is
  built into this kernel but has never been tested on real hardware,
  and needs the Pi's Ethernet.  Treat it as untried here; it works on
  the desktop emulator.

THE BOOK
  doc/guide/k4510-guide.pdf in the repository is the handbook -- the
  shell, the languages, the editors, the chips, and an appendix of every
  register, generated from the machine's own headers.

SOURCES
  github.com/mlongval/bmc-k4510 (GPL-2.0-or-later).  THIRD_PARTY_SOURCES.md
  records every borrowed component and its licence; EhBASIC is Lee
  Davison's, see k4510/README-EhBASIC.txt.  Built in August 2026 by Doc
  and Claude.


Keyboard layout
---------------
cmdline.txt in the root of the card carries Circle's kernel options, and
the one that matters here is the keyboard layout:

    keymap=us

**No trailing newline.**  CKernelOptions splits the command line on spaces
and nothing else, so a newline stays inside the value, "us\n" matches no
layout, and Circle falls back to its compiled-in default -- which in this
Circle is "DE", with y and z swapped.  Write it with `printf`, not `echo`.

The layouts available: us, uk, de, fr, es, it, dv.
