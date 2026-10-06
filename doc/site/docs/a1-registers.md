# The Registers

Everything the machine’s devices do, they do through the I/O page. This appendix is the register-level reference, and it is not written by hand: `doc/guide/mkregs.py` lifts it out of the machine’s own headers at build time, so what you read here is what the emulator was compiled against. When a device gains a register, this appendix gains it in the next build — which is the only way a reference of this kind stays true.

Nothing in it is reworded: every word below is a word from a header. What the generator decides is only which column a word belongs in — the address on the left, the register’s name after it, the comment’s own sentence following. Where a comment’s own spacing was doing the work of a table, that table is kept exactly as it was written, in monospace, because there it is the spacing that carries the meaning.

The voice is still the voice of working code: terse, occasionally opinionated, and using the machine’s own shorthand — `LE` for little-endian, `R:` and `W:` for a register that reads and writes differently, `$` for hex. Addresses are given as an offset inside the device’s block where the block’s base is obvious, and in full where it is not.

[Chapter 13, The I/O Page](21-io.md) is the map of which device lives where; this is what is inside each one.

## The I/O page

Generated from `core/io.h`.

### The OPL2, wired the AdLib’s way, so every AdLib register list means what it

The OPL2, wired the AdLib’s way, so every AdLib register list means what it says here. The machine’s one sound chip.

`$D480`*W* **`ADDR`** the register to write next R STATUS bit7 IRQ, bit6 timer 1 expired, bit5 timer 2 expired

`$D481`*W* **`DATA`** write it; R the last value written to that register

`$D482`*R* ID

`$02`= an OPL2 is fitted

### The status bands and the clock’s format, for the guest at `$D52D-$D52F`

The status bands and the clock’s format, for the guest at `$D52D-$D52F`. `$D521` was the natural home and is FULL: all eight bits are spoken for (CP/M-by-name, margin, STARTUP.BAT, the bands, the mode request, and three of the video mode). So these are their own bytes, and there is room after them for whatever the menu grows next.

### `$D800` status: bit 0 a co-processor is up, bit 2 the host shell is fitted, bit 3 a UCI chess

`$D800` status: bit 0 a co-processor is up, bit 2 the host shell is fitted, bit 3 a UCI chess engine is fitted (K4510_UCI, or stockfish where it usually lives), bit 7 a byte waits. `$D803` program: 1 BBC BASIC, 3 CP/M, 4 the host shell (‘!‘), 5 the chess engine, 2 stop. `$D804-7` the command string’s address for program 4 (empty = an interactive shell), `$D808`/9 its rows/columns.

### `$D53A` R BATTERY the host’s battery, for the status band: charge in % in bits 0-6

`$D53A` R BATTERY the host’s battery, for the status band: charge in % in bits 0-6

(0-100), bit 7 set on AC power or charging, `$FF` = no battery (a desktop, the browser build). The frontend reads it from the host every ten seconds; K/OS draws “nn%” and an arrow at the right of the bottom band.

### Bank registers and the far gate

BANK registers: `$D600` + 4n, n = 0..7, one per 8 KB block of the CPU view.

`bytes 0-2`physical base bits 0-23 (little-endian); they only set the base

`byte 3`bits 24-27 of the base, and bit 7 = OFF. Writing byte 3 switches the block: bit 7 clear = on, set = off. (So STQ works, and a byte-wise save/restore never switches a block on by accident.) Reads give the base; byte 3 reads `$FF` while the block is off.

`$D620`read: bit n = block n banked

`$D621`read: MAP mask (bit n = MAPped)

A banked block resolves phys = base + (cpu & `$1FFF`). MAP rewrites all eight blocks, so the ROM’s “MAP off” at program exit clears the banks too. The I/O page `$D000-$DFFF` and the stub page `$FF00-$FFFF` stay visible whatever is banked, so banking blocks 6 and 7 reveals the RAM under the ROM only. FAR gate: JSR `$DF00` + 4n calls descriptor n of the table at FARTAB.

