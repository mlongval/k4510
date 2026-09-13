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
<td style="text-align: left;">system</td>
<td style="text-align: left;">zero page and stack</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>$0200-$07FF</code></td>
<td style="text-align: left;">system</td>
<td style="text-align: left;">the ROM’s data and C stack, and two bytes worth knowing (below)</td>
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
<td style="text-align: left;">always I/O, whatever is banked</td>
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

## Programs

A `.prg` begins with two addresses, where it loads and where it starts, and the shell honours both. The C programs of `/SYSTEM/BIN` load at `$6000`, Mad Pascal’s at `$0800`, the two BASICs at `$7000`, and `MONITOR` at `$E000`, in the RAM under the ROM — so that the memory a monitor is there to look at, `$0800` to `$CFFF`, is left exactly as it was.

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

Anything handed to a system call must lie below `$A000`: during the call the ROM is banked in over `$A000–$FFFF`, and a line kept up there would read as the ROM’s bytes. `SHELL` copies the line into the shell’s own buffer before running it, so the program’s copy is left alone.

## RAM under the ROM

`rom_out()` (or three pokes to the bank registers) banks blocks 5–7 onto the RAM beneath the ROM: a program then owns `$0800-$CFFF` + `$E000-$FEFF` = 57 KB. Every system call still works, because the calls go through the `$FF00` stub page, which banks the ROM in and out around them.

## Sideways ROM

The operating system is bigger than its half of the 64 KB — so, like the BBC Micro before it, the machine grew *sideways*. The ROM file is a 24 KB base image plus appended 8 KB banks paged into the `$A000-$BFFF` window: bank 0 is the base image itself, banks 1 and 2 hold the colder commands, and bank 3 has the line editor, its history and the directory commands. [Chapter 3, Every Command](03-commands.md) says which bank every command lives in. The shell pages the right bank in around each call, and up to sixteen banks — 128 KB of operating system — fit in the scheme.

The ROM is full, and it is kept that way on purpose: a command whose work can be a program on the disk becomes one — `TYPE` and the monitor did in September 2026 — because a program costs nothing to load (the disk is the host’s, and on the K4510’s own Linux it is in RAM) and can be replaced without touching the ROM.

The calls go one way, and this is the rule to hold on to if you write code of your own for a bank: code in a bank may call resident code as freely as it likes, but resident code must never call into a bank. At that address a different bank holds something else entirely, and the dispatch is the only thing that knows which bank is in. A routine that everything uses belongs in the base image, whatever it costs there.

## RAM under the I/O page

A `MAP` of block 6 hides `$D000-$DFFF` and exposes RAM: with the ROM also banked away a program owns a contiguous field from `$0800` to `$FEFF` — 61.75 KB of the 64. The trick that makes it safe is that `MAP` is an *instruction*, not a register: the program that hid the I/O can always ask for it back, so there is no way to lock yourself out. (The far-call gate is unreachable while block 6 is mapped; unmap before far calls.)

## Programs bigger than the window

The `K4SG` executable format loads segments to any physical address and the far-call gate (`$DF00`) calls between them: a plain `JSR` into slot n banks descriptor n’s code in, and the callee’s `RTS` banks it back out. `SEGDEMO` shows two overlays sharing one address.
