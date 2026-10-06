# Memory

## The 64 KB view and the 256 MB behind it

The 45GS10’s program counter is 16 bits: code executes inside a 64 KB window. The 256 MB of physical memory is reached three ways:

- **Flat pointers**: `LDA [ptr],Z` and the Q forms read and write any byte of the 256 MB directly. Data never needs banking.

- **Bank registers** (`$D600`, one per 8 KB block): write a 28-bit base and that block of the window shows that memory. Bytes 0–2 set the base; byte 3 switches (bit 7 = off).

- **DMA** (`$D200`): copy, fill, swap, line, triangle — instant, physical addresses.

## The CPU view, unmapped

<div class="center">

<table>
<tbody>
<tr class="odd">
<td style="text-align: left;"><code>$0000-$01FF</code></td>
<td style="text-align: left;">user</td>
<td style="text-align: left;">zero page and stack: the program’s own</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>$0200-$07FF</code></td>
<td style="text-align: left;">system</td>
<td style="text-align: left;">the ROM’s data, its base page (page 6) and 6502 stack (page 7), and two bytes worth knowing (below)</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>$0800-$CFFF</code></td>
<td style="text-align: left;">user</td>
<td style="text-align: left;">50 KB for programs, ROM-free at launch</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>$A000-$BFFF</code></td>
<td style="text-align: left;">ROM</td>
<td style="text-align: left;"><em>the sideways window; RAM underneath</em></td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>$C000-$CFFF</code></td>
<td style="text-align: left;">ROM</td>
<td style="text-align: left;"><em>RAM underneath, bankable</em></td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>$D000-$DFFF</code></td>
<td style="text-align: left;">I/O</td>
<td style="text-align: left;">always I/O, whatever is banked; <code>$DB00-$DEFF</code> is K/OS’s workspace</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>$E000-$FEFF</code></td>
<td style="text-align: left;">ROM</td>
<td style="text-align: left;"><em>RAM underneath, bankable</em></td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>$FF00-$FFFF</code></td>
<td style="text-align: left;">stub</td>
<td style="text-align: left;">always ROM: system-call stub and vectors</td>
</tr>
</tbody>
</table>

</div>

Two bytes of system state are fixed, so that programs can rely on them:

`$022E` — a script is running  
non-zero while `EXEC` (and so `/STARTUP.BAT`) is feeding the shell. A program that would wait for a key — `TYPE`’s “more”, say — should not, because there is nobody there to press it.

`$03FF` — the result  
the shell’s result code: 0 for success. A program sets it to say it failed, which is what an RX script reads as `RC`.

### Low memory

The first two kilobytes, where the machine and its programs meet:

<div class="center">

<table>
<tbody>
<tr class="odd">
<td style="text-align: left;"><code>$0000-$00FF</code></td>
<td style="text-align: left;">program</td>
<td style="text-align: left;">the zero page, all of it; system calls take their arguments in <code>$F0-$F9</code></td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>$0100-$01FF</code></td>
<td style="text-align: left;">program</td>
<td style="text-align: left;">the 6502 stack, all of it: a program started at the prompt finds it empty</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>$0200-$022D</code></td>
<td style="text-align: left;">K/OS</td>
<td style="text-align: left;">the ROM’s initialised variables</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>$022E</code></td>
<td style="text-align: left;">K/OS</td>
<td style="text-align: left;">a script is running (above)</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>$0230-$02CF</code></td>
<td style="text-align: left;">EhBASIC</td>
<td style="text-align: left;">lent to the interpreter: part of its code</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>$02D8-$02F5</code></td>
<td style="text-align: left;">K/OS</td>
<td style="text-align: left;">the launch trampoline: banks the RAM under the ROM in, calls the program, banks it out</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>$0300-$043F</code></td>
<td style="text-align: left;">program</td>
<td style="text-align: left;">EhBASIC’s vectors and input buffer; <code>$03FF</code> the result (above)</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>$0440-$05FF</code></td>
<td style="text-align: left;">K/OS</td>
<td style="text-align: left;">the ROM’s variables</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>$0600-$06FF</code></td>
<td style="text-align: left;">K/OS</td>
<td style="text-align: left;">K/OS’s own base page: its zero page at <code>$0602-$062F</code>, the caller’s <code>$F0-$F9</code> copied to <code>$0630</code> during LOAD, SAVE and ARGS; the rest free</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>$0700-$07FF</code></td>
<td style="text-align: left;">K/OS</td>
<td style="text-align: left;">K/OS’s own 6502 stack</td>
</tr>
</tbody>
</table>

</div>