`$DF00-$DF7F` call slots

`$DFF0`return gate (RTS lands here)

`$DF80-$DF83`**`FARTAB`** 28-bit pointer to the descriptor table (read/write)

`$DF84`read: nesting depth

`$DF85`read: last error (1 overflow, 2 underflow, 3 bad slot); write clears

descriptor, 8 bytes: base\[4\] block flags entry\[2\]; flags bit0 leave banked on return, bit1 do not bank (long jump to resident code). A/X/Y/Z pass through both ways: cc65 \_\_fastcall\_\_ works across it.

### The MATH unit

MATH unit. Results are ready the cycle after the write that triggers them.

`$D700-$D71F`**`F0..F7`** eight IEEE-754 single registers, little-endian, read/write

`$D720`**`FOP`** write = execute; low 5 bits op, see below

`$D721`**`FARG`** (dst \<\< 4) | src, register numbers 0..7, write before FOP

`$D722`**`FFLAGS`** from the last op: bit0 result zero, bit1 negative, bit2 NaN/inf

`$D724-$D727`**`FI`** int32 for ITOF / FTOI

    ops: 0 MOV Fd=Fs  1 ADD  2 SUB  3 MUL  4 DIV   (Fd = Fd op Fs)
         5 SQRT 6 SIN 7 COS 8 TAN 9 ATAN 10 EXP 11 LOG 14 ABS 15 NEG 16 FLOOR 17 ROUND  (Fd =
             f(Fs))
         10 ATAN2 (Fd = atan2(Fd,Fs))  13 POW (Fd = Fd^Fs)  21 FMOD (Fd = fmod(Fd,Fs))
         18 CMP   flags from Fd - Fs, registers unchanged
         19 ITOF  Fd = (float)FI        20 FTOI  FI = (int32)Fs, truncated

Math list – a program for the unit in RAM, run with one write:

`$D728-$D72B`**`MLPTR`** 28-bit pointer to the list

`$D72C`**`MLRUN`** write = run from MLPTR until END or a STOP fires

`$D72D`**`MLSTAT`** 0 reached END, 1 a STOP fired, `$FF` runaway (65536 ops)

`$D72E,$D72F`**`MLCNT`** 16-bit counter for DJNZ, read/write

list ops, 2 bytes each (op, arg) unless noted; ops 0..21 are the FOP ops with arg = (dst\<\<4)|src, and in addition:

`$80`**`END`**

`$81`**`STOPNEG`** stop if last flags negative

`$82`**`STOPPOS`** if not negative

`$83`**`STOPZERO`**

`$84`**`STOPNZ`**

`$85`**`JUMP`** arg (signed, in ops from the next op)

`$86`**`DJNZ`** arg MLCNT–, jump if nonzero

`$87`**`STOPFIGE`** arg stop if FI \>= arg (unsigned byte compare on the low byte, FI clamped)

`$88`**`LDF`** arg=dst\<\<4, then 4 bytes: IEEE single immediate into Fdst (6-byte op)

`$89`**`LDI`**, then 4 bytes: int32 immediate into FI (6-byte op)

`$8A`**`LDMS`** arg=dst\<\<4, then 4 bytes: 28-bit address of a Microsoft-format float (exponent excess-128, mantissa1 with sign in bit 7, mantissa2, mantissa3 – EhBASIC’s packed variable format); converted into Fdst (6-byte op)

MEGA65-compatible integer unit (same addresses as the MEGA65):

`$D770-$D773`**`MULTINA`**

`$D774-$D777`**`MULTINB`** (unsigned 32-bit, LE)

`$D778-$D77F`**`MULTOUT`** = A \* B, 64-bit

`$D76C-$D76F`**`DIVOUT`** integer part of A / B

`$D768-$D76B`fractional part (32.32)

recomputed on every write to an input byte; B = 0 gives all-ones.

