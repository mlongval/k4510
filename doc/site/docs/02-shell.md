# The Shell

The shell is what the machine boots into. It is a file manager, a program launcher and the front door to everything else, and every command here also works from a BASIC with a `*` in front. Type `HELP` for a summary on the screen — the text it prints is itself a file on disk, `/SYSTEM/ETC/HELP` — and see [Chapter 3, Every Command](03-commands.md) for every command with a line about each. This chapter is how the shell behaves.

## This book, on the machine

`BOOK` is this handbook, on the machine it describes — no browser, no printer:

    BOOK              the contents
    BOOK 3            chapter 3, every command
    BOOK SHELL        the chapter whose title says SHELL

The arrows, PgUp and PgDn (or Space) and Home and End move through a page. Tab chooses the next link and Shift+Tab the one before; Enter follows it and Backspace comes back, as far as eight pages. `/` finds a word further down, and `n` finds it again. Q or Escape leaves. A link marked *\[picture\]* is one of this book’s screenshots: Enter shows it full screen, drawn by the machine as the machine drew it, and any key comes back to the page.

The pages are made from the same source as this book and its web edition, at the same time, so the three never disagree. They live in `/SYSTEM/DOC`, one to a chapter, in Gemini text — a small, open format where a line’s first characters say what it is: `#` a heading, `=>` a link, `*` an item in a list. So `TYPE` reads them too, and so does any Gemini reader on another computer.

## The disk

Files live on the host: the `fs/` directory beside the emulator. The machine sees that directory as `/` and nothing above it. The root has six folders and one file in it, and that is fixed — everything else lives one level down:

<div class="center">

<table>
<tbody>
<tr class="odd">
<td style="text-align: left;"><code>/SYSTEM</code></td>
<td style="text-align: left;">the machine’s own, in three parts: <code>BIN</code> the tools (<code>VI</code>, <code>TYPE</code>, <code>MONITOR</code>, <code>RANGER</code>, <code>SETUP</code>, <code>BUG</code>…), <code>ETC</code> what it reads (<code>HELP</code>, <code>VI.SAMPLE</code>, <code>STARTUP.SAMPLE</code>, <code>PALETTES/</code>, a <code>chargen.bin</code> if you add one), <code>LOG</code> what it writes (<code>BENCH-*</code>, <code>TRACE.TXT</code>, <code>BUGREPORTS/</code>)</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>/LANG/</code><em>name</em></td>
<td style="text-align: left;">one folder per language: its runtime, a <code>README</code> and an <code>EX/</code> of examples. <code>PASCAL</code> and <code>C</code> keep the sources <em>beside</em> the programs they compile to</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>/APPS/</code><em>name</em></td>
<td style="text-align: left;">one folder per program, with its data beside it — <code>LODE</code>’s levels, <code>CHESS</code>’s openings, <code>OPLPLAY</code>’s tunes</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>/HOME</code></td>
<td style="text-align: left;">yours. The prompt starts here</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>/CPM</code></td>
<td style="text-align: left;">the Z80’s drives, <code>A</code> to <code>P</code>, one folder each (<a href="09-cpm.md">Chapter 9, CP/M: the Z80 Second Processor</a>)</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>/MNT</code></td>
<td style="text-align: left;">things from outside: <code>SHARE</code>, the folder the container flavour is given, and anything you <code>MOUNT</code></td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>/STARTUP.BAT</code></td>
<td style="text-align: left;">yours; run at power-on (<a href="02-shell.md#when-startupbat-is-the-problem">When STARTUP.BAT is the problem</a>)</td>
</tr>
</tbody>
</table>

</div>

### The search path, which is why you never type a path

