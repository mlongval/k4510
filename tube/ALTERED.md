# Altered source notice (licence.txt, condition 2)

This directory vendors a subset of Richard T. Russell's BBCSDL
(https://github.com/rtrussell/BBCSDL), specifically the Console Mode
edition ("BBCTTY") interpreter, for use as the K4510's Tube
co-processor.

"BBC BASIC" is the name of Richard Russell's interpreter, published
under whatever arrangement he holds with the BBC. This project has not
sought and does not hold any licence to that name, from the BBC or from
anyone else, and asserts no rights in it. It is used here only to
identify the software that is vendored -- which is why the K4510's
documentation says "Richard Russell's BBC BASIC interpreter, running on
the Tube co-processor" rather than presenting it as ours.

(An earlier version of this file stated that the name was "used by
permission of the BBC". That was never true of this project and has been
removed.)

Alterations, all marked with [K4510] comments:
- include/bbccon.h: MAXIMUM_RAM reduced from 4 GB to 256 MB, to match
  the machine the Tube is fitted to.

- src/bbccos.c: OSCLI's unknown-command / *RUN path no longer calls the
  host's system(); it emits `ESC ] K4510 ; <cmd> BEL`, which the K4510
  console executes in the machine's own shell.

- src/bbccos.c: xeqvdu() forwards the graphics VDU codes the console
  edition previously dropped -- 16 (CLG), 18 (GCOL), 19 (palette),
  25 (PLOT) and 29 (origin), plus the mode number on 22 (MODE) -- as
  `ESC ] K4G ; <args> BEL` strings, which the K4510 console interprets
  onto its VICKY video chip.

- src/bbccos.c: xeqvdu() forwards VDU 23,27 (Acorn's sprite hook) as
  `ESC ] K4G ; 23,27,... BEL`; the K4510's Tube ULA keeps VICKY's
  hardware sprites behind it.

- src/bbccos.c: *DIR lists subdirectories (with a trailing /) whatever
  the file pattern; the stock version hid them behind `*.bbc`.

- src/bbccon.c: osload()/osopen() retry a missed read with the extension
  in capitals (fopen_rd), so LOAD "KALEID" finds KALEID.BBC on a
  case-sensitive host.

- src/bbccon.c: PLATFORM is "K4510" in every build and szVersion reads
  "BBC BASIC for the K4510 Tube v0.50" (Doc's wording) rather than
  naming the host or the console edition.

- src/bbccon.c: sound() and quiet(), empty stubs in the console edition,
  emit `ESC ] K4S ; chan,ampl,pitch,dur BEL` / `ESC ] K4S ; Q BEL`; the
  K4510 console queues these on the machine's sound sequencer ($D5E0),
  which plays them on the SID chips.

Everything else is unmodified from BBCSDL commit as cloned 2026-08-24.

- The `K4510_TUBE` build flag, and with it the in-process Tube build (the
  interpreter compiled into the emulator, on a core of the bare-metal Pi
  or a thread of the desktop `make tubetest`), was removed on 2026-09-14:
  the Pi port was retired on 2026-09-07 and the test build was all that
  used it. BBC BASIC runs as its own process on a pty (`tube/bbcbasic`),
  the only way it runs.

- src/bbccos.c: BBC BASIC's file view is sandboxed to the machine's
  filesystem. When the emulator sets K4510_ROOT (the absolute path of
  fs/), *DIR and *CD show paths relative to it (the fs root as "/", the
  host tree above it hidden), and *CD cannot climb above the root. With
  K4510_ROOT unset (a bare bbcbasic, run by hand) nothing changes. The emulator also starts the co-processor
  in the machine's current directory, so LOAD needs no directory prefix.

- src/bbccos.c + src/bbccon.c (both builds): `*VI` or `*EDIT` with nothing
  after it edits the program in memory. k4_editprog() LISTs it as plain
  text (LISTO 1, LF endings, through the optval redirection) to
  EDITTMP.BBC in the current directory, sends `ESC ] K4510W ; VI path BEL`,
  and k4_ack_then() waits for the console's ACK (0x06, sent once the
  machine's command has finished -- the W is the request for it) before
  typing `LOAD "EDITTMP.BBC"` into the input queue; LOAD reads text and
  tokenises it. Escape abandons the wait.