### System registers, and the SIDs

SYS registers (read-only unless noted):

`$00,01`CPU clock, kHz, LE (40500)

`$02,03`physical RAM, MB, LE (256)

`$04`read: latch the host clock into `$05-$0C` and return 0

`$05`sec

`$06`min

`$07`hour

`$08`day

`$09`month

`$0A,0B`year LE

`$0C`weekday (0=Sun)

`$0D,0E,0F`frames since reset, 24-bit LE (vblank count)

`$10-$1F`version string, NUL-terminated

`$20`ROM base page (e.g. `$A0` for a 24 KB ROM)

`$F0`write: DUMP – the host writes dumps/dump-NNN.txt (machine state, screen, PC history, keys, the shell log); read: the number of the last dump

`$F1`write: append a byte to the shell log (the ROM logs command lines and DUMP notes)

`$F2`write 1/0: automatic dump every 900 frames (15 s) on/off (on at reset on the desktop, off on the Pi; DUMP or DUMP ON also arms the per-instruction PC recorder); read: the setting

`$23`read: the CPU clock setting in force (0 = 40.5 MHz, 1 = 30, 2 = 20, 3 = 15, 4 = 10); write: ask for one – the host applies it next frame (BENCH sweeps them)

`$24,25`audio gaps LE: callbacks that found the ring empty since last cleared; any write clears

### The keyboard

Keyboard: a FIFO of key-down events. Printable keys arrive as ASCII (`$20-$7E`, already shifted/dead-keyed by the host layout on the desktop); control keys as ASCII controls; everything else as `$80`+ codes.

### bit7 event available; bit0 shift, bit1 ctrl, bit2 alt held;

bit7 event available; bit0 shift, bit1 ctrl, bit2 alt held; bit6 the byte LAST READ from KBD was a key code (KEY\_\*), not a character; bit5 the byte WAITING is a key code. The two share `$80-$9F` – KEY_LEFT and é are both `$82` – and these bits are how a line editor tells them apart.

### 

— WATCH (`$D530-$D535`): who is trampling this byte? ——————

`$30-$33`-bit physical address to watch

`$34`write 1 to arm; write 0 to disarm; reads back the armed state

`$35`hits since last arm (saturates at 255)

A CPU write to the watched byte – normal, MAPped, banked or a flat 32-bit store – fires a DUMP (dumps/dump-NNN.txt, tagged “watch”) and disarms, so one bug produces one dump. DMA and the host itself do not trip it: the question WATCH answers is “which instruction wrote this”, and the dump’s PC history is the answer.

### The stack fence

— FENCE (`$D550-$D55C`): how deep does the ROM’s C stack go? ———-

`$50`write: the base-page address of the C stack pointer – arms (0 disarms) and starts the measure; read: it

`$51`the base page that pointer lives in (the B the ROM runs with)

`$52,53`the floor, LE: the lowest address the stack may reach

`$54,55`read: the deepest address reached since armed; write `$54`: start again

`$56`read: trips – accesses below the floor (saturates at 255)

`$57,58`read: the lowest 6502 S seen on the ROM’s own stack page

`$59,5A`read: the pointer as it was when armed (the stack’s top)

`$5B,5C`read: the lowest S on page 1 under a program (K4510_FENCE_DEEP only)

The ROM arms it at reset (crt0.s); INFO -m shows it. The first trip writes a DUMP tagged “stack fence”. core/mem.h has the rest.

### WAIT: sleeping until something happens

— WAIT (`$D545`): write anything, and the CPU sleeps until the next interrupt (the frame’s, sixty a second) or a key arrives; reads `$FF`. The way to wait for a key or a frame without the host spending its time on the loop. Time goes on while it sleeps.

### The Tube

The Tube (`$D800`): Acorn’s answer, refitted. The HOST runs Richard Russell’s BBC BASIC interpreter (the vendored BBCTTY console edition, tube/bbcbasic) on a pty; the machine talks to it byte-wise:

`$D800`*R* **`status`** bit0 alive, bit7 a byte waits in `$D801`

`$D801`*R* next byte from the co-processor (pops)

`$D802`*W* a byte to the co-processor (its keyboard)

`$D803`*W* 1 start (spawn), 2 stop (kill)

The co-processor has its own flat 256 MB; PAGE/HIMEM live there, far beyond the 64 KB view. The co-processor is a process on the host (BBC BASIC, RunCPM’s Z80 as program 3); where none can be started, status reads 0. The console it talks to is JIM, the terminal at `$DA00` (core/term.h).

## The DigiMAX

Generated from `core/digimax.h`.

### The K4510’s DigiMAX

The K4510’s DigiMAX – four 8-bit DACs at `$D4C0`, beside the OPL2.

`$D4C0-$D4C3`*R/W* **`DAC`** 0..3 unsigned 8 bits; `$80` is silence

`$D4C4`*R* ID

`$04`= four DACs are fitted

The design has had it since the machine was drawn (“DigiMAX PCM: built-in, always present”, docs/K4510-Design.md A-09; “`$D480-$D4FF` OPL2, DigiMAX”). It arrived on 2026-09-17, when Doc asked for DOOM’s sound effects “and the engine”. It is the C64 cartridge’s shape and nothing more: no FIFO, no DMA, no interrupt – whatever is in a DAC’s register is what that DAC is putting out, and a program makes sound by changing it quickly.

One thing can write DAC 0 besides a program: a STREAM, which the machine clocks in at a fixed rate on somebody’s behalf. That is how the Tube’s DOOM is heard (core/io.c hands its shared ring over as the stream), in the same spirit as opl2_write_reg(): the co-processor cannot reach a register, so the machine performs the write for it.

## VICKY, SHEILA and the sprites

Generated from `core/vicky.h`.

### VICKY

VICKY – the K4510 video chip.

Register block at IO_VICKY (`$D000`), 256 bytes, byte-addressed. Every pointer is a 28-bit physical address into main RAM; there is no video memory. Rendering is per scanline into an 8-bit indexed framebuffer the frontend supplies; the frontend applies vicky_palette_rgb().

`$00`**`CTRL`** bit0 display enable; the rest pick the mode. Without bit5 the glass is 640x480 and the raster lines 0..479 – a smaller mode is drawn into it, doubled, and centred. bit5 (hd-modes branch, 2026-09-14): the HD family, drawn at its own size, the raster lines 0..h-1:

    1|32        1440x1080    1|32|2|4     720x540

1|32|2|4|16 360x270 (a 1080-line panel shows them 1x, 2x, 4x)

bit1 columns halved (320), bit2 lines halved (240), bit3 a 200-line field (40 blank lines top and bottom), bit4 columns quartered (160; with bit1).

`1`640x480 1|4 640x240

`1|2`320x240 1|2|8 320x200

`1|2|8|16`160x200 1|4|8 640x200

`$01`**`BGCOL`** background palette index (where nothing is drawn)

`$02`**`RASTER`** read: current line (low 8); write: raster-compare low

`$03`read: line high bits; write: compare high

`$04`**`IRQSTAT`** bit0 vblank, bit1 raster==compare, bit2 SHEILA IRQ op, bit3 sprite collision. Write 1s to acknowledge.

`$05`**`IRQMASK`** same bits; IRQ line = IRQSTAT & IRQMASK

`$06`**`PALIDX`** palette index for the write port

`$07`**`PALR`**

`$08`**`PALG`**

`$09`**`PALB`** – writing B commits the entry and increments PALIDX

`$0A-$0D`**`SPRTAB`** 28-bit pointer to the sprite attribute table (128 x 16 B)

`$0E`**`SPRCTL`** bit0 sprites enable

`$0F`reserved

`$60-$63`**`SHEILA`** 28-bit pointer to SHEILA’s list