A bare name not found where you are is looked for, in this order, in `/SYSTEM/BIN`, then in `/APPS/NAME/`, `/LANG/NAME/` and `/HOME/PROJECTS/NAME/` — where *NAME* is the name you typed without its extension, uppercased, because on this disk **the name is the folder** — and then by extension: a `.BAS` in `/LANG/EHBASIC/EX`, a `.BBC` in `/LANG/BBCBASIC/EX`, a `.RX` in `/LANG/RX`, a `.PAS`, `.C` or `.prg` in the two compiled languages’ folders. So `SKYFIRE`, `EHBASIC`, `RANGER` and `HELLO` run from wherever you are standing, and `RUN "INVADERS.BAS"` inside EhBASIC still finds it. A name is matched regardless of case when the exact one is absent, and `..` never climbs above `/`.

    DIR -a *.BAS        LS is the same; -a shows the hidden names
    CD /LANG/LOGO       the rest of the line is the name, spaces and all
    TYPE README.TXT     a screenful at a time; Esc or Q stops
    CP README.TXT /HOME

Names that begin with a dot are hidden; `DIR -a` shows them, and `DIR -l` lists one name to a line. `DIR` also takes a directory to look in, or a pattern to match: `DIR APPS` lists that folder without going there, `DIR *.PAS` and `DIR ?.TXT` match here (`*` any run of characters, `?` any one).

![](img/dir.png)

<p class="caption"><code>DIR</code> at the root: the disk’s whole shape in six folders and one file. Directories come first, in white; files carry their sizes; two columns.</p>


### Nothing is deleted in a hurry

`RM` moves a file to `/.TRASH` rather than destroying it. `RM -f` is the one that really removes. `DELETE -l` lists the trash, `DELETE -r name` puts one back, and `DELETE -e` empties it; the file managers’ own delete uses the same trash. `RENAME` and `CP` refuse to overwrite an existing file, and take `-f` when you mean it — the host’s own habit is to overwrite without a word, and that habit has cost this project a file.

## Running things

`RUN balls.prg` runs a program; so does `RUN balls`, and so does plain `BALLS` — an unknown word is tried as a program on disk before the shell gives up, *with its arguments*, the way REXX did it on the mainframes. A program reads its arguments through the `ARGS` system call; `SAY` is the demonstration:

    SAY HELLO FROM THE DISK

prints `HELLO FROM THE DISK` — there is no `SAY` command, only a `say.prg` in `/SYSTEM/BIN`. The same rule takes an `.RX` script ([Chapter 12, RX: the Machine’s REXX](12-rx.md)), so a REXX program is a command too. Only a program is run this way: typing the name of a text file at the prompt does not load it over the machine.

Several of the machine’s commands are programs in exactly this sense — `TYPE`, `VI`, `SETUP` and the monitor among them — and you cannot tell from the prompt which is which, which is the point. The disk is the host’s (and on the K4510’s own Linux, in RAM), so a program starts as quickly as a word in the ROM.

Seven words open whole languages: `EHBASIC` ([Chapter 4, EhBASIC](04-ehbasic.md)), `MSBASIC` ([Chapter 5, Microsoft BASIC, 1977](05-msbasic.md)), `BBC` ([Chapter 6, The Tube: BBC BASIC](06-tube.md)), `FORTH` ([Chapter 7, Forth](07-forth.md)), `LOGO` ([Chapter 8, LOGO](08-logo.md)), `CPM` ([Chapter 9, CP/M: the Z80 Second Processor](09-cpm.md)) and `RX` ([Chapter 12, RX: the Machine’s REXX](12-rx.md)).

### What is on the disk

`/APPS` holds the programs the machine ships with, one folder each, and any of them runs by name from anywhere:

<div class="center">

<table>
<tbody>
<tr class="odd">
<td style="text-align: left;"><code>LODE</code></td>
<td style="text-align: left;">a lode-runner: dig, climb, collect the gold. The levels are text files in its folder and <code>E</code> on the title card is a level editor</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>SKYFIRE</code></td>
<td style="text-align: left;">a formation shooter, in the Galaxian rules</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>FLUFFY</code></td>
<td style="text-align: left;">a platformer: variable jump height, stomping, spikes</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>BOMBER</code></td>
<td style="text-align: left;">a bomb-party arena</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>CHESS</code></td>
<td style="text-align: left;">a chess set with an engine; it can also be driven from a script (<a href="12-rx.md">Chapter 12, RX: the Machine’s REXX</a>)</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>TINY</code></td>
<td style="text-align: left;">a dungeon crawl</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>OPL2</code></td>
<td style="text-align: left;">the sound chip, all nine voices</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>OPLPLAY</code></td>
<td style="text-align: left;">plays <code>.OPL</code> streams from its <code>TUNES/</code></td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>MANDEL</code></td>
<td style="text-align: left;">the set, drawn by the machine</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>CUBE</code></td>
<td style="text-align: left;">a rotating solid</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>BALLS</code></td>
<td style="text-align: left;">the sprites, all of them at once</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>ANSIDEMO</code></td>
<td style="text-align: left;">what JIM can draw</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>SEGDEMO</code></td>
<td style="text-align: left;">two overlays sharing one address</td>
</tr>
</tbody>
</table>