The 45GS10 can put the zero page and the stack on any page (its B register and the stack pointer’s high byte), so the ROM runs on pages 6 and 7 and the system-call stub switches both on the way in and back on the way out. Its C stack is in the I/O page, `$DB00` to `$DEFF`, where no device is: a kilobyte of RAM, growing down from `$DF00`. `INFO -m` shows how much of it has been used — a fence in the machine watches every access to it, and one below `$DB00` would write a dump and say so.

### Who uses the 256 MB

The 256 MB behind the window, by 28-bit address. Everything not in the list is free for programs — in the main `$0040000` to `$DFFFFFF`, about 220 MB.

<div class="center">

<table>
<tbody>
<tr class="odd">
<td style="text-align: left;"><code>$0000000-$000FFFF</code></td>
<td style="text-align: left;">the CPU’s 64 KB, unmapped: the RAM under the ROM and under the I/O page included (the workspace is <code>$000DB00</code>). <code>SWAP</code> saves and restores all of it</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>$0010000-$0013FFF</code></td>
<td style="text-align: left;">the fonts: 8×8 and 8×16 in use, then the CP437 pair and the K4510 page’s pair</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>$0030000</code></td>
<td style="text-align: left;">the console’s text cells, 4 bytes each, up to 180×67</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>$003C000</code></td>
<td style="text-align: left;">the status bands’ own cells, drawn by JIM (12 KB)</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>$003F000</code></td>
<td style="text-align: left;">one blank text row in the current colours</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>$E000000-$EC00000</code></td>
<td style="text-align: left;">the editors’ (VI, EDIT, PROG): document slots, the file as loaded, the unnamed register, RENUM’s table, MAKE.ERR</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>$F000000</code></td>
<td style="text-align: left;">the editors’ undo</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>$FD00000</code></td>
<td style="text-align: left;"><code>SWAP</code>’s copy of the caller’s 64 KB</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>$FD10000</code></td>
<td style="text-align: left;"><code>SWAP</code>’s copy of the screen</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>$FD40000</code></td>
<td style="text-align: left;">the second screen’s text cells (<a href="02-shell.md#two-screens">Two screens</a>)</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>$FDFF000</code></td>
<td style="text-align: left;">the shell’s palette, kept while a program runs</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>$FE00000</code></td>
<td style="text-align: left;"><code>EXEC</code>’s script, loaded whole</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>$FE10000</code></td>
<td style="text-align: left;">a <code>.PAL</code> being loaded</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>$FF00000-$FF1FFFF</code></td>
<td style="text-align: left;">sideways ROM banks 1 to 16, 8 KB each (1 to 3 are used)</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>$FFFA000-$FFFFFFF</code></td>
<td style="text-align: left;">the ROM image, 24 KB, with a hole where the I/O page covers it</td>
</tr>
</tbody>
</table>

</div>

## Programs

A `.prg` begins with two addresses, where it loads and where it starts, and the shell honours both. The C programs of `/SYSTEM/BIN` load at `$0800`, as Mad Pascal’s do (their C stack is the top of `$0800-$CFFF`; the big ones — VI, EDIT, PROG, WORD — have layouts of their own that also use the RAM under the ROM), MS BASIC at `$7000`, EhBASIC in the RAM under the ROM (it leaves `$0800-$BCFF` to BASIC), and `MONITOR` at `$E000`, in the RAM under the ROM — so that the memory a monitor is there to look at, `$0800` to `$CFFF`, is left exactly as it was.

##### Waiting.

A program that waits for a key or for the next frame should say so: a write of anything to `$D545`, WAIT, puts the CPU to sleep until the next interrupt — the frame’s, sixty a second — or a key. Nothing is missed, the loop simply looks again when the CPU wakes, and an idle machine stops costing the computer beneath it most of a core. The ROM’s own key wait, EhBASIC’s line input and `wait_vblank()` in `demo/k4510.h` all use it:

    while (!(k = rom_getin())) K_WAIT();

## System calls

The page `$FF00` is always the ROM, whatever is banked, and its jump table is the whole of the interface a program needs:

<div class="center">