`$64`**`SHEILACTL`** bit0 enable; the list restarts every frame at line 0

`$70-$73`**`BLTSRC`** 28-bit

`$74-$77`**`BLTDST`** 28-bit

`$78,79`**`BLTW`** width px

`$7A,7B`**`BLTH`** height px

`$7C,7D`**`BLTSS`** source stride (bytes)

`$7E,7F`**`BLTDS`** dest stride

`$80`**`BLTOP`** 0 copy, 1 keyed copy (src 0 skipped), 2 fill (value = BLTSRC byte 0), 3 AND, 4 OR, 5 XOR,  
6 LINE: from (LX0,LY0) to (LX1,LY1), colour = BLTSRC byte 0, into the BLTDST surface of stride BLTDS, clipped to 0..BLTW-1 x 0..BLTH-1  
7 TRIANGLE: filled (LX0,LY0)-(LX1,LY1)-(LX2,LY2), same colour, surface and clip as LINE

`$81`**`BLTFLG`** bit0 H-flip, bit1 V-flip

`$82`**`BLTCMD`** write anything: go. Instant. Reads 0.

`$84,85`**`LX0`**

`$86,87`**`LY0`**

`$88,89`**`LX1`**

`$8A,8B`**`LY1`**

`$8C,8D`**`LX2`**

`$8E,8F`**`LY2`** (signed 16-bit)

Blits are 8 bpp (one byte per pixel) in this version.

`$90-$9F`**`COLSS`** read: sprite-sprite collision bits, one bit per sprite (sprite n hit another sprite this frame). All 16 cleared on read of `$90`.

`$A0-$AF`**`COLSL`** read: sprite-layer collision bits (sprite n over a non-transparent layer pixel). All 16 cleared on read of `$A0`.

`$B0-$B7`**`LAYOUT`** the status bands and the console between them. VICKY is the one owner of where they are (2026-10-01, Doc: "A then B"; docs/notes/status-bars.md). Writes say what is wanted, reads of `$B5-$B7` say what is in force:

`$B0`**`BANDTOP`** a program’s top band, rows (used while BANDCTL bit1)

`$B1`**`BANDBOT`** a program’s bottom band, rows

`$B2`**`BANDCTL`** bit0 the user’s bands are on – the F12 switch; the host sets it and a guest write cannot change it bit1 the bands are the PROGRAM’s: its heights, and K/OS

    draws nothing in them.  It works with bit0 off.  Set it,

then call VIDEO (`$FF92`); a program MUST clear it and call VIDEO again before it exits (BANDS.PRG is the example).

`$B3`**`TCOLS`** the text grid, columns } written by whoever sets the

`$B4`**`TROWS`** the text grid, rows } mode (K/OS’s VIDEO); 0 = none

`$B5`**`CONOY`** read: the top band in force = the console’s first row

`$B6`**`CONROWS`** read: the console’s rows

`$B7`**`CONBOT`** read: the bottom band in force

`$B8-$BB`**`BANDMAP`** 28-bit: the bands’ OWN text32 cells (option B, 2026-10-01): the top band’s rows, then the bottom’s, TCOLS cells a row. While layer 0 shows CONMAP, its band rows are drawn from here and not from the map, so nothing that writes the console’s map can reach a band. 0 = off.

`$BC-$BF`**`CONMAP`** 28-bit: the console’s text32 map, as K/OS declares it. A program that points layer 0 at a map of its own gets its own rows, bands or no bands. Writing `$BF` latches the layout the bands are drawn to: K/OS writes it after it has laid the console out, so a band switched on in F12 covers nothing until K/OS has moved the console for it. The rules: bands only on a grid of 40x30 or more; the user’s are one row each; a program’s that would leave the console under VICKY_BAND_MIN_ROWS, or take more than VICKY_BAND_MAX_ROWS between them, fall back to one each. JIM’s window (`$DA05-$DA08`) is clamped inside the console.

Sprite attribute entry, 16 bytes, in main RAM:

`+0,1`X (signed 16)

`+2,3`Y (signed 16)

`+4..7`**`DATA`** 28-bit pointer

`+8`**`CTRL`** bit0 enable, bit1 8 bpp (else 4), bit2 H-flip, bit3 V-flip, bits4-5 Z: drawn after layer Z (0..3)

`+9`**`SIZE`** bits0-1 width 8/16/32/64, bits2-3 height 8/16/32/64

`+10`**`PALOFS`** (4 bpp: index = PALOFS\<\<4 | pixel)

`+11..15`reserved

Pixel 0 is transparent. No per-line limit. 128 sprites.

SHEILA – the display-list coprocessor (Doc named it, 2026-08-22; the Amiga’s copper is the ancestor). 4-byte instructions in main RAM, executed at the start of each scanline until a WAIT blocks. Register writes take effect for the line about to be drawn.

`00`**`END`** stop until next frame

`01`**`WAIT`** lo hi wait for line \>= (hi\<\<8|lo)

`02`**`MOVE`** reg val write val to VICKY register reg

`03`**`SKIP`** lo hi skip next instruction if line \>= value

`04`**`JUMP`** a0 a1 a2 continue at 24-bit address

`05`**`IRQ`** set IRQSTAT bit2

At most 256 instructions per line are executed (runaway guard).

Layers 0..3 at `$10` + n\*`$10`, 16 bytes each:

`+0`**`LCTRL`** bit0 enable, bits1-2 mode (0 bitmap, 1 tile, 2 text8, 3 text32), bits3-4 bpp (0=1, 1=2, 2=4, 3=8), bits5-6 cell size (tile: 8/16/32/64 px square; text: 0 = 8x8, 1 = 8x16)

`+1`**`LPALOFS`** palette offset for \<8 bpp: index = (value \<\< depth) | pixel

`+2,3`**`SCROLLX`** 16-bit, pixels

`+4,5`**`SCROLLY`** 16-bit, pixels

`+6,7`**`STRIDE`** bitmap: bytes per row. tile/text: map entries per row.

`+8..+B`**`DATA`** 28-bit pointer: pixels (bitmap) or glyph/tile set

`+C..+F`**`MAP`** 28-bit pointer: the map

Map formats:

`tile` bytes/cell: bits 0-9 tile index, 10 H-flip, 11 V-flip, 12-15 palette offset (used for \<8 bpp). Tile pixel data at DATA + index \* (size\*size\*bpp/8), rows MSB-first packed.

`text8` byte/cell: glyph index. 1 bpp 8xH glyphs at DATA + g\*H. Colours from LPALOFS: index = (LPALOFS\<\<1) | pixel.

`text32` bytes/cell: glyph lo, glyph hi (16-bit index), fg, bg – byte-wide palette indices per cell. bit7 of glyph hi = reverse.

Layer 0 is bottom. Pixel index 0 is transparent in every layer; BGCOL is the ground (text32 bg is never transparent). Changed 2026-08-22 from “opaque in the lowest layer” so SHEILA backgrounds show under text.

### The layout in force (`$B5-$B7`): top band, console rows, bottom band. rows is

The layout in force (`$B5-$B7`): top band, console rows, bottom band. rows is 0 while no text grid has been declared (`$B4` = 0).

## The network

Generated from `core/net.h`.

### The N: device

The network, three ways – each borrowed from the machine that did it first.

The Meatloaf rule (the C64’s Meatloaf cartridge): a URL is a file name. A name beginning http://, https:// or tnfs:// that reaches the filesystem device (`$D300`) for reading – LOAD, TYPE, CP, RUN, EhBASIC’s LOAD, BBC BASIC’s LOAD on the Tube – is fetched and served like a file. No ROM change: the ROM passes names through untouched and the host fetches.