</div>

## The file managers

Two of them, because they are two different ideas about what a file manager is for, and neither is a compromise.

`KOMMANDER` is the one with two panels and function keys along the bottom — Tab moves between the panels, and the keys copy, move, make a directory and delete. `RANGER` is the other tradition: three miller columns, what is under the cursor previewed in the right-hand one, and vi’s fingers (`hjkl`, `gg`, `G`, `/` to search).

Both share the rules that matter:

- **Enter on a directory** goes into it.

- **Enter on a program** — a `.prg`, or a CP/M `.com` — leaves the file manager and runs it, because the output of a program belongs on the machine’s screen and not inside a browser’s frame. A `.com` starts CP/M to do it.

- **Enter or F4 on a text file** opens it in `VI` or `EDIT`, and you come back to where you were standing.

- **`DD` deletes** to `/.TRASH`, the same trash the shell’s `RM` uses, so `DELETE -r` brings it back.

## SWAP: running one thing from inside another

A program is loaded where the last one was, so starting one from inside a BASIC would ordinarily flatten the program you were writing. `SWAP command` puts the caller away first — the whole of the CPU’s memory, and the screen — runs the command on a clean machine, and gives the caller back untouched:

    *SWAP EDIT MYPROG.BAS

from EhBASIC edits a file and returns to your program, variables and screen as they were. One level deep; the thing being run must be an ordinary program, and a BASIC’s far memory is not disturbed because nothing else touches it. BBC BASIC needs none of this: it lives on the co-processor, out of reach of anything running here.

`SWAP -k` is the same but *keeps* the screen the program left, instead of restoring the caller’s. A file manager wants its display back; a script that ran a program to read what it printed wants the other thing, which is why RX uses this form — and why Microsoft BASIC runs every `*` command through it.

## At the prompt

The line you are typing is editable: Left and Right move through it, Home and End go to its ends, Delete takes the character under the cursor and Backspace the one before it, characters insert where the cursor is, and Escape clears the line. It works across a line that has wrapped, and it takes accented letters.

**Up and Down walk the last eight lines you typed**, and Down past the newest gives you the empty line again. A line that repeats the one before it is not kept. The history lives in the ROM bank the line editor has to itself, so it survives a reset — but not a power cycle, which is what `/STARTUP.BAT` is for.

## Aliases

`ALIAS` gives a name to a line.

    ALIAS                    list them
    ALIAS LL DIR -l          define one
    ALIAS LL                 nothing after the name: remove it

Whatever you type after the alias is added to the end, so a definition takes arguments. Aliases are tried *last*, after every other rule, so one can never hide a real command: `ALIAS DIR ECHO no` is accepted and `DIR` still lists the directory. They live in memory — in a sideways bank, not in the 64 KB — so they survive a reset but not a power cycle, which is what `/STARTUP.BAT` is for.

## The status bands

Turn on *status bands* (F12, under Terminal) and the console stops being the whole screen: a band at the top and a band at the bottom stay still while the text scrolls between them. Each band is one row high: the bands are on or off, and that is the only setting.

They are shared, and the split is worth knowing. **The top band is yours** — the machine draws the clock and the date in it, in whichever formats the Terminal page is set to, and on a laptop the battery: its charge, with an arrow up while it charges and down while it does not. **The bottom band is the running program’s**, and a program that wants it writes there through JIM; `BANDS` in `/SYSTEM/BIN` is the demonstration.

