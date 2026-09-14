# Altered source notice (RunCPM, MIT -- see LICENSE-RunCPM.txt)

`src/` is RunCPM as vendored (VENDORED-FROM.txt). The K4510 runs it as it
is: `cpm/runcpm`, built from `src/main.c` with the unmodified POSIX
abstraction, is a process on a pty behind the Tube.

Until 2026-09-14 there was a second, in-process build (`-DK4510_TUBE
-Dmain=tube_cpm_main`) for the bare-metal Pi and the desktop `make
tubetest`: `src/main.c` selected a generated `abstraction_k4510.h`, made
by `patch_cpm.py`, that put the console on the Tube's rings. The Pi port
was retired on 2026-09-07; the in-process build, its abstraction, the
generator and the K4510_TUBE lines in `src/main.c` were removed with it.