TNFS (the Spectranet’s file protocol, the one FujiNet and Meatloaf servers speak; UDP, port 16384): a tnfs://host\[:port\]/path is also a DIRECTORY. CD tnfs://host/path puts the machine’s current directory on the server: DIR lists it, a bare name loads from it (so the REXX rule runs programs off the internet), CD .. climbs it, CD - comes home. The server is read-only from here.

The N: device (FujiNet’s network device, as the Atari and Apple saw it), at `$D900`, for programs that want a live connection: four channels, each a URL opened for reading and writing.

`$D900`*W* command

`$D901`*R* status (0 ok, 1 not found / refused, 2 i/o error, 3 bad command, 4 closed by the peer, 5 name too long, 6 not fitted)

`$D902`*RW* channel 0-3

`$D904-$D907`**`NAMEPTR`** 28-bit -\> URL, NUL-terminated

`$D908-$D90B`**`ADDR`** 28-bit

`$D90C-$D90F`**`LEN`** bytes requested; updated to bytes done

`$D910-$D913`**`SIZE`** after OPEN: the content length, `$FFFFFFFF` if unknown; after STATUS: bytes waiting

commands: 1 OPEN (tcp://host:port a connection; http://, https://, tnfs:// a file, fetched whole, then READ)

2 READ (up to LEN bytes to ADDR, what has arrived so far; LEN = done, 0 = nothing yet)  
3 WRITE (LEN bytes from ADDR; tcp only)  
4 CLOSE  
5 STATUS (SIZE = bytes waiting; status 4 once the peer has closed and the bytes are gone)  
6 GET (the Meatloaf rule for programs: fetch the whole URL into ADDR, at most LEN; LEN = done, SIZE = total)

Reads never block the machine: a program polls, as it would a UART. Underneath: core/net_plat.h – sockets and curl on the desktop, Circle’s stack on the Pi (no TLS there: https answers 6).

## JIM, the terminal

Generated from `core/term.h`.

### JIM

JIM, the terminal (`$DA00`) – the Beeb’s third page, given a job: a VT100 with the ANSI colour and VT220 editing additions, in hardware – the way a real 8-bit machine got a serious terminal: a card, not a program. It draws on the VICKY text32 screen the ROM console uses, inside the geometry the ROM gives it, so the console and the terminal share one screen and one cursor. Anything that needs a terminal writes its byte stream here: the ROM for the Tube (CP/M programs set up for VT100/ANSI, BBC BASIC’s console edition) and TELNET for the BBSes (ANSI-BBS: CP437 glyphs, 16 colours).

`$DA00`*W* **`DATA`** a byte of the stream

`$DA01`*R* **`STATUS`** bit7 a reply byte waits; bit0 the stream moved the cursor since CX/CY were written

`$DA02`*R* **`REPLY`** the next reply byte (pops): answers to ESC\[6n / ESC\[c, and translated keys

`$DA03`*W* **`KEY`** a K4510 key code (io.h): its terminal bytes go to REPLY (arrows ESC\[A.. or ESC OA.. in application mode, Home/End, PgUp/PgDn/Ins ESC\[n~, Del `$7F`, F1-F4 ESC OP.., F5-F12 ESC\[15~.., everything else through unchanged)

`$DA04`*W* **`CTRL`** 1 reset (modes, attributes, cursor home; the screen kept) 2 clear the screen and home

`$DA05-$DA0D`*RW* **`COLS ROWS OX OY CX CY FG BG STRIDE`** the window: origin (OX,OY) cells, STRIDE cells per row. The window is kept inside the console VICKY lays out (`$D0B5`/`$D0B6`): moved, not shrunk, if it would cross a status band (2026-10-01).

`$DA0E`*RW* **`FLAGS`** bit0 cursor shown (blinking) bit1 read: application cursor keys (DECCKM) bit2 PETSCII mode bit3 the status bands are the program’s – a DOOR onto VICKY’s BANDCTL bit1 (`$D0B2`), which is where the claim lives since 2026-10-01; see core/vicky.h.

`$DA0F`*RW* **`BANDTOP`** a door onto VICKY’s BANDTOP (`$D0B0`)

`$DA16`*RW* **`BANDBOT`** a door onto VICKY’s BANDBOT (`$D0B1`). The three doors are kept for programs written before VICKY owned the layout; new ones use `$D0B0-$D0B2`.

`$DA17`*RW* **`CODEPAGE`** 0 strict CP437 (power-on), 1 the K4510 page: JIM’s table and the fonts follow

`$DA18`*RW* **`SCREEN`** which screen JIM shows: 0 K/OS, 1 the terminal (a session on the Linux beneath or beyond, /SYSTEM/ETC/TERMINAL.CFG; the TERMINAL command writes 1). Alt+1 / Alt+2, F12 \> Screen and ESC \] 4510 ; kos BEL from the session switch too (2026-10-05)

`$DA10-$DA13`*RW* **`BASE`** 28-bit address of the text32 map (reset: `$030000`)

`$DA14,$DA15`*RW* **`DEFFG DEFBG`** the colours SGR 0 / 39 / 49 return to

Sequences: the VT100 set (cursor, ED/EL, DECSTBM, DECSC/DECRC, IND/RI/NEL, tabs, DECAWM/DECOM/DECCKM, DEC line drawing via ESC(0 and SO/SI, DSR, DA, DECALN, RIS), ANSI SGR 0/1/4/5/7/22/24/27/30-37/39/40-47/49/90-97/100-107 and 38;5;n / 48;5;n for n \< 16, VT220 ICH/DCH/IL/DL/ECH/SU/SD/CHA/VPA, IRM, ESC\[?25 cursor, ESC\[s/u, DECSTR, DECSCUSR (ESC \[ n SP q: 0-2 block, 3-4 underline, 5-6 bar – the shape VI changes with its mode). Bytes `$80-$FF` are glyphs (CP437). UTF-8: ESC % G on, ESC % @ off (CTRL 1 leaves it). On, a UTF-8 sequence draws as its CP437 glyph (or a near one, or ’?’), and a byte that continues no sequence is CP437 as before. The ‘!‘ shell and TELNET turn it on for their sessions. Two K4510 additions, for a program that draws its whole screen through JIM (EDIT, PROG, WORD; demo/jimscr.h), 2026-10-05: ESC\[?4510h makes 38;5;n and 48;5;n with n \< 16 the palette’s own entry n (SGR’s ANSI order reaches only twelve of the sixteen), ESC\[?4510l puts xterm’s meaning back; SGR 11 draws the bytes `$00-$1F` and `$7F` as their glyphs (all but BS HT LF VT FF CR SO SI ESC: the Linux console’s display-control flag), SGR 10 stops. A reset clears both.

## Memory and the far view

Generated from `core/mem.h`.

### The ROM window and the stub page

The ROM image lives in the top 64 KB of physical memory and is seen in the unmapped CPU view from mem_rom_base up; the physical RAM at `$A000-$FFFF` is “RAM under the ROM”, revealed by banking blocks 5/7 onto `$A000`/`$E000` (K-05). The page `$FF00-$FFFF` always reads the ROM, whatever is banked: the system-call stub and the vectors live there.

### K/OS’s workspace

K/OS’s workspace – 1 KB of RAM in the I/O page, `$DB00-$DEFF` (2026-10-06), where no device is. Visible wherever the I/O is – so whenever the ROM runs, whatever a program has banked. The ROM keeps its C stack there (its base page was there too at first, and cost the host 6-7%: it is at `$0600`). The bytes are the RAM under the I/O page at the same addresses, physical `$00DB00-$00DEFF`: a buffer on the ROM’s C stack has the same address for the CPU as for DMA and the devices, which take physical ones (the first try put the workspace elsewhere, and every file name the shell built on its stack was read by the file device as empty), and SWAP’s 64 KB image includes it, as it included the old stack at `$0600`.