At its left, the top band says what is running: `K/OS` at the prompt, `EhBASIC INVADER2.BAS` once a program is loaded, and `EhBASIC PROG.BAS > VI EDITTMP.BAS` while `*VI` has it, and `BBC BASIC > VI EDITTMP.BBC` from the Tube — a trail of who started whom, with the file each one has open, LOGO’s `.LGO`, BOOK’s page and CP/M or the Linux prompt on the Tube included. `K/OS` is named only at the prompt: once something runs on top of it, the trail starts there, and the line stays short. And while somebody types into the machine from another computer ([Chapter 13, The Linux Underneath](13-linux.md)), the keys they send show at the left of the bottom band. Programs that take the whole screen — the games, the editors — get the whole screen anyway: the bands are part of the console, and the console is what a program leaves behind when it asks for the glass.

## The network

A URL is a file name. Anything the shell reads — `TYPE`, `LOAD`, `CP`, `RUN`, and the bare-word rule above — accepts `http://` or `https://` in place of a name, and the machine fetches it:

    TYPE https://raw.githubusercontent.com/mlongval/k4510/master/README.md
    CP http://example.org/game.prg game.prg
    RUN http://example.org/game.prg

EhBASIC’s `LOAD` and BBC BASIC’s `LOAD` on the Tube take URLs the same way. The idea is the C64’s Meatloaf cartridge, where a disk name could be an address on the internet; here it costs the ROM nothing, because the ROM never looks at a name — the host does the fetching. A typed line is 95 characters, and so is the longest URL.

Some URLs are more than a file: they are a place. TNFS is the little file protocol the FujiNet and Meatloaf servers speak, and `ftp://` and `sftp://` are what the rest of the world does; the machine’s current directory can be on any of them:

    CD tnfs://fujinet.online/
    DIR
    CD CBM
    CD -

`DIR` lists the server, a bare name loads from it (so `RUN`, and the bare-word rule, run programs straight off the internet), `CD ..` climbs, `CD -` comes home. The prompt shows where you are. A name and password go in the URL the ordinary way (`sftp://me:secret@host/dir`), and the FujiNet spelling — `N:TNFS://…` — is understood too.

Or give the place a name on the disk, and it stays there:

    MOUNT tnfs://fujinet.online/ /MNT/FUJI
    DIR /MNT/FUJI
    MOUNT                     list what is mounted
    UMOUNT /MNT/FUJI

Everything that reads a directory — `DIR`, `CD`, `TYPE`, `RANGER`, a program started by name — then works there as on the machine’s own disk. A mounted place is read-only.

For programs that want a live connection there is the *N: device* at `$D900` (FujiNet’s name for it): four channels, each a URL opened for reading and writing — `tcp://host:port` for a connection, `http://` for a page. `TELNET host port` is the demonstration, a `telnet.prg` in `/SYSTEM/BIN`: what you type goes out, what arrives is drawn by JIM, the terminal ([Chapter 6, The Tube: BBC BASIC](06-tube.md)), so a BBS gets its ANSI colours and CP437 art and the cursor and function keys go out as VT sequences; F12 hangs up (Escape belongs to the far end). It offers the far end the terminal types *xterm-color*, *VT220*, *VT100* and *ANSI*, and a Unix host that takes the first gets UTF-8 as well. The register map is in [Chapter 15, The I/O Page](21-io.md) and `core/net.h`.

So that there is no guessing, this is the whole list of what the machine speaks:

<div class="center">

<table>
<thead>
<tr class="header">
<th style="text-align: left;"><strong>Scheme</strong></th>
<th style="text-align: left;"><strong>What</strong></th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td style="text-align: left;"><code>http://</code>, <code>https://</code></td>
<td style="text-align: left;">fetch a file; a name anywhere a name goes; <code>MOUNT</code> shows one as a folder</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>tnfs://</code></td>
<td style="text-align: left;">a directory on a TNFS server</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>ftp://</code>, <code>sftp://</code></td>
<td style="text-align: left;">a directory on a server; sftp does not check the server’s key</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>tcp://</code></td>
<td style="text-align: left;">a raw connection, through the N: device only</td>
</tr>
</tbody>
</table>