<table>
<tbody>
<tr class="odd">
<td style="text-align: left;"><code>$FF80</code></td>
<td style="text-align: left;">CHROUT</td>
<td style="text-align: left;">print the character in A</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>$FF83</code></td>
<td style="text-align: left;">CHRIN</td>
<td style="text-align: left;">wait for a key; it comes back in A</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>$FF86</code></td>
<td style="text-align: left;">GETIN</td>
<td style="text-align: left;">a key in A, or 0 if none is waiting</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>$FF89</code></td>
<td style="text-align: left;">LOAD</td>
<td style="text-align: left;">name at (<code>$F0</code>), to <code>$F2–$F5</code>; status in A, size in <code>$F6–$F9</code></td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>$FF8C</code></td>
<td style="text-align: left;">SAVE</td>
<td style="text-align: left;">name at (<code>$F0</code>), from <code>$F2–$F5</code>, length <code>$F6–$F9</code></td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>$FF8F</code></td>
<td style="text-align: left;">SHELL</td>
<td style="text-align: left;">run the line A/X points at, as if it were typed</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>$FF92</code></td>
<td style="text-align: left;">VIDEO</td>
<td style="text-align: left;">put the ROM’s video mode and palette back after a program drew</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>$FF95</code></td>
<td style="text-align: left;">ARGS</td>
<td style="text-align: left;">what followed the program’s name: (<code>$F0</code>), length in A</td>
</tr>
</tbody>
</table>

</div>

A program has the zero page and the stack page to itself. K/OS runs on a base page and a 6502 stack of its own — the 45GS10 can put both anywhere: they are pages `$06` and `$07` — and keeps its C stack in a kilobyte of RAM in the I/O page, `$DB00-$DEFF`, where no device is. The stub switches both on the way in and back on the way out, and for LOAD, SAVE and ARGS copies the caller’s `$F0–$F9` across; nothing else of the program’s is touched.

Anything handed to a system call must lie below `$A000`: during the call the ROM is banked in over `$A000–$FFFF`, and a line kept up there would read as the ROM’s bytes. `SHELL` copies the line into the shell’s own buffer before running it, so the program’s copy is left alone.

##### What CHROUT does with control characters.

Everything goes to JIM, the terminal, which is a faithful VT100 — with three exceptions, because CHROUT is the machine’s own console and keeps the promises 8-bit consoles made:

- **A new line** is CR (`$0D`), LF (`$0A`), or CR followed by LF: each is *one* new line, so a program written for any of the three conventions, and a text file from DOS or the web, comes out single-spaced.

- **Backspace** (`$08`) rubs out the character before the cursor; a terminal’s backspace only moves.

- **Form feed** (`$0C`) clears the console and leaves the status bands alone.

A program that wants the terminal’s own meanings — a bare CR that returns to the start of the line, for a progress counter — writes its bytes to JIM at `$DA00` instead, as TELNET does.

## RAM under the ROM

`rom_out()` (or three pokes to the bank registers) banks blocks 5–7 onto the RAM beneath the ROM: a program then owns `$0800-$CFFF` + `$E000-$FEFF` = 57 KB. Every system call still works, because the calls go through the `$FF00` stub page, which banks the ROM in and out around them.

## Sideways ROM

The operating system is bigger than its half of the 64 KB — so, like the BBC Micro before it, the machine grew *sideways*. The ROM file is a 24 KB base image plus appended 8 KB banks paged into the `$A000-$BFFF` window: bank 0 is the base image itself, banks 1 and 2 hold the colder commands, and bank 3 has the line editor, its history and the directory commands. [Chapter 3, Every Command](03-commands.md) says which bank every command lives in. The shell pages the right bank in around each call, and up to sixteen banks — 128 KB of operating system — fit in the scheme.

The ROM is full, and it is kept that way on purpose: a command whose work can be a program on the disk becomes one — `TYPE` and the monitor did in September 2026 — because a program costs nothing to load (the disk is the host’s, and on the K4510’s own Linux it is in RAM) and can be replaced without touching the ROM.

The calls go one way, and this is the rule to hold on to if you write code of your own for a bank: code in a bank may call resident code as freely as it likes, but resident code must never call into a bank. At that address a different bank holds something else entirely, and the dispatch is the only thing that knows which bank is in. A routine that everything uses belongs in the base image, whatever it costs there.

## RAM under the I/O page

A `MAP` of block 6 hides `$D000-$DFFF` and exposes RAM: with the ROM also banked away a program owns a contiguous field from `$0800` to `$FEFF` — 61.75 KB of the 64. The trick that makes it safe is that `MAP` is an *instruction*, not a register: the program that hid the I/O can always ask for it back, so there is no way to lock yourself out. (The far-call gate is unreachable while block 6 is mapped; unmap before far calls.) One kilobyte of that RAM is not free: physical `$DB00-$DEFF` is K/OS’s workspace, and holds the frames of the shell that started the program. A program that maps block 6 straight under itself must leave those bytes alone, or map the block onto RAM somewhere else.

## Programs bigger than the window

The `K4SG` executable format loads segments to any physical address and the far-call gate (`$DF00`) calls between them: a plain `JSR` into slot n banks descriptor n’s code in, and the callee’s `RTS` banks it back out.
