# mon/ — SUPERMON+64, ported to the K4510

Jim Butterfield's Supermon+64 V1.2 (1985), the classic Commodore machine
language monitor, running as a K4510 `.prg`. Type `SUPERMON` at the shell.

    M 2000 207F        memory display (hex + ASCII)
    D B000 B020        disassemble          A 4000 LDA #$12   assemble
    R  /  ; ..         registers show/set   > addr bytes      poke
    T src end dst      transfer             C src end dst     compare
    H start end 'text  hunt (or hex bytes)  F start end val   fill
    G addr / J addr    go / call            X                 exit to shell
    L "FILE" ADDR      load raw bytes       S "FILE" A B      save [A,B)
    $1234 +4660 &.. %..  base conversion    @<line>           K/OS shell escape
                                            (@ alone = DIR; ESC stops M/D/T/H/F)

## Provenance and licence

Ported from J.B. Langston's commented 64tass restoration of the original
source (github.com/jblang/supermon64). Supermon is **public domain** —
Butterfield distributed it freely all his life; Langston's comments and
build harness are public domain too (he asks for attribution, given
gladly here and in the handbook). `supermon64.asm` is the pristine
upstream file; `supermon.asm` is the K4510 port; the diff between them
is the whole port. Credits entry for CREDITS.md is pending (that file
was in flight in another session when this landed):

> Supermon+64 V1.2 by Jim Butterfield et al. (public domain), ported from
> J.B. Langston's annotated edition, github.com/jblang/supermon64.

## What the port changed

- C64 KERNAL -> K4510 ROM jump table ($FF80 CHROUT, $FF86 GETIN, $FF89
  LOAD, $FF8C SAVE, $FF8F SHELL), with shims preserving the KERNAL's
  register-saving contract.
- The CBM screen editor (input by re-reading the screen line) -> RDLINE,
  a plain polling line editor with backspace; the assembler's
  auto-advance re-types "A XXXX " through RDLINE's prefill buffer
  instead of stuffing the C64 keyboard buffer.
- Variables live at $0300 (the page the ROM reserves for programs) as
  EQUs so the image stays contiguous; zero-page pointers moved into the
  .prg window ($40/$42). Code loads at $B000, clear of the $6000 space
  other .prgs use -- SWAP SUPERMON inspects a swapped-out world.
- L/S do raw byte loads/saves through the host filesystem; no device
  numbers, V(erify) dropped. @ passes its line to the K/OS shell.
- STOP is ESC or Ctrl-C (the $D103 break peek).

## Building

    mon/build.sh     # needs 64tass; writes fs/SYSTEM/BIN/supermon.prg

The built .prg is committed (like every .prg), so other machines need
nothing but a pull. Not in the main Makefile: that would add a 64tass
dependency to `make all` for one file that changes rarely.

## The 45GS02 disassembler

D decodes the full 256-opcode 45GS02/65CE02 map -- LDZ, INW/DEW, PHW,
STA ($nn),Z, ($nn,SP),Y, 16-bit branches with computed targets, the
RMB/SMB/BBR/BBS families with their bit digits.  The tables are
generated from `core/xemu/cpu65ce02_disasm_tables.c`, the emulator's own
opcode map, so the disassembler and the CPU cannot disagree.

The in-place assembler (A) still speaks Butterfield's 6502 set only --
extending its matcher to the new modes is a separate piece of work.

## Not yet

- A knows only 6502; D knows everything (above).
- The $EA-prefixed flat forms disassemble as EOM + the base-page
  instruction, not as one [ptr],Z line.
- Breakpoints: the ROM's IRQ path does not yet distinguish BRK, so G into
  a BRK does not re-enter the monitor. J (JSR) returns on RTS.