</div>

An ssh *session* is `SSH` ([Chapter 13, The Linux Underneath](13-linux.md)).

## CAPSLOCK

`CAPSLOCK` toggles a caps-lock: while it is on, letters read from the keyboard come up uppercase, so a language whose keywords are uppercase — BBC BASIC, EhBASIC — needs no Shift held. Digits and symbols are untouched, so it is a caps lock and not a shift lock, and Shift gives you the *other* case while it is on. It is also suspended while a program is running, so a program that wants lower case gets it, and the lock comes back when the program does. `CAPSLOCK ON` and `CAPSLOCK OFF` set it explicitly; `*CAPSLOCK` works from inside a BASIC.

## Scripts and the boot file

`EXEC name` runs a text file as shell commands, one per line. A line whose first character is `#` is a comment, and a blank line is ignored. At power-on the machine runs `/STARTUP.BAT` as such a script, if it exists — straight in, with no pause and nothing to catch. It is the place to put your aliases; `/SYSTEM/ETC/STARTUP.SAMPLE` is one to copy from. If one ever stops the machine booting, [When STARTUP.BAT is the problem](02-shell.md#when-startupbat-is-the-problem) is the way out.

A `.BAT` boots the machine and an `.RX` automates it: when a script needs to make a decision, read a result or talk to two things at once, that is RX’s job ([Chapter 12, RX: the Machine’s REXX](12-rx.md)) and `EXEC` does not try to compete.

## The monitor

`MON` (old-timers may type `WOZ`) opens the machine monitor at a `*` prompt; its grammar is Wozmon’s, with 28-bit addresses:

    *FF80.FF8F           examine a range
    *0300:A9 41 60       store bytes
    *0300R               run from $0300
    *X                   leave (or Q, or EXIT)

![](img/mon.png)

<p class="caption">The monitor examining the system-call jump table. <code>MON FF80.FF8F</code> runs one line without entering the prompt.</p>


Anything else typed at the `*` prompt is a shell command, run on a clean machine and returned from. `FILL from.to value` and `COPY from.to dest` work on memory, by DMA, anywhere in the 256 MB.

The monitor is a program, `MONITOR`, and it loads at `$E000`, in the RAM under the ROM, so that the memory it is there to look at — `$0800` to `$CFFF` — is exactly as the last program left it. Addresses below `$10000` read as a program sees them: `$A000` to `$FEFF` is that RAM, not the ROM, and the ROM shows only in its always-there page at `$FF00`.

`SUPERMON` in `/SYSTEM/BIN` is the larger tool, in the tradition of Jim Butterfield’s: `M` memory, `D` a disassembly that speaks the whole 45GS02, `A` an assembler, registers, hunting and filling — and `@` escapes to the shell without leaving it.

## Colours and palettes

`COLOR fg bg` sets the text colours (palette indices, in hex), and `PALETTE` the 256 colours behind them. The machine’s own is the C64’s sixteen, yellow on blue; `/SYSTEM/ETC/PALETTES` holds others:

    PALETTE LOAD GREEN        a phosphor-green tube
    PALETTE LOAD AMBER        an amber one
    PALETTE RESET             the machine's own back

`AMBER`, `GREEN` and `GREY` are ramps — black and fifteen shades of one colour — so each carries a `COLOR` line that sets the text to a bright shade on black. Colour 1 in each is the brightest shade, because the shell draws its highlights in colour 1 (directories in `DIR`, the banner) and they must stay brighter than the text. `C64` is the machine’s own sixteen, as it boots them, and `PEPTO` is the same sixteen as Philip Timmermann measured them off a real C64 — duller, warmer, and what one looked like on a television. A palette file is plain text — an index and three hex bytes to a line, `#` for a comment — and names only the entries it changes; `PALETTE SAVE name` writes one of yours. `PALETTE LOAD` in `/STARTUP.BAT` makes it the machine’s.

## Housekeeping

`INFO` is the machine’s self-description; `TIME` the clock; `MODE` sets the text screen — `CLS` clears it and `CLG` clears the bitmap over it, whoever drew it — and both reach a BASIC through the `*` escape, which is how you tidy up after a demo that left its picture behind. `HUSH` silences the sound, whichever part of the machine is making it. `BANNER` clears the screen and prints the power-on banner again, which is a tidy way to end a session or start a screenshot. `RESET` restarts the machine from the shell, the same cold start the reset chord performs.

`IDEA` is a screenshot in words: a thought about how something could be better, kept before it goes. `IDEA the menu should remember its place` writes it to `/BRAINSHOTS` at once; `IDEA` alone opens VI on a new one, for an idea that needs a paragraph. Either way the machine adds what it knew at that moment — the time, the directory, what was running, the screen — so an idea found a week later still says what it was about. From a BASIC it is `*IDEA`, and it loads nothing over the program. The files are plain text; `TYPE` and VI read them, and so does the person you asked to build the idea ([Chapter 13, The Linux Underneath](13-linux.md)).

`SETUP` measures the computer the machine is running on, with the sound and the picture really running, and keeps the fastest clock it holds without a gap — for that computer, so a later boot there pays nothing. `BENCH` measures it again whenever you like: frames a second and gaps in the sound at every clock of the menu’s ladder, about 25 seconds, the report in `/SYSTEM/LOG/BENCH-NN.TXT`. A clock is right for a host when it holds 60 frames a second with no gaps.

`MODE` on its own says where you are; `MODE n` moves. The text always starts in the top-left cell: there is no margin.

<table>
<tbody>
<tr class="odd">
<td style="text-align: left;"><strong>MODE</strong></td>
<td style="text-align: left;"><strong>pixels</strong></td>
<td style="text-align: left;"><strong>text</strong></td>
<td style="text-align: left;"></td>
</tr>
<tr class="even">
<td style="text-align: left;">0</td>
<td style="text-align: left;">640×480</td>
<td style="text-align: left;">80×30</td>
<td style="text-align: left;">8×16 cells; the machine starts here</td>
</tr>
<tr class="odd">
<td style="text-align: left;">1</td>
<td style="text-align: left;">640×240</td>
<td style="text-align: left;">80×30</td>
<td style="text-align: left;"></td>
</tr>
<tr class="even">
<td style="text-align: left;">2</td>
<td style="text-align: left;">320×240</td>
<td style="text-align: left;">40×30</td>
<td style="text-align: left;"></td>
</tr>
</tbody>
</table>

The F12 menu, under Video, has the same two knobs: *Resolution* shows what the machine is actually in — it follows a `MODE` you type — and choosing another asks the ROM to perform it, because the console’s geometry belongs to the ROM and not to the host. VICKY can draw smaller fields than these, but they are for a program that wants the pixels, not for a shell; a program asks for them through the video registers ([Chapter 15, The I/O Page](21-io.md)).

The picture is always painted into 640×480: a 240-line mode has each line drawn twice, and a 320-wide one has its pixels doubled sideways. Raster lines and SHEILA’s display list count the glass, 0–479, not the mode — which matters the moment a program asks VICKY for a 200-line field, where the first line of the picture is raster line 40.

## When STARTUP.BAT is the problem

`/STARTUP.BAT` runs at power-on, and a bad one runs at every power-on. There is no moment to catch — the machine boots straight into it — so the way out is not a key held at the right instant but a switch that stays where you put it: the F12 menu, under Shell: *Run STARTUP.BAT*. That setting is the host’s, kept in `k4510.cfg`, so it survives a power cycle and no wedged program in the machine can take it away from you. Boot clean, fix the file, switch it back on.

There is a second way, for one boot only:

    ./k4510 --no-startup.bat

(`--no-startup` does the same, as does `K4510_NO_STARTUP=1` in the environment). It skips `/STARTUP.BAT` for that run and touches nothing: the F12 setting and `k4510.cfg` are left exactly as they were.

## When something goes wrong

Type `DUMP ON`, make it go wrong again, then type `BUG` and answer its questions: the report it writes is the issue. [Appendix B, Filing an Issue](a2-issues.md) is the whole of it, on two pages.
