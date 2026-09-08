# Mad Pascal for the K4510

A cross-compiler, like cc65: [Mad Pascal](https://github.com/tebe6502/Mad-Pascal)
(Tomasz Biela, MIT) compiles on the desktop to 6502 assembly, MADS
assembles it, the ROM's `RUN` loads the `.prg`. Not an on-machine
compiler -- that role is Turbo Pascal under CP/M on the Tube.
On K4510x, though, the loop closes: `PAS name` at the prompt runs
`tools/k4510-pas` on the Linux beside the machine and leaves `name.prg`
next to `name.PAS` in the current directory (`CC name` does the same
for C with `tools/k4510-cc`); errors come back on the screen.

`mp/` holds the K4510 target as it lives inside a Mad-Pascal checkout:

- `base/rtl6502_k4510.asm`, `base/k4510/` -- the runtime: the register
  equates (`k4510.hea`), `@putchar` and `@ClrScr` on JIM, the terminal
  at `$DA00`.
- `lib/system_k4510.inc`, `systemh_`, `crt_`, `crth_` -- the SYSTEM and
  CRT units' machine halves: Pause on the frame counter, Random,
  ParamStr from the ROM's ARGS call; CRT on JIM (GotoXY and TextColor
  are register stores; the palette constants are the machine's).
- `lib/k4510.pas` -- the machine as typed absolute variables (VICKY,
  SID, DMA, FS, SYS, MATH, TERM...), FarPeek/FarPoke through the
  45GS02's flat 32-bit addressing, DmaCopy/DmaFill, Shell, LoadFile,
  SaveFile.

`install.py [MP_DIR]` copies these into the checkout, patches
`src/Targets.pas` and `src/mp.pas` (the target's id, memory layout and
header; idempotent), and rebuilds `mp` with FPC. The top-level Makefile
then builds `demo/pas/*.pas` into `fs/PRG/*.prg`:

    make pascal            # MP_DIR and MADS overridable

Memory: code from `$0800` (programs own `$0800-$CFFF`), zero page
`$22-$3F` for the compiler's registers and `$64-$A3` for its expression
stack (the ROM keeps `$02-$21` and `$F0-$F9`), `$0300` the string buffer.
